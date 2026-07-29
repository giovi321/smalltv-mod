---
title: Flashing
description: How to install smalltv-mod on each board, back up the stock firmware first, and recover if something goes wrong.
---

Flash the method that matches your board. The ESP8266 installs over the air from its stock web UI. The ESP32-C2 and the NM-TV-154 install over the USB cable with esptool. Back up the stock image first on any board so you can always go back.

Get the firmware image from the [Actions tab](https://github.com/mosvov/smalltv-mod/actions) (latest `build` run) or the [Releases page](https://github.com/mosvov/smalltv-mod/releases), or [build it yourself](/smalltv-mod/reference/building/).

## SmallTV (ESP8266)

### Over the air, from the stock web UI

The stock GeekMagic firmware exposes an OTA updater at `/update` that accepts any valid ESP8266 image, so you can install this without opening the device.

1. Find the device IP. It is shown on screen or in the stock Settings app.
2. Browse to `http://<device-ip>/update`.
3. Upload `smalltv-mod-firmware.bin`. It reboots into this firmware.

Back up the stock firmware first if you want the option to return to it. The stock image is not redistributed here, so read it off your own device over the UART header before you overwrite it.

### UART header, for recovery or a direct install

The serial header is the guaranteed way in. Use it if OTA is not available, if the device will not boot, or if you would rather skip OTA and flash directly.

Open the case first: remove the two screws on the underside and lift out the inner tray that holds the PCB. Six labelled pads sit on the board:

| Pad | Wire it to |
|-----|-----------|
| `3V3` | 3.3 V on the adapter |
| `GND` | GND on the adapter |
| `TXD0` | the adapter's RX |
| `RXD0` | the adapter's TX |
| `GPIO0` | GND, to enter flash mode (only during power-on) |
| `RST` | optional, tie to GND briefly to reset |

Use a 3.3 V USB-UART adapter. Hold `GPIO0` to `GND` while powering on to enter flash mode, then release it. Pad map from [ViToni/esphome-geekmagic-smalltv](https://github.com/ViToni/esphome-geekmagic-smalltv).

Back up the stock image first if you might want it back:

```bash
esptool.py --port COM5 read_flash 0x0 0x400000 stock-backup.bin
```

Then flash. On a SmallTV-ultra, erase first so the stock partition layout goes with it; on a plain SmallTV the erase is optional but harmless:

```bash
# clear the old layout (partition + data), then write the firmware
esptool.py --port COM5 erase_flash
esptool.py --port COM5 write_flash 0x0 smalltv-mod-firmware.bin
```

That single image carries everything the ESP8266 needs, so there is no separate partition file to flash. On first boot it recreates its own filesystem and opens the `SmallTV-Setup` portal.

## SmallTV-ultra (stock updater says "Not Enough Space")

The SmallTV-ultra is the same ESP-12F (ESP8266) as the original SmallTV: same 4 MB flash, the same 1.54" 240×240 ST7789 panel, and the same pin map. Only its stock firmware differs. The "Ultra" firmware (for example Ultra-V9.0.50) reserves most of the flash for image and GIF storage, which leaves its OTA updater a small app slot. Uploading `smalltv-mod-firmware.bin` at `/update` fails with `Update error: ERROR[4]: Not Enough Space`, even though the flash has plenty of free user space, because the full image does not fit that small slot.

The fix is a two-step install through a tiny loader, no soldering. The loader is a ~308 KB image that does fit the stock slot. Once running it uses this firmware's own 4m1m flash layout, whose app region is large, so the full image fits on the second hop.

1. Get `smalltv-mod-loader.bin` from the [Releases page](https://github.com/mosvov/smalltv-mod/releases).
2. Browse to `http://<device-ip>/update` and upload `smalltv-mod-loader.bin`. It fits the stock slot. The device reboots into the loader.
3. The loader opens an open WiFi access point named `SmallTV-Loader`. Join it and browse to `http://192.168.4.1/update`.
4. Upload `smalltv-mod-firmware.bin` there. It fits because the loader uses this firmware's flash layout, which leaves about 700 KB free for OTA against the roughly 651 KB image. The device reboots into the full smalltv-mod.
5. It comes up in the usual `SmallTV-Setup` captive portal for WiFi. From here it is a normal ESP8266 smalltv-mod device.

After this first install, normal OTA works from the Update tab, because this firmware's layout has room for two sketch copies. You only need the loader once.

If the loader ever will not come back up, flash over the serial header instead. The board is a plain ESP-12F, so the [UART recovery](#uart-header-for-recovery) steps above apply directly: `esptool.py --port COM5 write_flash 0x0 smalltv-mod-firmware.bin`.

## SmallTV (ESP32-C2 / ESP8684)

The ESP32-C2 has no OTA path from the stock firmware, so the first install goes over the USB-C cable. The onboard CH340C handles it with auto-reset, so no button is needed. You need a system Python with esptool (`pip install esptool`).

### Back up the stock image first

```bash
python -m esptool --chip esp32c2 --port COM3 read_flash 0x0 0x400000 stock-backup.bin
```

Keep `stock-backup.bin` somewhere safe. Writing it back with `write_flash 0x0 stock-backup.bin` returns the device to factory at any point.

### Write this firmware

Download `smalltv-mod-firmware-c2.factory.bin` from the [Releases page](https://github.com/mosvov/smalltv-mod/releases): a single merged image with the bootloader, partition table, and app (a local build produces the same file as `firmware.factory.bin`). Write it at offset 0:

```bash
python -m esptool --chip esp32c2 --port COM3 --baud 921600 write_flash 0x0 smalltv-mod-firmware-c2.factory.bin
```

With a source checkout, `pio run -e smalltv_c2 -t upload` does the same thing.

:::caution
Use the system esptool, not the one bundled with PlatformIO. The bundled version hangs entering download mode on this board's CH340C. The `smalltv_c2` PlatformIO env is configured to call the system esptool for uploads for this reason.
:::

## NM-TV-154 (classic ESP32)

Same procedure as the ESP32-C2, with `--chip esp32` and `smalltv-mod-firmware-esp32.factory.bin` from the [Releases page](https://github.com/mosvov/smalltv-mod/releases) (or build it with `pio run -e smalltv_esp32`).

### Back up the stock image first

The NMMiner stock firmware is not redistributed anywhere official, so this backup is your only way back:

```bash
python -m esptool --chip esp32 --port COM3 read_flash 0x0 0x400000 stock-backup.bin
```

If `esptool flash_id` reports more than 4 MB of flash, adjust the read length to match.

### Write this firmware

```bash
python -m esptool --chip esp32 --port COM3 --baud 921600 write_flash 0x0 smalltv-mod-firmware-esp32.factory.bin
```

With a source checkout, `pio run -e smalltv_esp32 -t upload` does the same thing.

## After the first flash

Every board then updates from the browser: open the web UI's **Update** tab and either let the device pull the newest GitHub release itself (each board fetches its own image) or upload a firmware file manually. The manual upload takes the plain app image (`smalltv-mod-firmware*.bin`), not the `.factory.bin`.

:::caution[ESP8266 on firmware 2.6.1 or older: update manually once]
The GitHub self-update was broken on the ESP8266 (original SmallTV and SmallTV-ultra) in every firmware up to and including 2.6.1: the release check misparsed GitHub's occasionally chunked responses, and the download needs a 16 KB TLS buffer that does not fit next to the running firmware, so it always failed with `download failed: HTTP error: connection failed`. A broken updater cannot update itself. Update these devices **once by hand**: download `smalltv-mod-firmware.bin` from the [Releases page](https://github.com/mosvov/smalltv-mod/releases) and upload it in the web UI's **Update** tab. From 2.7.0 on, self-update works on the ESP8266 too.
:::

The ESP32 boards download and flash in place. The ESP8266 (from 2.7.0) uses an update-at-boot flow instead, because the download's 16 KB TLS buffer only fits before the firmware's features start: the device queues the update, reboots, shows `updating...` while it downloads, then reboots again into the new version. Expect two reboots and a couple of minutes; if the download fails, the device boots normally and the Update tab shows why.

## Recovery

- **Re-flash anything** (your stock backup or this firmware) with the method for your board.
- **Factory reset** in the Update tab wipes saved settings and restarts in SETUP MODE. It does not change the firmware.
- On the ESP32 boards, if a bad flash leaves the device unresponsive, it still enters download mode over USB, so esptool can always rewrite it.
