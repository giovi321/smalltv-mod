#pragma once
#include "BlockList.h"
#include "ThemePackage.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <utility>
// The list reply's fixed text stays in flash on the ESP8266, where string
// constants otherwise take RAM for good. Elsewhere PSTR() and the _P
// functions are the plain ones.
#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
#include <pgmspace.h>
#endif
#ifndef PSTR
#define PSTR(s) (s)
#define strlen_P strlen
#define memcpy_P memcpy
#endif

namespace smalltv {
// One row of GET /api/themes: metadata for an installed package regardless
// of whether it loads successfully -- an invalid package still gets an `id`,
// `bytes`, and `error` so the web UI can list and offer to remove it.
struct InstalledTheme {
  std::string id, name, author, version, error;
  uint32_t bytes = 0;
  bool valid = false;
};
// Identity always comes from the installed filename, even when the manifest is
// unreadable or has a conflicting ID. Such packages remain visible/removable.
inline InstalledTheme inspectInstalledTheme(Source& source, const std::string& id) {
  InstalledTheme entry;
  entry.id = entry.name = id;
  entry.bytes = source.size();
  if (!validId(id)) {
    entry.error = "Invalid installed filename";
    return entry;
  }
  Package package(source);
  Theme theme;
  if (!package.load(theme, entry.error)) return entry;
  if (theme.id != id) {
    entry.error = "Theme ID does not match installed filename";
    return entry;
  }
  entry.valid = true;
  entry.name = theme.name;
  entry.author = theme.author;
  entry.version = theme.version;
  return entry;
}
// Every install already runs the full validator, so it also records the
// listing fields in one index file for all installed themes; GET /api/themes
// then reads that instead of loading each package again next to the running
// theme (issue #30). One file, not one per theme: on the ESP8266 every file
// takes at least one 8 KB block of storage. An entry is trusted only for a
// package of exactly the size it records and for the same MetaRevision. Bump
// MetaRevision whenever Package::load() starts rejecting something it used to
// accept, so packages checked by an older firmware are validated again
// instead of keeping their old result.
constexpr int MetaRevision = 1;
// The longest line the limits below allow is about 600 bytes (a name made of
// quotes doubles in JSON); a longer one is skipped unread.
constexpr size_t MaxIndexLine = 768;
// 16 themes (MaxThemes in ThemeWeb.cpp) of the longest lines. A larger
// index was not written by this firmware; it is not read at all.
constexpr size_t MaxIndexBytes = 16 * MaxIndexLine;
// One entry: a JSON object on a line of its own.
inline std::string themeIndexLine(const InstalledTheme& entry) {
  JsonDocument doc;
  doc["revision"] = MetaRevision;
  doc["id"] = entry.id;
  doc["bytes"] = entry.bytes;
  doc["name"] = entry.name;
  doc["author"] = entry.author;
  doc["version"] = entry.version;
  std::string json;
  serializeJson(doc, json);
  json += '\n';
  return json;
}
enum class IndexEntry { Usable, Unusable, End };
// Reads the index one line at a time through a 32-byte buffer: the listing
// runs with little loop stack and heap left, so the file is never held whole.
class ThemeIndexReader {
 public:
  explicit ThemeIndexReader(Source& index)
      : source_(index), size_(index.size() > MaxIndexBytes ? 0 : index.size()) {}
  // The next entry. A line that is not a well-formed entry -- wrong revision,
  // an invalid id, a missing or oversized field, malformed JSON, longer than
  // MaxIndexLine -- is Unusable and skipped; the lines after it still count.
  // Each line ends with a pause for the device (CONTRIBUTING: a loop over
  // input-sized data yields).
  IndexEntry next(InstalledTheme& entry) {
    // Free the last entry's strings before the next line is parsed.
    // Assigning empty strings would keep their buffers, and swapping with a
    // whole empty entry took 128 B of loop stack on the list path.
    for (std::string* s : {&entry.id, &entry.name, &entry.author, &entry.version, &entry.error}) std::string().swap(*s);
    entry.bytes = 0;
    entry.valid = false;
    int c;
    while ((c = peek()) == '\n' || c == '\r' || c == ' ') take();
    if (c < 0) return IndexEntry::End;
    bool usable;
    {
      JsonDocument doc;
      Line line(*this);
      usable = !deserializeJson(doc, line) && lineEnds() && fill(doc.as<JsonObjectConst>(), entry);
    }
    while ((c = take()) >= 0 && c != '\n') {}
    source_.pause();
    return usable ? IndexEntry::Usable : IndexEntry::Unusable;
  }
 private:
  // What deserializeJson() reads: one line, cut off after MaxIndexLine bytes,
  // or inside a string longer than any field can be (96 bytes once
  // unescaped): ArduinoJson grows a string's buffer as it reads, and an
  // over-long one took a 1 KB block. It stops at the end of the object, so
  // the rest of the line stays unread.
  struct Line {
    explicit Line(ThemeIndexReader& reader) : index(reader) {}
    ThemeIndexReader& index;
    size_t used = 0, length = 0;
    bool inString = false, escaped = false;
    int read() {
      if (used++ >= MaxIndexLine) return -1;
      int c = index.take();
      if (escaped) {
        escaped = false;
        ++length;
      } else if (inString && c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = !inString;
        length = 0;
      } else if (inString) {
        ++length;
      }
      return inString && length > 96 ? -1 : c;
    }
    size_t readBytes(char* out, size_t count) {
      size_t n = 0;
      for (int c; n < count && (c = read()) >= 0;) out[n++] = char(c);
      return n;
    }
  };
  int peek() {
    if (next_ == length_) {
      uint32_t count = std::min<uint32_t>(sizeof(buffer_), size_ - offset_);
      if (!count || !source_.read(offset_, buffer_, count)) return -1;
      offset_ += count;
      length_ = count;
      next_ = 0;
    }
    return static_cast<unsigned char>(buffer_[next_]);
  }
  int take() {
    int c = peek();
    if (c >= 0) ++next_;
    return c;
  }
  // Only spaces may follow the object on its line.
  bool lineEnds() {
    int c;
    while ((c = peek()) == ' ' || c == '\r') take();
    return c < 0 || c == '\n';
  }
  // Line keeps every string to 96 bytes, the limit for a name and an author.
  // `entry` comes in empty (next() cleared it).
  static bool fill(JsonObjectConst root, InstalledTheme& entry) {
    const char* id = root["id"];
    const char* name = root["name"];
    const char* author = root["author"];
    const char* version = root["version"];
    if (!root["revision"].is<int>() || root["revision"].as<int>() != MetaRevision || !root["bytes"].is<uint32_t>() ||
        !id || !validId(id) || !name || !author || !version || strlen(version) > 32) return false;
    entry.id = id;
    entry.name = name;
    entry.author = author;
    entry.version = version;
    entry.bytes = root["bytes"].as<uint32_t>();
    entry.valid = true;
    return true;
  }
  Source& source_;
  uint32_t size_, offset_ = 0;
  uint8_t length_ = 0, next_ = 0;
  char buffer_[32];
};
class Sink {
 public:
  virtual ~Sink() = default;
  virtual bool write(const char* data, size_t count) = 0;
};
// Collects what serializeJson() writes in a buffer of `Piece` bytes and
// hands it to a Sink each time the buffer is full, and once more at the end.
template <size_t Piece> class PieceWriter {
 public:
  explicit PieceWriter(Sink& to) : to_(to) {}
  size_t write(uint8_t c) { return write(&c, 1); }
  size_t write(const uint8_t* data, size_t count) {
    put(data, count, memcpy);
    return count;
  }
  // Text kept in flash (PSTR).
  void writeFlash(const char* text) { put(text, strlen_P(text), memcpy_P); }
  void flush() {
    if (used_) to_.write(buffer_, used_);
    used_ = 0;
  }
 private:
  void put(const void* data, size_t count, void* (*copy)(void*, const void*, size_t)) {
    const char* from = static_cast<const char*>(data);
    for (size_t done = 0; done < count;) {
      if (used_ == Piece) flush();
      const size_t n = std::min(Piece - used_, count - done);
      copy(buffer_ + used_, from + done, n);
      used_ += n;
      done += n;
    }
  }
  Sink& to_;
  size_t used_ = 0;
  char buffer_[Piece];
};
// One package of GET /api/themes while the list is put together. Its five
// text fields are packed into one string, each ended by a 0 byte: 32 bytes
// and one short string per package. 3.2.0 held the list in one JSON
// document, about 3.2 KB with 16 themes, and with the reply built next to it
// the ESP8266 ran out of heap after a row of installs (issue #63).
class ListedTheme {
 public:
  uint32_t bytes = 0;
  bool valid = false;
  // A field ends at its first 0 byte. The fields may be this package's own:
  // the new text is put together before the old one goes.
  void set(const char* id, const char* name, const char* author, const char* version, const char* error) {
    std::string text;
    text.reserve(strlen(id) + strlen(name) + strlen(author) + strlen(version) + strlen(error) + 5);
    for (const char* field : {id, name, author, version, error}) text.append(field, strlen(field) + 1);
    text_.swap(text);
  }
  const char* id() const { return field(0); }
  const char* name() const { return field(1); }
  const char* author() const { return field(2); }
  const char* version() const { return field(3); }
  const char* error() const { return field(4); }
 private:
  const char* field(int n) const {
    const char* p = text_.c_str();
    if (text_.empty()) return p;
    while (n--) p += strlen(p) + 1;
    return p;
  }
  std::string text_;
};
namespace listing {
// Fixed text, given as PSTR().
template <class Out> void raw(Out& out, const char* text) { out.writeFlash(text); }
// A string value as ArduinoJson writes it, escapes and all. The document
// only points at the text, so it takes no heap.
template <class Out> void text(Out& out, const char* value) {
  JsonDocument doc;
  doc.set(JsonString(value, true));
  serializeJson(doc, out);
}
// Written digit by digit from the end: no format string in RAM.
template <class Out> void number(Out& out, uint32_t value) {
  char digits[10];
  size_t n = 0;
  do {
    digits[sizeof(digits) - ++n] = char('0' + value % 10);
    value /= 10;
  } while (value);
  out.write(reinterpret_cast<const uint8_t*>(digits + sizeof(digits) - n), n);
}
}
// The reply of GET /api/themes, written straight to `to` in pieces of
// `Piece` bytes (the last one shorter), never whole in one block of heap:
// the same bytes as 3.2.0's document, fields in the same order.
template <size_t Piece>
void writeThemeList(const char* selected, const char* error, uint32_t freeBytes, const BlockList<ListedTheme>& themes, Sink& to) {
  PieceWriter<Piece> out(to);
  listing::raw(out, PSTR("{\"selected\":"));
  listing::text(out, selected);
  listing::raw(out, PSTR(",\"error\":"));
  listing::text(out, error);
  listing::raw(out, PSTR(",\"freeBytes\":"));
  listing::number(out, freeBytes);
  listing::raw(out, PSTR(",\"themes\":["));
  bool first = true;
  for (const ListedTheme& t : themes) {
    listing::raw(out, first ? PSTR("{\"id\":") : PSTR(",{\"id\":"));
    first = false;
    listing::text(out, t.id());
    listing::raw(out, PSTR(",\"name\":"));
    listing::text(out, t.name());
    listing::raw(out, PSTR(",\"author\":"));
    listing::text(out, t.author());
    listing::raw(out, PSTR(",\"version\":"));
    listing::text(out, t.version());
    listing::raw(out, PSTR(",\"bytes\":"));
    listing::number(out, t.bytes);
    listing::raw(out, t.valid ? PSTR(",\"valid\":true,\"error\":") : PSTR(",\"valid\":false,\"error\":"));
    listing::text(out, t.error());
    listing::raw(out, PSTR("}"));
  }
  listing::raw(out, PSTR("]}"));
  out.flush();
}
// Install and delete rewrite the index through a copy: every usable entry
// except the one for `id`, then `added` if given. False if a write fails.
inline bool copyThemeIndex(Source& from, Sink& to, const std::string& id, const InstalledTheme* added) {
  ThemeIndexReader reader(from);
  InstalledTheme entry;
  for (IndexEntry kind; (kind = reader.next(entry)) != IndexEntry::End;) {
    if (kind != IndexEntry::Usable || entry.id == id) continue;
    std::string line = themeIndexLine(entry);
    if (!to.write(line.data(), line.size())) return false;
  }
  if (!added) return true;
  std::string line = themeIndexLine(*added);
  return to.write(line.data(), line.size());
}
}
