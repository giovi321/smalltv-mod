#include "ThemeEngine.h"
#include "BudgetAllocator.h"
#ifdef ARDUINO
#include "config.h"
#endif
#if !defined(ARDUINO) || WITH_THEME
#include <ArduinoJson.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#ifdef ARDUINO
#include <pgmspace.h>   // PROGMEM, memcpy_P (both cores ship it)
#else
#define PROGMEM
#define pgm_read_byte(p) (*reinterpret_cast<const unsigned char*>(p))
#define strcmp_P strcmp
#define strlen_P strlen
#define memcmp_P memcmp
#define memcpy_P memcpy
#endif

namespace smalltv {
// Dirty-rectangle primitives: intersect() clips a region to the canvas or to
// another layer's row; unite() merges overlapping repaint regions so update()
// never reports more rectangles than necessary.
Rect intersect(Rect a, Rect b) {
  int x = std::max(a.x, b.x), y = std::max(a.y, b.y);
  return Rect(x, y, std::max(0, std::min(a.x + a.w, b.x + b.w) - x), std::max(0, std::min(a.y + a.h, b.y + b.h) - y));
}
Rect unite(Rect a, Rect b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  int x = std::min(a.x, b.x), y = std::min(a.y, b.y);
  return Rect(x, y, std::max(a.x + a.w, b.x + b.w) - x, std::max(a.y + a.h, b.y + b.h) - y);
}
// Theme/layer/data-source/field IDs: 1-48 ASCII letters, digits, '-' or '_'.
// No '.' -- the dotted "<source>.<field>" token syntax relies on that.
bool validId(const std::string& s) {
  if (s.empty() || s.size() > 48) return false;
  for (char c : s) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
}
// A safe relative asset path: '/'-separated components of ASCII letters,
// digits, '-', '_', '.', with no empty, '.', or '..' component and no leading
// or trailing '/'. This is what keeps a package's `source` from escaping the
// package or reaching an absolute filesystem path.
bool validPath(const std::string& s) {
  if (s.empty() || s.size() > 120 || s.front() == '/' || s.back() == '/') return false;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == '/') {
      auto part = s.substr(start, i - start);
      if (part.empty() || part == "." || part == "..") return false;
      start = i + 1;
    } else {
      char c = s[i];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    }
  }
  return true;
}
static bool asciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
// strtod() alone is too permissive for a fetched-value parser: it accepts hex
// floats (0x1p0) and locale-dependent forms, and it silently stops at the
// first non-numeric character rather than rejecting trailing garbage. This
// walks a strict decimal grammar first and only then hands the pre-validated
// span to strtod(), so a value like "12abc" or "0x10" is rejected outright
// instead of read as a truncated or unintended number.
static bool decimalSyntax(const char* begin, const char* end) {
  const char* p = begin;
  if (p != end && (*p == '+' || *p == '-')) ++p;
  bool digits = false;
  while (p != end && *p >= '0' && *p <= '9') {
    digits = true;
    ++p;
  }
  if (p != end && *p == '.') {
    ++p;
    while (p != end && *p >= '0' && *p <= '9') {
      digits = true;
      ++p;
    }
  }
  if (!digits) return false;
  if (p != end && (*p == 'e' || *p == 'E')) {
    ++p;
    if (p != end && (*p == '+' || *p == '-')) ++p;
    const char* exponent = p;
    while (p != end && *p >= '0' && *p <= '9') ++p;
    if (p == exponent) return false;
  }
  return p == end;
}
// Parses a fetched field's text as a finite number for a binding, trimming
// ASCII whitespace but rejecting anything strtod() would otherwise accept
// loosely (infinities, NaN spellings, trailing junk). A binding's source
// falling back to the layer's static value (see Engine::resolveLayer) starts
// here: any false return leaves that fallback in place.
bool parseFiniteNumber(const std::string& value, double& number) {
  const char* begin = value.c_str();
  const char* end = begin + value.size();
  while (begin != end && asciiSpace(*begin)) ++begin;
  while (begin != end && asciiSpace(end[-1])) --end;
  if (begin == end || !decimalSyntax(begin, end)) return false;
  errno = 0;
  char* parsed = nullptr;
  const double result = std::strtod(begin, &parsed);
  if (parsed != end || errno == ERANGE || !std::isfinite(result)) return false;
  number = result;
  return true;
}
static int clampInteger(int value, int lo, int hi) {
  return std::max(lo, std::min(value, hi));
}
// Linear-maps a fetched value through a NumericBinding to an integer property,
// per the "Numeric mapping" contract in docs/features/themes.md: clamp (or
// extrapolate) between input0/input1, scale to output0/output1, round half
// away from zero, then clamp to the property's own safe [lo, hi] range
// regardless of `clamp` -- that final clamp is mandatory and not something a
// theme can opt out of.
//
// The direct formula `output0 + (value-input0)*(output1-output0)/(input1-input0)`
// is used whenever it stays finite. Declared endpoints are unconstrained by
// the parser beyond "finite and different", so a theme can legally set them
// to something like 1e300 and -1e300; at that scale the direct numerator or
// denominator can overflow even `long double`. The fallback branch instead
// divides every term by the larger endpoint's magnitude first, trading a
// little precision for staying finite at the extremes.
int resolveNumericBinding(const NumericBinding& binding, double value, int lo, int hi) {
  if (lo > hi) std::swap(lo, hi);
  if (!std::isfinite(value) || !std::isfinite(binding.input0) ||
      !std::isfinite(binding.input1) || binding.input0 == binding.input1)
    return clampInteger(binding.output0, lo, hi);
  if (binding.clamp) {
    const double inputLow = std::min(binding.input0, binding.input1);
    const double inputHigh = std::max(binding.input0, binding.input1);
    value = std::max(inputLow, std::min(value, inputHigh));
  }
  const long double input0 = binding.input0, input1 = binding.input1, source = value;
  const long double outputDelta = static_cast<long double>(binding.output1) - binding.output0;
  const long double inputDelta = input1 - input0, sourceDelta = source - input0;
  const long double numerator = sourceDelta * outputDelta;
  long double mapped = 0;
  if (std::isfinite(inputDelta) && std::isfinite(numerator)) {
    mapped = static_cast<long double>(binding.output0) + numerator / inputDelta;
  } else {
    // Scaled fallback: normalize both endpoints by their larger magnitude
    // before subtracting, so the intermediate values used for the ratio stay
    // within a representable range even when the raw endpoints do not.
    const long double scale = std::max(std::fabs(input0), std::fabs(input1));
    if (scale == 0) return clampInteger(binding.output0, lo, hi);
    const long double denominator = input1 / scale - input0 / scale;
    if (denominator == 0) return clampInteger(binding.output0, lo, hi);
    const long double position = (source / scale - input0 / scale) / denominator;
    mapped = static_cast<long double>(binding.output0) + position * outputDelta;
  }
  if (std::isnan(mapped)) return clampInteger(binding.output0, lo, hi);
  if (mapped <= lo) return lo;
  if (mapped >= hi) return hi;
  return static_cast<int>(std::round(mapped));
}
// Picks the last stop whose `at` is <= value; a value below the first stop
// uses the first stop's color. Stops are parsed in strictly increasing `at`
// order (DuplicateBindingScanner/parseBindings below enforce that), so a
// simple linear scan that keeps overwriting `result` is enough -- no need to
// search for the tightest bracketing pair.
uint16_t resolveColorBinding(const ColorBinding& binding, double value) {
  if (binding.stops.empty()) return 0;
  uint16_t result = binding.stops.front().value;
  for (const auto& stop : binding.stops) {
    if (stop.at > value) break;
    result = stop.value;
  }
  return result;
}
// Clock/date tokens recognized inside {...} regardless of what data sources a
// theme declares; anything else must resolve to a declared "<source>.<field>".
static const char* const tokens[] = {"HH", "hh", "MM", "SS", "DD", "MON", "MONTH", "WD", "WEEKDAY", "YYYY"};
// Added by spec 2 (#31 item 6): the hour without a leading zero, the month as
// a number, and AM/PM. A spec 1 theme that uses one is refused like any other
// unknown {...} span.
static const char* const tokensSpec2[] = {"H", "MO", "AMPM"};
// The `device.*` names a firmware may provide, shared by binding sources and
// text tokens (docs/features/themes.md).
static const char deviceNames[5][12] PROGMEM = {"focusToday", "focusGoal", "streak", "humidity", "temperature"};
// True if `token` is a declared "<source-id>.<field-id>" reference: both
// halves must be valid IDs, and the source and field must actually appear in
// this theme's `data` block. Shared between text-template validation
// (validText) and binding source validation (declaredSource).
static bool validFieldToken(const std::string& token, const Theme& theme) {
  size_t dot = token.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= token.size()) return false;
  std::string source = token.substr(0, dot), field = token.substr(dot + 1);
  if (!validId(source) || !validId(field)) return false;
  for (const auto& data : theme.data) {
    if (data.id != source) continue;
    // Spec 2: every source also has an "age" field, the minutes since its last
    // successful fetch, which the firmware supplies (DataAges in ThemeData.h).
    if (theme.spec >= 2 && field == "age") return true;
    for (const auto& f : data.fields) if (f.id == field) return true;
  }
  return false;
}
// A text `value` template: printable ASCII only, with every {...} span either
// a clock token or a declared data-field reference. A bare '}' outside a
// {...} span is rejected so authors cannot accidentally leave one unmatched.
static bool validText(const std::string& s, const Theme& theme) {
  if (s.size() > 128) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] < 32 || s[i] > 126 || s[i] == '}') return false;
    if (s[i] != '{') continue;
    size_t end = s.find('}', i);
    if (end == std::string::npos) return false;
    bool found = false;
    for (auto token : tokens) if (s.substr(i + 1, end - i - 1) == token) found = true;
    if (theme.spec >= 2) {
      const std::string span = s.substr(i + 1, end - i - 1);
      for (auto token : tokensSpec2) if (span == token) found = true;
      if (span.compare(0, 7, "device.") == 0) for (size_t n = 0; n < 5; ++n) if (strcmp_P(span.c_str() + 7, deviceNames[n]) == 0) found = true;
    }
    if (!found) found = validFieldToken(s.substr(i + 1, end - i - 1), theme);
    if (!found) return false;
    i = end;
  }
  return true;
}
// A manifest held in RAM, for the string overload of parseTheme().
class StringManifest : public ManifestSource {
 public:
  explicit StringManifest(const std::string& text) : text_(text) {}
  size_t size() const override { return text_.size(); }
  bool read(size_t offset, char* out, size_t count) override {
    if (offset > text_.size() || count > text_.size() - offset) return false;
    memcpy(out, text_.data() + offset, count);
    return true;
  }
 private:
  const std::string& text_;
};
// Reads bytes [begin, end) of a ManifestSource forward through a 32-byte
// buffer. It is ArduinoJson's Reader interface (read/readBytes) plus the
// peek() the scanner below needs, so every pass over a manifest streams it
// instead of needing the whole text in RAM. A failed source read ends the
// input early and sets failed(), which callers report as an unreadable
// package rather than as a JSON error.
class ManifestReader {
 public:
  ManifestReader(ManifestSource& source, size_t begin, size_t end) : source_(source), position_(begin), end_(end) {}
  int peek() {
    if (position_ >= end_) return -1;
    if (position_ < start_ || position_ >= start_ + length_) {
      size_t count = std::min(sizeof(buffer_), end_ - position_);
      if (!source_.read(position_, buffer_, count)) {
        failed_ = true;
        end_ = position_;
        return -1;
      }
      start_ = position_;
      length_ = count;
    }
    return static_cast<unsigned char>(buffer_[position_ - start_]);
  }
  int read() {
    int c = peek();
    if (c >= 0) ++position_;
    return c;
  }
  size_t readBytes(char* out, size_t count) {
    size_t n = 0;
    for (int c; n < count && (c = read()) >= 0;) out[n++] = char(c);
    return n;
  }
  size_t position() const { return position_; }
  bool failed() const { return failed_; }
 private:
  ManifestSource& source_;
  size_t position_, end_, start_ = 0, length_ = 0;
  bool failed_ = false;
  char buffer_[32];
};
// Parses a "#RRGGBB" string into RGB565 (5/6/5 bits), the format the
// renderer and the .sti asset container both use directly.
static bool parseColor(JsonVariantConst v, uint16_t& out) {
  if (!v.is<const char*>()) return false;
  JsonString s = v.as<JsonString>();
  if (s.size() != 7 || s.c_str()[0] != '#') return false;
  uint32_t rgb = 0;
  for (int i = 1; i < 7; ++i) {
    char c = s.c_str()[i];
    int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (n < 0) return false;
    rgb = (rgb << 4) | n;
  }
  out = ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
  return true;
}
// An unknown key as it appears in a diagnostic: at most 64 bytes, with
// anything outside printable ASCII written as \u00XX.
static std::string printableKey(const char* raw, size_t size) {
  std::string key;
  for (size_t i = 0; i < std::min<size_t>(size, 64); ++i) {
    unsigned char c = raw[i];
    if (c >= 32 && c <= 126) key += char(c);
    else {
      char hex[7];
      snprintf(hex, sizeof(hex), "\\u%04x", c);
      key += hex;
    }
  }
  return key;
}
// The device and desktop tool share both validation rules and field diagnostics.
class Fields {
 public:
  Fields(JsonObjectConst object, std::string path, std::string& error)
    : object_(object), path_(std::move(path)), error_(error) {}
  bool fail(const char* key, const std::string& reason) const {
    std::string path = path_;
    if (key && *key) {
      if (!path.empty()) path += '.';
      path += key;
    }
    error_ = (path.empty() ? "theme.json" : path) + ": " + reason;
    return false;
  }
  // Rejects any object key not present in `allowed`, a '|'-delimited list
  // like "|id|type|x|y|" (leading/trailing '|' so every entry, including the
  // first and last, matches the same "|name|" scan). This is what makes an
  // unknown field a manifest error instead of a silently ignored one, and
  // what makes the allowed-key set differ per layer type/shape.
  bool keys(const char* allowed) const {
    if (object_.isNull()) return fail("", "expected an object");
    for (JsonPairConst p : object_) {
      bool found = false;
      for (const char* start = allowed + 1; pgm_read_byte(start);) {
        const char* end = start;
        while (pgm_read_byte(end) && pgm_read_byte(end) != '|') ++end;
        if (!pgm_read_byte(end)) break;
        if (p.key().size() == size_t(end - start) && !memcmp_P(p.key().c_str(), start, end - start)) {
          found = true;
          break;
        }
        start = end + 1;
      }
      if (!found) return fail(printableKey(p.key().c_str(), p.key().size()).c_str(), "unknown field");
    }
    return true;
  }
  // `T` may be narrower than int (Layer stores 16-bit fields); the range
  // check happens on the int, so `lo`..`hi` must fit in `T`.
  template <typename T>
  bool number(const char* key, int lo, int hi, T& value, bool required = true) const {
    auto v = object_[key];
    if (v.isNull() && !required) return true;
    if (!v.is<int>() || v.as<int>() < lo || v.as<int>() > hi)
      return fail(key, "expected an integer from " + std::to_string(lo) + " to " + std::to_string(hi));
    value = static_cast<T>(v.as<int>());
    return true;
  }
  bool text(const char* key, std::string& value, size_t max, bool required = true) const {
    auto v = object_[key];
    if (v.isNull() && !required) return true;
    if (!v.is<const char*>()) return fail(key, "expected a string");
    JsonString s = v.as<JsonString>();
    value.assign(s.c_str(), s.size());
    if (value.empty() || value.size() > max) return fail(key, "expected 1 to " + std::to_string(max) + " bytes");
    for (unsigned char c : value) if (c < 32 || c == 127) return fail(key, "control characters are not allowed");
    return true;
  }
  bool boolean(const char* key, bool& value, bool required = true) const {
    auto v = object_[key];
    if (v.isNull() && !required) return true;
    if (!v.is<bool>()) return fail(key, "expected a boolean");
    value = v.as<bool>();
    return true;
  }
  bool color(const char* key, uint16_t& value) const {
    return parseColor(object_[key], value) || fail(key, "expected a color in #RRGGBB form");
  }
  bool real(const char* key, double& value) const {
    JsonVariantConst v = object_[key];
    if (!v.is<double>() && !v.is<long>() && !v.is<int>())
      return fail(key, "expected a finite number");
    value = v.as<double>();
    return std::isfinite(value) || fail(key, "expected a finite number");
  }
  // Used to distinguish "field omitted" from "field present with its default
  // value" for the handful of optional fields (pause, gap, clamp, ...) whose
  // absence should keep a struct default rather than overwrite it with 0.
  bool has(const char* key) const {
    for (JsonPairConst pair : object_) if (pair.key().size() == strlen(key) && !memcmp(pair.key().c_str(), key, pair.key().size())) return true;
    return false;
  }
 private:
  JsonObjectConst object_;
  std::string path_;
  std::string& error_;
};
// What ManifestScanner records for parseTheme() to read the layers one at a
// time instead of as one large document: the byte range of every element of
// the root `layers` array (of its last occurrence, matching ArduinoJson) and
// the root keys, whose check the filtered first pass cannot do itself.
struct ManifestLayout {
  struct Range {
    size_t begin, end;
  };
  // Root `layers` elements, or none when the root has no `layers` array.
  bool layersFound = false;
  std::vector<Range> layerRanges;
  size_t layerCount = 0;
  std::vector<std::string> rootKeys;
};
// ArduinoJson retains only the last duplicate object member. Scan binding keys
// in the raw manifest so that a manifest cannot silently replace a target.
// The same walk fills in the ManifestLayout.
// Run only after the first JSON pass (including its eight-level nesting
// limit). The manifest byte limit bounds string storage; at most eight
// binding keys, 33 layer ranges and the root keys are kept.
class ManifestScanner {
 public:
  ManifestScanner(ManifestSource& input, ManifestLayout& layout, std::string& error)
    : input_(input, 0, input.size()), layout_(layout), error_(error) {}
  bool valid() {
    const bool scanned = scanRoot();
    space();
    if (scanned && input_.peek() < 0 && !input_.failed()) return true;
    if (error_.empty()) error_ = input_.failed() ? "theme.json: could not be read" : "theme.json: expected standard JSON object syntax";
    return false;
  }
 private:
  // This is a minimal, purpose-built JSON walker: it only needs to reach
  // every `bind` object's keys in source order, so it does not build any
  // value representation, just skips over whatever it is not looking at.
  bool more() { return input_.peek() >= 0; }
  bool at(char c) { return input_.peek() == static_cast<unsigned char>(c); }
  void space() { while (at(' ') || at('\n') || at('\r') || at('\t')) input_.read(); }
  bool take(char expected) {
    space();
    if (!at(expected)) return false;
    input_.read();
    return true;
  }
  bool string(std::string& output) {
    space();
    if (input_.read() != '\"') return false;
    output.clear();
    for (int raw; (raw = input_.read()) >= 0;) {
      unsigned char c = raw;
      if (c == '\"') return true;
      if (c < 32) return false;
      if (c != '\\') {
        output += char(c);
        continue;
      }
      int escaped = input_.read();
      if (escaped < 0) return false;
      c = escaped;
      if (c == '\"' || c == '\\' || c == '/') output += char(c);
      else if (c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') output += '?';
      else if (c == 'u') {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
          int hex = input_.read();
          int digit = hex >= '0' && hex <= '9' ? hex - '0' : hex >= 'a' && hex <= 'f' ? hex - 'a' + 10 : hex >= 'A' && hex <= 'F' ? hex - 'A' + 10 : -1;
          if (digit < 0) return false;
          value = (value << 4) | unsigned(digit);
        }
        output += value <= 127 ? char(value) : '?';
      } else return false;
    }
    return false;
  }
  // Skips one JSON value without interpreting it -- used for every value this
  // scanner does not otherwise care about (anything outside `layers[].bind`).
  bool value() {
    space();
    if (!more()) return false;
    if (at('{') || at('[')) {
      if (depth_ == 8) return false;
      ++depth_;
      const bool valid = at('{') ? object() : array();
      --depth_;
      return valid;
    }
    if (at('\"')) {
      std::string ignored;
      return string(ignored);
    }
    const size_t start = input_.position();
    while (more() && !at(',') && !at(']') && !at('}') && !at(' ') && !at('\n') && !at('\r') && !at('\t')) input_.read();
    return input_.position() > start;
  }
  bool object() {
    if (!take('{')) return false;
    space();
    if (at('}')) {
      input_.read();
      return true;
    }
    for (;;) {
      std::string key;
      if (!string(key) || !take(':') || !value()) return false;
      space();
      if (!more()) return false;
      if (at('}')) {
        input_.read();
        return true;
      }
      if (input_.read() != ',') return false;
    }
  }
  bool array() {
    if (!take('[')) return false;
    space();
    if (at(']')) {
      input_.read();
      return true;
    }
    for (;;) {
      if (!value()) return false;
      space();
      if (!more()) return false;
      if (at(']')) {
        input_.read();
        return true;
      }
      if (input_.read() != ',') return false;
    }
  }
  // The one object this whole scanner exists for: walks a layer's `bind`
  // object key by key, in source order, so a repeated key is caught before
  // ArduinoJson's parsed representation has already silently kept only the
  // last occurrence.
  bool bindings(size_t layer) {
    if (!take('{')) return false;
    std::vector<std::string> seen;
    space();
    if (at('}')) {
      input_.read();
      return true;
    }
    for (;;) {
      std::string key;
      if (!string(key) || !take(':')) return false;
      for (const auto& old : seen) if (old == key) {
        error_ = "layers[" + std::to_string(layer) + "].bind." + key + ": duplicate binding target";
        return false;
      }
      if (seen.size() == 8) {
        error_ = "layers[" + std::to_string(layer) + "].bind: expected at most 8 bindings";
        return false;
      }
      seen.push_back(key);
      if (!value()) return false;
      space();
      if (!more()) return false;
      if (at('}')) {
        input_.read();
        return true;
      }
      if (input_.read() != ',') return false;
    }
  }
  // Walks one layer object, descending into `bind` specifically and skipping
  // every other key via value().
  bool layer(size_t index) {
    if (!take('{')) return false;
    space();
    if (at('}')) {
      input_.read();
      return true;
    }
    for (;;) {
      std::string key;
      if (!string(key) || !take(':')) return false;
      if (key == "bind") {
        space();
        if (at('{')) {
          if (!bindings(index)) return false;
        } else if (!value()) return false;
      } else if (!value()) return false;
      space();
      if (!more()) return false;
      if (at('}')) {
        input_.read();
        return true;
      }
      if (input_.read() != ',') return false;
    }
  }
  // Records every element's byte range, objects or not, so parseTheme()
  // reports a non-object layer exactly as before. Ranges stop at 33: one
  // more than allowed is enough to report "too many layers".
  bool layers() {
    if (!take('[')) return false;
    layout_.layersFound = true;
    layout_.layerRanges.clear();
    layout_.layerCount = 0;
    size_t index = 0;
    space();
    if (at(']')) {
      input_.read();
      return true;
    }
    for (;;) {
      space();
      const size_t begin = input_.position();
      if (at('{')) {
        if (!layer(index)) return false;
      } else if (!value()) return false;
      if (layout_.layerRanges.size() <= MaxLayers) layout_.layerRanges.push_back({begin, input_.position()});
      ++layout_.layerCount;
      ++index;
      space();
      if (!more()) return false;
      if (at(']')) {
        input_.read();
        return true;
      }
      if (input_.read() != ',') return false;
    }
  }
  // Entry point: descends into the root object's `layers` array specifically
  // and skips everything else, since only layers can declare `bind`.
  bool scanRoot() {
    if (!take('{')) return false;
    space();
    if (at('}')) {
      input_.read();
      return true;
    }
    for (;;) {
      std::string key;
      if (!string(key) || !take(':')) return false;
      bool known = false;
      for (const auto& old : layout_.rootKeys) if (old == key) known = true;
      if (!known) layout_.rootKeys.push_back(key);
      if (key == "layers") {
        space();
        if (at('[')) {
          if (!layers()) return false;
        } else {
          layout_.layersFound = false;
          if (!value()) return false;
        }
      } else if (!value()) return false;
      space();
      if (!more()) return false;
      if (at('}')) {
        input_.read();
        return true;
      }
      if (input_.read() != ',') return false;
    }
  }
  ManifestReader input_;
  ManifestLayout& layout_;
  std::string& error_;
  unsigned depth_ = 0;
};
// Out of line, so the scanner and its reader are on the stack only while it
// walks; parseTheme() keeps just the layout.
__attribute__((noinline)) static bool scanManifest(ManifestSource& json, ManifestLayout& layout, std::string& error) {
  ManifestScanner scanner(json, layout, error);
  return scanner.valid();
}
// Maps a `bind` object's key to the numeric BoundProperty it names, or false
// if the name is not a numeric property at all (it might still be a color
// one -- see colorProperty below).
static bool numericProperty(const std::string& name, BoundProperty& property) {
  struct Name {
    const char* name;
    BoundProperty property;
  };
  static const Name names[] = {
    {"x", BoundProperty::X}, {"y", BoundProperty::Y}, {"width", BoundProperty::Width},
    {"height", BoundProperty::Height}, {"radius", BoundProperty::Radius},
    {"cornerRadius", BoundProperty::CornerRadius}, {"x2", BoundProperty::X2},
    {"y2", BoundProperty::Y2}, {"size", BoundProperty::Size},
    {"strokeWidth", BoundProperty::StrokeWidth}, {"scroll.width", BoundProperty::ScrollWidth},
    {"scroll.speed", BoundProperty::ScrollSpeed}, {"angle", BoundProperty::Angle},
    {"sweep", BoundProperty::Sweep}
  };
  for (const auto& item : names) if (name == item.name) {
    property = item.property;
    return true;
  }
  return false;
}
static bool colorProperty(const std::string& name, BoundProperty& property) {
  // Only the three properties every color-capable layer type already has
  // statically are bindable -- see applicable() for the per-layer-type gate.
  if (name == "color") {
    property = BoundProperty::Color;
    return true;
  }
  if (name == "fill") {
    property = BoundProperty::Fill;
    return true;
  }
  if (name == "stroke") {
    property = BoundProperty::Stroke;
    return true;
  }
  return false;
}
// The mandatory safe [lo, hi] clamp for each bindable numeric property (see
// resolveNumericBinding). These match the manifest's own static ranges for
// the same field, so a binding can never produce a value the static field
// itself could not have declared.
static bool propertyRange(BoundProperty property, int& lo, int& hi) {
  switch (property) {
    case BoundProperty::X: case BoundProperty::Y: case BoundProperty::X2: case BoundProperty::Y2:
      lo = -240;
      hi = 479;
      return true;
    case BoundProperty::Width: case BoundProperty::Height: case BoundProperty::Radius:
      lo = 0;
      hi = 240;
      return true;
    case BoundProperty::CornerRadius:
      lo = 0;
      hi = 120;
      return true;
    case BoundProperty::Size:
      lo = 8;
      hi = 96;
      return true;
    case BoundProperty::StrokeWidth:
      lo = 1;
      hi = 32;
      return true;
    case BoundProperty::ScrollWidth: case BoundProperty::ScrollSpeed:
      lo = 1;
      hi = 240;
      return true;
    case BoundProperty::Angle: case BoundProperty::Sweep:
      lo = -360;
      hi = 360;
      return true;
    default:
      return false;
  }
}
// The full layer/property matrix from docs/features/themes.md, enforced here
// rather than only documented: a color property needs the matching static
// field already set (binding `stroke` cannot conjure a stroke into existence
// on a shape declared without one), and scroll.width/scroll.speed need a
// static `scroll` object already present.
static bool applicable(const Layer& layer, BoundProperty property, bool color) {
  if (color) {
    if (property == BoundProperty::Color) return layer.type == LayerType::Text;
    if (property == BoundProperty::Fill) return layer.type == LayerType::Shape && layer.hasFill;
    return property == BoundProperty::Stroke && layer.type == LayerType::Shape && layer.hasStroke;
  }
  if (property == BoundProperty::X || property == BoundProperty::Y) return true;
  if (property == BoundProperty::ScrollWidth || property == BoundProperty::ScrollSpeed)
    return layer.type == LayerType::Text && layer.scroll.enabled;
  if (layer.type == LayerType::Text) return property == BoundProperty::Size && !layer.font;
  if (layer.type != LayerType::Shape) return false;
  if (property == BoundProperty::StrokeWidth) return true;
  if (layer.shape == Shape::Rectangle)
    return property == BoundProperty::Width || property == BoundProperty::Height || property == BoundProperty::CornerRadius;
  if (layer.shape == Shape::Circle) return property == BoundProperty::Radius;
  if (layer.shape == Shape::Arc) return property == BoundProperty::Radius || property == BoundProperty::Sweep;
  if (layer.shape == Shape::Hand) return property == BoundProperty::Angle;
  if (layer.shape == Shape::Ticks) return property == BoundProperty::Radius;
  return property == BoundProperty::X2 || property == BoundProperty::Y2;
}
// Built-in names, spec 2 only. `time.*` is fixed; the five `device.*` names are
// the ones a firmware may provide (docs/features/themes.md).
// The tables and the messages sit in flash on the ESP8266, where a string
// literal would otherwise take RAM; the host tests see plain arrays.
static const char timeNames[9][12] PROGMEM = {"second", "minute", "hour", "hour12", "secondOfDay", "day", "month", "weekday", "moon"};
static const char needSpec2[] PROGMEM = "time.* and device.* sources need \"spec\": 2";
static const char unknownBuiltIn[] PROGMEM = "unknown time.* or device.* name";
static const char reservedId[] PROGMEM = "time and device are the names of built-in sources";
static const char fontNeedsSpec2[] PROGMEM = "needs \"spec\": 2";
static const char sizeWithFont[] PROGMEM = "not allowed with a font, which sets the size";
static const char tooManyFonts[] PROGMEM = "expected at most 4 fonts per theme";
static std::string fromFlash(const char* text) {
  std::string out(strlen_P(text), '\0');
  memcpy_P(&out[0], text, out.size());
  return out;
}
BuiltIn builtInSource(const std::string& source) {
  if (source.compare(0, 5, "time.") == 0) {
    for (size_t i = 0; i < 9; ++i)
      if (strcmp_P(source.c_str() + 5, timeNames[i]) == 0) return static_cast<BuiltIn>(static_cast<int>(BuiltIn::TimeSecond) + i);
  } else if (source.compare(0, 7, "device.") == 0) {
    for (size_t i = 0; i < 5; ++i) if (strcmp_P(source.c_str() + 7, deviceNames[i]) == 0) return BuiltIn::Device;
  }
  return BuiltIn::None;
}
// Age of the moon in whole days, 0-29: the time since the reference new moon
// (2000-01-06 18:14 UTC) modulo the mean synodic month (29.530588 d, rounded to
// 2551443 s). Integers only, no floating point. The clock's fields are read as
// UTC; the configured timezone's offset adds to the error (docs/features/themes.md).
static int moonAge(const tm& t) {
  // Days since 1970-01-01 of the proleptic Gregorian date (Howard Hinnant's algorithm).
  int32_t y = t.tm_year + 1900;
  const int32_t m = t.tm_mon + 1;
  if (m <= 2) --y;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const int32_t yoe = y - era * 400;
  const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int32_t days = era * 146097 + doe - 719468;
  const int64_t seconds = static_cast<int64_t>(days) * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
  const int64_t sinceNewMoon = seconds - 947182440;     // 2000-01-06 18:14:00 UTC
  int32_t into = static_cast<int32_t>(sinceNewMoon % 2551443);
  if (into < 0) into += 2551443;                        // before the reference: count back from it
  return into / 86400;
}
double timeValue(BuiltIn which, const tm& time) {
  switch (which) {
    case BuiltIn::TimeSecond: return time.tm_sec;
    case BuiltIn::TimeMinute: return time.tm_min;
    case BuiltIn::TimeHour: return time.tm_hour;
    case BuiltIn::TimeHour12: return time.tm_hour % 12;
    case BuiltIn::TimeSecondOfDay: return time.tm_hour * 3600 + time.tm_min * 60 + time.tm_sec;
    case BuiltIn::TimeDay: return time.tm_mday;
    case BuiltIn::TimeMonth: return time.tm_mon + 1;
    case BuiltIn::TimeWeekday: return time.tm_wday;
    case BuiltIn::TimeMoon: return moonAge(time);
    default: return 0;
  }
}
// A binding source is a declared data field or, under spec 2, a built-in
// value. Spec 1 keeps its exact rule: a name that is not a declared field is
// refused, and for a time./device. name the message says why.
static bool declaredSource(const Theme& theme, Binding& binding, const Fields& fields) {
  const bool dotted = binding.source.compare(0, 5, "time.") == 0 || binding.source.compare(0, 7, "device.") == 0;
  if (dotted && theme.spec < 2) {
    if (validFieldToken(binding.source, theme)) return true;
    return fields.fail("source", fromFlash(needSpec2));
  }
  if (dotted) {
    binding.builtin = builtInSource(binding.source);
    if (binding.builtin == BuiltIn::None) return fields.fail("source", fromFlash(unknownBuiltIn));
    return true;
  }
  return validFieldToken(binding.source, theme) || fields.fail("source", "expected a declared data field");
}
// Shared by `input` (two finite doubles) and, via twoIntegers below, `output`
// (two ranged ints) -- both binding schemas need exactly a two-element array.
static bool twoReals(JsonVariantConst value, const Fields& fields, const char* key, double& first, double& second) {
  JsonArrayConst values = value.as<JsonArrayConst>();
  if (values.isNull() || values.size() != 2) return fields.fail(key, "expected exactly two finite numbers");
  JsonVariantConst a = values[0], b = values[1];
  if ((!a.is<double>() && !a.is<long>() && !a.is<int>()) || (!b.is<double>() && !b.is<long>() && !b.is<int>()))
    return fields.fail(key, "expected exactly two finite numbers");
  first = a.as<double>();
  second = b.as<double>();
  return (std::isfinite(first) && std::isfinite(second)) || fields.fail(key, "expected exactly two finite numbers");
}
static bool twoIntegers(JsonVariantConst value, const Fields& fields, const char* key, int lo, int hi, int& first, int& second) {
  JsonArrayConst values = value.as<JsonArrayConst>();
  if (values.isNull() || values.size() != 2 || !values[0].is<int>() || !values[1].is<int>())
    return fields.fail(key, "expected exactly two integers from " + std::to_string(lo) + " to " + std::to_string(hi));
  first = values[0].as<int>();
  second = values[1].as<int>();
  if (first < lo || first > hi || second < lo || second > hi)
    return fields.fail(key, "expected exactly two integers from " + std::to_string(lo) + " to " + std::to_string(hi));
  return true;
}
// Parses a text layer's `scroll` object. `mode` and `speed` are required;
// `pause` and `gap` keep their Scroll struct defaults when omitted. `gap` is
// rejected outright in bounce mode, where it has no effect (bounce only ever
// shows one copy of the text).
static bool parseScroll(JsonObjectConst object, const std::string& path, std::string& error, Layer& result) {
  JsonObjectConst scroll = object["scroll"].as<JsonObjectConst>();
  Fields fields(scroll, path + ".scroll", error);
  if (!fields.keys("|width|mode|speed|pause|gap|")) return false;
  if (!fields.number("width", 1, 240, result.scroll.width)) return false;
  std::string mode;
  if (!fields.text("mode", mode, 6) || !fields.number("speed", 1, 240, result.scroll.speed)) return false;
  if (mode == "loop") result.scroll.mode = ScrollMode::Loop;
  else if (mode == "bounce") result.scroll.mode = ScrollMode::Bounce;
  else return fields.fail("mode", "expected loop or bounce");
  if (fields.has("pause") && !fields.number("pause", 0, 10000, result.scroll.pauseMs)) return false;
  if (fields.has("gap") && !fields.number("gap", 0, 240, result.scroll.gap)) return false;
  if (result.scroll.mode == ScrollMode::Bounce && fields.has("gap")) return fields.fail("gap", "not allowed in bounce mode");
  result.scroll.enabled = true;
  return true;
}
// Parses a layer's `bind` object: at most 8 entries (already enforced key-by-
// key by ManifestScanner, but re-checked here on the parsed count),
// each either a numeric mapping or a color-stop table depending on which
// property it names.
static bool parseBindings(JsonObjectConst object, const Theme& theme, const std::string& path, std::string& error, Layer& layer) {
  JsonObjectConst objectBindings = object["bind"].as<JsonObjectConst>();
  Fields bindings(objectBindings, path + ".bind", error);
  if (objectBindings.isNull()) return bindings.fail("", "expected an object");
  if (objectBindings.size() > 8) return bindings.fail("", "expected at most 8 bindings");
  layer.bindings.reserve(objectBindings.size());
  for (JsonPairConst pair : objectBindings) {
    std::string name(pair.key().c_str(), pair.key().size());
    Binding binding;
    if (numericProperty(name, binding.property)) binding.color = false;
    else if (colorProperty(name, binding.property)) binding.color = true;
    else return bindings.fail(name.c_str(), "unknown binding target");
    if (!applicable(layer, binding.property, binding.color)) return bindings.fail(name.c_str(), "binding is not applicable to this layer");
    JsonObjectConst declaration = pair.value().as<JsonObjectConst>();
    Fields fields(declaration, path + ".bind." + name, error);
    if (declaration.isNull()) return fields.fail("", "expected an object");
    if (binding.color) {
      if (!fields.keys("|source|stops|") || !fields.text("source", binding.source, 64)) return false;
      if (!declaredSource(theme, binding, fields)) return false;
      JsonArrayConst stops = declaration["stops"].as<JsonArrayConst>();
      if (stops.isNull() || stops.size() == 0 || stops.size() > 8) return fields.fail("stops", "expected 1 to 8 stops");
      double previous = 0;
      for (size_t i = 0; i < stops.size(); ++i) {
        JsonObjectConst item = stops[i].as<JsonObjectConst>();
        Fields stop(item, path + ".bind." + name + ".stops[" + std::to_string(i) + "]", error);
        ColorStop color;
        if (!stop.keys("|at|value|") || !stop.real("at", color.at) || !stop.color("value", color.value)) return false;
        if (i && color.at <= previous) return stop.fail("at", "stops must be strictly increasing");
        previous = color.at;
        binding.colors.stops.push_back(color);
      }
    } else {
      int lo = 0, hi = 0;
      if (!propertyRange(binding.property, lo, hi)) return fields.fail("", "expected a numeric binding target");
      if (!fields.keys("|source|input|output|clamp|") || !fields.text("source", binding.source, 64)) return false;
      if (!declaredSource(theme, binding, fields)) return false;
      if (!twoReals(declaration["input"], fields, "input", binding.numeric.input0, binding.numeric.input1)) return false;
      if (binding.numeric.input0 == binding.numeric.input1) return fields.fail("input", "endpoints must differ");
      if (!twoIntegers(declaration["output"], fields, "output", lo, hi, binding.numeric.output0, binding.numeric.output1)) return false;
      if (fields.has("clamp") && !fields.boolean("clamp", binding.numeric.clamp)) return false;
    }
    if (binding.builtin != BuiltIn::None && binding.builtin != BuiltIn::Device) layer.timeBound = true;
    layer.bindings.push_back(std::move(binding));
  }
  return true;
}
// Strictly parses a manifest into an immutable Theme, or fails closed with a
// diagnostic naming the offending field (`layers[2].bind.width: ...`). This
// is the single source of truth for the format: the device and the desktop
// tools (theme_native.cpp) both call this exact function, so validation can
// never drift between them. Every unknown field, wrong type, out-of-range
// value, or structural inconsistency (duplicate ID, undeclared binding
// source, ...) is rejected rather than coerced or ignored.
bool parseTheme(const std::string& json, Theme& out, std::string& error) {
  StringManifest source(json);
  return parseTheme(source, out, error);
}
__attribute__((noinline)) static bool parseLayer(JsonObjectConst o, Theme& t, BlockList<std::string>& ids, std::string& error);
// A JSON pass's failure as a diagnostic: an unreadable source, or the
// ArduinoJson error with the absolute byte it stopped at.
static bool jsonFailed(DeserializationError err, const ManifestReader& reader, std::string& error) {
  if (reader.failed()) error = "theme.json: could not be read";
  else if (err) error = "theme.json: " + std::string(err.c_str()) + " at byte " + std::to_string(reader.position());
  else return false;
  return true;
}
// On the ESP8266 the loop stack is 4 KiB, and parsing JSON recurses: one
// frame per nesting level, in ArduinoJson and in ManifestScanner alike. So
// every recursive step runs from a small frame, and the validation, whose
// field checkers need large frames, runs only after it has returned. Each
// is a separate out-of-line function that parseTheme() calls in turn.

