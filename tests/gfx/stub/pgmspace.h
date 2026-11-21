// Host stand-in for the ESP8266 core's pgmspace.h, for tests/gfx only.
// pgm_read_word returns a uint16_t, as on the chip, so a test sees the same
// types the firmware does (the int16_t tables come back unsigned).
#pragma once
#include <stdint.h>
#include <string.h>

#define PROGMEM

static inline uint16_t pgm_read_word(const void *addr)
{
  uint16_t v;
  memcpy(&v, addr, sizeof v);
  return v;
}
