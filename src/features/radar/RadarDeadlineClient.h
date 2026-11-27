#pragma once
#include <memory>
#include "Platform.h"

// One budget for HTTP headers, redirects and the streamed body, even when
// bytes keep arriving within HTTPClient's idle timeout. SDK DNS/connect/TLS
// calls cannot be preempted here; their blocking call may finish after expiry.
// WaitingStream still provides the shared packet-gap behavior above this layer.
class RadarDeadlineClient : public NetClient {
 public:
  RadarDeadlineClient(NetClient& in, uint32_t start, uint32_t budget)
      : in_(in), start_(start), budget_(budget) { setTimeout(budget); }
  bool expired() {
    if (static_cast<uint32_t>(millis() - start_) < budget_) return false;
    // HTTPClient reads header lines with Stream::timedRead(). Stop that idle
    // wait too, rather than waiting another full timeout after the deadline.
    Stream::setTimeout(0);
    return true;
  }
  uint32_t remaining() {
    uint32_t elapsed = static_cast<uint32_t>(millis() - start_);
    if (elapsed < budget_) return budget_ - elapsed;
    Stream::setTimeout(0);
    return 0;
  }
  int connect(const char* host, uint16_t port) override {
    uint32_t left = remaining();
    if (!left) return 0;
    in_.setTimeout(left);
    int result = in_.connect(host, port);
    return expired() ? 0 : result;
  }
  int connect(IPAddress ip, uint16_t port) override {
    uint32_t left = remaining();
    if (!left) return 0;
    in_.setTimeout(left);
    int result = in_.connect(ip, port);
    return expired() ? 0 : result;
  }
#if defined(SMALLTV_FAMILY_ESP32)
  int connect(const char* host, uint16_t port, int32_t timeout) override {
    uint32_t left = remaining();
    if (!left) return 0;
    int32_t bounded = timeout < 0 || static_cast<uint32_t>(timeout) > left
        ? static_cast<int32_t>(left) : timeout;
    int result = in_.connect(host, port, bounded);
    return expired() ? 0 : result;
  }
  int connect(IPAddress ip, uint16_t port, int32_t timeout) override {
    uint32_t left = remaining();
    if (!left) return 0;
    int32_t bounded = timeout < 0 || static_cast<uint32_t>(timeout) > left
        ? static_cast<int32_t>(left) : timeout;
    int result = in_.connect(ip, port, bounded);
    return expired() ? 0 : result;
  }
#endif
#if !defined(SMALLTV_FAMILY_ESP32)
  // The ESP8266 HTTPClient does not keep the client it is given: begin()
  // stores client.clone() (core 3.1). WiFiClient::clone() would copy only the
  // plain WiFiClient part of this wrapper, so an HTTPS URL would go out as
  // cleartext TCP and the deadline would be skipped. The copy wraps the same
  // client, which outlives the HTTPClient in fetchUrl.
  std::unique_ptr<WiFiClient> clone() const override {
    return std::unique_ptr<WiFiClient>(new RadarDeadlineClient(in_, start_, budget_));
  }
#endif
  int available() override { return expired() ? 0 : in_.available(); }
  int peek() override { return expired() ? -1 : in_.peek(); }
  uint8_t connected() override { return !expired() && in_.connected(); }
  operator bool() override { return connected(); }
  int read() override {
    uint32_t left = remaining();
    if (!left) return -1;
    in_.setTimeout(left);
    int value = in_.read();
    if (expired()) return -1;
    if (value < 0) delay(1);
    if (static_cast<uint32_t>(millis() - lastYield_) >= 16) {
      delay(0);
      lastYield_ = millis();
    }
    return value;
  }
  int read(uint8_t* buffer, size_t length) override {
    size_t n = 0;
    while (n < length) {
      int value = read();
      if (value < 0) break;
      buffer[n++] = static_cast<uint8_t>(value);
    }
    return static_cast<int>(n);
  }
  using Stream::readBytes;
  size_t readBytes(char* buffer, size_t length) override {
    size_t n = 0;
    while (n < length && !expired()) {
      int value = read();
      if (value >= 0) buffer[n++] = static_cast<char>(value);
      else if (!connected() && available() <= 0) break;
    }
    return n;
  }
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* bytes, size_t length) override {
    uint32_t left = remaining();
    if (!left) return 0;
    in_.setTimeout(left);
    return in_.write(bytes, length);
  }
  void flush() override { if (!expired()) in_.flush(); }
  void stop() override { in_.stop(); }
 private:
  NetClient& in_;
  uint32_t start_, budget_, lastYield_ = 0;
};
