// The filesystem is only reached by loadSettings()/saveSettings(), which the
// harness never calls; this exists so Settings.cpp compiles. A File can also
// be backed by memory (the `rain` target hands the decoder its tile and
// output this way); a default File is closed, as on the device.
#pragma once
#include "Arduino.h"
struct File {
  File() {}
  static File reader(const uint8_t* data, size_t len) { File f; f.rd_ = data; f.len_ = len; f.open_ = true; return f; }
  static File writer(std::string* out) { File f; f.wr_ = out; f.open_ = true; return f; }
  explicit operator bool() const { return open_; }
  void close() { open_ = false; }
  size_t write(const uint8_t* b, size_t n) { if (!open_ || !wr_) return 0; wr_->append(reinterpret_cast<const char*>(b), n); return n; }
  size_t write(uint8_t c) { return write(&c, 1); }
  void flush() {}
  size_t size() const { return wr_ ? wr_->size() : len_; }
  bool getWriteError() const { return !wr_; }
  int read() { return open_ && rd_ && pos_ < len_ ? rd_[pos_++] : -1; }
  size_t read(uint8_t* b, size_t n) {
    size_t i = 0;
    for (; i < n; ++i) { int c = read(); if (c < 0) break; b[i] = uint8_t(c); }
    return i;
  }
  int peek() { return open_ && rd_ && pos_ < len_ ? rd_[pos_] : -1; }
  size_t readBytes(char* b, size_t n) { return read(reinterpret_cast<uint8_t*>(b), n); }
  bool seek(uint32_t p) { pos_ = p; return open_; }   // past the end: reads then fail
  size_t position() const { return pos_; }
 private:
  const uint8_t* rd_ = nullptr;
  size_t len_ = 0, pos_ = 0;
  std::string* wr_ = nullptr;
  bool open_ = false;
};
struct FSClass {
  bool begin() { return false; }
  bool format() { return false; }
  File open(const char*, const char*) { return File(); }
  bool rename(const char*, const char*) { return false; }
  bool remove(const char*) { return false; }
};
extern FSClass LittleFS;
