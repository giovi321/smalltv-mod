---
title: Hardware and variants
description: The three supported boards (ESP8266, ESP32-C2, and the classic-ESP32 NM-TV-154), how to tell them apart, and the pin map for each.
---

Three boards wear the same cube. Confirm which one you have before you flash it, because they use different chips and flash differently. The screen is the same 1.54" 240×240 ST7789 IPS panel on all of them.

The GeekMagic SmallTV is a 45 × 35 × 40 mm cube with a 28 × 28 mm colour screen and a USB-C port for power. It sells for about 6 to 8 EUR on AliExpress. A second version of the hardware, sold under the same "smart weather clock" listing, swaps the ESP8266 for an ESP32-C2 but keeps the case and screen. A third device, the NMMiner NM-TV-154 BTC lottery miner, uses the same cube and screen with a classic ESP32 inside.

## Tell them apart

Look at the board through the case vents, or open it (four clips, no glue).

- **SmallTV (ESP8266)**: an ESP8266 module, no separate USB-serial chip. Flashes over the air.
- **SmallTV (ESP32-C2)**: a bare **ESP8684** chip (that is the ESP32-C2) plus a **CH340C** USB-serial chip next to the USB-C port. Flashes over USB-C.
- **NM-TV-154 (ESP32)**: the PCB reads **NM-TV-Miner** and carries an **ESP32-WROOM-32E** module. Sold as an NMMiner BTC lottery miner, not as a weather clock. Flashes over USB.

The two chips on the ESP32-C2 board are the giveaway. The main SoC is marked `ESP8684`, and the small 16-pin chip by the USB-C port is the `CH340C`.

![The ESP8684 (ESP32-C2) main chip on the ESP32-C2 board](/smalltv-mod/assets/board-c2-esp8684.jpg)

![The CH340C USB-serial chip next to the USB-C port](/smalltv-mod/assets/board-c2-ch340c.jpg)

## SmallTV (ESP8266)

![The SmallTV (ESP8266)](/smalltv-mod/assets/product-8266.png)

| | |
|---|---|
| MCU | ESP-12F (ESP8266), 4 MB flash |
| Display | 1.54" 240×240 IPS ST7789, hardware SPI |
| Body | 45 × 35 × 40 mm, USB-C power |
| Light sensor | optional LDR on `A0` (not populated on all units) |
| Build env | `smalltv` |

### Pin map

Hardware SPI on the ESP8266 uses fixed clock and data pins. The rest are the SmallTV wiring, confirmed from teardowns and the ESPHome and Tasmota communities.

| Signal | GPIO | Note |
|--------|------|------|
| SPI CLK | 14 | hardware SPI (fixed) |
| SPI MOSI | 13 | hardware SPI (fixed) |
| DC | 0 | boot-strap pin |
| RST | 2 | boot-strap pin |
| CS | 15 | boot-strap pin |
| Backlight | 5 | PWM, active-low |

## SmallTV (ESP32-C2 / ESP8684)

![The SmallTV (ESP32-C2)](/smalltv-mod/assets/product-c2.png)

| | |
|---|---|
| MCU | ESP32-C2 (ESP8684), 4 MB embedded flash, RISC-V, 120 MHz |
| USB-serial | CH340C on the USB-C port (auto-reset wired) |
| Regulator | AMS1117-3.3 |
| Display | 1.54" 240×240 IPS ST7789V, SPI, RGB colour order |
| Build env | `smalltv_c2` |

The ESP32-C2 has no native USB. The CH340C bridges the USB-C port to the chip's UART, which is how it is flashed. Its auto-reset is wired, so esptool enters download mode on its own with no button to hold.

### Pin map

The ESP32-C2 routes SPI through the GPIO matrix, so the display pins are arbitrary GPIOs rather than fixed hardware-SPI pins. This map comes from a community ESPHome config for this exact board and is confirmed working.

| Signal | GPIO | Note |
|--------|------|------|
| SPI CLK | 4 | |
| SPI MOSI | 6 | |
| DC | 5 | |
| RST | 1 | |
| CS | GND | tied low on the panel, not driven |
| Backlight | 18 | PWM, active-low |

The pins are set in `src/board_esp32c2.h`. Two panel quirks are worth knowing: the display needs SPI mode 3, and its colour order is RGB. Both are handled in `src/Gfx.cpp`. If red and blue look swapped on your unit, flip `TFT_BGR` in the board header and reflash.

## NM-TV-154 (classic ESP32)

![The NM-TV-154 running its stock NMMiner firmware](/smalltv-mod/assets/product-esp32.png)

| | |
|---|---|
| MCU | ESP32-WROOM-32E (ESP32-D0WD-V3), 40 MHz crystal, 4 MB flash |
| Display | 1.54" 240×240 IPS ST7789, SPI, RGB colour order |
| Sold as | NMMiner NM-TV-154 BTC lottery miner, PCB marked "NM-TV-Miner" |
| Build env | `smalltv_esp32` |

The pin map comes from [NMMiner's own custom-firmware guide](https://www.nmminer.com/2026/03/02/how-to-develop-nm-tv-custom-firmware/) for the device and is confirmed working on hardware by a community tester in [issue #1](https://github.com/giovi321/smalltv-mod/issues/1): display, colours, backlight PWM, and the 4 MB flash layout all check out.

### Pin map

| Signal | GPIO | Note |
|--------|------|------|
| SPI CLK | 14 | |
| SPI MOSI | 13 | |
| DC | 2 | |
| RST | not connected | panel reset line is unwired |
| CS | 15 | |
| Backlight | 19 | PWM, active-low |
| Panel power | 21 | driven low at boot to power the display |

The pins are set in `src/board_esp32.h`.

## If the screen looks wrong

The pins are fixed to the SmallTV wiring and are not editable from the web UI. A few display symptoms have settings-level fixes:

- **Dark screen with backlight on**: try toggling "Backlight is active-low" in the Display tab. All boards default to active-low.
- **Wrong orientation**: change Orientation in the Display tab.
- **Red and blue swapped**: set `TFT_BGR` to `1` in your board's `src/board_*.h` header, rebuild, and reflash.

For a different board entirely, change the pins in the relevant `src/board_*.h` header.
