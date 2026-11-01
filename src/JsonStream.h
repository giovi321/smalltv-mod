#pragma once
// Writing a JSON reply straight to the client in small pieces, never whole in
// one block of heap. Shared by GET /api/themes (ThemeCatalog.h) and
// GET /api/status (StatusJson.h). Native tests include it too, so it needs
// nothing from the firmware.
#include <ArduinoJson.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
// Fixed text stays in flash on the ESP8266, where string constants otherwise
// take RAM for good. Elsewhere PSTR() and the _P functions are the plain ones.
#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
#include <pgmspace.h>
#endif
#ifndef PSTR
#define PSTR(s) (s)
#define strlen_P strlen
#define memcpy_P memcpy
#endif

namespace smalltv {
class Sink {
 public:
  virtual ~Sink() = default;
  virtual bool write(const char* data, size_t count) = 0;
};
// Collects what serializeJson() writes in a buffer of `Piece` bytes and
// hands it to a Sink each time the buffer is full, and once more at the end.
template <size_t Piece> class PieceWriter {
 public:
  explicit PieceWriter(Sink& to) : to_(to) {}
  size_t write(uint8_t c) { return write(&c, 1); }
  size_t write(const uint8_t* data, size_t count) {
    put(data, count, memcpy);
    return count;
  }
  // Text kept in flash (PSTR).
  void writeFlash(const char* text) { put(text, strlen_P(text), memcpy_P); }
  void flush() {
    if (used_) to_.write(buffer_, used_);
    used_ = 0;
  }
 private:
  void put(const void* data, size_t count, void* (*copy)(void*, const void*, size_t)) {
    const char* from = static_cast<const char*>(data);
    for (size_t done = 0; done < count;) {
      if (used_ == Piece) flush();
      const size_t n = std::min(Piece - used_, count - done);
      copy(buffer_ + used_, from + done, n);
      used_ += n;
      done += n;
    }
  }
  Sink& to_;
  size_t used_ = 0;
  char buffer_[Piece];
};
namespace jsonout {
// Fixed text, given as PSTR().
template <class Out> void raw(Out& out, const char* text) { out.writeFlash(text); }
// A string value as ArduinoJson writes it, escapes and all. The document
// only points at the text, so it takes no heap.
template <class Out> void text(Out& out, const char* value) {
  JsonDocument doc;
  doc.set(JsonString(value, true));
  serializeJson(doc, out);
}
// Any other single value (number, bool) as ArduinoJson writes it. The
// value sits in the document itself, so this takes no heap either.
template <class Out, class T> void value(Out& out, T v) {
  JsonDocument doc;
  doc.set(v);
  serializeJson(doc, out);
}
// Written digit by digit from the end: no format string in RAM.
template <class Out> void number(Out& out, uint32_t value) {
  char digits[10];
  size_t n = 0;
  do {
    digits[sizeof(digits) - ++n] = char('0' + value % 10);
    value /= 10;
  } while (value);
  out.write(reinterpret_cast<const uint8_t*>(digits + sizeof(digits) - n), n);
}
}
}
