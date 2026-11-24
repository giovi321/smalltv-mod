#pragma once
// How much heap the engine adds to a theme that was just parsed (issue #132).
// No Arduino code in here, so the host tests run it against the real engine
// (tests/theme/test_memory.cpp, "estimate").
//
// The parse leaves a Theme. Engine::setTheme() then builds one LayerState for
// every layer, and the first update() expands the text of every text layer
// into a string of its own. Together that is most of what a running theme
// holds, and the ESP8266 has no room to find out by trying: a theme that takes
// the heap under the Wi-Fi reserve (WifiReserve.h) can crash the device inside
// the SDK before any later check runs. So the firmware asks first
// (ThemeMargin.h, tooBigToBuild) and refuses a theme that would not fit.
//
// The estimate is an upper bound, and it is built to stay one:
//  - Allocations are charged as umm_malloc charges them: 4 bytes of header,
//    whole 8-byte blocks.
//  - A text longer than 15 characters lives on the heap in a std::string built
//    one character at a time, so its capacity grows in steps: 15, 30, 60, 120,
//    240. A 60-character text holds 61 bytes, a 61-character one 121: the
//    estimate takes the step, not the length. (A single append of more than 15
//    characters could land between two steps; the engine appends one
//    character, or a token's value of at most 13, at a time.)
//  - A token is counted at the longest text it can become at the first update:
//    {WEEKDAY} 13 characters, {MONTH} 10, {MON} and {WD} 4, a number 2 or 4, any
//    other token "--" (2). The longest of the layer's own text and its
//    translations counts, whatever language the device is set to.
//  - What it does NOT see: a fetched value or a "device." value that is longer
//    than "--" once it arrives. Values come after the first frame; the check
//    after the load (ThemeMargin.h, MarginWatch) starts again when the first
//    ones are applied, and is the net for those.
#include "ThemeEngine.h"
#include <cstddef>
#include <vector>

namespace smalltv {

// The bytes the heap takes for one allocation of `bytes` (umm_malloc: a 4-byte
// header, whole 8-byte blocks). The ESP8266 core charges small ones the same way.
constexpr size_t heapCharge(size_t bytes) { return (bytes + 4 + 7) / 8 * 8; }

// The capacity of a std::string that was built one character at a time up to
// `length` characters: 15 (inside the string itself), then doubling.
inline size_t textCapacity(size_t length) {
  size_t capacity = 15;
  while (capacity < length) capacity *= 2;
  return capacity;
}

// The most characters a {token} can become at the first update (see above).
// `key` is `length` characters of `value` from `begin`; nothing is copied, so
// the estimate allocates nothing while the heap is what it is asking about.
inline size_t tokenLength(const std::string& value, size_t begin, size_t length) {
  auto is = [&](const char* name) { return value.compare(begin, length, name) == 0; };
  if (is("YYYY") || is("MON") || is("WD")) return 4;
  if (is("WEEKDAY")) return 13;
  if (is("MONTH")) return 10;
  return 2;   // HH hh MM SS DD MO WW YY H AMPM, and "--" for everything else
}

// The most characters `value` can expand to at the first update.
inline size_t expandedLengthBound(const std::string& value) {
  size_t length = 0;
  for (size_t i = 0; i < value.size();) {
    if (value[i] != '{') {
      ++length;
      ++i;
      continue;
    }
    const size_t end = value.find('}', i);
    if (end == std::string::npos) {   // an open brace: the rest is copied as it is
      length += value.size() - i;
      break;
    }
    length += tokenLength(value, i + 1, end - i - 1);
    i = end + 1;
  }
  return length;
}

// What the engine adds to `theme` (parsed, not yet given to the engine): the
// states of the layers in their blocks, the texts that outgrow a string's own
// room, and the dirty list of the first frame.
inline size_t engineBytes(const Theme& theme) {
  size_t bytes = 0;
  const size_t layers = theme.layers.size();
  if (layers) {
    // BlockList<LayerState>: a vector of blocks, and each block exactly as big
    // as it is reserved (the last one holds what is left).
    constexpr size_t PerBlock = BlockList<LayerState>::PerBlock;
    const size_t blocks = (layers + PerBlock - 1) / PerBlock;
    bytes += heapCharge(blocks * sizeof(std::vector<LayerState>));
    bytes += layers / PerBlock * heapCharge(PerBlock * sizeof(LayerState));
    if (layers % PerBlock) bytes += heapCharge(layers % PerBlock * sizeof(LayerState));
  }
  for (size_t i = 0; i < layers; ++i) {
    const Layer& layer = theme.layers[i];
    if (layer.type != LayerType::Text) continue;
    size_t longest = expandedLengthBound(layer.value);
    for (size_t t = 0; t < layer.translationCount; ++t) {
      const size_t other = expandedLengthBound(theme.translations[layer.firstTranslation + t].text);
      if (other > longest) longest = other;
    }
    if (longest > 15) bytes += heapCharge(textCapacity(longest) + 1);
  }
  return bytes + heapCharge(sizeof(Rect));   // update() returns the whole canvas as one rectangle
}

}  // namespace smalltv