// The first pass's JSON: everything except the layers, which the scanner
// locates for parseLayers() to read one at a time.
__attribute__((noinline)) static bool readHeader(ManifestSource& json, JsonDocument& doc, std::string& error) {
  JsonDocument filter;
  for (const char* key : {"spec", "theme", "display", "data"}) filter[key] = true;
  ManifestReader reader(json, 0, json.size());
  auto err = deserializeJson(doc, reader, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8));
  if (jsonFailed(err, reader, error)) return false;
  for (int c; (c = reader.read()) >= 0;) {
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      error = "theme.json: trailing content after manifest";
      return false;
    }
  }
  return !jsonFailed(err, reader, error);
}
// Validates the first pass's document and the root keys `scan` found.
__attribute__((noinline)) static bool parseHeader(JsonObjectConst root, const ManifestLayout& scan, Theme& t, std::string& error) {
  for (const auto& key : scan.rootKeys) {
    if (key != "spec" && key != "theme" && key != "display" && key != "layers" && key != "data") {
      error = printableKey(key.data(), key.size()) + ": unknown field";
      return false;
    }
  }
  Fields r(root, "", error);
  // Spec 2 (#31) also allows STI2 images. Firmware up to 3.1.1 refuses it:
  // here, or already at the first STI2 image (readIndex() runs first).
  int spec = 0;
  if (!r.number("spec", 1, 2, spec)) return false;
  t.spec = static_cast<uint8_t>(spec);
  // ---- theme metadata ----
  auto meta = root["theme"].as<JsonObjectConst>();
  Fields m(meta, "theme", error);
  if (!m.keys("|id|name|author|version|") || !m.text("id", t.id, 48)) return false;
  if (!validId(t.id)) return m.fail("id", "use ASCII letters, digits, '-' or '_'");
  if (!m.text("name", t.name, 96) || !m.text("author", t.author, 96) || !m.text("version", t.version, 32)) return false;
  // ---- display: width/height are fixed at 240x240 but still required, so a
  // manifest is explicit about the canvas it was authored for ----
  auto display = root["display"].as<JsonObjectConst>();
  Fields d(display, "display", error);
  int w = 0, h = 0;
  if (!d.keys("|width|height|background|") || !d.number("width", 240, 240, w) || !d.number("height", 240, 240, h) || !d.color("background", t.background)) return false;
  // ---- data sources: optional, at most 4, each with 1-8 fields ----
  bool hasData = false;
  for (JsonPairConst pair : root) if (pair.key() == "data") hasData = true;
  if (hasData) {
    auto sources = root["data"].as<JsonArrayConst>();
    if (sources.isNull()) return r.fail("data", "expected an array of sources");
    if (sources.size() > MaxDataSources) return r.fail("data", "expected at most 4 sources");
    t.data.reserve(sources.size());
    for (JsonObjectConst source : sources) {
      ThemeDataSource data;
      Fields sf(source, "data[" + std::to_string(t.data.size()) + "]", error);
      int interval = 0;
      if (!sf.keys("|id|url|interval|insecureTls|fields|") || !sf.text("id", data.id, 32) || !sf.text("url", data.url, 200) || !sf.number("interval", 10, 86400, interval) || !sf.boolean("insecureTls", data.insecureTls, false)) return false;
      if (!validId(data.id) || data.id.find('.') != std::string::npos) return sf.fail("id", "use ASCII letters, digits, '-' or '_'");
      if (t.spec >= 2 && (data.id == "time" || data.id == "device")) return sf.fail("id", fromFlash(reservedId));
      // The embedded TLS client cannot validate a server certificate, so an
      // https:// source must explicitly acknowledge that with `insecureTls`
      // rather than the firmware silently accepting unauthenticated TLS.
      size_t hostStart = data.url.rfind("https://", 0) == 0 ? 8 : data.url.rfind("http://", 0) == 0 ? 7 : std::string::npos;
      if (hostStart == std::string::npos) return sf.fail("url", "only http:// and https:// URLs are supported");
      if (hostStart == 8 && !data.insecureTls) return sf.fail("insecureTls", "set true to acknowledge that HTTPS certificate validation is unavailable");
      size_t hostEnd = data.url.find('/', hostStart);
      if (hostStart == data.url.size() || (hostEnd == hostStart) || (data.url[hostStart] == '?' || data.url[hostStart] == '#')) return sf.fail("url", "URL must include a host");
      data.interval = static_cast<uint32_t>(interval);
      auto fields = source["fields"].as<JsonArrayConst>();
      if (fields.isNull() || fields.size() == 0 || fields.size() > MaxDataFields) return sf.fail("fields", "expected 1 to 8 fields");
      data.fields.reserve(fields.size());
      for (JsonObjectConst field : fields) {
        ThemeDataField item;
        Fields ff(field, "data[" + std::to_string(t.data.size()) + "].fields[" + std::to_string(data.fields.size()) + "]", error);
        if (!ff.keys("|id|path|") || !ff.text("id", item.id, 24) || !ff.text("path", item.path, 96)) return false;
        if (!validId(item.id) || item.id.find('.') != std::string::npos) return ff.fail("id", "use ASCII letters, digits, '-' or '_'");
        for (const auto& old : data.fields) if (old.id == item.id) return ff.fail("id", "duplicate field ID");
        size_t pathPart = 0;
        for (size_t p = 0; p <= item.path.size(); ++p) {
          if (p == item.path.size() || item.path[p] == '.') {
            if (p == pathPart) return ff.fail("path", "use non-empty dotted JSON object keys");
            pathPart = p + 1;
            continue;
          }
          char c = item.path[p];
          if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return ff.fail("path", "use a dotted JSON object path");
        }
        data.fields.push_back(std::move(item));
      }
      for (const auto& old : t.data) if (old.id == data.id) return sf.fail("id", "duplicate data source ID");
      t.data.push_back(std::move(data));
    }
  }
  // ---- layers: 0-32, drawn back to front in array order ----
  if (!scan.layersFound || scan.layerCount > MaxLayers) return r.fail("layers", "expected an array of at most 32 layers");
  return true;
}
// One element of the root `layers` array, as its own document.
__attribute__((noinline)) static bool readLayer(ManifestSource& json, const ManifestLayout::Range& range, JsonDocument& doc, std::string& error) {
  ManifestReader reader(json, range.begin, range.end);
  // The root object and the layers array are two of the eight levels. The
  // first pass already enforces the limit on the whole document (a looser
  // value here changes no test result); this keeps a layer's parse from
  // depending on that.
  auto err = deserializeJson(doc, reader, DeserializationOption::NestingLimit(6));
  return !jsonFailed(err, reader, error);
}
// A spec 2 theme's translations are counted before its layers are parsed, so
// the list reserves exactly that many entries. Left to open its blocks as the
// layers come, the first translation would take a whole block (issue #30),
// about 750 bytes for a text of two letters. The count is capped at what
// MaxTranslationBytes can hold at the least, and parseTextValue() still checks
// every text against it, so a manifest over the limit is refused there.
__attribute__((noinline)) static bool reserveTranslations(ManifestSource& json, const ManifestLayout& scan, Theme& t, std::string& error) {
  JsonDocument filter;
  filter["value"] = true;
  size_t count = 0;
  for (const auto& range : scan.layerRanges) {
    JsonDocument doc;
    ManifestReader reader(json, range.begin, range.end);
    auto err = deserializeJson(doc, reader, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(6));
    if (jsonFailed(err, reader, error)) return false;
    for (JsonPairConst p : doc["value"].as<JsonObjectConst>())
      if (languageIndex(p.key().c_str(), p.key().size()) > 0) ++count;
  }
  size_t most = MaxTranslationBytes / sizeof(LocalizedText);
  t.translations.reserve(count < most ? count : most);
  return true;
}
// The second pass: each layer `scan` found, parsed on its own and its JSON
// freed before the next.
__attribute__((noinline)) static bool parseLayers(ManifestSource& json, const ManifestLayout& scan, Theme& t, std::string& error) {
  t.layers.reserve(scan.layerRanges.size());
  if (t.spec >= 2 && !reserveTranslations(json, scan, t, error)) return false;
  // The layer IDs seen so far, for the duplicate check; Layer does not keep
  // its ID, so they exist only while the layers are parsed.
  BlockList<std::string> ids;
  ids.reserve(scan.layerRanges.size());
  for (const auto& range : scan.layerRanges) {
    JsonDocument doc;
    if (!readLayer(json, range, doc, error) || !parseLayer(doc.as<JsonObjectConst>(), t, ids, error)) return false;
  }
  return true;
}
// The manifest is never held as one document, which on the ESP8266 would
// not fit next to a running theme (issue #30). A first pass parses
// everything except the layers; the scanner then finds each layer's bytes,
// and every layer is parsed on its own. This function holds only what the
// passes share, so its own stack frame stays small.
bool parseTheme(ManifestSource& json, Theme& out, std::string& error) {
  error.clear();
  if (json.size() == 0 || json.size() > MaxManifest) {
    error = "theme.json: expected 1 to 16384 bytes";
    return false;
  }
  ManifestLayout scan;
  // On the heap: 132 bytes that every pass would otherwise carry on the
  // stack, and the stack is the tighter limit here.
  std::unique_ptr<Theme> t(new Theme());
  {
    // Freed at the end of this block, before the layers are read.
    JsonDocument doc;
    if (!readHeader(json, doc, error)) return false;
    // ArduinoJson has already validated syntax and nesting depth at this
    // point; only now is it safe to re-walk the raw text for what the
    // filtered pass cannot see (see ManifestScanner's own comment).
    if (!scanManifest(json, scan, error) || !parseHeader(doc.as<JsonObjectConst>(), scan, *t, error)) return false;
  }
  if (!parseLayers(json, scan, *t, error)) return false;
  out = std::move(*t);
  error.clear();
  return true;
}
// Each shape's allowed keys. In flash on the ESP8266, where a string literal
// would take RAM; Fields::keys() reads them with the _P functions.
static const char rectangleKeys[] PROGMEM = "|id|type|shape|x|y|width|height|fill|stroke|strokeWidth|cornerRadius|scroll|bind|";
static const char circleKeys[] PROGMEM = "|id|type|shape|x|y|radius|fill|stroke|strokeWidth|scroll|bind|";
static const char lineKeys[] PROGMEM = "|id|type|shape|x|y|x2|y2|stroke|strokeWidth|scroll|bind|";
static const char arcKeys[] PROGMEM = "|id|type|shape|x|y|radius|start|sweep|stroke|strokeWidth|bind|";
static const char handKeys[] PROGMEM = "|id|type|shape|x|y|length|tail|angle|stroke|strokeWidth|bind|";
static const char ticksKeys[] PROGMEM = "|id|type|shape|x|y|radius|count|length|stroke|strokeWidth|bind|";
// What one translation holds while the theme runs: its entry and, once the
// text outgrows the string's inline buffer, the heap block behind it. This is
// what MaxTranslationBytes bounds -- the text alone says nothing of the entry,
// which dwarfs a two-letter text -- and what reserveTranslations() sets aside.
static size_t translationCost(size_t length) {
  return sizeof(LocalizedText) + (length > std::string().capacity() ? length + 1 : 0);
}
// A text layer's `value`: a string, or under spec 2 an object of strings by
// language code. `en` is required, so there is always a text to fall back to;
// every code must be a known one, at most once, and every string passes the
// same checks as a plain value.
__attribute__((noinline)) static bool parseTextValue(JsonObjectConst o, Theme& t, const Fields& f, Layer& l) {
  JsonObjectConst languages = o["value"].as<JsonObjectConst>();
  if (languages.isNull()) {
    if (!f.text("value", l.value, 128)) return false;
    return validText(l.value, t) || f.fail("value", "use printable ASCII and declared clock/data variables only");
  }
  if (t.spec < 2) return f.fail("value", "a text per language needs spec 2");
  bool english = false;
  for (JsonPairConst p : languages) {
    int index = languageIndex(p.key().c_str(), p.key().size());
    if (index < 0) return f.fail("value", "unknown language " + printableKey(p.key().c_str(), p.key().size()) + ", expected en, pt, es, fr, de or it");
    if (!p.value().is<const char*>()) return f.fail("value", "each language needs a string");
    JsonString s = p.value().as<JsonString>();
    std::string text(s.c_str(), s.size());
    if (text.empty() || text.size() > 128) return f.fail("value", "each language expects 1 to 128 bytes");
    if (!validText(text, t)) return f.fail("value", "use printable ASCII and declared clock/data variables only");
    if (index == 0) {
      english = true;
      l.value = std::move(text);
    } else {
      size_t cost = translationCost(text.size());
      if (t.translationBytes + cost > MaxTranslationBytes)
        return f.fail("value", "the translations of a theme share " + std::to_string(MaxTranslationBytes) + " bytes of RAM, "
                      + std::to_string(sizeof(LocalizedText)) + " for each plus its text past " + std::to_string(std::string().capacity()) + " characters");
      t.translationBytes = static_cast<uint16_t>(t.translationBytes + cost);
      if (!l.translationCount) l.firstTranslation = static_cast<uint16_t>(t.translations.size());
      ++l.translationCount;
      LocalizedText& item = t.translations.emplace_back();
      item.language = static_cast<uint8_t>(index);
      item.text = std::move(text);
    }
  }
  return english || f.fail("value", "the en text is required");
}
// Parses one element of the root `layers` array and appends it to `t`, whose
// data sources it is checked against; `ids` holds the earlier layers' IDs.
// The layer is built in place in the list rather than in a local copy: a
// Layer is over 100 bytes, too many for the ESP8266's loop stack this deep in
// a parse. A failure leaves it half-filled, which is harmless because
// parseTheme() then discards `t`.
__attribute__((noinline)) static bool parseLayer(JsonObjectConst o, Theme& t, BlockList<std::string>& ids, std::string& error) {
  std::string type, id;
  std::string layerPath = "layers[" + std::to_string(t.layers.size()) + "]";
  Layer& l = t.layers.emplace_back();
  Fields f(o, layerPath, error);
  if (o.isNull()) return f.fail("", "expected an object");
  if (!f.text("id", id, 48)) return false;
  if (!validId(id)) return f.fail("id", "use ASCII letters, digits, '-' or '_'");
  for (const auto& old : ids) if (old == id) return f.fail("id", "duplicate layer ID");
  ids.push_back(std::move(id));
  if (!f.text("type", type, 16) || !f.number("x", -240, 479, l.x) || !f.number("y", -240, 479, l.y)) return false;
  if (type == "text") {
    l.type = LayerType::Text;
    if (!f.keys("|id|type|x|y|anchor|value|size|font|color|scroll|bind|") || !parseTextValue(o, t, f, l)) return false;
    if (f.has("font")) {
      // A package font (#31): fonts/<name>.stf, whose line height is the
      // layer's size, so `size` is not given.
      std::string name;
      if (t.spec < 2) return f.fail("font", fromFlash(fontNeedsSpec2));
      if (!f.text("font", name, 32)) return false;
      if (!validId(name)) return f.fail("font", "use ASCII letters, digits, '-' or '_'");
      if (f.has("size")) return f.fail("size", fromFlash(sizeWithFont));
      size_t i = 0;
      while (i < t.fonts.size() && t.fonts[i].name != name) ++i;
      if (i == MaxFonts) return f.fail("font", fromFlash(tooManyFonts));
      if (i == t.fonts.size()) {
        t.fonts.emplace_back();
        t.fonts.back().name = std::move(name);
      }
      l.font = static_cast<uint8_t>(i + 1);
    } else if (!f.number("size", 8, 96, l.size)) return false;
    if (!f.color("color", l.color)) return false;
    std::string anchor = "top-left";
    if (!f.text("anchor", anchor, 20, false)) return false;
    // The 9 anchor names are a 3x3 grid in row-major order (left/center/right
    // columns, top/center/bottom rows), so its index decomposes directly
    // into the 0/1/2 column and row bounds() later uses to offset the text
    // cell by half its width/height per axis.
    const char* anchors[] = {"top-left", "top-center", "top-right", "center-left", "center", "center-right", "bottom-left", "bottom-center", "bottom-right"};
    int a = 0;
    for (; a < 9; ++a) if (anchor == anchors[a]) break;
    if (a == 9) return f.fail("anchor", "unsupported text anchor");
    l.anchorX = a % 3;
    l.anchorY = a / 3;
  } else if (type == "image" || type == "animation") {
    l.type = type == "image" ? LayerType::Image : LayerType::Animation;
    if (!f.keys(type == "image" ? "|id|type|x|y|source|scroll|bind|" : "|id|type|x|y|width|height|source|frames|fps|loop|scroll|bind|") || !f.text("source", l.source, 110)) return false;
    if (!validPath(l.source)) return f.fail("source", "expected a safe relative asset path");
    if (type == "animation") {
      int frames = 0, fps = 0;
      if (!f.number("width", 1, 240, l.width) || !f.number("height", 1, 240, l.height) || !f.number("frames", 1, 240, frames) || !f.number("fps", 1, 15, fps)) return false;
      if (!o["loop"].is<bool>()) return f.fail("loop", "expected true or false");
      l.frames = frames;
      l.fps = fps;
      l.loop = o["loop"].as<bool>();
    }
    // assetPath() appends a per-frame suffix; the *compiled* path (what
    // ends up inside the .stheme container) must itself fit the package
    // format's 120-byte path limit, not just the declared `source`.
    if (assetPath(l, l.frames - 1).size() > 120) return f.fail("source", "compiled asset path exceeds 120 bytes");
  } else if (type == "shape") {
    l.type = LayerType::Shape;
    std::string shape;
    if (!f.text("shape", shape, 16)) return false;
    const bool rotation = shape == "arc" || shape == "hand" || shape == "ticks";
    if (rotation && t.spec < 2) return f.fail("shape", "arc, hand and ticks need \"spec\": 2");
    if (shape != "rectangle" && shape != "circle" && shape != "line" && !rotation)
      return f.fail("shape", t.spec < 2 ? "expected rectangle, circle or line" : "expected rectangle, circle, line, arc, hand or ticks");
    const char* allowed = shape == "rectangle" ? rectangleKeys : shape == "circle" ? circleKeys :
      shape == "arc" ? arcKeys : shape == "hand" ? handKeys : shape == "ticks" ? ticksKeys : lineKeys;
    if (!f.keys(allowed)) return false;
    l.hasFill = !o["fill"].isNull();
    l.hasStroke = !o["stroke"].isNull();
    if (!l.hasFill && !l.hasStroke) return f.fail(shape == "line" || rotation ? "stroke" : "fill", "provide a fill or stroke color");
    if ((l.hasFill && !f.color("fill", l.fill)) || (l.hasStroke && !f.color("stroke", l.stroke)) || !f.number("strokeWidth", 1, 32, l.strokeWidth, false)) return false;
    if (rotation) {
      // Every pixel of an arc, hand or tick is `stroke`; `fill` is not a key.
      if (shape == "arc") {
        l.shape = Shape::Arc;
        if (!f.number("radius", 1, 240, l.radius) || !f.number("start", -360, 360, l.x2) || !f.number("sweep", -360, 360, l.y2)) return false;
      } else if (shape == "hand") {
        l.shape = Shape::Hand;
        if (!f.number("length", 1, 240, l.radius) || !f.number("tail", 0, 240, l.cornerRadius, false) || !f.number("angle", -360, 360, l.x2)) return false;
      } else {
        l.shape = Shape::Ticks;
        if (!f.number("radius", 1, 240, l.radius) || !f.number("count", 1, 60, l.x2) || !f.number("length", 1, 240, l.y2)) return false;
      }
    } else if (shape == "rectangle") {
      if (!f.number("width", 1, 240, l.width) || !f.number("height", 1, 240, l.height)) return false;
      if (f.has("cornerRadius") && !f.number("cornerRadius", 0, 120, l.cornerRadius)) return false;
    } else if (shape == "circle") {
      l.shape = Shape::Circle;
      if (!f.number("radius", 1, 240, l.radius)) return false;
    } else {
      l.shape = Shape::Line;
      if (!l.hasStroke) return f.fail("stroke", "required for a line");
      if (!f.number("x2", -240, 479, l.x2) || !f.number("y2", -240, 479, l.y2)) return false;
    }
  } else return f.fail("type", "expected text, image, animation or shape");
  // `scroll` and `bind` are common to every layer type's own `keys()` list
  // above, so they are parsed once here rather than duplicated per branch.
  if (f.has("scroll")) {
    if (l.type != LayerType::Text) return f.fail("scroll", "only allowed on text layers");
    if (!parseScroll(o, layerPath, error, l)) return false;
  }
  if (f.has("bind") && !parseBindings(o, t, layerPath, error, l)) return false;
  return true;
}
// A fetched value as text: strings are cut to 31 bytes, numbers and booleans
// are written as JSON, everything else (null, list, object) is "--".
static std::string dataValue(JsonVariantConst value) {
  if (value.isNull()) return "--";
  if (value.is<const char*>()) {
    std::string s = value.as<const char*>();
    if (s.size() > 31) s.resize(31);
    return s;
  }
  if (!value.is<bool>() && !value.is<int>() && !value.is<long>() && !value.is<float>() && !value.is<double>()) return "--";
  char encoded[40] = {};
  size_t n = serializeJson(value, encoded, sizeof(encoded) - 1);
  encoded[n] = 0;
  if (n >= sizeof(encoded) - 1) return "--";
  return std::string(encoded);
}
// Follows a dotted path through objects; null when a part is missing or the
// value at that step is not an object.
static JsonVariantConst dataPath(JsonVariantConst root, const std::string& path) {
  size_t start = 0;
  while (start < path.size()) {
    size_t dot = path.find('.', start);
    std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (part.empty() || !root.is<JsonObjectConst>()) return JsonVariantConst();
    root = root[part.c_str()];
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return root;
}
namespace {
// Builds the filter for a source: a tree with `true` where a field's path ends.
// (A path with an empty part finds nothing in dataPath; here it only adds a key
// that nothing has.)
static void addFieldFilter(JsonVariant node, const std::string& path) {
  size_t start = 0;
  for (;;) {
    if (node.is<bool>() && node.as<bool>()) return;   // everything under here is kept already
    const size_t dot = path.find('.', start);
    const std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    JsonObject object = node.is<JsonObject>() ? node.as<JsonObject>() : node.to<JsonObject>();
    // Bound to the member itself (a JsonVariant assigned from another would
    // copy its value instead).
    JsonVariant member = object[part].isNull() ? object[part].to<JsonVariant>() : object[part].as<JsonVariant>();
    if (dot == std::string::npos) {
      member.set(true);
      return;
    }
    // Assigning a JsonVariant copies the value it points to; to walk on, the
    // handle itself is rebuilt in place (it holds two pointers and needs no
    // destructor), which keeps this loop at one frame whatever the path's depth.
    new (&node) JsonVariant(member);
    start = dot + 1;
  }
}
// A reply held in a string, in the reader form ArduinoJson takes.
class StringReply : public DataReply {
 public:
  explicit StringReply(const std::string& text) : text_(text) {}
  int read() override { return at_ < text_.size() ? static_cast<unsigned char>(text_[at_++]) : -1; }
  size_t readBytes(char* dest, size_t count) override {
    const size_t n = std::min(count, text_.size() - at_);
    memcpy(dest, text_.data() + at_, n);
    at_ += n;
    return n;
  }
 private:
  const std::string& text_;
  size_t at_ = 0;
};
// Passes the bytes on and remembers the first one that is not white space.
// With a filter, ArduinoJson skips what no field names and does not look at
// what it skips: a root that is no JSON at all (an error page) passes as an
// empty object. A reply must be an object or a list, so this says which it is.
class RootWatch {
 public:
  explicit RootWatch(DataReply& reply) : reply_(reply) {}
  int read() {
    const int c = reply_.read();
    if (c >= 0) see(static_cast<char>(c));
    return c;
  }
  // (ArduinoJson reads JSON one byte at a time through read(); readBytes() is
  // only for MessagePack, so a use of it would not compile here.)
  bool container() const { return first_ == '{' || first_ == '['; }
 private:
  void see(char c) {
    if (!first_ && c != ' ' && c != '\t' && c != '\r' && c != '\n') first_ = c;
  }
  DataReply& reply_;
  char first_ = 0;
};
// The two steps around the parse are out of line, siblings of it: their
// locals (path parts, the strings of a value) are gone while the parse runs
// and the reader waits on the network, which is the deep part of the loop
// stack (TRAPS T12, T13).
__attribute__((noinline)) bool buildFilter(JsonDocument& filter, const ThemeDataSource& source) {
  for (const auto& field : source.fields) addFieldFilter(filter.as<JsonVariant>(), field.path);
  return !filter.overflowed();
}
__attribute__((noinline)) void appendValues(const JsonDocument& doc, const ThemeDataSource& source, std::vector<ThemeValue>& values) {
  values.reserve(values.size() + source.fields.size());
  for (const auto& field : source.fields)
    values.push_back({source.id + "." + field.id, dataValue(dataPath(doc.as<JsonVariantConst>(), field.path))});
}
// The filter, the parse and the values, for any reply that ArduinoJson can read.
bool extractFiltered(DataReply& reply, const ThemeDataSource& source, std::vector<ThemeValue>& values, size_t budget) {
  BudgetAllocator allocator(budget);
  JsonDocument filter(&allocator);
  if (!buildFilter(filter, source)) return false;
  JsonDocument doc(&allocator);
  RootWatch input(reply);
  if (deserializeJson(doc, input, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8)) || !input.container()) return false;
  appendValues(doc, source, values);
  return true;
}
}
bool extractThemeValues(const std::string& body, const ThemeDataSource& source, std::vector<ThemeValue>& values, size_t budget) {
  StringReply reply(body);
  return extractFiltered(reply, source, values, budget);
}
bool extractThemeValues(DataReply& reply, const ThemeDataSource& source, std::vector<ThemeValue>& values, size_t budget) {
  return extractFiltered(reply, source, values, budget);
}
std::string expandText(const std::string& value, const tm* t) {
  static const std::vector<ThemeValue> empty;
  return expandText(value, t, empty);
}
// Month and weekday names, one fixed-width NUL-padded slot each, in flash
// (PROGMEM on the ESP8266): the full names and, separately, the abbreviations,
// so none is cut from the full name. ASCII only, because the built-in font is.
// Only the slot being expanded is copied out, into a small stack buffer.
constexpr size_t MonthSlot = 11, DaySlot = 14, ShortSlot = 5;
static const char monthNames[kLanguageCount][12][MonthSlot] PROGMEM = {
  {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"},
  {"Janeiro", "Fevereiro", "Marco", "Abril", "Maio", "Junho", "Julho", "Agosto", "Setembro", "Outubro", "Novembro", "Dezembro"},
  {"Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio", "Julio", "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"},
  {"Janvier", "Fevrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Aout", "Septembre", "Octobre", "Novembre", "Decembre"},
  {"Januar", "Februar", "Maerz", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"},
  {"Gennaio", "Febbraio", "Marzo", "Aprile", "Maggio", "Giugno", "Luglio", "Agosto", "Settembre", "Ottobre", "Novembre", "Dicembre"}};
static const char monthShort[kLanguageCount][12][ShortSlot] PROGMEM = {
  {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
  {"Jan", "Fev", "Mar", "Abr", "Mai", "Jun", "Jul", "Ago", "Set", "Out", "Nov", "Dez"},
  {"Ene", "Feb", "Mar", "Abr", "May", "Jun", "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"},
  {"Janv", "Fevr", "Mars", "Avr", "Mai", "Juin", "Juil", "Aout", "Sept", "Oct", "Nov", "Dec"},
  {"Jan", "Feb", "Mar", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"},
  {"Gen", "Feb", "Mar", "Apr", "Mag", "Giu", "Lug", "Ago", "Set", "Ott", "Nov", "Dic"}};
static const char dayNames[kLanguageCount][7][DaySlot] PROGMEM = {
  {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"},
  {"Domingo", "Segunda-feira", "Terca-feira", "Quarta-feira", "Quinta-feira", "Sexta-feira", "Sabado"},
  {"Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado"},
  {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"},
  {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"},
  {"Domenica", "Lunedi", "Martedi", "Mercoledi", "Giovedi", "Venerdi", "Sabato"}};
static const char dayShort[kLanguageCount][7][ShortSlot] PROGMEM = {
  {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},
  {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sab"},
  {"Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"},
  {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"},
  {"Son", "Mon", "Die", "Mit", "Don", "Fre", "Sam"},
  {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"}};
// The name held in a flash slot of `width` bytes.
static std::string localName(const char* slot, size_t width) {
  char b[DaySlot];
  memcpy_P(b, slot, width);
  b[width - 1] = 0;
  return std::string(b);
}
// The text of a layer in `language`: its translation, else the English value.
// A reference into the theme, so a re-expansion copies nothing.
const std::string& layerText(const Theme& theme, const Layer& l, uint8_t language) {
  if (language)
    for (size_t i = 0; i < l.translationCount; ++i) {
      const LocalizedText& item = theme.translations[l.firstTranslation + i];
      if (item.language == language) return item.text;
    }
  return l.value;
}
// Substitutes every {token} in a validated text template: clock/date tokens
// first (only when `t` is non-null and the field is in range), then a
// matching fetched value, and "--" if neither resolves -- the same fallback
// an unsynchronized clock or a field that has never been fetched produces.
std::string expandText(const std::string& value, const tm* t, const std::vector<ThemeValue>& values) {
  static const std::vector<ThemeValue> noDevice;
  return expandText(value, t, values, noDevice, 0);
}
std::string expandText(const std::string& value, const tm* t, const std::vector<ThemeValue>& values, const std::vector<ThemeValue>& device) {
  return expandText(value, t, values, device, 0);
}
std::string expandText(const std::string& value, const tm* t, const std::vector<ThemeValue>& values, uint8_t language) {
  static const std::vector<ThemeValue> noDevice;
  return expandText(value, t, values, noDevice, language);
}
// `device` holds the firmware's values keyed "device.<name>"; a name it does
// not provide stays "--". Validation only lets a spec 2 theme use them.
// `language` is the index of Languages.h whose month and weekday names {MON},
// {MONTH}, {WD} and {WEEKDAY} use.
std::string expandText(const std::string& value, const tm* t, const std::vector<ThemeValue>& values, const std::vector<ThemeValue>& device, uint8_t language) {
  if (language >= kLanguageCount) language = 0;
  std::string out;
  for (size_t i = 0; i < value.size();) {
    if (value[i] != '{') {
      out += value[i++];
      continue;
    }
    size_t end = value.find('}', i);
    if (end == std::string::npos) {
      out += value.substr(i);
      break;
    }
    std::string key = value.substr(i + 1, end - i - 1), v = "--";
    if (t) {
      char b[16];
      int n = -1;
      if (key == "HH") n = t->tm_hour;
      else if (key == "hh") n = t->tm_hour % 12 ? t->tm_hour % 12 : 12;
      else if (key == "MM") n = t->tm_min;
      else if (key == "SS") n = t->tm_sec;
      else if (key == "DD") n = t->tm_mday;
      else if (key == "YYYY") n = t->tm_year + 1900;
      else if (key == "MO" && t->tm_mon >= 0 && t->tm_mon < 12) n = t->tm_mon + 1;
      if (n >= 0) {
        snprintf(b, sizeof(b), key == "YYYY" ? "%04d" : "%02d", n);
        v = b;
      } else if (key == "H" && t->tm_hour >= 0 && t->tm_hour < 24) {
        snprintf(b, sizeof(b), "%d", t->tm_hour);
        v = b;
      } else if (key == "AMPM" && t->tm_hour >= 0 && t->tm_hour < 24) {
        v = t->tm_hour < 12 ? "AM" : "PM";
      } else if ((key == "MON" || key == "MONTH") && t->tm_mon >= 0 && t->tm_mon < 12) {
        v = key == "MON" ? localName(monthShort[language][t->tm_mon], ShortSlot) : localName(monthNames[language][t->tm_mon], MonthSlot);
      } else if ((key == "WD" || key == "WEEKDAY") && t->tm_wday >= 0 && t->tm_wday < 7) {
        v = key == "WD" ? localName(dayShort[language][t->tm_wday], ShortSlot) : localName(dayNames[language][t->tm_wday], DaySlot);
      }
    }
    if (v == "--") for (const auto& item : values) if (item.key == key) {
      v = item.value;
      break;
    }
    if (v == "--") for (const auto& item : device) if (item.key == key) {
      v = item.value;
      break;
    }
    out += v;
    i = end + 1;
  }
  return out;
}
// Maps a layer's logical `source` to the compiled .sti path inside the
// package: an image is exactly "<source>.sti", an animation frame is
// "<source>/<3-digit frame>.png.sti" -- the same convention theme_pack.py
// uses when building the container, so the two never drift apart.
std::string assetPath(const Layer& l, uint16_t frame) {
  if (l.type == LayerType::Image) return l.source + ".sti";
  char b[24];
  snprintf(b, sizeof(b), "/%03u.png.sti", frame);
  return l.source + b;
}
// Recomputes one layer's ResolvedLayer from scratch: start from the static
// manifest values, then let each binding whose source currently has a valid
// finite value override its one property. A binding whose source is missing
// or non-finite is simply skipped (`continue`), which is exactly what leaves
// the static fallback in effect -- there is no separate "use default" branch.
void Engine::resolveLayer(size_t index) {
  const Layer& layer = theme_.layers[index];
  ResolvedLayer resolved;
  resolved.x = layer.x;
  resolved.y = layer.y;
  resolved.width = layer.width;
  resolved.height = layer.height;
  resolved.x2 = layer.x2;
  resolved.y2 = layer.y2;
  resolved.radius = layer.radius;
  resolved.cornerRadius = layer.cornerRadius;
  resolved.size = layer.size;
  resolved.strokeWidth = layer.strokeWidth;
  resolved.scrollWidth = layer.scroll.width;
  resolved.scrollSpeed = layer.scroll.speed;
  resolved.color = layer.color;
  resolved.fill = layer.fill;
  resolved.stroke = layer.stroke;
  for (const auto& binding : layer.bindings) {
    double value = 0;
    if (binding.builtin == BuiltIn::None || binding.builtin == BuiltIn::Device) {
      const auto& pool = binding.builtin == BuiltIn::Device ? device_ : values_;
      const std::string* source = nullptr;
      for (const auto& item : pool) if (item.key == binding.source) {
        source = &item.value;
        break;
      }
      if (!source || !parseFiniteNumber(*source, value)) continue;
    } else {
      // time.*: until the clock is set there is no value, so the static one stays.
      if (!hadTime_) continue;
      value = timeValue(binding.builtin, lastTime_);
    }
    if (binding.color) {
      const uint16_t color = resolveColorBinding(binding.colors, value);
      if (binding.property == BoundProperty::Color) resolved.color = color;
      else if (binding.property == BoundProperty::Fill) resolved.fill = color;
      else if (binding.property == BoundProperty::Stroke) resolved.stroke = color;
      continue;
    }
    int lo = 0, hi = 0;
    if (!propertyRange(binding.property, lo, hi)) continue;
    const int number = resolveNumericBinding(binding.numeric, value, lo, hi);
    switch (binding.property) {
      case BoundProperty::X: resolved.x = number; break;
      case BoundProperty::Y: resolved.y = number; break;
      case BoundProperty::Width: resolved.width = number; break;
      case BoundProperty::Height: resolved.height = number; break;
      case BoundProperty::Radius: resolved.radius = number; break;
      case BoundProperty::CornerRadius: resolved.cornerRadius = number; break;
      case BoundProperty::X2: resolved.x2 = number; break;
      case BoundProperty::Y2: resolved.y2 = number; break;
      case BoundProperty::Size: resolved.size = number; break;
      case BoundProperty::StrokeWidth: resolved.strokeWidth = number; break;
      case BoundProperty::ScrollWidth: resolved.scrollWidth = number; break;
      case BoundProperty::ScrollSpeed: resolved.scrollSpeed = number; break;
      case BoundProperty::Angle: resolved.x2 = number; break;
      case BoundProperty::Sweep: resolved.y2 = number; break;
      default: break;
    }
  }
  // The mandatory cornerRadius clamp: applied after bindings resolve width,
  // height, and cornerRadius, so a rectangle that shrinks (via a bound
  // dimension) never draws rounding outside its own resolved bounds.
  if (layer.type == LayerType::Shape && layer.shape == Shape::Rectangle)
    resolved.cornerRadius = std::min<int>(resolved.cornerRadius,
      std::max(0, std::min(resolved.width, resolved.height) / 2));
  states_[index].resolved = resolved;
}
// True once `now` is strictly past `deadline`; wrap-safe across millis() rollover.
static bool scrollDeadlinePassed(uint32_t now, uint32_t deadline) {
  return int32_t(now - deadline) > 0;
}
// Restarts a text layer's scroll at its initial position and pause, per the
// spec's "changing the expanded text, or a binding that affects size/
// scroll geometry, resets the scroll" rule (Engine::update decides when to
// call this; this function only applies the reset itself).
static void resetScroll(LayerState& s, const Layer& l, uint32_t now) {
  s.scrollOffset = 0;
  s.scrollPhase = 0;
  s.scrollDirection = -1;
  s.scrollLastMs = now;
  s.scrollPauseUntil = now + l.scroll.pauseMs;
}
// Advances loop/bounce scroll state to `now`, resolving however many pause/leg
// boundaries fall within the elapsed interval without iterating per pixel.
//
// One "leg" is a single pass across `target` pixels: the full loop cycle
// (text width + gap) in loop mode, or one direction of travel (text width -
// viewport width) in bounce mode. Position within a leg is tracked in
// "ticks", milli-pixels (1000 ticks = 1 px), so that elapsed_ms * speed_px_per_s
// accumulates exact whole-pixel steps with the leftover fractional part kept
// in `scrollPhase` -- the same accumulator technique frame animation already
// uses in Engine::update. Carrying that leftover across leg boundaries (rather
// than resetting it to 0 each leg) is what keeps a non-integer travel
// duration (e.g. speed=7 px/s) from drifting: see the "fractional" scroll
// timing test in test_engine.cpp for a case that would otherwise mis-round.
//
// The loop below resolves one leg boundary per iteration instead of one pixel
// per iteration, so a single call can jump forward by many laps (e.g. after a
// long gap between update() calls, or a millis() wraparound) in O(laps), not
// O(pixels).
static void advanceScroll(const Layer& l, LayerState& s, uint32_t now, int textWidth) {
  const int viewportWidth = s.resolved.scrollWidth;
  const int speed = s.resolved.scrollSpeed;
  const bool loop = l.scroll.mode == ScrollMode::Loop;
  const int target = loop ? (textWidth + l.scroll.gap) : (textWidth - viewportWidth);
  if (textWidth <= viewportWidth || target <= 0 || speed <= 0) return;
  for (;;) {
    if (!scrollDeadlinePassed(now, s.scrollPauseUntil)) return;
    // While still within the pause that follows a completed leg, `scrollLastMs`
    // has not advanced past `scrollPauseUntil` yet; once the pause has ended,
    // elapsed time is measured from the later of the two.
    const uint32_t base = scrollDeadlinePassed(s.scrollLastMs, s.scrollPauseUntil) ? s.scrollLastMs : s.scrollPauseUntil;
    const uint32_t elapsed = now - base;
    const uint64_t ticksAvailable = uint64_t(elapsed) * speed + s.scrollPhase;
    // In bounce mode, `scrollOffset` itself measures distance from the near
    // edge on the way out (direction -1) but distance *remaining* to the near
    // edge on the way back (direction +1), so progress into the current leg
    // is `target - scrollOffset` on the return trip.
    const int legProgress = loop ? s.scrollOffset : (s.scrollDirection < 0 ? s.scrollOffset : target - s.scrollOffset);
    const uint64_t neededTicks = uint64_t(target - legProgress) * 1000;
    if (ticksAvailable < neededTicks) {
      // This call's elapsed time does not finish the current leg: apply a
      // partial step and stop.
      const uint64_t newProgressTicks = uint64_t(legProgress) * 1000 + ticksAvailable;
      const int newProgressPixels = int(newProgressTicks / 1000);
      s.scrollPhase = uint32_t(newProgressTicks % 1000);
      s.scrollOffset = loop ? newProgressPixels : (s.scrollDirection < 0 ? newProgressPixels : target - newProgressPixels);
      s.scrollLastMs = now;
      return;
    }
    // The leg completes before `now`; find the exact millisecond it did, so the
    // leftover sub-pixel remainder and the next pause carry no rounding drift.
    const uint64_t remainingTicks = neededTicks - s.scrollPhase;
    const uint64_t msNeeded = (remainingTicks + uint64_t(speed) - 1) / speed;
    const uint32_t completion = base + uint32_t(msNeeded);
    const uint64_t leftover = msNeeded * speed + s.scrollPhase - neededTicks;
    s.scrollPhase = uint32_t(leftover);
    s.scrollLastMs = completion;
    s.scrollPauseUntil = completion + l.scroll.pauseMs;
    // Loop always wraps back to 0 (the same conveyor repeats); bounce clamps
    // at whichever edge it just reached and reverses for the next leg. The
    // loop then re-checks the new scrollPauseUntil against `now`, so a jump
    // spanning several legs keeps resolving one leg at a time above.
    if (loop) {
      s.scrollOffset = 0;
    } else if (s.scrollDirection < 0) {
      s.scrollOffset = target;
      s.scrollDirection = 1;
    } else {
      s.scrollOffset = 0;
      s.scrollDirection = -1;
    }
  }
}
// Used by Engine::update to decide whether a binding change actually needs a
// repaint. Deliberately excludes scrollSpeed: a speed-only change alone does
// not move any pixel by itself (see the scroll reset trigger below, which
// checks it separately for the unrelated "restart scrolling" decision).
static bool sameGeometry(const ResolvedLayer& a, const ResolvedLayer& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height &&
    a.x2 == b.x2 && a.y2 == b.y2 && a.radius == b.radius &&
    a.cornerRadius == b.cornerRadius && a.size == b.size &&
    a.strokeWidth == b.strokeWidth && a.scrollWidth == b.scrollWidth;
}
static bool sameColors(const ResolvedLayer& a, const ResolvedLayer& b) {
  return a.color == b.color && a.fill == b.fill && a.stroke == b.stroke;
}
// The built-in bitmap font's cell width scales with `size`: each glyph is
// drawn in a 5-of-6-pixel-wide cell (the 6th pixel is inter-character
// spacing), sized in eighths of `size` so the whole font scales uniformly.
// A package font's text is as wide as its glyphs' advances.
static int textPixelWidth(const Theme& theme, const Layer& l, int size, const std::string& text) {
  if (!l.font) return static_cast<int>(text.size()) * ((size * 6 + 7) / 8);
  int width = 0;
  for (unsigned char c : text) width += theme.fonts[l.font - 1].advance(c);
  return width;
}
// ---- Rotation shapes (spec 2, #31) ----
// Angles are kept in sixteenths of a degree, 0 at 12 o'clock, growing
// clockwise. There is no sine table and no floating point: sin comes from
// Bhaskara I's rational approximation (error under 0.002) and a pixel's angle
// from a quadratic arctangent (error under 0.2 degrees), which keeps both
// within about a pixel at the edge of the canvas.
constexpr int Turn16 = 5760;
static int wrap16(int a) {
  a %= Turn16;
  return a < 0 ? a + Turn16 : a;
}
// sin(a / 16 degrees) in Q14. 10368000 is 40500 * 256, the approximation's
// denominator for angles in sixteenths.
static int sinQ14(int a) {
  a = wrap16(a);
  const bool negative = a >= Turn16 / 2;
  if (negative) a -= Turn16 / 2;
  const int64_t k = int64_t(a) * (Turn16 / 2 - a), den = 10368000 - k;
  const int v = int((4 * k * 16384 + den / 2) / den);
  return negative ? -v : v;
}
// Unit vector of a clockwise-from-12-o'clock angle in screen coordinates.
static void direction(int a, int& dx, int& dy) {
  dx = sinQ14(a);
  dy = -sinQ14(a + Turn16 / 4);
}
static int roundQ14(int v) {
  return v >= 0 ? (v + 8192) >> 14 : -((8192 - v) >> 14);
}
// atan(a / b) in sixteenths of a degree for 0 <= a <= b, b > 0.
static int arctan16(int a, int b) {
  const int r = a * 1024 / b;
  return (r * (737280 + 250 * (1024 - r))) >> 20;
}
// The angle of the pixel (dx, dy) from a center, same convention as above.
static int pixelAngle16(int dx, int dy) {
  const int u = abs(dx), v = abs(dy);
  if (!u && !v) return 0;
  const int base = u <= v ? arctan16(u, v) : Turn16 / 4 - arctan16(v, u);
  if (dx >= 0) return dy <= 0 ? base : Turn16 / 2 - base;
  return dy > 0 ? Turn16 / 2 + base : Turn16 - base;
}
struct Segment {
  int x0, y0, x1, y1;
};
// A hand runs from `tail` behind the pivot to `length` in front of it. The
// renderer and bounds() both come here, so the box always holds the ink.
static Segment handSegment(const Layer& l, const ResolvedLayer& r) {
  int dx, dy;
  direction(r.x2 * 16, dx, dy);
  return Segment{r.x - roundQ14(dx * l.cornerRadius), r.y - roundQ14(dy * l.cornerRadius),
    r.x + roundQ14(dx * l.radius), r.y + roundQ14(dy * l.radius)};
}
// Tick `k` of `count`: a radial segment from radius - length to radius.
static Segment tickSegment(const Layer& l, const ResolvedLayer& r, int k) {
  int dx, dy;
  direction((k * Turn16 + l.x2 / 2) / l.x2, dx, dy);
  const int inner = std::max(0, r.radius - l.y2);
  return Segment{r.x + roundQ14(dx * inner), r.y + roundQ14(dy * inner),
    r.x + roundQ14(dx * r.radius), r.y + roundQ14(dy * r.radius)};
}
static bool rotationShape(Shape shape) {
  return shape == Shape::Arc || shape == Shape::Hand || shape == Shape::Ticks;
}
// An arc's start in [0, Turn16) and its sweep, always clockwise from there
// (a negative sweep is the same arc started at its far end).
static void arcRange(const ResolvedLayer& r, int& start, int& sweep) {
  start = r.x2 * 16;
  sweep = r.y2 * 16;
  if (sweep < 0) {
    start += sweep;
    sweep = -sweep;
  }
  start = wrap16(start);
}
// An arc's two edges as unit vectors, so a pixel is tested against the wedge
// with two cross products and no division. Up to half a turn the pixel must be
// clockwise of the start edge and counter-clockwise of the end edge; past half
// a turn it must not be in the gap between the end edge and the start edge.
struct ArcWedge {
  int sx, sy, ex, ey;
  bool full, reflex;
};
static ArcWedge arcWedge(const ResolvedLayer& r) {
  int start, sweep;
  arcRange(r, start, sweep);
  ArcWedge w{};
  direction(start, w.sx, w.sy);
  direction(start + sweep, w.ex, w.ey);
  w.full = sweep >= Turn16;
  w.reflex = sweep > Turn16 / 2;
  return w;
}
static bool inWedge(const ArcWedge& w, int dx, int dy) {
  if (w.full) return true;
  const int fromStart = w.sx * dy - w.sy * dx, toEnd = dx * w.ey - dy * w.ex;
  if (!w.reflex) return fromStart >= 0 && toEnd >= 0;
  return !(toEnd < 0 && fromStart < 0);
}
// The box of an arc: its ends (inner and outer), the compass points inside
// its sweep and, in the pad, the approximations' error. Only this box turns
// dirty when the sweep changes.
static Rect arcBounds(const ResolvedLayer& r) {
  if (r.radius <= 0 || r.y2 == 0) return Rect(r.x, r.y, 0, 0);
  int start, sweep;
  arcRange(r, start, sweep);
  int x0 = r.x - r.radius, x1 = r.x + r.radius, y0 = r.y - r.radius, y1 = r.y + r.radius;
  if (sweep < Turn16) {
    x0 = x1 = r.x;
    y0 = y1 = r.y;
    const int inner = std::max(0, r.radius - r.strokeWidth);
    auto grow = [&](int radius, int angle) {
      int dx, dy;
      direction(angle, dx, dy);
      const int x = r.x + roundQ14(dx * radius), y = r.y + roundQ14(dy * radius);
      x0 = std::min(x0, x);
      x1 = std::max(x1, x);
      y0 = std::min(y0, y);
      y1 = std::max(y1, y);
    };
    for (int angle : {start, start + sweep}) {
      grow(r.radius, angle);
      grow(inner, angle);
    }
    for (int compass = 0; compass < 4; ++compass)
      if (wrap16(compass * (Turn16 / 4) - start) <= sweep) grow(r.radius, compass * (Turn16 / 4));
  }
  return Rect(x0 - 2, y0 - 2, x1 - x0 + 5, y1 - y0 + 5);
}
// The rectangle render() (and Engine::update's dirty-region union) treats as
// this layer's footprint. For scrolling text this is the fixed viewport, not
// the full (possibly much wider) expanded text -- render() maps viewport-
// local pixels back to a position in the text itself via scrollContentColumn.
static Rect bounds(const Theme& theme, const Layer& l, const ResolvedLayer& resolved, const std::string& text) {
  if (l.type == LayerType::Text) {
    int w = l.scroll.enabled ? resolved.scrollWidth : textPixelWidth(theme, l, resolved.size, text);
    return Rect(resolved.x - w * l.anchorX / 2, resolved.y - resolved.size * l.anchorY / 2, w, resolved.size);
  }
  if (l.type == LayerType::Shape && l.shape == Shape::Circle && resolved.radius == 0)
    return Rect(resolved.x, resolved.y, 0, 0);
  if (l.type == LayerType::Shape && l.shape == Shape::Circle)
    return Rect(resolved.x - resolved.radius, resolved.y - resolved.radius, 2 * resolved.radius + 1, 2 * resolved.radius + 1);
  if (l.type == LayerType::Shape && l.shape == Shape::Line) {
    int pad = (resolved.strokeWidth + 1) / 2;
    return Rect(std::min(resolved.x, resolved.x2) - pad, std::min(resolved.y, resolved.y2) - pad,
      abs(resolved.x - resolved.x2) + 2 * pad + 1, abs(resolved.y - resolved.y2) + 2 * pad + 1);
  }
  if (l.type == LayerType::Shape && l.shape == Shape::Arc) return arcBounds(resolved);
  if (l.type == LayerType::Shape && l.shape == Shape::Hand) {
    const Segment h = handSegment(l, resolved);
    const int pad = (resolved.strokeWidth + 1) / 2;
    return Rect(std::min(h.x0, h.x1) - pad, std::min(h.y0, h.y1) - pad,
      abs(h.x0 - h.x1) + 2 * pad + 1, abs(h.y0 - h.y1) + 2 * pad + 1);
  }
  if (l.type == LayerType::Shape && l.shape == Shape::Ticks) {
    if (resolved.radius <= 0) return Rect(resolved.x, resolved.y, 0, 0);
    const int reach = resolved.radius + (resolved.strokeWidth + 1) / 2 + 1;
    return Rect(resolved.x - reach, resolved.y - reach, 2 * reach + 1, 2 * reach + 1);
  }
  return Rect(resolved.x, resolved.y, resolved.width, resolved.height);
}
// Installs a new theme and resets all runtime state (scroll, animation,
// resolved geometry) from scratch. The next update() reports a full-canvas
// dirty rectangle, since every layer's previous on-screen footprint is gone.
void Engine::setTheme(Theme theme, uint32_t now) {
  theme_ = std::move(theme);
  states_.assign(theme_.layers.size(), LayerState{});
  for (size_t i = 0; i < states_.size(); ++i) {
    states_[i].lastMs = now;
    resolveLayer(i);
  }
  full_ = true;
}
// The device language for {MON}, {WD} and the text of localized layers; the
// next update() re-expands every text layer when it changed.
void Engine::setLanguage(uint8_t language) {
  if (language >= kLanguageCount) language = 0;
  if (language == language_) return;
  language_ = language;
  languageChanged_ = true;
}
// Records the latest fetched values for the next update() to resolve
// bindings against. Reinstalling an identical set is a deliberate no-op
// (including for scroll's "reinstalling the same values must not restart the
// pause" rule) rather than a wasted resolve pass every call.
void Engine::setValues(std::vector<ThemeValue> values) {
  if (values_ == values) return;
  values_ = std::move(values);
  valuesChanged_ = true;
}
// Same as setValues(), for the firmware's `device.*` values.
void Engine::setDeviceValues(std::vector<ThemeValue> values) {
  if (device_ == values) return;
  device_ = std::move(values);
  valuesChanged_ = true;
}
// Merges a newly dirty region into the accumulated list, absorbing any
// existing rectangle it overlaps so update() never reports two overlapping
// regions (which would otherwise make render() redraw the overlap twice).
static void addDirty(std::vector<Rect>& dirty, Rect r) {
  r = intersect(r, Rect(0, 0, 240, 240));
  if (r.empty()) return;
  for (size_t i = 0; i < dirty.size();) {
    if (!intersect(r, dirty[i]).empty()) {
      r = unite(r, dirty[i]);
      dirty.erase(dirty.begin() + i);
      i = 0;
    } else ++i;
  }
  dirty.push_back(r);
}
// Advances every layer to `now`/`time` and returns the regions that actually
// need repainting. This is the engine's one time-stepping entry point:
// resolving bindings, expanding text, advancing animation frames, and
// scheduling scroll all happen here, each only when its own trigger fired, so
// a static theme with unchanged data produces zero display writes.
std::vector<Rect> Engine::update(uint32_t now, const tm* time) {
  std::vector<Rect> dirty;
  // A full second-granularity clock comparison, not just tm_sec, because a
  // caller can hand in noon-to-midnight or DST-boundary jumps between calls.
  const bool timeChanged = full_ || bool(time) != hadTime_ || (time &&
    (time->tm_sec != lastTime_.tm_sec || time->tm_min != lastTime_.tm_min ||
     time->tm_hour != lastTime_.tm_hour || time->tm_mday != lastTime_.tm_mday ||
     time->tm_mon != lastTime_.tm_mon || time->tm_year != lastTime_.tm_year || time->tm_wday != lastTime_.tm_wday));
  hadTime_ = bool(time);
  if (time) lastTime_ = *time;
  for (size_t i = 0; i < states_.size(); ++i) {
    auto& s = states_[i];
    const auto& l = theme_.layers[i];
    bool changed = full_;
    const ResolvedLayer previous = s.resolved;
    // Re-resolve bindings only when fetched values actually changed; a
    // geometry or color change (but not a scrollSpeed-only change, see
    // sameGeometry's own comment) marks this layer dirty.
    // A layer bound to time.* also re-resolves when the clock moved; the
    // result only marks it dirty if a property really changed (a minute
    // binding does nothing on 59 of 60 seconds).
    if (valuesChanged_ || (timeChanged && l.timeBound)) {
      resolveLayer(i);
      changed = changed || !sameGeometry(previous, s.resolved) || !sameColors(previous, s.resolved);
    }
    bool textChanged = false;
    if (l.type == LayerType::Text && (timeChanged || valuesChanged_ || languageChanged_)) {
      // Only spec 2 reads device.* values and follows the device language: a
      // spec 1 `device` data source with no fetched value shows "--", never
      // what the firmware supplies, and spec 1 names stay English.
      std::string text = theme_.spec >= 2 ? expandText(layerText(theme_, l, language_), time, values_, device_, language_) : expandText(l.value, time, values_);
      if (text != s.text) {
        s.text = std::move(text);
        changed = true;
        textChanged = true;
      }
    } else if (l.type == LayerType::Animation && !s.finished) {
      // Same milli-frame accumulator technique as scroll's ticks: elapsed_ms *
      // fps accumulates exact whole frames, with the remainder kept in
      // `phase` so a late update() jumps straight to the due frame instead of
      // replaying skipped ones.
      uint64_t ticks = uint64_t(uint32_t(now - s.lastMs)) * l.fps + s.phase;
      uint64_t step = ticks / 1000;
      s.phase = ticks % 1000;
      s.lastMs = now;
      uint16_t next = l.loop ? (s.frame + step) % l.frames : std::min<uint64_t>(s.frame + step, l.frames - 1);
      if (!l.loop && next == l.frames - 1) s.finished = true;
      if (next != s.frame) {
        s.frame = next;
        changed = true;
      }
    }
    if (l.type == LayerType::Text && l.scroll.enabled) {
      // Reset triggers on changed text or a binding affecting size/scroll
      // geometry (per docs/features/themes.md); anything else -- an unrelated
      // data value, a position/color binding, invalidate() -- must not
      // disturb an in-progress scroll.
      const bool scrollResetNeeded = textChanged || previous.size != s.resolved.size ||
        previous.scrollWidth != s.resolved.scrollWidth || previous.scrollSpeed != s.resolved.scrollSpeed;
      if (scrollResetNeeded) {
        resetScroll(s, l, now);
        changed = true;
      } else {
        const int textWidth = textPixelWidth(theme_, l, s.resolved.size, s.text);
        const int previousOffset = s.scrollOffset;
        advanceScroll(l, s, now, textWidth);
        if (s.scrollOffset != previousOffset) changed = true;
      }
    }
    // The dirty region is the union of this layer's old and new footprint, so
    // moving/shrinking/hiding a layer also erases wherever it used to be.
    Rect next = bounds(theme_, l, s.resolved, s.text);
    // A rotation shape sweeps a thin diagonal: its old and new boxes are
    // repainted apart, since their union would also cover everything between.
    if (changed && !full_) {
      if (l.type == LayerType::Shape && rotationShape(l.shape)) {
        addDirty(dirty, s.bounds);
        addDirty(dirty, next);
      } else addDirty(dirty, unite(s.bounds, next));
    }
    s.bounds = next;
  }
  if (full_) {
    dirty.assign(1, Rect(0, 0, 240, 240));
    full_ = false;
  }
  valuesChanged_ = false;
  languageChanged_ = false;
  return dirty;
}
// Alpha-composites an RGB565 foreground pixel over a background one, blending
// each of the 5/6/5 channels independently with rounding (+127 before the
// /255 integer division). Used for image/animation alpha and a package font's
// coverage -- shapes and built-in font text are fully opaque where drawn.
static uint16_t blend(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a == 255) return fg;
  if (a == 0) return bg;
  unsigned b = 255 - a;
  return ((((fg >> 11) * a + (bg >> 11) * b + 127) / 255) << 11) |
    (((((fg >> 5) & 63) * a + ((bg >> 5) & 63) * b + 127) / 255) << 5) |
    (((fg & 31) * a + (bg & 31) * b + 127) / 255);
}
// True if (x, y) falls within strokeWidth/2 of the segment (lx,ly)-(lx2,ly2),
// giving it round ends (a "capsule" shape): project the pixel onto the
// segment via the dot product, and if that projection falls before the start
// or past the end, test distance to the nearest endpoint instead of to the
// infinite line. All distances are compared squared (and doubled, `4 *`, to
// compare against strokeWidth rather than strokeWidth/2) to avoid a sqrt, and
// widened to int64_t since squaring a 240-range coordinate can exceed 32 bits.
static bool linePixel(int lx, int ly, int lx2, int ly2, int strokeWidth, int x, int y) {
  int64_t dx = lx2 - lx, dy = ly2 - ly, px = x - lx, py = y - ly;
  int64_t len = dx * dx + dy * dy, dot = px * dx + py * dy;
  int64_t sw = strokeWidth;
  if (dot <= 0 || len == 0) return 4 * (px * px + py * py) <= sw * sw;
  if (dot >= len) {
    px = x - lx2;
    py = y - ly2;
    return 4 * (px * px + py * py) <= sw * sw;
  }
  // Perpendicular distance from the line, via the cross product magnitude
  // divided by the segment length -- kept as cross^2 vs sw^2*len to stay in
  // integer arithmetic (no division, no sqrt).
  int64_t cross = px * dy - py * dx;
  return 4 * cross * cross <= sw * sw * len;
}
// Classifies a pixel against a rounded rectangle's outer outline (fill) and the
// outer-minus-inner ring (stroke), where the inner radius is max(0,radius-strokeWidth).
//
// A pixel outside all four corner quadrants (i.e. in the straight top/bottom/
// left/right bands) uses the same fast distance-to-edge test as the
// radius==0 case, since rounding only ever affects the corners. Only a pixel
// inside a corner quadrant needs the squared-distance circle test, against a
// center placed `radius` pixels in from that corner -- the outer and inner
// arcs share that same center, just at radius and radius-strokeWidth, so one
// distance computation classifies both.
static void roundedRectHit(int x, int y, int rx, int ry, int rw, int rh, int radius, int strokeWidth,
                            bool& inside, bool& edge) {
  if (radius <= 0) {
    inside = true;
    edge = x - rx < strokeWidth || rx + rw - x <= strokeWidth || y - ry < strokeWidth || ry + rh - y <= strokeWidth;
    return;
  }
  const bool left = x < rx + radius, right = x >= rx + rw - radius;
  const bool top = y < ry + radius, bottom = y >= ry + rh - radius;
  if (!((left || right) && (top || bottom))) {
    inside = true;
    edge = x - rx < strokeWidth || rx + rw - x <= strokeWidth || y - ry < strokeWidth || ry + rh - y <= strokeWidth;
    return;
  }
  const int64_t cx = left ? rx + radius : rx + rw - radius - 1;
  const int64_t cy = top ? ry + radius : ry + rh - radius - 1;
  const int64_t ddx = x - cx, ddy = y - cy;
  const int64_t dist = ddx * ddx + ddy * ddy;
  inside = dist <= int64_t(radius) * radius;
  const int inner = std::max(0, radius - strokeWidth);
  edge = inside && (inner == 0 || dist > int64_t(inner) * inner);
}
// Maps a scrolling text layer's viewport-local x to its position within the
// full expanded text, or returns false when that position falls in the gap
// (loop mode) between repeated copies.
static bool scrollContentColumn(const Layer& l, const LayerState& s, int textWidth, int local, int& contentX) {
  if (!l.scroll.enabled) {
    contentX = local;
    return true;
  }
  if (l.scroll.mode == ScrollMode::Loop) {
    const int cycle = textWidth + l.scroll.gap;
    if (cycle <= 0) return false;
    contentX = (local + s.scrollOffset) % cycle;
    return contentX < textWidth;
  }
  contentX = local + s.scrollOffset;
  return contentX < textWidth;
}
// Draws the part `r` (one row) of a text layer in a package font into
// `pixels`, which starts at r.x. Every glyph's ink lies within its advance
// (the package check makes sure), so walking the advances from the left
// finds the one glyph under each pixel; only that glyph's row is read, into
// `coverage`, and blended with the layer's colour over what is below.
// Out of line: render() already holds three row buffers on the stack.
__attribute__((noinline)) static bool fontRow(const Theme& theme, const Layer& l, const LayerState& s, Rect r,
                                              AssetHandle& handle, Assets& assets, uint16_t* pixels, uint8_t* coverage) {
  if (l.font > theme.fonts.size()) return false;
  const ThemeFont& font = theme.fonts[l.font - 1];
  if (handle == InvalidAsset) handle = assets.resolve("fonts/" + font.name + ".stf");
  if (handle == InvalidAsset) return false;
  const int width = textPixelWidth(theme, l, s.resolved.size, s.text), line = r.y - s.bounds.y;
  size_t index = 0, loaded = SIZE_MAX;
  int pen = 0, left = 0, inked = 0;
  for (int x = r.x; x < r.x + r.w; ++x) {
    int contentX = 0;
    if (!scrollContentColumn(l, s, width, x - s.bounds.x, contentX)) continue;
    if (contentX < pen) {   // the next copy of a looping scroll
      index = 0;
      pen = 0;
    }
    while (index < s.text.size() && contentX >= pen + font.advance(s.text[index])) pen += font.advance(s.text[index++]);
    if (index == s.text.size()) continue;
    if (index != loaded) {
      if (!assets.glyphRow(handle, font.glyph(s.text[index]), line, coverage, left, inked)) return false;
      loaded = index;
    }
    const int column = contentX - pen - left;
    if (column >= 0 && column < inked) pixels[x - r.x] = blend(pixels[x - r.x], s.resolved.color, coverage[column]);
  }
  return true;
}
// Rebuilds every dirty row from scratch, in layer order back to front, and
// streams each finished row straight to `display` -- there is no persistent
// 240x240 framebuffer anywhere in this path. `handles` caches one resolved
// AssetHandle per layer for the whole call, so a layer touched by several
// disjoint dirty regions (or several rows of the same region) still resolves
// its asset exactly once.
bool render(const Engine& e, const std::vector<Rect>& dirty, Assets& assets, Display& display) {
  uint16_t pixels[240], colors[240];
  uint8_t alpha[240];
  if (e.theme().layers.size() > MaxLayers || e.states().size() != e.theme().layers.size()) return false;
  AssetHandle handles[MaxLayers];
  std::fill(handles, handles + MaxLayers, InvalidAsset);
  for (Rect region : dirty) {
    region = intersect(region, Rect(0, 0, 240, 240));
    for (int y = region.y; y < region.y + region.h; ++y) {
      std::fill(pixels, pixels + region.w, e.theme().background);
      for (size_t i = 0; i < e.theme().layers.size(); ++i) {
        const auto& l = e.theme().layers[i];
        const auto& s = e.states()[i];
        Rect r = intersect(s.bounds, Rect(region.x, y, region.w, 1));
        if (r.empty()) continue;
        if (l.type == LayerType::Image || l.type == LayerType::Animation) {
          // Assets stream by row too: only the `r.w` pixels this dirty region
          // actually needs are ever read, never a whole image or frame.
          if (handles[i] == InvalidAsset) handles[i] = assets.resolve(assetPath(l, s.frame));
          if (handles[i] == InvalidAsset || !assets.row(handles[i], y - s.resolved.y, r.x - s.resolved.x, r.w, colors, alpha)) return false;
          for (int x = 0; x < r.w; ++x) pixels[r.x - region.x + x] = blend(pixels[r.x - region.x + x], colors[x], alpha[x]);
          continue;
        }
        if (l.type == LayerType::Text && l.font) {
          if (!fontRow(e.theme(), l, s, r, handles[i], assets, pixels + (r.x - region.x), alpha)) return false;
          continue;
        }
        // The rotation shapes' per-row setup: one sine for a whole row of
        // pixels instead of one per pixel.
        int tickIndex = -1;
        ArcWedge wedge{};
        Segment hand{}, tick{};
        if (l.type == LayerType::Shape && l.shape == Shape::Arc) wedge = arcWedge(s.resolved);
        if (l.type == LayerType::Shape && l.shape == Shape::Hand) hand = handSegment(l, s.resolved);
        for (int x = r.x; x < r.x + r.w; ++x) {
          uint16_t c = 0;
          bool draw = false;
          if (l.type == LayerType::Text) {
            // `local` is the pixel's position within the viewport (s.bounds);
            // scrollContentColumn maps it to `contentX`, its position within
            // the full expanded text, or reports "no ink here" for a loop
            // mode's inter-copy gap. From there it is an ordinary glyph-cell
            // lookup: which character (charIndex), which column within its
            // 6-wide cell (col, with column 5 always blank as spacing), and
            // which row within the glyph (scaled from pixel row to the font's
            // fixed 8-row bitmap by `resolved.size`).
            const int cell = (s.resolved.size * 6 + 7) / 8, local = x - s.bounds.x;
            int contentX = 0;
            if (scrollContentColumn(l, s, textPixelWidth(e.theme(), l, s.resolved.size, s.text), local, contentX)) {
              const int col = (contentX % cell) * 6 / cell, charIndex = contentX / cell;
              const int row = (y - s.bounds.y) * 8 / s.resolved.size;
              draw = col < 5 && charIndex < static_cast<int>(s.text.size()) &&
                (display.glyphColumn(s.text[charIndex], col) & (1 << row));
              c = s.resolved.color;
            }
          } else if (rotationShape(l.shape)) {
            const int dx = x - s.resolved.x, dy = y - s.resolved.y, dist = dx * dx + dy * dy;
            if (l.shape == Shape::Arc) {
              const int outer = s.resolved.radius, inner = std::max(0, outer - s.resolved.strokeWidth);
              draw = s.resolved.y2 != 0 && dist <= outer * outer && (inner == 0 || dist > inner * inner) &&
                inWedge(wedge, dx, dy);
            } else if (l.shape == Shape::Hand) {
              draw = linePixel(hand.x0, hand.y0, hand.x1, hand.y1, s.resolved.strokeWidth, x, y);
            } else {
              // Only the tick nearest in angle can reach this pixel: the
              // distance to a radial line grows with the angle away from it.
              const int pad = (s.resolved.strokeWidth + 1) / 2 + 1, near = std::max(0, s.resolved.radius - l.y2 - pad),
                far = s.resolved.radius + pad;
              if (dist <= far * far && dist >= near * near) {
                const int k = ((pixelAngle16(dx, dy) * l.x2 + Turn16 / 2) / Turn16) % l.x2;
                if (k != tickIndex) {
                  tickIndex = k;
                  tick = tickSegment(l, s.resolved, k);
                }
                draw = linePixel(tick.x0, tick.y0, tick.x1, tick.y1, s.resolved.strokeWidth, x, y);
              }
            }
            c = s.resolved.stroke;
          } else {
            bool inside = false, edge = false;
            if (l.shape == Shape::Rectangle) {
              roundedRectHit(x, y, s.resolved.x, s.resolved.y, s.resolved.width, s.resolved.height,
                              s.resolved.cornerRadius, s.resolved.strokeWidth, inside, edge);
            } else if (l.shape == Shape::Circle) {
              int dx = x - s.resolved.x, dy = y - s.resolved.y, dist = dx * dx + dy * dy, inner = std::max(0, s.resolved.radius - s.resolved.strokeWidth);
              inside = dist <= s.resolved.radius * s.resolved.radius;
              edge = inside && (inner == 0 || dist > inner * inner);
            } else {
              inside = edge = linePixel(s.resolved.x, s.resolved.y, s.resolved.x2, s.resolved.y2, s.resolved.strokeWidth, x, y);
            }
            // Fill first, stroke second and unconditionally overwriting: a
            // pixel that is both `inside` and `edge` (the stroke band) ends
            // up stroke-colored, exactly as if fill were painted first and
            // the stroke ring painted on top of it.
            if (inside && l.hasFill) {
              draw = true;
              c = s.resolved.fill;
            }
            if (edge && l.hasStroke) {
              draw = true;
              c = s.resolved.stroke;
            }
          }
          if (draw) pixels[x - region.x] = c;
        }
      }
      display.row(region.x, y, pixels, region.w);
    }
  }
  return true;
}
}
#endif
