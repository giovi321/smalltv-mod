// Forced into every translation unit of the rain host tests (-include): the
// parts of the Arduino core that RainClient.cpp uses on top of the settings
// shim in tests/fuzz/shim/Arduino.h.
#pragma once
#include "../../fuzz/shim/Arduino.h"
#include "../../fuzz/shim/progmem.h"   // yield() (RainClient.cpp: rainDecodeYield)
#include <Print.h>   // tests/rain/shim, or the simulator's fuller one
#include <cmath>
#include <string>

#ifndef PI
#define PI 3.14159265358979f
#endif

extern uint32_t g_fakeMillis;   // the test drives the clock
inline uint32_t millis() { return g_fakeMillis; }
inline void delay(uint32_t ms) { g_fakeMillis += ms; }

// ArduinoJson reads an Arduino Stream as on the device, through readBytes()
// (ARDUINOJSON_ENABLE_ARDUINO_STREAM=1 in the builds that use this shim: its
// generic reader would call read(), which never waits). That support also
// names Print and Printable.
class Printable {
 public:
  virtual ~Printable() {}
  virtual size_t printTo(Print&) const = 0;
};

// Arduino Stream, as far as the HTTP body and ArduinoJson need it, with the
// cores' timing: readBytes() is virtual and reads through timedRead(), which
// waits for each byte up to the stream timeout (setTimeout, 1 s by default)
// and starts that wait again on every byte. The wait runs on the fake clock:
// a stream says when its next byte comes (nextByteInMs); the clock jumps to
// it, or to the end of the timeout. A stream that has ended returns at once
// (the cores wait the timeout there too, even once the server has closed:
// not modelled, so the other tests keep their timing).
class Stream {
 public:
  static constexpr uint32_t kEnded = 0;            // nextByteInMs(): nothing more will come
  static constexpr uint32_t kNever = 0xFFFFFFFFu;  // ... nothing yet, the connection stays open
  virtual ~Stream() {}
  virtual int read() = 0;
  virtual int available() { return 0; }
  virtual int peek() { return -1; }
  virtual size_t write(uint8_t) { return 0; }
  void setTimeout(unsigned long ms) { timeout_ = ms; }
  unsigned long getTimeout() const { return timeout_; }
  virtual size_t readBytes(char* buf, size_t n) {
    size_t i = 0;
    for (; i < n; i++) { int c = timedRead(); if (c < 0) break; buf[i] = (char)c; }
    return i;
  }
  virtual size_t readBytes(uint8_t* buf, size_t n) { return readBytes((char*)buf, n); }

 protected:
  // After read() found nothing: ms until the next byte (kEnded, kNever).
  virtual uint32_t nextByteInMs() { return kEnded; }
  unsigned long timeout_ = 1000;

 private:
  int timedRead() {
    const uint32_t start = millis();
    for (;;) {
      int c = read();
      if (c >= 0) return c;
      const uint32_t next = nextByteInMs();
      if (next == kEnded || timeout_ == 0) return -1;
      const uint32_t waited = millis() - start;
      if (waited >= timeout_) return -1;
      const uint32_t left = (uint32_t)timeout_ - waited;
      g_fakeMillis += next < left ? next : left;   // the wait (the cores yield() meanwhile)
      yield();
    }
  }
};
