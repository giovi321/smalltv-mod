// Arduino Print, as far as ArduinoJson's Arduino Stream support needs it
// (ARDUINOJSON_ENABLE_ARDUINO_STREAM). The rain simulator has a fuller one
// (tools/rain_sim/shim/Print.h, first on its include path).
#pragma once
#include <cstddef>
#include <cstdint>
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
};
