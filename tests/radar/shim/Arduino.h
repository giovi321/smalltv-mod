#pragma once
#include "Stream.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#define F(s) (s)
#define PROGMEM
#define PI 3.14159265358979323846
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
  String(float n, int) : std::string(std::to_string(n)) {}
  String(unsigned n) : std::string(std::to_string(n)) {}
  bool startsWith(const char* s) const { return find(s) == 0; }
  int indexOf(char c) const { auto n = find(c); return n == npos ? -1 : int(n); }
  bool equalsIgnoreCase(const char* s) const {
    std::string lower = *this, other = s;
    for (char& c : lower) c = char(tolower(static_cast<unsigned char>(c)));
    for (char& c : other) c = char(tolower(static_cast<unsigned char>(c)));
    return lower == other;
  }
};
inline size_t strlcpy(char* out, const char* in, size_t size) {
  size_t n = strlen(in);
  if (size) { size_t copied = n < size - 1 ? n : size - 1; memcpy(out, in, copied); out[copied] = 0; }
  return n;
}
