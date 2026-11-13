// stub FS.h : File minimal pour tester RainPng.cpp sur PC
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>

class File {
 public:
  File() {}
  File(const char* path, const char* mode) { f_ = fopen(path, mode); }
  ~File() { if (f_) fclose(f_); }
  operator bool() const { return f_ != nullptr; }
  size_t read(uint8_t* buf, size_t n) { return f_ ? fread(buf, 1, n, f_) : 0; }
  size_t write(const uint8_t* buf, size_t n) { return f_ ? fwrite(buf, 1, n, f_) : 0; }
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
