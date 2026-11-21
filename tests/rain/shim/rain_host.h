// Forced into every translation unit of the rain host tests (-include): the
// parts of the Arduino core that RainClient.cpp uses on top of the settings
// shim in tests/fuzz/shim/Arduino.h.
#pragma once
#include "../../fuzz/shim/Arduino.h"
#include "../../fuzz/shim/progmem.h"   // yield() (RainClient.cpp: rainDecodeYield)
#include <cmath>
#include <string>

#ifndef PI
#define PI 3.14159265358979f
#endif

extern uint32_t g_fakeMillis;   // the test drives the clock
inline uint32_t millis() { return g_fakeMillis; }
inline void delay(uint32_t ms) { g_fakeMillis += ms; }

// Arduino Stream, as far as the HTTP body and ArduinoJson need it.
class Stream {
 public:
  virtual ~Stream() {}
  virtual int read() = 0;
  size_t readBytes(char* buf, size_t n) {
    size_t i = 0;
    for (; i < n; i++) { int c = read(); if (c < 0) break; buf[i] = (char)c; }
    return i;
  }
  size_t readBytes(uint8_t* buf, size_t n) { return readBytes((char*)buf, n); }
};
