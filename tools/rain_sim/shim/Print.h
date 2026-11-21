// Arduino Print, as far as Arduino_GFX uses it on the host.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
class __FlashStringHelper;
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t print(const char* s) { return write(s); }
  size_t print(const __FlashStringHelper* s) { return write((const char*)s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { char b[16]; snprintf(b, sizeof b, "%d", v); return write(b); }
  size_t print(unsigned v) { char b[16]; snprintf(b, sizeof b, "%u", v); return write(b); }
  size_t print(long v) { char b[24]; snprintf(b, sizeof b, "%ld", v); return write(b); }
  size_t println(const char* s = "") { size_t n = write(s); return n + write((uint8_t)'\n'); }
};
