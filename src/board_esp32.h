// board_esp32.h — pin map + panel quirks for the NMMiner NM-TV-154 ("NM-TV-Miner"
// PCB), a SmallTV-style BTC lottery miner: classic ESP32 (ESP32-D0WD-V3 in a
// WROOM-32E module, 40 MHz crystal), 1.54" 240x240 ST7789 IPS over SPI.
//
// Pin map taken from the vendor's own custom-firmware guide (TFT_eSPI User_Setup
// values): https://www.nmminer.com/2026/03/02/how-to-develop-nm-tv-custom-firmware/
// Verified on hardware by a community tester (issue #1): display, RGB colour
// order, backlight PWM, and the 4 MB flash layout all confirmed working.
#pragma once

#define TFT_SCLK   14
#define TFT_MOSI   13
#define TFT_DC      2
#define TFT_RST    -1   // reset line not connected on this panel -> GFX_NOT_DEFINED
#define TFT_CS     15
#define TFT_BL     19   // backlight (PWM, active-low per vendor guide)

// Separate panel power rail, switched by a GPIO. The vendor's init code drives it
// LOW to power the display; Gfx.cpp asserts it before touching the panel.
#define TFT_PWR_PIN 21
#define TFT_PWR_ON  LOW

// Panel colour order: verified RGB on hardware (issue #1), like the other boards.
#define TFT_BGR     0

// Backlight is active-low (vendor guide: TFT_BACKLIGHT_ON LOW). Runtime-overridable.
#define TFT_BL_DEFAULT_INVERTED true

// No ambient-light sensor documented on this board -> auto-brightness compiled out.
#define HAS_LDR     0
#define ADC_MAX  4095   // classic ESP32 ADC is 12-bit (unused while HAS_LDR == 0)
