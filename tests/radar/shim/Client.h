#pragma once
#include "Stream.h"
struct IPAddress {};
class Client : public Stream {
 public:
  using Stream::read;
  virtual int connect(const char*, uint16_t) { return 1; }
  virtual int connect(IPAddress, uint16_t) { return 1; }
  virtual uint8_t connected() { return 1; }
  virtual operator bool() { return connected(); }
  virtual void stop() {}
  virtual int read(uint8_t*, size_t) = 0;
};
