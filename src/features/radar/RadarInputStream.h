#pragma once

#include <Stream.h>

// A TLS read can return -1 temporarily between packets; the pinned ESP32
// NetworkClient bulk reader stops on a negative read. Use Stream's timed
// readBytes() instead, including under the chunk decoder, and yield on gaps.
// The timeout is per byte/read wait, not an overall response deadline; an
// underlying network operation can also spend time in its own timeout.
// This wrapper cannot distinguish temporary lack of data from final EOF.
class RadarInputStream : public Stream {
 public:
  RadarInputStream(Stream& upstream, unsigned long timeout) : upstream_(upstream) {
    setTimeout(timeout);
  }
  int available() override { return upstream_.available(); }
  int peek() override { return upstream_.peek(); }
  int read() override {
    int value = upstream_.read();
    if (value < 0) delay(1);
    if (millis() - lastYield_ >= 16) {
      delay(0);
      lastYield_ = millis();
    }
    return value;
  }
  using Stream::readBytes;
  size_t readBytes(char* buffer, size_t length) override {
    return Stream::readBytes(buffer, length);
  }
  using Stream::write;
  size_t write(uint8_t value) override { return upstream_.write(value); }
  void flush() override { upstream_.flush(); }
 private:
  Stream& upstream_;
  unsigned long lastYield_ = 0;
};
