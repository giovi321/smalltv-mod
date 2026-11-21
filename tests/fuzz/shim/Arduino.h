// Just enough of the Arduino core for src/Settings.cpp to build on the host.
// Behaviour follows the ESP8266 core where the settings code depends on it.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define PROGMEM
#define A0 17
#define F(s) (s)
#define HEX 16
#define DEC 10
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

class String {
 public:
  String() {}
  String(const char* s) { if (s) s_ = s; }
  String(const char* s, size_t n) : s_(s, n) {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int n) : s_(std::to_string(n)) {}
  String(unsigned n) : s_(std::to_string(n)) {}
  String(long n) : s_(std::to_string(n)) {}
  String(unsigned long n) : s_(std::to_string(n)) {}
  String(unsigned long n, unsigned char base) { char b[40]; snprintf(b, sizeof b, base == 16 ? "%lx" : "%lu", n); s_ = b; }
  String(float n) { char b[40]; snprintf(b, sizeof b, "%g", double(n)); s_ = b; }
  String(unsigned n, unsigned char base) : String((unsigned long)n, base) {}
  String(float n, unsigned char decimals) { char b[48]; snprintf(b, sizeof b, "%.*f", (int)decimals, double(n)); s_ = b; }
  unsigned length() const { return s_.size(); }
  const char* c_str() const { return s_.c_str(); }
  bool isEmpty() const { return s_.empty(); }
  char operator[](unsigned i) const { return i < s_.size() ? s_[i] : 0; }
  char& operator[](unsigned i) { return s_[i]; }
  bool concat(const char* s, size_t n) { s_.append(s, n); return true; }
  bool concat(const char* s) { s_ += s; return true; }
  bool concat(const String& o) { s_ += o.s_; return true; }
  bool concat(char c) { s_ += c; return true; }
  bool reserve(unsigned n) { s_.reserve(n); return true; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += o; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  String& operator+=(int n) { s_ += std::to_string(n); return *this; }
  String operator+(const String& o) const { return String(s_ + o.s_); }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return s_ == o; }
  bool operator!=(const String& o) const { return s_ != o.s_; }
  bool operator!=(const char* o) const { return s_ != o; }
  bool operator<(const String& o) const { return s_ < o.s_; }
  bool equals(const char* o) const { return s_ == o; }
  bool equalsIgnoreCase(const String& o) const {
    if (s_.size() != o.s_.size()) return false;
    for (size_t i = 0; i < s_.size(); ++i)
      if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
    return true;
  }
  bool startsWith(const String& p) const { return s_.compare(0, p.s_.size(), p.s_) == 0; }
  bool endsWith(const String& p) const { return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0; }
  int indexOf(char c, unsigned from = 0) const { size_t p = s_.find(c, from); return p == std::string::npos ? -1 : int(p); }
  int indexOf(const String& t, unsigned from = 0) const { size_t p = s_.find(t.s_, from); return p == std::string::npos ? -1 : int(p); }
  int lastIndexOf(char c) const { size_t p = s_.rfind(c); return p == std::string::npos ? -1 : int(p); }
  String substring(unsigned from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
  String substring(unsigned from, unsigned to) const { return from >= to || from >= s_.size() ? String() : String(s_.substr(from, to - from)); }
  void trim() {
    size_t b = s_.find_first_not_of(" \t\r\n"), e = s_.find_last_not_of(" \t\r\n");
    s_ = b == std::string::npos ? "" : s_.substr(b, e - b + 1);
  }
  void toLowerCase() { for (auto& c : s_) c = tolower((unsigned char)c); }
  void toUpperCase() { for (auto& c : s_) c = toupper((unsigned char)c); }
  void remove(unsigned i, unsigned n = 0xffffffffu) { if (i < s_.size()) s_.erase(i, n); }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(s_.c_str(), nullptr); }
  operator const char*() const { return s_.c_str(); }
 private:
  std::string s_;
};
inline String operator+(const char* a, const String& b) { return String(a) + b; }
