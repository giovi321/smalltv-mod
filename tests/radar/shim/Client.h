#pragma once
#include "Stream.h"
class Client : public Stream {
 public:
  using Stream::read;
  virtual int read(uint8_t*, size_t) = 0;
};
