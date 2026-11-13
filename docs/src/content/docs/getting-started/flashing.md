---
title: Flashing
description: How to install smalltv-mod on each board, back up the stock firmware first, and recover if something goes wrong.
---

Flash the method that matches your board. The ESP8266 and the SmallTV Pro install over the air from their stock web UI. The ESP32-C2 and the NM-TV-154 install over the USB cable with esptool. Back up the stock image first on any board so you can always go back.

Get the firmware image from the [Actions tab](https://github.com/giovi321/smalltv-mod/actions) (latest `build` run) or the [Releases page](https://github.com/giovi321/smalltv-mod/releases), or [build it yourself](/smalltv-mod/reference/building/).

## SmallTV (ESP8266)

### Over the air, from the stock web UI

The stock GeekMagic firmware exposes an OTA updater at `/update` that accepts any valid ESP8266 image, so you can install this without opening the device.

1. Find the device IP. It is shown on screen or in the stock Settings app.
2. Browse to `http://<device-ip>/update`.
3. Upload `smalltv-mod-firmware.bin`. It reboots into this firmware.

Upload `smalltv-mod-firmware-lean.bin` instead if you want the build without Home Assistant screens, the usage meter and theme clocks, which leaves more heap for the radar and the cash.ch tickers. Both files install the same way and you can swap between them later. See [Which release file to download](/smalltv-mod/reference/release-assets/).

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

The fix is a two-step install through a tiny loader, no soldering. The loader is an image of about 314 KB that does fit the stock slot. Once running it uses this firmware's own 4m1m flash layout, whose app region is large, so the full image fits on the second hop.

1. Get `smalltv-mod-loader.bin` from the [Releases page](https://github.com/giovi321/smalltv-mod/releases).
2. Browse to `http://<device-ip>/update` and upload `smalltv-mod-loader.bin`. It fits the stock slot. The device reboots into the loader.
3. The loader opens an open WiFi access point named `SmallTV-Loader`, and its screen shows the network name and the address. Join it and browse to `http://192.168.4.1`; the upload page opens there.
4. Upload `smalltv-mod-firmware.bin` there. It fits because the loader uses this firmware's flash layout, which leaves the upload about 2.8 MB against an image of about 840 KB. The screen shows the upload progress, then the device reboots into the full smalltv-mod. Keep it powered until smalltv-mod's own screen appears.
5. It comes up in the usual `SmallTV-Setup` captive portal for WiFi. From here it is a normal ESP8266 smalltv-mod device.

After this first install, normal OTA works from the System tab, because this firmware's layout has room for two sketch copies. You only need the loader once.

The loader path does not erase the flash, so the ESP8266's stored RF settings from the stock firmware survive it. If the device then never joins your WiFi and the router log shows `router refused (203)`, update to a firmware that has the join fix (it resets those settings before every join). If it still fails, erase the flash over the serial header (`esptool.py --port COM5 erase_flash`) and write the image as in the [UART recovery](#uart-header-for-recovery-or-a-direct-install) steps.

If the loader ever will not come back up, flash over the serial header instead. The board is a plain ESP-12F, so the [UART recovery](#uart-header-for-recovery-or-a-direct-install) steps above apply directly: `esptool.py --port COM5 write_flash 0x0 smalltv-mod-firmware.bin`.

## SDPro Smart Weather Clock

Confirmed working on a unit by a community tester ([issue #76](https://github.com/giovi321/smalltv-mod/issues/76)). The SDPro is an unbranded SmallTV-ultra clone with `SDPro_V1.x` firmware. A [teardown](https://github.com/michaeldale/smartclock-oss/blob/main/docs/specifications/hardware.md) shows the same ESP-12 (ESP8266), 4 MB flash, ST7789 panel and pin map as the SmallTV, so `smalltv-mod-firmware.bin` is the image it runs.

Two things in the stock firmware decide the install:

- The update page refuses a file whose name does not start with `SDP`. The check runs only in the browser, so renaming the file is enough
- The stock flash layout keeps a 3 MB filesystem, which leaves an upload about 550 KB. `smalltv-mod-firmware.bin` (about 840 KB) and `smalltv-mod-firmware-lean.bin` (about 680 KB) do not fit; `smalltv-mod-loader.bin` (about 314 KB) does

So the install is the SmallTV-ultra's two steps with a renamed loader:

1. Rename `smalltv-mod-loader.bin` to `SDP_loader.bin` and upload it through the stock update page.
2. Join the open `SmallTV-Loader` access point and browse to `http://192.168.4.1`, where the upload page opens. The loader's screen shows the network name and the address.
3. Upload `smalltv-mod-firmware.bin` there. On first boot the firmware formats its filesystem, since that area still holds part of the stock volume, and opens the usual `SmallTV-Setup` portal.

The USB-C port is power only and the unit has no button, so a failed flash can only be recovered over the ESP-12's UART pads (TX, RX, GPIO0, GND, 3V3, EN; GPIO0 to GND for flash mode). Find them and back up the stock image (`esptool.py --port COM5 read_flash 0x0 0x400000 stock-backup.bin`) before step 1. The [UART recovery](#uart-header-for-recovery-or-a-direct-install) steps then apply as for any ESP-12.

## SmallTV (ESP32-C2 / ESP8684)

The ESP32-C2 has no OTA path from the stock firmware, so the first install goes over the USB-C cable. The onboard CH340C handles it with auto-reset, so no button is needed. You need a system Python with esptool (`pip install esptool`).

### Back up the stock image first

```bash
python -m esptool --chip esp32c2 --port COM3 read_flash 0x0 0x400000 stock-backup.bin
```

Keep `stock-backup.bin` somewhere safe. Writing it back with `write_flash 0x0 stock-backup.bin` returns the device to factory at any point.

### Write this firmware

Download `smalltv-mod-firmware-c2.factory.bin` from the [Releases page](https://github.com/giovi321/smalltv-mod/releases): a single merged image with the bootloader, partition table, and app (a local build produces the same file as `firmware.factory.bin`). Write it at offset 0:

```bash
python -m esptool --chip esp32c2 --port COM3 --baud 921600 write_flash 0x0 smalltv-mod-firmware-c2.factory.bin
```

With a source checkout, `pio run -e smalltv_c2 -t upload` does the same thing.

:::caution
Use the system esptool, not the one bundled with PlatformIO. The bundled version hangs entering download mode on this board's CH340C. The `smalltv_c2` PlatformIO env is configured to call the system esptool for uploads for this reason.
:::

## NM-TV-154 (classic ESP32)

Same procedure as the ESP32-C2, with `--chip esp32` and `smalltv-mod-firmware-esp32.factory.bin` from the [Releases page](https://github.com/giovi321/smalltv-mod/releases) (or build it with `pio run -e smalltv_esp32`).

This board has two images. The plain one above has no WireGuard client; `smalltv-mod-firmware-esp32-wg.factory.bin` does, for reaching the device from outside your LAN. They share a partition table, so either can be flashed here, and you can move between them later as an ordinary over-the-air upload in the System tab. [Which release file to download](/smalltv-mod/reference/release-assets/#switch-a-device-from-one-variant-to-the-other) explains the swap and what it costs in flash.

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

Swap in `smalltv-mod-firmware-esp32-wg.factory.bin` for the image with the WireGuard client.

With a source checkout, `pio run -e smalltv_esp32 -t upload` does the same thing, or `pio run -e smalltv_esp32_wg -t upload` for the WireGuard image.

## SmallTV Pro (classic ESP32, 8 MB)

The SmallTV Pro has **no USB-serial chip**: its USB-C port is power only. There are two ways in: over the air from the stock web UI (no tools, no soldering), or over an internal UART header (needs opening the case and a 3.3 V USB-UART adapter).

### Over the air, from the stock web UI

The stock firmware exposes an OTA updater, the same way the ESP8266 SmallTV does. This firmware's 8 MB partition layout matches the stock table exactly (same OTA slots, verified from a live device), so the stock updater can install it directly:

1. Find the device IP (shown in the stock Settings app).
2. Browse to `http://<device-ip>/update`.
3. Upload `smalltv-mod-firmware-esp32-pro.bin` (the plain app image, **not** the `.factory.bin`) from the [Releases page](https://github.com/giovi321/smalltv-mod/releases). It reboots into this firmware.

This path keeps the stock bootloader and partition table in place and cannot be undone without a backup, and the stock image is not redistributed anywhere official. If you want the option to return to stock, take the UART backup below **before** flashing OTA.

### UART header, for backup, recovery, or a direct install

Open the case. The PCB has a 6-pad serial header (pad map from the [HA community thread](https://community.home-assistant.io/t/installing-esphome-on-geekmagic-smart-weather-clock-smalltv-pro/618029/384)):

| Pad | Function | Wire it to |
|-----|----------|-----------|
| 1 (square) | GND | GND on the adapter |
| 2 | TX | the adapter's RX |
| 3 | RX | the adapter's TX |
| 4 | 3V3 | 3.3 V on the adapter |
| 5 | GPIO0 | GND during power-on to enter download mode |
| 6 | RST | optional, tie to GND briefly to reset |

Hold pad 5 (GPIO0) to GND while the device powers on, then release it. Back up the full 8 MB stock image first:

```bash
python -m esptool --chip esp32 --port COM3 read_flash 0x0 0x800000 stock-backup.bin
```

Writing it back with `write_flash 0x0 stock-backup.bin` returns the device to factory at any point. To install this firmware over UART instead of OTA, write the merged image:

```bash
python -m esptool --chip esp32 --port COM3 --baud 921600 write_flash 0x0 smalltv-mod-firmware-esp32-pro.factory.bin
```

With a source checkout, `pio run -e smalltv_esp32_8mb -t upload` does the same thing over the adapter's COM port.

## After the first flash

Every board then updates from the browser: open the web UI's **System** tab and either let the device pull the newest GitHub release itself (each board fetches its own image) or upload a firmware file manually. The manual upload takes the plain app image (`smalltv-mod-firmware*.bin`), not the `.factory.bin`.

:::caution[ESP8266 on firmware 2.6.1 or older: update manually once]
The GitHub self-update was broken on the ESP8266 (original SmallTV and SmallTV-ultra) in every firmware up to and including 2.6.1: the release check misparsed GitHub's occasionally chunked responses, and the download needs a 16 KB TLS buffer that does not fit next to the running firmware, so it always failed with `download failed: HTTP error: connection failed`. A broken updater cannot update itself. Update these devices **once by hand**: download `smalltv-mod-firmware.bin` from the [Releases page](https://github.com/giovi321/smalltv-mod/releases) and upload it in the web UI's **System** tab. From 2.7.0 on, self-update works on the ESP8266 too.
:::

The ESP32 boards download and flash in place. The ESP8266 (from 2.7.0) uses an update-at-boot flow instead, because the download's 16 KB TLS buffer only fits before the firmware's features start: the device queues the update, reboots, shows `updating...` while it downloads, then reboots again into the new version. Expect two reboots and a couple of minutes; if the download fails, the device boots normally and the System tab shows why.

## Recovery

- **Re-flash anything** (your stock backup or this firmware) with the method for your board.
- **Factory reset** in the System tab wipes saved settings and restarts in SETUP MODE. It does not change the firmware.
- On the ESP32 boards, if a bad flash leaves the device unresponsive, it still enters download mode over USB, so esptool can always rewrite it.
