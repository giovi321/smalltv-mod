// The patched Arduino_GFX YCbCr2RGB.h (tools/patch_gfx_ycbcr.py), compiled as
// on the ESP8266 against a host pgm_read_word: every table read through its
// macro (GFX_YCBCR_READ_S16 for the int16_t tables, _U16 for the uint16_t ones)
// gives the value a plain index gives, and so does the pixel expression of the
// patched writeYCbCrPixels for every Y, Cb and Cr. A table read with the wrong
// macro shows up here: a negative int16_t entry, or a uint16_t entry above
// 0x7fff, comes back with another value.
#include <stdint.h>
#include <stdio.h>

#ifndef ESP8266
#error "build with -DESP8266: the test is about the ESP8266 branch of the header"
#endif
#include "YCbCr2RGB.h"

static int failures = 0;

#define CHECK_TABLE(t, READ)                                                   \
  for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); ++i)                      \
    if (READ(t, i) != t[i])                                                    \
    {                                                                          \
      printf("FAIL %s[%u]: %d via %s, %d direct\n", #t, i, (int)READ(t, i),    \
             #READ, (int)t[i]);                                                \
      ++failures;                                                              \
    }

static uint16_t pixelDirect(uint8_t y, uint8_t cb, uint8_t cr)
{
  int16_t pxR = CR2R16[cr];
  int16_t pxG = -CB2G16[cb] - CR2G16[cr];
  int16_t pxB = CB2B16[cb];
  int16_t pxY = Y2I16[y];
  return CLIPRBE[pxY + pxR] | CLIPGBE[pxY + pxG] | CLIPBBE[pxY + pxB];
}

// The statements of the patched function (tools/patch_gfx_ycbcr.py SOURCE_NEW).
static uint16_t pixelPatched(uint8_t y, uint8_t cb, uint8_t cr)
{
  int16_t pxR = GFX_YCBCR_READ_S16(CR2R16, cr);
  int16_t pxG = -GFX_YCBCR_READ_S16(CB2G16, cb) - GFX_YCBCR_READ_S16(CR2G16, cr);
  int16_t pxB = GFX_YCBCR_READ_S16(CB2B16, cb);
  int16_t pxY = GFX_YCBCR_READ_S16(Y2I16, y);
  return GFX_YCBCR_READ_U16(CLIPRBE, pxY + pxR) | GFX_YCBCR_READ_U16(CLIPGBE, pxY + pxG) | GFX_YCBCR_READ_U16(CLIPBBE, pxY + pxB);
}

int main()
{
  CHECK_TABLE(Y2I16, GFX_YCBCR_READ_S16)
  CHECK_TABLE(CR2R16, GFX_YCBCR_READ_S16)
  CHECK_TABLE(CR2G16, GFX_YCBCR_READ_S16)
  CHECK_TABLE(CB2G16, GFX_YCBCR_READ_S16)
  CHECK_TABLE(CB2B16, GFX_YCBCR_READ_S16)
  CHECK_TABLE(CLIPR, GFX_YCBCR_READ_U16)
  CHECK_TABLE(CLIPG, GFX_YCBCR_READ_U16)
  CHECK_TABLE(CLIPB, GFX_YCBCR_READ_U16)
  CHECK_TABLE(CLIPRBE, GFX_YCBCR_READ_U16)
  CHECK_TABLE(CLIPGBE, GFX_YCBCR_READ_U16)
  CHECK_TABLE(CLIPBBE, GFX_YCBCR_READ_U16)

  unsigned long pixels = 0;
  for (int y = 0; y < 256 && failures < 20; ++y)
    for (int cb = 0; cb < 256; ++cb)
      for (int cr = 0; cr < 256; ++cr, ++pixels)
      {
        uint16_t a = pixelDirect(y, cb, cr), b = pixelPatched(y, cb, cr);
        if (a != b && ++failures <= 20)
          printf("FAIL Y=%d Cb=%d Cr=%d: 0x%04x patched, 0x%04x original\n", y, cb, cr, b, a);
      }

  if (failures)
  {
    printf("%d failures\n", failures);
    return 1;
  }
  printf("ok: 11 tables and %lu pixels read the same through the macros\n", pixels);
  return 0;
}
