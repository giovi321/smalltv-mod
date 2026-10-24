---
title: Building from source
description: Build any of the four board targets with PlatformIO, the lean ESP8266 and WireGuard ESP32 variants, and the ESP32 toolchain notes.
---

The four board targets share one codebase and build from [PlatformIO](https://platformio.org/). Pick the env that matches your board.

```bash
pio run -e smalltv                 # ESP8266
pio run -e smalltv_lean            # ESP8266, without HA screens or the usage meter
pio run -e smalltv_c2              # ESP32-C2
pio run -e smalltv_esp32           # NM-TV-154 (classic ESP32)
pio run -e smalltv_esp32_wg        # the same board, with the WireGuard client
pio run -e smalltv_esp32_8mb       # SmallTV Pro (classic ESP32, 8 MB flash)
pio run -e smalltv_c2 -t upload    # build + flash the C2 over USB-C
pio device monitor -e smalltv_c2   # serial logs @ 115200
pio run -e smalltv_loader          # ESP8266 loader for the SmallTV-ultra
```

Seven envs, six published images plus the loader. Two boards get two images each: the ESP8266 (`smalltv` and `smalltv_lean`) and the NM-TV-154 (`smalltv_esp32` and `smalltv_esp32_wg`). Which file each env becomes is in [Which release file to download](/smalltv-mod/reference/release-assets/).

## The smalltv_lean env

`smalltv_lean` builds the same ESP8266 code as `smalltv` with Home Assistant screens and the Claude usage meter compiled out. It exists to buy heap, and it is published as `smalltv-mod-firmware-lean.bin`.

```bash
pio run -e smalltv_lean
```

The ESP8266 shares one 80 KB DRAM arena between static allocations and the heap, so a byte of static dropped is a byte the heap gains. On the 2.12.0 build:

| Build | Static RAM | Heap gained |
|-------|-----------|-------------|
| `smalltv` | 55,536 B | baseline |
| `smalltv_lean` | 46,804 B | 8,732 B |

Home Assistant is the bulk of that at 7,668 bytes, and two objects are almost all of Home Assistant: `g_screens`, the four-screen store, at 5,792 bytes, and `g_ic`, the icon cache, at 1,704 bytes. The 768-byte PubSubClient receive buffer also stops being allocated at runtime. The usage meter contributes 1,080 bytes. The two do not sum exactly to 8,732 because they share a little code and string data.

That headroom decides whether two fetch paths run at all. `features/radar/RadarClient.cpp` and `features/ticker/StockClient.cpp` both skip their fetch unless a 16,000-byte contiguous block is available, which is what the BearSSL handshake actually allocates. Both refuse quietly rather than crash, so a device below the threshold shows an empty scope or a blank ticker with no error. On a busy LAN, or over a weak link where retransmissions keep the WiFi queues full, the standard build can sit under both.

Read the current figures from `/api/status`: `heap` is free bytes and `maxblk` is the largest contiguous block.

The env inherits `smalltv` through `extends` and interpolates its `build_flags` (`${env:smalltv.build_flags}`) before adding its own, so the board, flash layout, libraries and shared flags stay identical. Three flags do the work: `-D WITH_HA=0` and `-D WITH_USAGE=0` drop the features, and `-D SMALLTV_LEAN` selects the matching `UPDATE_ASSET` in `src/config.h` so a lean device self-updates to the lean image rather than the standard one.

### Rolling your own combination

The four `WITH_*` flags in `src/config.h` each default to 1 and can be set to 0 in any env, or passed through `PLATFORMIO_BUILD_FLAGS`:

| Flag | Drops | Static RAM saved on the ESP8266 |
|------|-------|--------------------------------|
| `WITH_HA=0` | Home Assistant screens over MQTT | 7,668 B |
| `WITH_TICKER=0` | The stock and crypto ticker | 3,328 B |
| `WITH_RADAR=0` | The plane radar | 2,180 B |
| `WITH_USAGE=0` | The Claude usage meter and its mascot | 1,080 B |

Each figure is that one flag measured on its own against the 55,536-byte standard build of 2.12.0. Combining flags saves slightly less than the sum, because the features share some code and string data.

Every feature is guarded at the module level, so dropping one takes its code, its settings handling, and its web UI tab with it. The tab disappears because `/api/config` reports the compiled-in features and the UI hides what is missing, which means a slim build needs no separate web UI.

Two caveats if you publish your own combination. `UPDATE_ASSET` still points at whichever variant your flags select, so a build that is neither standard nor lean will self-update into one of the published images and pick up the features you removed. And the mascot frame data lives in flash rather than RAM, so `WITH_USAGE=0` saves far more flash than heap.

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
  Settings.*            settings struct and LittleFS persistence
  Net.*                 WiFi station, fallback AP, captive portal, mDNS
  WebPortal.*           web server, REST API, OTA endpoint
  webui.h               the single-page UI (HTML/CSS/JS, served from flash)
  Gfx.*                 shared ST7789 core (Arduino_GFX), colour correction
  Clock.*               SNTP and the night-mode window
  WgClient.*            WireGuard tunnel (no-op stubs without SMALLTV_WIREGUARD)
  BearSslTuning.cpp     ESP8266 TLS cipher/curve pinning
  OtaUpdate.*           GitHub self-update (ESP8266)
  features/
    ticker/             TickerMode + StockClient
    usage/              UsageMode + UsageClient + Mascot
    radar/              RadarMode + RadarClient
    notify/             NotifyMode + its overlay frames (armed over HTTP, never persisted)
    ha/                 Home Assistant screens over MQTT
    theme/              the .stheme clock-face engine and its web API
partitions/             ESP32 flash layouts (4 MB shared by C2 + NM-TV-154, 8 MB for the Pro)
n8n/                    webhook contract and importable workflows
extra_script.py         Windows-only SCons spawn workaround for the ESP8266 envs
```

## Windows builds

`extra_script.py` replaces SCons's process spawn on Windows. The default path sends build-tool command lines through `cmd.exe`, which can mis-tokenize the long quoted lines this project generates and fail a `.cpp` compile with "no such file or directory". The script re-tokenizes the line itself and starts the process directly, including the `\"`-escaped quotes the ESP8266 `ARDUINO_BOARD_ID` define carries.

It is wired into `[env:smalltv]` with `extra_scripts = pre:extra_script.py`, so `smalltv_lean` inherits it through `extends`. The override is gated on `os.name == "nt"` and does nothing on Linux or macOS, where SCons escapes for `sh` with single quotes that this tokenizer does not parse. The ESP32 envs do not use it.

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
| `smalltv_esp32` | 1,434,192 B | 91.2% | 138,672 B |
| `smalltv_esp32_wg` | 1,479,984 B | 94.1% | 92,880 B |

Measured in CI on v3.0.0, which is what builds the published binaries. The client costs 45,792 bytes, about a third of the plain image's spare flash. Both fit with room left, so this is a headroom decision rather than a hard limit: the plain image keeps its spare bytes for whatever the firmware grows into next, and the WireGuard one spends a third of them on the tunnel.

Growing the slot is the alternative to the split, and it is not one a field device can take: raising `app0` and `app1` in `partitions/smalltv_4mb_ota.csv` at the expense of `spiffs` (0xF0000 is generous for one `config.json`) is a partition-table change, and only `firmware.factory.bin` over USB can install that.

The env inherits `smalltv_esp32` through `extends` and interpolates that env's `build_flags` and `lib_deps` rather than restating them, so anything added to the base env reaches both images. It adds `-D SMALLTV_ESP32_WG`, which selects the matching `UPDATE_ASSET` and `FW_VARIANT` in `src/config.h` so a tunnel device self-updates to the tunnel image and a plain one stays plain. Everything else, including the board pin map, still keys off `SMALLTV_ESP32`.

`custom_sdkconfig` is inherited unchanged, which matters for build times: pioarduino keeps one `sdkconfig.defaults` per project and rebuilds the whole Arduino/IDF framework whenever its hash changes. The two envs hash the same, so building them back to back costs one framework build, not two.

## Footprint

Measured as the flashable `firmware.bin` in CI on v3.0.0, which is what an OTA slot has to hold:

| Build | `firmware.bin` | Slot | Used | Free |
|-------|---------------|------|------|------|
| `smalltv` | 795,184 B | 1,044,464 B | 76.1% | 249,280 B |
| `smalltv_lean` | 658,896 B | 1,044,464 B | 63.1% | 385,568 B |
| `smalltv_c2` | 1,441,216 B | 1,572,864 B | 91.6% | 131,648 B |
| `smalltv_esp32` | 1,434,192 B | 1,572,864 B | 91.2% | 138,672 B |
| `smalltv_esp32_wg` | 1,479,984 B | 1,572,864 B | 94.1% | 92,880 B |
| `smalltv_esp32_8mb` | 1,499,104 B | 2,228,224 B | 67.3% | 729,120 B |

The ESP8266 slot comes from `eagle.flash.4m1m.ld`, the 4 MB ESP32 slot from `partitions/smalltv_4mb_ota.csv` and the Pro's from `partitions/smalltv_8mb_ota.csv`. The mascot frame data lives in flash, not the heap.

CI checks every image against its slot on each build (`tools/ci_checks.py size`) and fails the build when an image has less than 8,192 bytes free, so an image that cannot be installed over the air is never published. After a local `pio run`, `python tools/ci_checks.py size --min-free 8192 --allow-missing` runs the same check on the envs you built.

The PC-side usage daemon is a separate repo: [clawdmeter-daemon](https://github.com/giovi321/clawdmeter-daemon).
