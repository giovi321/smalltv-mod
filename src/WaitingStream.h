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
// This wrapper gives the parser the usual Arduino Stream behavior. read()
// returns -1 when no data is available. Stream::readBytes() (timedRead) then
// waits until the stream timeout. The stream timeout is the HTTP timeout.
//
// ESP8266: the client already waits in readBytes(). Thus, the wrapper sends
// readBytes() directly to the client, and the behavior does not change.
class WaitingStream : public Stream {
 public:
  WaitingStream(Stream& in, uint32_t timeoutMs) : in_(in) { setTimeout(timeoutMs); }
  int available() override { return in_.available(); }
  int read() override {
    if (in_.available() <= 0) { delay(1); return -1; }   // let the TLS stack run
    return in_.read();
  }
  int peek() override { return in_.available() > 0 ? in_.peek() : -1; }
  size_t write(uint8_t) override { return 0; }
#if defined(SMALLTV_ESP8266)
  size_t readBytes(char* buffer, size_t length) override { return in_.readBytes(buffer, length); }
#endif

 private:
  Stream& in_;
};
