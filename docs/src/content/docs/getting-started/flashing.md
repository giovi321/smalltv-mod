---
title: Flashing
description: How to install smalltv-mod on each board, back up the stock firmware first, and recover if something goes wrong.
---

Flash the method that matches your board. The ESP8266 installs over the air from its stock web UI. The ESP32-C2 and the NM-TV-154 install over the USB cable with esptool. Back up the stock image first on any board so you can always go back.

Get the firmware image from the [Actions tab](https://github.com/giovi321/smalltv-mod/actions) (latest `build` run) or the [Releases page](https://github.com/giovi321/smalltv-mod/releases), or [build it yourself](/smalltv-mod/reference/building/).

## SmallTV (ESP8266)

### Over the air, from the stock web UI

The stock GeekMagic firmware exposes an OTA updater at `/update` that accepts any valid ESP8266 image, so you can install this without opening the device.

1. Find the device IP. It is shown on screen or in the stock Settings app.
2. Browse to `http://<device-ip>/update`.
3. Upload `smalltv-mod-firmware.bin`. It reboots into this firmware.

Back up the stock firmware first if you want the option to return to it. The stock image is not redistributed here, so read it off your own device over the UART header before you overwrite it.

### UART header, for recovery

If OTA is not available or the device will not boot, flash over the serial header. You need a 3.3 V USB-UART adapter. Pull **GPIO0 to GND** while powering on to enter flash mode, then:

```bash
# back up the original firmware first (4 MB)
esptool.py --port COM5 read_flash 0x0 0x400000 stock-backup.bin

# write this firmware
esptool.py --port COM5 write_flash 0x0 smalltv-mod-firmware.bin
```

## SmallTV (ESP32-C2 / ESP8684)

The ESP32-C2 has no OTA path from the stock firmware, so the first install goes over the USB-C cable. The onboard CH340C handles it with auto-reset, so no button is needed. You need a system Python with esptool (`pip install esptool`).

### Back up the stock image first

```bash
python -m esptool --chip esp32c2 --port COM3 read_flash 0x0 0x400000 stock-backup.bin
```

Keep `stock-backup.bin` somewhere safe. Writing it back with `write_flash 0x0 stock-backup.bin` returns the device to factory at any point.

### Write this firmware

The build produces a single merged image, `firmware.factory.bin`, that contains the bootloader, partition table, and app. Write it at offset 0:

```bash
python -m esptool --chip esp32c2 --port COM3 --baud 921600 write_flash 0x0 firmware.factory.bin
```

With a source checkout, `pio run -e smalltv_c2 -t upload` does the same thing.

:::caution
Use the system esptool, not the one bundled with PlatformIO. The bundled version hangs entering download mode on this board's CH340C. The `smalltv_c2` PlatformIO env is configured to call the system esptool for uploads for this reason.
:::

## NM-TV-154 (classic ESP32)

Same procedure as the ESP32-C2, with `--chip esp32`. Build the image from source with `pio run -e smalltv_esp32`; no prebuilt image is published yet.

### Back up the stock image first

The NMMiner stock firmware is not redistributed anywhere official, so this backup is your only way back:

```bash
python -m esptool --chip esp32 --port COM3 read_flash 0x0 0x400000 stock-backup.bin
```

If `esptool flash_id` reports more than 4 MB of flash, adjust the read length to match.

### Write this firmware

```bash
python -m esptool --chip esp32 --port COM3 --baud 921600 write_flash 0x0 firmware.factory.bin
```

With a source checkout, `pio run -e smalltv_esp32 -t upload` does the same thing.

## After the first flash

Every board then updates from the browser. Open the web UI, go to the **Update** tab, and upload a new firmware image. On the ESP8266 you can also let the device pull the newest GitHub release itself. The ESP32 boards use the manual browser upload.

## Recovery

- **Re-flash anything** (your stock backup or this firmware) with the method for your board.
- **Factory reset** in the Update tab wipes saved settings and restarts in SETUP MODE. It does not change the firmware.
- On the ESP32 boards, if a bad flash leaves the device unresponsive, it still enters download mode over USB, so esptool can always rewrite it.
