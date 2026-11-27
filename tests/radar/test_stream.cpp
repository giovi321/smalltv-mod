#include "features/radar/RadarDeadlineClient.h"
#include "WaitingStream.h"
#include <ArduinoJson.h>
#include <StreamUtils/Configuration.hpp>
#include <StreamUtils/Streams/ChunkDecodingStream.hpp>
#include <cassert>
#include <cstdio>
#include <string>

unsigned long ticks = 0;

// A deterministic TLS-like stream: read() temporarily returns -1 during a
// packet gap and its network-specific bulk read returns immediately on -1.
class PacketStream : public WiFiClient {
 public:
  PacketStream(std::string bytes, size_t gapAt, unsigned long pause)
      : bytes_(bytes), gapAt_(gapAt), pause_(pause) {}
  int read() override {
    if (pos_ == gapAt_ && !paused_) {
      paused_ = true;
      resume_ = millis() + pause_;
    }
    if (paused_ && millis() < resume_) return -1;
    return pos_ < bytes_.size() ? static_cast<unsigned char>(bytes_[pos_++]) : -1;
  }
  uint8_t connected() override { return 1; }
  int available() override { return 0; }
  int peek() override { return -1; }
  size_t readBytes(char* out, size_t size) override {
    size_t n = 0;
    while (n < size) { int c = read(); if (c < 0) break; out[n++] = char(c); }
    return n;
  }
 private:
  std::string bytes_;
  size_t pos_ = 0, gapAt_;
  unsigned long pause_, resume_ = 0;
  bool paused_ = false;
};

// ArduinoJson uses Stream::readBytes(&c, 1) on the device. Use that same
// contract without bringing the Arduino runtime into the native build.
struct JsonReader {
  Stream& stream;
  int read() { char c; return stream.readBytes(&c, 1) ? (unsigned char)c : -1; }
  size_t readBytes(char* out, size_t n) { return stream.readBytes(out, n); }
};

// Bytes keep arriving within each idle timeout, but the full response exceeds
// one poll budget. It must not keep the main loop occupied indefinitely.
class SlowStream : public PacketStream {
 public:
  explicit SlowStream(const std::string& bytes) : PacketStream(bytes, bytes.size(), 0) {}
  int read() override { delay(2); return PacketStream::read(); }
};

int main() {
  const std::string json = "{\"ac\":[{\"lat\":39.7,\"lon\":-105.1}]}";
  const std::string wire = "10;ext=yes\r\n" + json.substr(0, 16) +
      "\r\n12\r\n" + json.substr(16) + "\r\n0\r\nX-Test: trailer\r\n\r\n";
  size_t cases = 0;
  // Gap at every byte: split hex lengths, CRLFs, extensions, body, trailers.
  for (size_t gap = 0; gap <= wire.size(); ++gap) {
    ticks = 0;
    PacketStream source(wire, gap, 25);
    RadarDeadlineClient limited(source, millis(), 100);
    WaitingStream body(limited, 100);
    StreamUtils::ChunkDecodingStream decoded(body);
    std::string result;
    char buffer[7];
    size_t n;
    while ((n = decoded.readBytes(buffer, sizeof(buffer)))) result.append(buffer, n);
    assert(result == json);
    assert(decoded.ended() && !decoded.error());
    ++cases;
  }
  // Truncation anywhere in the framing or body cannot report a complete body.
  for (size_t end = 0; end < wire.size(); ++end) {
    ticks = 0;
    PacketStream source(wire.substr(0, end), wire.size(), 0);
    RadarDeadlineClient limited(source, millis(), 100);
    WaitingStream body(limited, 100);
    StreamUtils::ChunkDecodingStream decoded(body);
    char buffer[7];
    while (decoded.readBytes(buffer, sizeof(buffer))) {}
    assert(!decoded.ended());
    assert(ticks >= 100 && ticks <= 200);
    ++cases;
  }
  for (bool chunked : {false, true}) {
    const std::string& bytes = chunked ? wire : json;
    for (size_t gap = 0; gap < bytes.size(); ++gap) {
      ticks = 0;
      PacketStream source(bytes, gap, 25);
      RadarDeadlineClient limited(source, millis(), 100);
    WaitingStream body(limited, 100);
      StreamUtils::ChunkDecodingStream decoded(body);
      JsonReader reader{chunked ? static_cast<Stream&>(decoded) : body};
      JsonDocument doc;
      assert(!deserializeJson(doc, reader));
      assert(doc["ac"][0]["lat"].as<float>() == 39.7f);
      ++cases;
    }
    // A true timeout in the JSON body must terminate, not loop forever.
    ticks = 0;
    PacketStream source(bytes, chunked ? 20 : 10, 200);
    RadarDeadlineClient limited(source, millis(), 100);
    WaitingStream body(limited, 100);
    StreamUtils::ChunkDecodingStream decoded(body);
    JsonReader reader{chunked ? static_cast<Stream&>(decoded) : body};
    JsonDocument doc;
    assert(deserializeJson(doc, reader) == DeserializationError::IncompleteInput);
    assert(ticks == 100);
    ++cases;
  }
  // Real EOF in plain JSON also returns after the configured timeout.
  ticks = 0;
  PacketStream eof(json.substr(0, json.size() - 2), json.size(), 0);
  RadarDeadlineClient eofLimited(eof, millis(), 100);
  WaitingStream body(eofLimited, 100);
  JsonReader reader{body};
  JsonDocument doc;
  assert(deserializeJson(doc, reader) == DeserializationError::IncompleteInput);
  assert(ticks == 100);
  ++cases;
  ticks = 0;
  SlowStream slow(std::string(200, 'x'));
  RadarDeadlineClient limited(slow, millis(), 100);
  char large[200];
  assert(limited.readBytes(large, sizeof(large)) < sizeof(large));
  assert(ticks <= 102);
  ++cases;
  // Wraparound uses the same 32-bit elapsed arithmetic as device millis().
  ticks = 0xfffffff0UL;
  SlowStream wrapped(std::string(200, 'x'));
  RadarDeadlineClient wrapLimited(wrapped, static_cast<uint32_t>(millis()), 100);
  assert(wrapLimited.readBytes(large, sizeof(large)) < sizeof(large));
  assert(static_cast<uint32_t>(millis() - 0xfffffff0UL) <= 102);
  ++cases;
  // The scalar header-reading path also expires while bytes keep arriving.
  ticks = 0;
  SlowStream headers(std::string(200, 'h'));
  RadarDeadlineClient headerLimited(headers, millis(), 100);
  unsigned read = 0;
  while (headerLimited.read() >= 0) ++read;
  assert(read < 200 && headerLimited.expired() && !headerLimited.connected());
  assert(headerLimited.available() == 0 && ticks <= 102);
  assert(!headerLimited.readBytes(large, sizeof(large)) && ticks <= 102);
  ++cases;
  ticks = 0;
  clockStep() = 2;
  RadarDeadlineClient racing(headers, 0, 1);
  assert(racing.remaining() <= 1);  // one clock sample: never unsigned underflow
  assert(racing.expired());
  clockStep() = 0;
  ++cases;
  printf("radar transport: %zu cases passed\n", cases);
}
