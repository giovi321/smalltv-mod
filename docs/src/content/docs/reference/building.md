---
title: Building from source
description: Build any of the three board targets with PlatformIO, and the ESP32 toolchain notes.
---

The three board targets share one codebase and build from [PlatformIO](https://platformio.org/). Pick the env that matches your board.

```bash
pio run -e smalltv                 # ESP8266
pio run -e smalltv_c2              # ESP32-C2
pio run -e smalltv_esp32           # NM-TV-154 (classic ESP32)
pio run -e smalltv_c2 -t upload    # build + flash the C2 over USB-C
pio device monitor -e smalltv_c2   # serial logs @ 115200
```

## How one codebase builds for all three

Chip differences are centralized so the feature code stays the same on every board.

- `src/Platform.h` holds every chip-specific include, class alias, and small shim. The WiFi stack, web server, HTTPS client, OTA, and reset handling all resolve through it. Both ESP32 targets share its Arduino core 3.x branch.
- `src/board_esp8266.h`, `src/board_esp32c2.h`, and `src/board_esp32.h` hold the pin map and panel quirks for each board. `src/config.h` includes the right one based on the build target.
- The three feature modes, the web UI, and the settings layer are identical across all targets.

The target is chosen by a build flag: `SMALLTV_ESP8266`, `SMALLTV_ESP32C2`, or `SMALLTV_ESP32`.

## Project layout

```
src/                    shared core (device, net, web, settings)
  main.cpp              setup/loop and the mode registry
  Platform.h            per-chip includes, aliases, and shims
  board_esp8266.h       ESP8266 pin map and panel quirks
  board_esp32c2.h       ESP32-C2 pin map and panel quirks
  board_esp32.h         NM-TV-154 (classic ESP32) pin map and panel quirks
  config.h              limits, feature flags, defaults, board selector
  Settings.*            settings struct and LittleFS persistence
  Net.*                 WiFi station, fallback AP, captive portal, mDNS
  WebPortal.*           web server, REST API, OTA endpoint
  webui.h               the single-page UI (HTML/CSS/JS, served from flash)
  Gfx.*                 shared ST7789 core (Arduino_GFX)
  OtaUpdate.*           GitHub self-update (ESP8266)
  features/
    ticker/             TickerMode + StockClient
    usage/              UsageMode + UsageClient + Mascot
    radar/              RadarMode + RadarClient
partitions/             ESP32 flash layout (shared by both ESP32 targets)
n8n/                    webhook contract and importable workflow
```

## ESP32 toolchain notes

The two ESP32 targets have a few requirements the ESP8266 does not.

- **Platform**: PlatformIO's official espressif32 does not support the C2 and is stuck on Arduino core 2.x. Both ESP32 envs use the [pioarduino](https://github.com/pioarduino/platform-espressif32) fork, which tracks Arduino core 3.x on ESP-IDF 5.x (`Platform.h`'s ESP32 branch needs core 3.x). The first build downloads a large toolchain and compiles the IDF from source, so it takes several minutes. Later builds are fast.
- **Display driver**: both use `Arduino_HWSPI` with explicit pins. The register-level `Arduino_ESP32SPI` hangs on the C2, and the software-SPI path in the library does not cover it. `Arduino_HWSPI` uses the stock SPI driver and works.
- **Flashing the C2**: uploads call the system esptool, not the one bundled with PlatformIO, which hangs entering download mode on that board. Install it with `pip install esptool`. The classic ESP32 flashes with either.
- **Partitions**: the 4 MB layout in `partitions/smalltv_4mb_ota.csv` gives two OTA app slots plus about 0.9 MB for LittleFS, and is shared by both ESP32 targets.

## Footprint

The ESP8266 build is about 620 KB of flash and roughly half the RAM at boot, with headroom for OTA, which needs room for two sketch copies. The ESP32-C2 build is about 1.27 MB in a 1.5 MB app slot, using around 15 percent of RAM. The mascot frame data lives in flash, not the heap.

The PC-side usage daemon is a separate repo: [clawdmeter-daemon](https://github.com/giovi321/clawdmeter-daemon).
