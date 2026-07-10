---
title: Building from source
description: Build either board target with PlatformIO, and the ESP32-C2 toolchain notes.
---

The two board targets share one codebase and build from [PlatformIO](https://platformio.org/). Pick the env that matches your board.

```bash
pio run -e smalltv                 # ESP8266
pio run -e smalltv_c2              # ESP32-C2
pio run -e smalltv_c2 -t upload    # build + flash the C2 over USB-C
pio device monitor -e smalltv_c2   # serial logs @ 115200
```

## How one codebase builds for both

Chip differences are centralized so the feature code stays the same on both boards.

- `src/Platform.h` holds every chip-specific include, class alias, and small shim. The WiFi stack, web server, HTTPS client, OTA, and reset handling all resolve through it.
- `src/board_esp8266.h` and `src/board_esp32c2.h` hold the pin map and panel quirks for each board. `src/config.h` includes the right one based on the build target.
- The three feature modes, the web UI, and the settings layer are identical across both targets.

The target is chosen by a build flag: `SMALLTV_ESP8266` or `SMALLTV_ESP32C2`.

## Project layout

```
src/                    shared core (device, net, web, settings)
  main.cpp              setup/loop and the mode registry
  Platform.h            per-chip includes, aliases, and shims
  board_esp8266.h       ESP8266 pin map and panel quirks
  board_esp32c2.h       ESP32-C2 pin map and panel quirks
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
partitions/             ESP32-C2 flash layout
n8n/                    webhook contract and importable workflow
```

## ESP32-C2 toolchain notes

The ESP32-C2 target has a few requirements the ESP8266 does not.

- **Platform**: PlatformIO's official espressif32 does not support the C2. The `smalltv_c2` env uses the [pioarduino](https://github.com/pioarduino/platform-espressif32) fork, which tracks Arduino core 3.x on ESP-IDF 5.x. The first build downloads a large toolchain and compiles the IDF from source, so it takes several minutes. Later builds are fast.
- **Display driver**: the C2 uses `Arduino_HWSPI` with explicit pins. The register-level `Arduino_ESP32SPI` hangs on this chip, and the software-SPI path in the library does not cover the C2. `Arduino_HWSPI` uses the stock SPI driver and works.
- **Flashing**: uploads call the system esptool, not the one bundled with PlatformIO, which hangs entering download mode on this board. Install it with `pip install esptool`.
- **Partitions**: the 4 MB layout in `partitions/smalltv_4mb_ota.csv` gives two OTA app slots plus about 0.9 MB for LittleFS.

## Footprint

The ESP8266 build is about 620 KB of flash and roughly half the RAM at boot, with headroom for OTA, which needs room for two sketch copies. The ESP32-C2 build is about 1.27 MB in a 1.5 MB app slot, using around 15 percent of RAM. The mascot frame data lives in flash, not the heap.

The PC-side usage daemon is a separate repo: [clawdmeter-daemon](https://github.com/giovi321/clawdmeter-daemon).
