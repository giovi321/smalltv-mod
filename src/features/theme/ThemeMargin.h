#pragma once
// The memory margin after a theme load: the pure part (no Arduino, so the
// host tests run it). tests/theme/test_margin.cpp holds it.
//
// After the engine takes a theme, and the first frame has allocated the
// layers' texts, the free heap and the largest free block must both stay at
// or above their limits: the free heap of the shared Wi-Fi reserve
// (WifiReserve.h, WifiReserveFreeHeap) and the parser's block limit
// (ThemeMode::ParseLargestBlock). Under it the ESP8266's Wi-Fi code can run out of
// memory inside the SDK and write through a null pointer: a crash that no
// check in this firmware can catch.
//
// One short reading is not a verdict: a web reply or the buffers of the
// select request can hold heap for a moment. So the watch refuses only when
// ShortInARow readings in a row, at least GapMs apart, are short. It watches
// for SettleMs after a load, and again after fetched values changed the
// texts. A running theme is never dropped because of a dip an hour later.
#include <cstdint>

namespace smalltv {

struct MarginLimits {
  uint32_t freeHeap;
  uint32_t largestBlock;
};

// Either number under its limit. Exactly at the limit is enough.
constexpr bool memoryShort(uint32_t freeHeap, uint32_t largestBlock, MarginLimits limits) {
  return freeHeap < limits.freeHeap || largestBlock < limits.largestBlock;
}

class MarginWatch {
 public:
  static constexpr uint32_t SettleMs = 8000;
  static constexpr uint32_t GapMs = 100;
  static constexpr uint8_t ShortInARow = 3;

  // A theme was loaded, or fetched values changed its texts.
  void restart(uint32_t now) {
    active_ = true;
    haveLast_ = false;
    shorts_ = 0;
    start_ = now;
  }
  bool active() const { return active_; }
  // One reading. True once: the moment the theme must be refused. After that
  // (and after the settle time) the watch is idle until restart().
  bool check(uint32_t now, uint32_t freeHeap, uint32_t largestBlock, MarginLimits limits) {
    if (!active_) return false;
    if (uint32_t(now - start_) >= SettleMs) {   // unsigned difference: safe across the millis() wrap
      active_ = false;
      return false;
    }
    if (haveLast_ && uint32_t(now - last_) < GapMs) return false;
    haveLast_ = true;
    last_ = now;
    if (!memoryShort(freeHeap, largestBlock, limits)) {
      shorts_ = 0;
      return false;
    }
    if (++shorts_ < ShortInARow) return false;
    active_ = false;
    return true;
  }

 private:
  bool active_ = false, haveLast_ = false;
  uint8_t shorts_ = 0;
  uint32_t start_ = 0, last_ = 0;
};

}  // namespace smalltv
