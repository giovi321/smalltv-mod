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
  // Called once, after write() refused a piece: give the connection up now.
  virtual void abandon() {}
};
// Collects what serializeJson() writes in a buffer of `Piece` bytes and
// hands it to a Sink each time the buffer is full, and once more at the end.
// After the Sink refuses a piece, the rest is dropped: a client that stopped
// reading then costs one send timeout, not one for every piece left.
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
    if (used_ && sent_) sent_ = to_.write(buffer_, used_);
    used_ = 0;
  }
  // False once the Sink has refused a piece.
  bool sent() const { return sent_; }
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
  bool sent_ = true;
  char buffer_[Piece];
};
// The end of a reply written through a PieceWriter: when a piece was refused,
// gives the connection up (once) and says so by returning false.
template <size_t Piece> bool endReply(PieceWriter<Piece>& out, Sink& to) {
  if (out.sent()) return true;
  to.abandon();
  return false;
}
// A Sink that writes to a client as a chunked HTTP body, with the chunk
// framing ("<hex size>\r\n<piece>\r\n") written here. The web server's own
// sendContent() frames the same way but does not say when a write came up
// short, so a client that stopped reading could not be noticed. `Client` has
// `size_t write(const uint8_t*, size_t)` (the bytes taken) and `abandon()`.
// The last chunk ("0\r\n\r\n") is not written here: the web server sends it
// (sendContent("", 0)), and it still sends it by itself after the handler if
// the reply is chunked and nobody did.
template <class Client> class ChunkedSink : public Sink {
 public:
  explicit ChunkedSink(Client& client) : client_(client) {}
  bool write(const char* data, size_t count) override {
    if (!count) return true;   // an empty chunk would end the reply
    char head[2 * sizeof(size_t) + 2];
    size_t at = sizeof(head);
    head[--at] = '\n';
    head[--at] = '\r';
    for (size_t rest = count; rest; rest >>= 4) {
      const unsigned digit = rest & 15;
      head[--at] = char(digit < 10 ? '0' + digit : 'a' + digit - 10);
    }
    return put(head + at, sizeof(head) - at) && put(data, count) && put("\r\n", 2);
  }
  void abandon() override { client_.abandon(); }
 private:
  bool put(const char* data, size_t count) {
    return client_.write(reinterpret_cast<const uint8_t*>(data), count) == count;
  }
  Client& client_;
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
