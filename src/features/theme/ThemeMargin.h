#pragma once
// The memory margin after a theme load: the pure part (no Arduino, so the
// host tests run it). tests/theme/test_margin.cpp holds it.
//
// The free heap and the largest free block must both stay at or above their
// limits while a theme runs: the free heap of the shared Wi-Fi reserve
// (WifiReserve.h, WifiReserveFreeHeap) and the parser's block limit
// (ThemeMode::ParseLargestBlock; the #132 measurement showed that the free
// heap separates clean runs from crashes and the block does not, so the block
// limit is the parser's own). Under them the ESP8266's Wi-Fi
// code can run out of memory inside the SDK and write through a null pointer:
// a crash that no check in this firmware can catch. Two checks hold it:
//
//  1. Before the engine builds anything (tooBigToBuild, below): the parse is
//     done, so the bytes the engine is about to add are known (engineBytes(),
//     ThemeFootprint.h). A theme that would leave less than the reserve is
//     refused at once, before the heap goes down. This is the check that
//     keeps a crashing theme from crashing. A check that only came after
//     the load was too late: the Wi-Fi code ran on the low heap for the whole
//     time it waited for its readings, and crashed (#132).
//  2. After the engine takes the theme and the first frame has allocated the
//     layers' texts (MarginWatch, below): the same reserve, judged on the
//     real heap, for what the estimate cannot know (values fetched later that
//     make a text longer).
//
// For the second check, one short reading is not a verdict: a web reply or the buffers of the
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

// The check before the engine builds a theme, from what it is about to add
// (engineBytes(), ThemeFootprint.h): true when the free heap after that would
// be under the reserve, or the largest block already is. A theme that would
// leave less is refused before the heap goes down, so the Wi-Fi code never
// runs on less than the reserve and no crash can come first. The block is
// judged as it is now: building only makes it smaller, so this refuses too
// little, never too much, and the check after the load (MarginWatch) is the net.
// `addedBytes` over the free heap must not wrap round to "plenty".
constexpr bool tooBigToBuild(uint32_t freeHeap, uint32_t largestBlock, uint32_t addedBytes, MarginLimits limits) {
  return freeHeap < addedBytes || memoryShort(freeHeap - addedBytes, largestBlock, limits);
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
