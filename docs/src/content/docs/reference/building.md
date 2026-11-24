---
title: Building from source
description: Build any of the four board targets with PlatformIO, the lean ESP8266 and WireGuard ESP32 variants, and the ESP32 toolchain notes.
---

The four board targets share one codebase and build from [PlatformIO](https://platformio.org/). Pick the env that matches your board.

```bash
pio run -e smalltv                 # ESP8266
pio run -e smalltv_lean            # ESP8266, without HA screens, the usage meter or theme clocks
pio run -e smalltv_noupdate        # ESP8266, without the GitHub self-update (a fork's build option, see below)
pio run -e smalltv_c2              # ESP32-C2
pio run -e smalltv_esp32           # NM-TV-154 (classic ESP32)
pio run -e smalltv_esp32_wg        # the same board, with the WireGuard client
pio run -e smalltv_esp32_8mb       # SmallTV Pro (classic ESP32, 8 MB flash)
pio run -e smalltv_c2 -t upload    # build + flash the C2 over USB-C
pio device monitor -e smalltv_c2   # serial logs @ 115200
pio run -e smalltv_loader          # ESP8266 loader for the SmallTV-ultra
```

Eight envs: six published images, the loader, and `smalltv_noupdate`, which CI builds but does not publish (see [Build options for forks](#build-options-for-forks)). Two boards get two images each: the ESP8266 (`smalltv` and `smalltv_lean`) and the NM-TV-154 (`smalltv_esp32` and `smalltv_esp32_wg`). Which file each env becomes is in [Which release file to download](/smalltv-mod/reference/release-assets/).

## The smalltv_lean env

`smalltv_lean` builds the same ESP8266 code as `smalltv` with Home Assistant screens, the Claude usage meter and theme clocks compiled out. It exists to buy heap, and it is published as `smalltv-mod-firmware-lean.bin`.

```bash
pio run -e smalltv_lean
```

The ESP8266 shares one 80 KB DRAM arena between static allocations and the heap, so a byte of static dropped is a byte the heap gains. Measured after 3.12.0, with the feature buffers below on the heap:

| Build | Static RAM | Heap gained |
|-------|-----------|-------------|
| `smalltv` | 48,288 B | baseline |
| `smalltv_lean` | 41,048 B | 7,240 B |

Lean compiles out Home Assistant, the usage meter and theme clocks. Since 3.5.0 the Home Assistant screen store (5,792 bytes) and the MQTT client with its 768-byte receive buffer are no longer static in either image: `haBoot()` (`src/features/ha/HaBoot.cpp`) takes them from the heap at boot, and only when a broker host is set. A standard device with the host blank therefore keeps that memory too, without the lean image.

Three more buffers come from the heap only while their feature is used, so a standard image with the feature switched off keeps that memory as well:

- the radar's aircraft list and TLS session (about 1 KB), while the radar is the mode or ticked in the carousel;
- the MQTT `WiFiClient`, the broker snapshot and the topic buffers (672 B), with the MQTT client above;
- the usage mascot's 400-byte diff buffer, from the first time the mascot is drawn until a save switches the usage page off.

Both ESP8266 images keep Arduino_GFX's YCbCr lookup tables (6,804 bytes) in flash rather than DRAM. The library declares them as plain `static const` arrays, which the ESP8266 copies into DRAM, and its virtual `writeYCbCrPixels` keeps them although the firmware never calls it. `tools/patch_gfx_ycbcr.py`, a `pre:` script of the ESP8266 envs, patches the installed library before each build: the tables get `PROGMEM` and the function reads them with `pgm_read_word`. It checks the hash of each file first and stops the build if the library is not the pinned 1.6.9, so a version bump needs a new look at the patch. CI checks that no table is left in DRAM (`python3 tools/ci_checks.py ycbcr`).

That headroom decides whether the HTTPS fetches run at all. The radar (`features/radar/RadarClient.cpp`) and a cash.ch quote (`features/ticker/StockClient.cpp`) skip their fetch unless a 16,000-byte contiguous block is available, which is what the BearSSL handshake allocates. A Yahoo quote skips its fetch unless the free heap is at least its receive buffer plus 12,000 bytes. All of them refuse quietly rather than crash, so a device below the threshold shows an empty scope or a blank ticker with no error. On a busy LAN, or over a weak link where retransmissions keep the WiFi queues full, the standard build can sit under both.

Read the current figures from `/api/status`: `heap` is free bytes and `maxblk` is the largest contiguous block.

The env inherits `smalltv` through `extends` and interpolates its `build_flags` (`${env:smalltv.build_flags}`) before adding its own, so the board, flash layout, libraries and shared flags stay identical. Four flags do the work: `-D WITH_HA=0`, `-D WITH_USAGE=0` and `-D WITH_THEME=0` drop the features, and `-D SMALLTV_LEAN` selects the matching `UPDATE_ASSET` in `src/config.h` so a lean device self-updates to the lean image rather than the standard one.

### Rolling your own combination

The six `WITH_*` flags in `src/config.h` each default to 1 and can be set to 0 in any env, or passed through `PLATFORMIO_BUILD_FLAGS`:

| Flag | Drops | Static RAM saved on the ESP8266 |
|------|-------|--------------------------------|
| `WITH_THEME=0` | Theme clocks | 5,340 B |
| `WITH_TICKER=0` | The stock and crypto ticker | 3,284 B |
| `WITH_HA=0` | Home Assistant screens over MQTT | 1,124 B |
| `WITH_RADAR=0` | The plane radar | 372 B |
| `WITH_USAGE=0` | The Claude usage meter and its mascot | 768 B |
| `WITH_NOTIFY=0` | Notification overlays | 1,048 B |

Each figure is that one flag measured on its own against the 48,288-byte standard build above. Combining flags does not add up exactly, because the features share some code and string data: the three flags of the lean image save 7,232 B one by one, and lean saves 7,240 B. The Home Assistant figure leaves out its screen store and MQTT client, which come from the heap at boot only when a broker host is set (see above).

Every feature is guarded at the module level, so dropping one takes its code, its settings handling, and its web UI tab with it. The tab disappears because `/api/config` reports the compiled-in features and the UI hides what is missing, which means a slim build needs no separate web UI.

Two caveats if you publish your own combination. `UPDATE_ASSET` still points at whichever variant your flags select, so a build that is neither standard nor lean will self-update into one of the published images and pick up the features you removed. And the mascot frame data lives in flash rather than RAM, so `WITH_USAGE=0` saves far more flash than heap.

## Build options for forks

Three build flags are for people who build and flash their own firmware, for example a fork that should not update itself over the original project's releases. None of them is set in the published images, which are built as before. They are build options only: there is no setting for any of them in the web UI or in `/api/config`, and a device cannot change them after it is flashed.

Set them in an env's `build_flags` in `platformio.ini`, or pass them through `PLATFORMIO_BUILD_FLAGS`.

| Flag | Effect |
|------|--------|
| `-D FW_VERSION_SUFFIX=\"-fork\"` | The label the device shows becomes `3.7.0-fork` instead of `3.7.0`. |
| `-D FW_NO_GITHUB_UPDATE` | The GitHub self-update is left out of the image. |
| `-D THEME_MAX_LAYERS=<n>` | The most layers a theme may have. The default is 32, and without the flag the image is the same as before. `<n>` is a plain decimal number from 1 to 255, so it can raise the limit or lower it (see below). |

**The version suffix is display only.** `FW_VERSION` in `src/config.h` stays the plain release number. The suffix goes into `FW_VERSION_FULL` (`src/BuildInfo.h`), which is shown in the `version` field of `/api/status`, on the serial line, on the boot screen and in the `ver` entry of the mDNS record (builds with the usage meter). The update check compares `FW_VERSION`, so the suffix never changes a version comparison, and the crash record keeps the plain number in its 16-byte field.

**With `FW_NO_GITHUB_UPDATE` no download code is compiled in.** `src/OtaUpdate.cpp` then holds only stubs that answer "no", and the update at boot and the download in the web loop are not built. The web API says so:

- `/api/selfupdate` answers `403` with `{"ok":false,"error":"updates are off in this build"}`.
- `/api/checkupdate` answers `200` with `"disabled":true` and the same error text. It does not contact GitHub.
- `/api/status` gets `"githubUpdate":false` (the key is absent in a normal build), and the Update card in the System tab says that updates are off and points to the manual upload.

The manual firmware upload in the System tab is not affected.

`smalltv_noupdate` is a ready-made env for the second flag: it extends `smalltv` and adds `-D FW_NO_GITHUB_UPDATE`. CI builds it on every run, so the option keeps compiling, and `tests/buildflags/check_images.py` checks that its image holds none of the download code. It is not a release image and has no release asset. For your own fork, copy the pattern: an env that extends `smalltv` and interpolates `${env:smalltv.build_flags}` before adding the flags.

**`THEME_MAX_LAYERS` sets the theme layer limit.** Release images allow 32 layers, and a package with more layers does not load on them. Write `<n>` as a plain decimal number, with no leading zero and no `0x`: `=040` is read by the compiler as octal, which is 32, while the error text prints `040` and the packer's tools read it as 40. See [A higher layer limit](/smalltv-mod/features/themes/#a-higher-layer-limit-build-flag) for the cost per layer and for `SMALLTV_THEME_MAX_LAYERS`, which tells the packer the limit of your build.

## The smalltv_loader env

`smalltv_loader` (source `src/loader.cpp`) builds a minimal ESP8266 image: WiFi plus a web OTA endpoint at `/update`, nothing else. Its only job is the two-step [SmallTV-ultra install](/smalltv-mod/getting-started/flashing/#smalltv-ultra-stock-updater-says-not-enough-space), where the stock Ultra layout rejects the full image. The loader is small enough to fit that stock slot, and it uses this firmware's own 4m1m flash layout (`eagle.flash.4m1m.ld`), so its own `/update` slot is large enough to then accept the full `smalltv-mod-firmware.bin`.

```bash
pio run -e smalltv_loader
```

By default the loader opens an open access point named `SmallTV-Loader` at `192.168.4.1`. To have it auto-join an existing network instead, bake the credentials in at build time by adding the two flags to the env's `build_flags` (or via `PLATFORMIO_BUILD_FLAGS`):

```
-DLOADER_SSID='"MyNet"' -DLOADER_PASS='"secret"'
```

Credentials are compile-time only and never live in the repo. The build output is published as the release asset `smalltv-mod-loader.bin`.

## How one codebase builds for all four

Chip differences are centralized so the feature code stays the same on every board.

- `src/Target.h` checks that the env selected exactly one chip and defines `SMALLTV_FAMILY_ESP32` (C2 and classic ESP32) or `SMALLTV_FAMILY_ESP8266`. Code that differs by chip family tests these two macros, and a build with no chip macro, or two, stops with an error
- `src/Platform.h` holds every chip-specific include, class alias, and small shim. The WiFi stack, web server, HTTPS client, OTA, and reset handling all resolve through it. The ESP32 targets share its Arduino core 3.x branch.
- `src/board_esp8266.h`, `src/board_esp32c2.h`, `src/board_esp32.h`, and `src/board_esp32_pro.h` hold the pin map and panel quirks for each board. `src/config.h` includes the right one based on the build target.
- The three feature modes, the web UI, and the settings layer are identical across all targets.

The target is chosen by a build flag: `SMALLTV_ESP8266`, `SMALLTV_ESP32C2`, or `SMALLTV_ESP32`. The SmallTV Pro defines `SMALLTV_ESP32` (chip-family code paths) plus `SMALLTV_ESP32_PRO` (its pin map and update asset).

In `platformio.ini`, `[common]` holds the source include paths and the libraries every firmware env uses, and `[esp32_base]` holds the single ESP32 platform pin. Each env interpolates them, so a change there reaches every board.

## Project layout

```
src/                    shared core (device, net, web, settings)
  main.cpp              setup/loop and the mode registry
  loader.cpp            the standalone smalltv_loader image (built on its own)
  Target.h              chip check and the SMALLTV_FAMILY_* macros
  Platform.h            per-chip includes, aliases, and shims
  Mode.h                the DisplayMode interface every feature implements
  board_esp8266.h       ESP8266 pin map and panel quirks
  board_esp32c2.h       ESP32-C2 pin map and panel quirks
  board_esp32.h         NM-TV-154 (classic ESP32) pin map and panel quirks
  board_esp32_pro.h     SmallTV Pro (classic ESP32, 8 MB) pin map and panel quirks
  config.h              limits, feature flags, defaults, board selector
  BuildInfo.h           the fork build options: FW_VERSION_SUFFIX, FW_NO_GITHUB_UPDATE
  Settings.*            settings struct and LittleFS persistence
  Net.*                 WiFi station, fallback AP, captive portal, mDNS
  WebPortal.*           web server, REST API, OTA endpoint
  webui.h               the single-page UI (HTML/CSS/JS, served from flash)
  Gfx.*                 shared ST7789 core (Arduino_GFX), colour correction
  Clock.*               SNTP and the night-mode window
  WgClient.*            WireGuard tunnel (no-op stubs without SMALLTV_WIREGUARD)
  BearSslTuning.cpp     ESP8266 TLS cipher/curve pinning
  OtaUpdate.*           GitHub self-update (download in place on the ESP32, update at boot on the ESP8266)
  StatusJson.h          the /api/status reply, written in 128-byte pieces
  CrashRecord.h         the last-crash record kept in RTC memory (ESP8266)
  features/
    ticker/             TickerMode + StockClient
    usage/              UsageMode + UsageClient + Mascot
    radar/              RadarMode + RadarClient
    notify/             NotifyMode + its overlay frames (armed over HTTP, never persisted)
    ha/                 Home Assistant screens over MQTT
    theme/              the .stheme clock-face engine and its web API
lib/ESP8266mDNS/        the ESP8266 core 3.1.2 mDNS library with two memory fixes (see below)
partitions/             ESP32 flash layouts (4 MB shared by C2 + NM-TV-154, 8 MB for the Pro)
n8n/                    webhook contract and importable workflows
extra_script.py         Windows-only SCons spawn workaround for the ESP8266 envs
```

## The mDNS library on the ESP8266

The ESP8266 envs build `lib/ESP8266mDNS/`, a copy of the ESP8266 core 3.1.2 mDNS library (LEAmDNS) with two changes, instead of the core's own. Both keep a busy network from running the heap out, where a failed allocation restarts the device:

- An answer record in a response from another device is handled when it is read, not after the whole packet (v3.6.0 or later)
- A known answer in a query is read whole only when the reply uses it; any other is skipped after its header. This change is on `main` after v3.10.0 and comes from esphome-libs/arduino-esp8266#9

`lib/ESP8266mDNS/README-SMALLTV.md` describes both changes. `tools/check_mdns_copy.py` runs in CI and checks that the copy still matches the pinned core apart from them, and `tests/mdns/` tests them on the host. The ESP32 envs ignore the folder (`lib_ignore` in `[esp32_base]`) and use the ESP32 core's own mDNS.

## Windows builds

`extra_script.py` replaces SCons's process spawn on Windows. The default path sends build-tool command lines through `cmd.exe`, which can mis-tokenize the long quoted lines this project generates and fail a `.cpp` compile with "no such file or directory". The script re-tokenizes the line itself and starts the process directly, including the `\"`-escaped quotes the ESP8266 `ARDUINO_BOARD_ID` define carries.

It is wired into `[env:smalltv]` with `extra_scripts = pre:extra_script.py`, so `smalltv_lean` and `smalltv_noupdate` inherit it through `extends`. The override is gated on `os.name == "nt"` and does nothing on Linux or macOS, where SCons escapes for `sh` with single quotes that this tokenizer does not parse. The ESP32 envs do not use it.

## ESP32 toolchain notes

The ESP32 targets have a few requirements the ESP8266 does not.

- **Platform**: PlatformIO's official espressif32 does not support the C2 and is stuck on Arduino core 2.x. All ESP32 envs use the [pioarduino](https://github.com/pioarduino/platform-espressif32) fork, pinned to one release in `[esp32_base]`, which tracks Arduino core 3.x on ESP-IDF 5.x (`Platform.h`'s ESP32 branch needs core 3.x). The first build downloads a large toolchain and compiles the IDF from source, so it takes several minutes. Later builds are fast.
- **Display driver**: both use `Arduino_HWSPI` with explicit pins. The register-level `Arduino_ESP32SPI` hangs on the C2, and the software-SPI path in the library does not cover it. `Arduino_HWSPI` uses the stock SPI driver and works.
- **Flashing the C2**: uploads call the system esptool, not the one bundled with PlatformIO, which hangs entering download mode on that board. Install it with `pip install esptool`. The classic ESP32 flashes with either.
- **Image size flags**: the ESP32 envs build with `-D CORE_DEBUG_LEVEL=0 -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables`. Without them the C2 and WireGuard images do not fit their 1,572,864-byte slot. The first flag also silences the Arduino core's own debug messages on the serial console; the firmware's serial output is unchanged
- **Partitions**: the 4 MB layout in `partitions/smalltv_4mb_ota.csv` gives two OTA app slots plus about 0.9 MB for LittleFS, and is shared by the C2 and the NM-TV-154. The SmallTV Pro's 8 MB layout in `partitions/smalltv_8mb_ota.csv` doubles the app slots (2.125 MB each) and gives about 3.7 MB of LittleFS, matching the stock firmware's table exactly.

## WireGuard

The optional WireGuard client is compiled into `smalltv_c2`, `smalltv_esp32_8mb` and `smalltv_esp32_wg`. Its switches live in those envs in `platformio.ini`: `-D SMALLTV_WIREGUARD=1` plus `-D CONFIG_WIREGUARD_MAX_PEERS=1` and `-D CONFIG_WIREGUARD_MAX_SRC_IPS=5`, and `droscy/esp_wireguard @ 0.4.5` in `lib_deps`. These have to be `build_flags`, not `build_src_flags`, because they must reach the library's own translation units. Without the flag `src/WgClient.cpp` compiles to no-op stubs, so every other env builds unchanged.

The two peer limits are compile-time because `esp_wireguard` allocates peers statically inside its device struct; both default to 1, and `MAX_SRC_IPS` has to cover every allowed-IPs entry plus the device's own address, which the component adds itself.

### The smalltv_esp32_wg env

`smalltv_esp32_wg` builds the same NM-TV-154 code as `smalltv_esp32` with the WireGuard client compiled in, and is published as `smalltv-mod-firmware-esp32-wg.bin`. It exists because this board's image is the tightest fit of the ESP32 boards, and the client is a meaningful slice of what is left:

| Build | `firmware.bin` | Of the 1,572,864 B slot | Free |
|-------|---------------|-------------------------|------|
| `smalltv_esp32` | 1,483,136 B | 94.3% | 89,728 B |
| `smalltv_esp32_wg` | 1,529,104 B | 97.2% | 43,760 B |

Measured on the v3.10.0 release assets, which CI builds. The client costs 45,968 bytes, about half of the plain image's spare flash. Both fit with room left, so this is a headroom decision rather than a hard limit: the plain image keeps its spare bytes for whatever the firmware grows into next, and the WireGuard one spends half of them on the tunnel.

Growing the slot is the alternative to the split, and it is not one a field device can take: raising `app0` and `app1` in `partitions/smalltv_4mb_ota.csv` at the expense of `spiffs` (0xF0000 is generous for one `config.json`) is a partition-table change, and only `firmware.factory.bin` over USB can install that.

The env inherits `smalltv_esp32` through `extends` and interpolates that env's `build_flags` and `lib_deps` rather than restating them, so anything added to the base env reaches both images. It adds `-D SMALLTV_ESP32_WG`, which selects the matching `UPDATE_ASSET` and `FW_VARIANT` in `src/config.h` so a tunnel device self-updates to the tunnel image and a plain one stays plain. Everything else, including the board pin map, still keys off `SMALLTV_ESP32`.

`custom_sdkconfig` is inherited unchanged, which matters for build times: pioarduino keeps one `sdkconfig.defaults` per project and rebuilds the whole Arduino/IDF framework whenever its hash changes. The two envs hash the same, so building them back to back costs one framework build, not two.

## Footprint

Measured as the flashable `firmware.bin` of the v3.10.0 release assets, which CI builds, and which is what an OTA slot has to hold:

| Build | `firmware.bin` | Slot | Used | Free |
|-------|---------------|------|------|------|
| `smalltv` | 846,752 B | 1,044,464 B | 81.1% | 197,712 B |
| `smalltv_lean` | 677,664 B | 1,044,464 B | 64.9% | 366,800 B |
| `smalltv_c2` | 1,492,880 B | 1,572,864 B | 94.9% | 79,984 B |
| `smalltv_esp32` | 1,483,136 B | 1,572,864 B | 94.3% | 89,728 B |
| `smalltv_esp32_wg` | 1,529,104 B | 1,572,864 B | 97.2% | 43,760 B |
| `smalltv_esp32_8mb` | 1,561,712 B | 2,228,224 B | 70.1% | 666,512 B |

The loader, `smalltv-mod-loader.bin`, is 315,920 B.

The ESP8266 slot comes from `eagle.flash.4m1m.ld`, the 4 MB ESP32 slot from `partitions/smalltv_4mb_ota.csv` and the Pro's from `partitions/smalltv_8mb_ota.csv`. The mascot frame data lives in flash, not the heap.

CI checks every image against its slot on each build (`tools/ci_checks.py size`) and fails the build when an image has less than 8,192 bytes free, so an image that cannot be installed over the air is never published. After a local `pio run`, `python tools/ci_checks.py size --min-free 8192 --allow-missing` runs the same check on the envs you built.

The PC-side usage daemon is a separate repo: [clawdmeter-daemon](https://github.com/giovi321/clawdmeter-daemon).
