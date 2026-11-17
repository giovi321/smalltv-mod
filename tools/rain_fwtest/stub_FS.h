// FS.h stub: a minimal File over stdio, to build RainPng.cpp on the host
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// >= 0: bytes still accepted before writes come up short (a full LittleFS).
inline long g_stubWriteLimit = -1;

class File {
 public:
  File() {}
  File(const char* path, const char* mode) { f_ = fopen(path, mode); }
  ~File() { if (f_) fclose(f_); }
  operator bool() const { return f_ != nullptr; }
  size_t read(uint8_t* buf, size_t n) { return f_ ? fread(buf, 1, n, f_) : 0; }
  size_t write(const uint8_t* buf, size_t n) {
    if (!f_) return 0;
    if (g_stubWriteLimit >= 0) {
      if ((long)n > g_stubWriteLimit) n = (size_t)g_stubWriteLimit;
      g_stubWriteLimit -= (long)n;
    }
    return fwrite(buf, 1, n, f_);
  }
  bool seek(uint32_t pos) { return f_ ? fseek(f_, (long)pos, SEEK_SET) == 0 : false; }
  uint32_t position() const { return f_ ? (uint32_t)ftell(f_) : 0; }
  uint32_t size() const {
    if (!f_) return 0;
    long cur = ftell(f_);
    fseek(f_, 0, SEEK_END);
    long sz = ftell(f_);
    fseek(f_, cur, SEEK_SET);
    return (uint32_t)sz;
  }
  void close() { if (f_) { fclose(f_); f_ = nullptr; } }
 private:
  FILE* f_ = nullptr;
};
