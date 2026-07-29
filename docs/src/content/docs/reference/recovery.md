---
title: Recovery and credits
description: Going back to stock, known limitations, and credits.
---

## Going back to stock

Keep a `stock-backup.bin` from before your first flash and you can always return the device to factory.

- **Re-flash anything** (your stock backup or this firmware) with the method for your board, described in [Flashing](/smalltv-mod/getting-started/flashing/).
- **Factory reset** in the Update tab wipes saved settings and restarts in SETUP MODE. It does not change the firmware.
- On the ESP32 boards (C2 and NM-TV-154), a bad flash never bricks the device: it still enters download mode over USB, so esptool can rewrite it.

## Notes and limitations

- 2.4 GHz WiFi only. WPA2. For an AP password use at least 8 characters, or leave it blank for an open hotspot.
- The web server is single-threaded, so the UI may pause briefly during a data poll.
- HTTPS works on the ESP8266 but is RAM-tight. Prefer plain HTTP on your LAN for a webhook if you see instability. The ESP32 boards have more headroom.
- The GitHub self-update works on every board from 2.7.0. The ESP32 boards download in place; the ESP8266 updates at boot (two reboots — see [Flashing](/smalltv-mod/getting-started/flashing/#after-the-first-flash)). ESP8266 devices still on 2.6.1 or older have a broken updater and need one manual upload in the Update tab to get current.
- Fonts are the built-in bitmap font, scaled, chosen for reliability and the retro look. No external font files are needed.

## Credits and references

- GeekMagic SmallTV and SmallTV-Pro, the original product and stock firmware ([GeekMagicClock/smalltv-pro](https://github.com/GeekMagicClock/smalltv-pro)).
- Pin maps and hardware notes from the ESPHome and Tasmota communities:
  - [ViToni/esphome-geekmagic-smalltv](https://github.com/ViToni/esphome-geekmagic-smalltv)
  - [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)
  - [Installing ESPHome on a new smart weather clock (HA community)](https://community.home-assistant.io/t/installing-esphome-on-new-smart-weather-clock-wifi-weather-station-display/1006172), which documented the ESP32-C2 pin map
  - [Puddle of Code, My Own GeekMagic SmallTV](https://puddleofcode.com/story/my-own-geekmagic-smalltv/)
  - [NMMiner's NM-TV-154 custom firmware guide](https://www.nmminer.com/2026/03/02/how-to-develop-nm-tv-custom-firmware/), which documents the NM-TV-154 pin map
- The plane radar's look reimplements [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar), a sonar-style ADS-B radar for a 1.28" round display: heading triangles, speed vectors, callsign and altitude tags, and rim dots for out-of-range traffic all come from that design.
- Claude usage mode reimplements [clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter) for this hardware, over HTTP and WiFi instead of serial. The mascot frames come from [claudepix](https://claudepix.vercel.app).
- Libraries: [Arduino_GFX](https://github.com/moononournation/Arduino_GFX), [ArduinoJson](https://arduinojson.org/).

## License

[WTFPL](https://github.com/mosvov/smalltv-mod/blob/main/LICENSE). Do What The F*ck You Want To Public License.
