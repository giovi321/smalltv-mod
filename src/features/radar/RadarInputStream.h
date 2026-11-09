#pragma once

#include <Stream.h>

// NetworkClientSecure::read() can return -1 while waiting for the next TLS
// packet. ESP32 NetworkClient::readBytes() treats that as EOF immediately.
// Use Stream's timed reader so packet gaps obey the configured HTTP timeout,
// including reads made by the chunk decoder. Yield while waiting for data.
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
