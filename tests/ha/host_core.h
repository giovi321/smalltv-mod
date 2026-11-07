// Pulled into every file of the Home Assistant host tests (-include): the few
// core functions the screen store and the MQTT client use that the shared
// stand-in headers in tests/fuzz/shim do not have. millis() is a clock the
// test moves by hand.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint32_t millis();

// The ESP8266 core has strlcpy; glibc only since 2.38 (the dev image has 2.36,
// CI's has more). Under another name, so both libraries build the same way.
#define strlcpy smalltv_host_strlcpy
static inline size_t smalltv_host_strlcpy(char* dst, const char* src, size_t size) {
  size_t n = strlen(src);
  if (size) {
    size_t c = n >= size ? size - 1 : n;
    memcpy(dst, src, c);
    dst[c] = 0;
  }
  return n;
}
