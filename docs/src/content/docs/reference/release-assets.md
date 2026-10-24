---
title: Which release file to download
description: Every firmware asset attached to a release, what each name means, and which one your board needs.
---

Eleven files are attached to every release, and the name tells you which one you need. Read it in three parts: the board, whether the image is an app image or a factory image, and, on the two boards that get a second image, which feature set it carries.

Get them from the [Releases page](https://github.com/giovi321/smalltv-mod/releases). Untagged builds of the same eleven files are on the [Actions tab](https://github.com/giovi321/smalltv-mod/actions) under the latest `build` run.

## Pick by board, then by install method

| File | Board | Use it for |
|------|-------|-----------|
| `smalltv-mod-firmware.bin` | SmallTV and SmallTV-ultra (ESP8266) | The normal install and every later update |
| `smalltv-mod-firmware-lean.bin` | Same ESP8266 boards | The same device when it needs more heap. Home Assistant screens and the usage meter are compiled out |
| `smalltv-mod-loader.bin` | SmallTV-ultra (ESP8266) | One-time first install when the stock updater rejects the full image |
| `smalltv-mod-firmware-c2.bin` | SmallTV (ESP32-C2 / ESP8684) | Updates, once this firmware is already running |
| `smalltv-mod-firmware-c2.factory.bin` | SmallTV (ESP32-C2 / ESP8684) | The first install, over USB-C |
| `smalltv-mod-firmware-esp32.bin` | NM-TV-154 (classic ESP32) | Updates, once this firmware is already running |
| `smalltv-mod-firmware-esp32.factory.bin` | NM-TV-154 (classic ESP32) | The first install, over USB |
| `smalltv-mod-firmware-esp32-wg.bin` | NM-TV-154 (classic ESP32) | The same board when you want the WireGuard tunnel. Updates, once this firmware is already running |
| `smalltv-mod-firmware-esp32-wg.factory.bin` | NM-TV-154 (classic ESP32) | The same image, for a first install over USB |
| `smalltv-mod-firmware-esp32-pro.bin` | SmallTV Pro (classic ESP32, 8 MB) | The first install over the stock web UI, and every later update |
| `smalltv-mod-firmware-esp32-pro.factory.bin` | SmallTV Pro (classic ESP32, 8 MB) | A direct install or a recovery over the internal UART header |

Not sure which board you have? [Hardware and variants](/smalltv-mod/getting-started/hardware/) has photos and the tell-tale signs for each.

## How to read a file name

Every name follows the same pattern:

```
smalltv-mod-<image>[-<target>][.factory].bin
```

The `<image>` part is `firmware` for the real thing and `loader` for the minimal ESP8266 installer.

The `<target>` suffix names the board. No suffix at all means the original ESP8266:

| Suffix | Board |
|--------|-------|
| none | SmallTV and SmallTV-ultra (ESP8266), all features |
| `-lean` | The same ESP8266 boards, without Home Assistant screens or the usage meter |
| `-c2` | SmallTV with the ESP32-C2 / ESP8684 chip |
| `-esp32` | NM-TV-154, classic ESP32, 4 MB flash |
| `-esp32-wg` | The same NM-TV-154, with the WireGuard client compiled in |
| `-esp32-pro` | SmallTV Pro, classic ESP32, 8 MB flash |

`.factory` marks a merged image. It contains the bootloader, the partition table, and the app, and it gets written to flash offset `0x0` over a cable. A name without `.factory` is an app image: just the application, sized to drop into an OTA slot. The ESP32 boards need the factory image for their first install and the app image for updates after that. The ESP8266 needs no factory variant, because its single image already carries everything.

## Check what a device is running

The System tab shows the running variant next to the version, for example `smalltv-mod 2.12.0 (esp8266-lean)`. The same string is in `/api/status` as `variant`.

That name also decides what a self-update downloads. Each build is compiled with its own `UPDATE_ASSET` and `FW_VARIANT` (`src/config.h`), and the updater only accepts the release asset whose name matches. So a lean device fetches `smalltv-mod-firmware-lean.bin` and stays lean, and an `esp32-wg` device fetches `smalltv-mod-firmware-esp32-wg.bin` and keeps its tunnel. A self-update never moves a device between variants, in either direction: it will not quietly take features away, and it will not quietly add them.

Two boards have a second variant to stay within: the ESP8266 (`esp8266` and `esp8266-lean`) and the NM-TV-154 (`esp32` and `esp32-wg`). Every other board has one image and one variant.

## Switch a device from one variant to the other

Crossing between variants is deliberate, and it is a manual upload rather than a self-update. Download the other `.bin` for the same board and upload it in the System tab. This works over the air on both boards that have a pair, so no cable is needed: the two images in a pair share a partition table, so the swap is an ordinary OTA into the same slot.

Settings survive it. Both images in a pair read the same `config.json` from the same LittleFS partition, and neither touches the layout. A WireGuard configuration survives a round trip in particular: the settings store carries the `wg` block whether or not the client is compiled into the running image, so a device moved from `esp32-wg` to `esp32` and back finds the tunnel set up exactly as it was left.

One thing does not survive, and it is on the ESP8266 pair only. The lean image has no Home Assistant module, so screens already pushed to the device are dropped and their MQTT topics are no longer subscribed. Your retained messages on the broker are untouched, so going back to the standard image and letting it resubscribe brings the screens back.

After the swap the System tab reports the new variant, and from then on self-update follows the new one.

## Why the ESP8266 and the NM-TV-154 get two images and the other boards get one

The ESP8266 shares a single 80 KB DRAM arena between static allocations and the heap, so anything compiled in costs heap that TLS then cannot have. Two fetch paths refuse to start a handshake when memory runs short: the plane radar and a cash.ch quote each want a 16,000-byte contiguous block. Below that the affected screen goes quiet, and since 2.12.1 the Status tab's Radar line says so explicitly.

The lean image gives that back. Measured on the 2.12.0 build, static RAM drops from 55,536 bytes to 46,804, so the heap starts 8,732 bytes larger. Almost all of it is Home Assistant: 7,668 bytes, of which the four-screen store (`g_screens`, 5,792 bytes) and the icon cache (`g_ic`, 1,704 bytes) are the bulk, plus the 768-byte PubSubClient receive buffer that no longer gets allocated at runtime. The usage meter contributes 1,080 bytes.

The ESP32 boards have several times the RAM and manage TLS buffers dynamically through mbedTLS, so RAM never forces a second image on them.

Flash does, on one of them. The NM-TV-154's 4 MB layout gives each OTA slot 1,572,864 bytes, the same as the ESP32-C2's, but the classic ESP32's framework libraries cost about 100 KB more than the C2's, so the same code starts closer to the ceiling here. The v3.0.0 images measure 1,434,192 bytes without the WireGuard client and 1,479,984 bytes with it: the tunnel costs 45,792 bytes, about a third of what the plain image has spare. Both fit, and the slot cannot be enlarged without a partition-table change that no over-the-air update can install, so the client ships as the separate `-esp32-wg` image and the plain one keeps the headroom. The ESP32-C2 and the SmallTV Pro have room for the client without that trade, so they carry it in their only image.

See [Building from source](/smalltv-mod/reference/building/#the-smalltv_lean-env) to build either ESP8266 image yourself, or a slimmer combination of your own, and [the smalltv_esp32_wg env](/smalltv-mod/reference/building/#the-smalltv_esp32_wg-env) for the NM-TV-154 pair.
