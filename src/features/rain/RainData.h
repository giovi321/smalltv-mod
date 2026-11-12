// RainData.h — shared data model for the rain radar feature (MODE_RAIN).
//
// A RainViewer radar frame arrives as a square PNG (256 px RGBA) centred on
// the home position. The PNG is decoded once, on the device, into a 240x240
// 8-bit file: each pixel becomes the index of its display colour in
// kRainColors (57,600 B/frame) in LittleFS under /rain/fNN.raw.
//
// kRainColors holds the colours the tiles actually render: the cloud layer
// and the Universal Blue rain ramp, measured from live z7 tiles and
// premultiplied on black (the screen's background). The device therefore
// renders exactly what RainViewer renders — no reinterpreted palette. The
// premultiplied value is also what the LZ window stores (alongside an
// alpha class), so back-references rebuild displayed colours directly.
#pragma once
#include <Arduino.h>
#include "config.h"

// One radar frame, oldest first. Times are unix UTC timestamps.
struct RainFrame {
  uint32_t time;
  bool     nowcast;   // true = prediction frame (beyond "now")
};

// Display colours: the real colours of RainViewer z7 tiles (cloud layer +
// Universal Blue rain ramp), premultiplied RGB on black, sorted dark->light.
// Index 0 = black/no rain. Measured from 6 live tiles (2026-11).
#define RAIN_COLOR_COUNT 58
static const uint8_t kRainColors[RAIN_COLOR_COUNT][3] PROGMEM = {
  {0,0,0},
  {7,7,6},
  {10,9,8},
  {12,12,10},
  {15,14,13},
  {17,17,15},
  {20,19,17},
  {23,22,19},
  {26,25,22},
  {30,28,24},
  {33,32,27},
  {37,35,30},
  {40,38,32},
  {44,42,35},
  {48,45,38},
  {52,49,40},
  {0,68,106},
  {57,53,44},
  {0,72,115},
  {0,76,123},
  {0,80,131},
  {68,63,50},
  {0,85,139},
  {0,89,139},
  {80,74,56},
  {0,97,148},
  {0,105,156},
  {0,112,163},
  {0,113,164},
  {92,86,64},
  {0,117,172},
  {0,119,170},
  {0,125,180},
  {0,127,180},
  {106,98,71},
  {0,136,191},
  {0,137,189},
  {0,145,202},
  {0,145,205},
  {121,112,79},
  {0,153,213},
  {0,154,213},
  {131,122,87},
  {0,161,230},
  {0,163,224},
  {142,133,95},
  {24,174,230},
  {27,174,226},
  {153,144,103},
  {49,186,230},
  {54,186,229},
  {165,154,112},
  {81,197,232},
  {82,198,238},
  {108,209,235},
  {106,210,238},
  {136,221,238},
  {139,222,238},
};

// Table RGB565 prémultiplié -> indice dans kRainColors : LUT R5G5B2 de 4096
// entrées précalculée en flash (rain_clut.h, générée par
// tools/make_rain_clut.py — 4 Ko de BSS économisés sur l'ESP8266).
void rainColorTableInit();
uint8_t rainColorIndex(uint16_t rgb565);
