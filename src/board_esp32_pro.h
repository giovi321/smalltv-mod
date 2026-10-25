// board_esp32_pro.h — pin map + panel quirks for the GeekMagic SmallTV Pro:
// classic ESP32 (esp32dev), 8 MB flash, 1.54" 240x240 ST7789V IPS over SPI
// (VSPI-default pins), same panel family as the other boards.
//
// Pin map taken from the ESPHome SmallTV Pro config and verified on hardware
// by the GeekTV-Pro alternative firmware (its stock firmware drives this panel
// at 60 MHz SPI, mode 3). Note the NM-TV-154 pins in board_esp32.h
// (GPIO 13/14/15/19/21) are a DIFFERENT board and do not apply here.
//
// Capacitive touch button on GPIO 32 (ESP32 T9), handled by buttonService() in main.cpp
// (same implementation as board_esp32.h). The press threshold is calibrated at runtime.
#pragma once

#define TFT_SCLK   18
#define TFT_MOSI   23
#define TFT_DC      2
#define TFT_RST     4
#define TFT_CS     -1   // CS tied to GND on the PCB -> GFX_NOT_DEFINED
#define TFT_BL     25   // backlight (PWM, active-low)

// Panel colour order: RGB, like the other SmallTV-family panels.
#define TFT_BGR     0

// Backlight is active-low (inverted PWM per the ESPHome config). Runtime-overridable.
#define TFT_BL_DEFAULT_INVERTED true

// No ambient-light sensor on this board -> auto-brightness compiled out.
#define HAS_LDR     0
#define ADC_MAX  4095   // classic ESP32 ADC is 12-bit (unused while HAS_LDR == 0)

#define BTN_PIN         32
#define BTN_TOUCH_RATIO 85   // pressed when touchRead < baseline * ratio/100
