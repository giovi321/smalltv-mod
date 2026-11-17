// RainHistoryMem.h — RainHistory kept in memory, for the host tests and the
// fuzz target. It checks the decoder's side of the contract: a segment is
// stored once and whole, read only while stored, and at most 4 are kept.
#pragma once
#include "RainPng.h"
#include <string.h>

class RainHistoryMem : public RainHistory {
 public:
  bool put(uint8_t seg, const uint8_t* px) override {
    if (seg >= kSegs || have_[seg] || kept_ >= 4) return fault();
    memcpy(px_[seg], px, RAIN_RING_PIXELS);
    have_[seg] = true;
    kept_++;
    puts_++;
    return true;
  }
  bool get(uint8_t seg, uint16_t off, uint8_t* out, uint16_t n) override {
    if (seg >= kSegs || !have_[seg] || (uint32_t)off + n > RAIN_RING_PIXELS) return fault();
    memcpy(out, px_[seg] + off, n);
    gets_++;
    return true;
  }
  void drop(uint8_t seg) override {
    if (seg >= kSegs || !have_[seg]) { fault(); return; }
    have_[seg] = false;
    kept_--;
  }
  bool     faulted() const { return fault_; }
  unsigned puts() const { return puts_; }
  unsigned gets() const { return gets_; }

 private:
  static const unsigned kSegs = 65536 / RAIN_RING_PIXELS;
  bool fault() { fault_ = true; return false; }
  uint8_t  px_[kSegs][RAIN_RING_PIXELS];
  bool     have_[kSegs] = {};
  unsigned kept_ = 0, puts_ = 0, gets_ = 0;
  bool     fault_ = false;
};
