// RainData.h — shared data model for the rain radar feature (MODE_RAIN).
//
// A RainViewer radar frame arrives as a square PNG (Universal Blue colour
// scheme) centred on the home position. The PNG is decoded once, on the
// device, into a 240x240 4-bit file: each pixel becomes a palette index
// (2 px/byte, 28,800 B/frame) in LittleFS under /rain/fNN.raw. RainViewer
// encodes intensity in the pixel colour itself, so the quantizer maps the
// source ramp onto kRainPalette and the PNG alpha channel is not needed.
#pragma once
#include <Arduino.h>
#include "config.h"

// One radar frame, oldest first. Times are unix UTC timestamps.
struct RainFrame {
  uint32_t time;
  bool     nowcast;   // true = prediction frame (beyond "now")
};

// 16-level blue -> violet palette, light rain -> heavy rain. Level 0 is
// black/no rain and is never drawn. RGB565, tinted through gfxTint() when
// the device copies it into RAM (see RainMode.cpp).
static const uint16_t kRainPalette[16] PROGMEM = {
  0x153E, 0x147D, 0x13BC, 0x12FB, 0x125A, 0x11BA, 0x1119, 0x0898,
  0x1877, 0x2876, 0x3875, 0x4074, 0x5074, 0x5873, 0x6052, 0x6851,
};

// The 20 opaque colours of RainViewer's Universal Blue ramp, weakest rain
// first, measured from live tiles (2026-11). RGB565, so the quantizer runs
// on the decoder's output directly: a pixel's level is its nearest ramp
// colour, remapped onto the 16-level palette above.
static const uint16_t kRainSourceRamp[20] PROGMEM = {
  0x8EFD, 0x6E9D, 0x563D, 0x35DC, 0x1D7C, 0x051C, 0x04DA, 0x0499,
  0x0457, 0x03F6, 0x03B5, 0x0394, 0x0353, 0x0312, 0x02D1, 0x02B1,
  0x0290, 0x026F, 0x024E, 0x022D,
};

// Nearest-ramp-colour -> palette level 1..15 (0 = transparent/no rain).
// 20 couleurs sources remappées sur les 15 niveaux de la palette (max 15 :
// un nibble 4-bit ne tient pas 16).
uint8_t rainLevelFor(uint16_t rgb565);

// Précalcule la table RGB332 -> niveau (256 entrées) utilisée pendant le
// décodage. Doit être appelée avant rainLevelLut().
void rainLevelLutBuild();
uint8_t rainLevelLut(uint16_t rgb565);
