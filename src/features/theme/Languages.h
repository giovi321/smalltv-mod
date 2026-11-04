#pragma once
#include <cstdint>
#include <cstring>

// The languages a device can be set to (settings: clock.lang). The index is
// what the theme engine uses; 0 is English, the fallback for anything unknown.
// A spec 2 theme can give a text layer one string per code (see themes.md).
namespace smalltv {
constexpr uint8_t kLanguageCount = 6;
inline const char* languageCode(uint8_t index) {
  static const char* const codes[kLanguageCount] = {"en", "pt", "es", "fr", "de", "it"};
  return index < kLanguageCount ? codes[index] : codes[0];
}
// -1 when `code` is not one of the six.
inline int languageIndex(const char* code, size_t length) {
  for (uint8_t i = 0; i < kLanguageCount; ++i)
    if (strlen(languageCode(i)) == length && !memcmp(languageCode(i), code, length)) return i;
  return -1;
}
inline uint8_t languageFromCode(const char* code) {
  int i = code ? languageIndex(code, strlen(code)) : -1;
  return i < 0 ? 0 : uint8_t(i);
}
}  // namespace smalltv
