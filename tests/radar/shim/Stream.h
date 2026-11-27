#pragma once
#include <cstddef>
#include <cstdint>
extern unsigned long ticks;
inline unsigned long& clockStep() { static unsigned long step = 0; return step; }
inline unsigned long millis() { unsigned long now = ticks; ticks += clockStep(); return now; }
inline void delay(unsigned long ms) { ticks += ms; }

// Minimal Arduino Stream contract: timed reads retry read(), unlike ESP32's
// network bulk reader. Virtual readBytes matches both supported ESP cores.
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) { return 1; }
  virtual size_t write(const uint8_t*, size_t size) { return size; }
};
class Printable {
 public:
  virtual ~Printable() {}
  virtual size_t printTo(Print&) const = 0;
};
class Stream : public Print {
 public:
  virtual ~Stream() {}
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual int available() = 0;
  virtual void flush() {}
  virtual size_t write(uint8_t) { return 1; }
  virtual size_t write(const uint8_t*, size_t n) { return n; }
  void setTimeout(unsigned long timeout) { _timeout = timeout; }
  virtual size_t readBytes(char* buffer, size_t size) {
    size_t n = 0;
    while (n < size) {
      unsigned long start = millis();
      int c;
      do { c = read(); if (c >= 0) break; }
      while (millis() - start < _timeout);
      if (c < 0) break;
      buffer[n++] = static_cast<char>(c);
    }
    return n;
  }
 protected:
  unsigned long _timeout = 1000;
};
