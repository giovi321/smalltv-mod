#pragma once
#include <Arduino.h>

// The body of an HTTPClient GET, for a parser that reads it directly from the
// stream (deserializeJson on http.getStream()).
//
// ESP32 (Arduino core 3.x): NetworkClientSecure::read() returns -1, not 0,
// when the next TLS record is not available yet. NetworkClient::readBytes()
// reads -1 as an error and stops immediately. ArduinoJson reads with
// readBytes(). Thus, when a reply does not arrive in one burst, the parse
// stops with IncompleteInput after some ms. The HTTP code is 200 and the
// socket is still open. On a SmallTV Pro, all Yahoo tickers failed.
//
// This wrapper's readBytes() waits for the next byte up to the stream timeout
// (the HTTP timeout), restarting the wait on every byte, like timedRead().
// It stops at once when the connection is closed and nothing is left to read,
// so a dropped or reset connection fails now instead of after the timeout.
//
// ESP8266: the client already waits in readBytes(). Thus, the wrapper sends
// readBytes() directly to the client, and the behavior does not change.
class WaitingStream : public Stream {
 public:
  WaitingStream(Client& in, uint32_t timeoutMs) : in_(in) { setTimeout(timeoutMs); }
  int available() override { return in_.available(); }
  int read() override { return in_.read(); }
  int peek() override { return in_.peek(); }
  size_t write(uint8_t) override { return 0; }
#if defined(SMALLTV_ESP8266)
  size_t readBytes(char* buffer, size_t length) override { return in_.readBytes(buffer, length); }
#else
  size_t readBytes(char* buffer, size_t length) override {
    size_t n = 0;
    uint32_t last = millis();
    while (n < length) {
      int c = in_.read();
      if (c >= 0) { buffer[n++] = (char)c; last = millis(); continue; }
      if (!in_.connected() && in_.available() <= 0) break;   // closed, nothing left
      if (millis() - last >= _timeout) break;                // no byte within the timeout
      delay(1);                                              // let the TLS stack run
    }
    return n;
  }
#endif

 private:
  Client& in_;
};
