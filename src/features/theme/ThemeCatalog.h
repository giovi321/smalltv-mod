#pragma once
#include "ThemePackage.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>

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
// Every install already runs the full validator, so it also writes a small
// `<id>.meta` sidecar with the listing fields; GET /api/themes then reads that
// instead of loading each package again next to the running theme (issue #30).
// A sidecar is trusted only for a package of exactly the size it records and
// for the same MetaRevision. Bump MetaRevision whenever Package::load() starts
// rejecting something it used to accept, so packages checked by an older
// firmware are validated again instead of keeping their old result.
constexpr int MetaRevision = 1;
constexpr size_t MaxMeta = 512;
inline std::string themeMetaJson(const InstalledTheme& entry) {
  JsonDocument doc;
  doc["revision"] = MetaRevision;
  doc["bytes"] = entry.bytes;
  doc["name"] = entry.name;
  doc["author"] = entry.author;
  doc["version"] = entry.version;
  std::string json;
  serializeJson(doc, json);
  return json;
}
// ArduinoJson Reader over a Source through a 32-byte buffer: the listing runs
// with little loop stack left, so the sidecar is never copied whole.
class MetaReader {
 public:
  explicit MetaReader(Source& source) : source_(source), size_(source.size()) {}
  int read() {
    if (next_ == length_) {
      uint32_t count = std::min<uint32_t>(sizeof(buffer_), size_ - offset_);
      if (!count || !source_.read(offset_, buffer_, count)) return -1;
      offset_ += count;
      length_ = count;
      next_ = 0;
    }
    return static_cast<unsigned char>(buffer_[next_++]);
  }
  size_t readBytes(char* out, size_t count) {
    size_t n = 0;
    for (int c; n < count && (c = read()) >= 0;) out[n++] = char(c);
    return n;
  }
 private:
  Source& source_;
  uint32_t size_, offset_ = 0;
  uint8_t length_ = 0, next_ = 0;
  char buffer_[32];
};
// Fills `entry` from a sidecar for the package `id` of `packageBytes` bytes.
// Anything unexpected -- wrong revision or size, a missing or oversized field,
// malformed JSON -- returns false and the caller inspects the package itself.
inline bool readThemeMeta(Source& meta, const std::string& id, uint32_t packageBytes, InstalledTheme& entry) {
  uint32_t size = meta.size();
  if (!validId(id) || size == 0 || size > MaxMeta) return false;
  MetaReader reader(meta);
  JsonDocument doc;
  if (deserializeJson(doc, reader)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (!root["revision"].is<int>() || root["revision"].as<int>() != MetaRevision ||
      !root["bytes"].is<uint32_t>() || root["bytes"].as<uint32_t>() != packageBytes) return false;
  const char* name = root["name"];
  const char* author = root["author"];
  const char* version = root["version"];
  if (!name || !author || !version || strlen(name) > 96 || strlen(author) > 96 || strlen(version) > 32) return false;
  entry = InstalledTheme();
  entry.id = id;
  entry.name = name;
  entry.author = author;
  entry.version = version;
  entry.bytes = packageBytes;
  entry.valid = true;
  return true;
}
}
