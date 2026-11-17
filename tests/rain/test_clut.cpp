// The colour lookup has two forms (rain_clut.h): a 32 KB table in flash on
// the ESP8266, and on the ESP32 images the same rule computed during each
// decode. This test links RainPng.cpp built the ESP32 way and checks that
// rainColorIndex() gives the flash table's answer for every RGB565 value.
#define RAIN_CLUT_IN_FLASH 1
#include "rain_clut.h"
#include "RainData.h"
#include <cstdio>

int main() {
  unsigned bad = 0;
  for (uint32_t c = 0; c < 65536; c++) {
    uint8_t want = c ? kRainClut[((c >> 11) << 10) | (((c >> 5) & 63) << 4) | ((c & 31) >> 1)] : 0;
    if (rainColorIndex((uint16_t)c) != want && bad++ < 5)
      printf("FAIL rgb565 %04x: computed %u, table %u\n", (unsigned)c, rainColorIndex((uint16_t)c), want);
  }
  // Every palette colour maps to itself, but where two share a key.
  unsigned self = 0;
  for (unsigned k = 0; k < RAIN_COLOR_COUNT; k++) {
    uint16_t v = (uint16_t)(((kRainColors[k][0] >> 3) << 11) | ((kRainColors[k][1] >> 2) << 5) | (kRainColors[k][2] >> 3));
    if (rainColorIndex(v) == k) self++;
  }
  printf("colour lookup: 65536 values, %u differ; %u of %u palette colours map to themselves\n",
         bad, self, (unsigned)RAIN_COLOR_COUNT);
  return bad || self + 1 < RAIN_COLOR_COUNT;
}
