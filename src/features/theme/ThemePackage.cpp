#include "ThemePackage.h"
#ifdef ARDUINO
#include "config.h"
#endif
#if !defined(ARDUINO) || WITH_THEME
#include <algorithm>
#include <cstring>
#include <utility>
namespace smalltv {
// Both container formats (STH1 packages, STI1/STI2 images) are little-endian.
static uint16_t u16(const uint8_t* b) { return b[0] | (uint16_t(b[1]) << 8); }
static uint32_t u32(const uint8_t* b) { return u16(b) | (uint32_t(u16(b + 2)) << 16); }
// The package's theme.json entry, read in place for parseTheme().
class EntryManifest : public ManifestSource {
 public:
  EntryManifest(Source& source, uint32_t offset, uint32_t length) : source_(source), offset_(offset), length_(length) {}
  size_t size() const override { return length_; }
  bool read(size_t offset, char* out, size_t count) override {
    return offset <= length_ && count <= length_ - offset && source_.read(offset_ + offset, out, count);
  }
 private:
  Source& source_;
  uint32_t offset_, length_;
};
static uint32_t pathHash(const std::string& path) {
  uint32_t hash = 2166136261u;
  for (unsigned char c : path) hash = (hash ^ c) * 16777619u;
  return hash;
}
// Compares `path` with the entry's path as stored in the package. In small
// pieces: this runs while a frame is drawn, when little loop stack is left.
bool Package::samePath(const Entry& entry, const std::string& path) const {
  if (entry.pathLength != path.size()) return false;
  char piece[16];
  for (size_t at = 0; at < path.size(); at += sizeof(piece)) {
    size_t count = std::min(sizeof(piece), path.size() - at);
    if (!source_.read(entry.offset - entry.pathLength + at, piece, count) || memcmp(piece, path.data() + at, count)) return false;
  }
  return true;
}
// The entry's position in the index, or entries_.size() if there is none.
size_t Package::indexOf(const std::string& path) const {
  const uint32_t hash = pathHash(path);
  size_t i = 0;
  while (i < entries_.size() && (entries_[i].hash != hash || !samePath(entries_[i], path))) ++i;
  return i;
}
const Package::Entry* Package::find(const std::string& path) const {
  size_t i = indexOf(path);
  return i < entries_.size() ? &entries_[i] : nullptr;
}
// Reads and validates an .stheme container's index, then its manifest, then
// cross-checks every image/animation layer's declared source against the
// entries actually present -- filling in an image layer's width/height from
// its asset (an image has no declared size of its own) and rejecting an
// animation whose frames disagree with its declared width/height. Nothing is
// decoded beyond the index: pixel data is read later, by row(), only for the
// rows a dirty rectangle actually needs.
//
// Each phase is its own out-of-line function, so only `candidate` sits under
// the manifest parse: on the ESP8266 that parse needs most of the 4 KiB loop
// stack, and the other phases' locals are dead by then.
bool Package::load(Theme& theme, std::string& error) {
  if (!readIndex(error)) return false;
  const Entry* manifest = find("theme.json");
  if (!manifest) {
    error = "Missing theme.json";
    return false;
  }
  // Parsed straight from the package: a copy of the manifest in RAM could
  // by itself exceed what the ESP8266 has free next to a running theme.
  EntryManifest json(source_, manifest->offset, manifest->length);
  Theme candidate;
  if (!parseTheme(json, candidate, error) || !checkSpec(candidate, error) || !checkAssets(candidate, error)) return false;
  theme = std::move(candidate);
  error.clear();
  return true;
}
__attribute__((noinline)) bool Package::readIndex(std::string& error) {
  entries_.clear();
  error = "Invalid or truncated .stheme package";
  uint8_t b[12];
  uint32_t size = source_.size();
  // Header: "STH1" magic, uint16 entry count, uint16 reserved (must be 0).
  if (size < 8 || size > MaxPackage || !source_.read(0, b, 8) || memcmp(b, "STH1", 4) || u16(b + 6)) return false;
  unsigned count = u16(b + 4);
  if (!count || count > MaxEntries) return false;
  entries_.reserve(count);
  uint32_t pos = 8;
  for (unsigned i = 0; i < count; ++i) {
    // Each entry: uint16 path length, uint32 payload length, path bytes,
    // then the payload itself (read lazily, never buffered here).
    if (pos > size || size - pos < 6 || !source_.read(pos, b, 6)) return false;
    unsigned len = u16(b);
    Entry e;
    e.length = u32(b + 2);
    pos += 6;
    if (!len || len > 120 || len > size - pos) return false;
    std::string path(len, '\0');
    if (!source_.read(pos, &path[0], len) || !validPath(path) || find(path)) return false;
    e.hash = pathHash(path);
    e.pathLength = static_cast<uint8_t>(len);
    pos += len;
    e.offset = pos;
    if (e.length > size - pos) return false;
    if (path == "theme.json") {
      if (e.length == 0 || e.length > MaxManifest) return false;
    } else {
      // Every non-manifest entry must be a well-formed .sti image: "STI1"
      // (raw pixels) or "STI2" (rows as runs) magic, width/height, an alpha
      // flag that also selects the pixel stride (2 bytes/pixel opaque, 3
      // with an alpha byte), then 3 reserved zero bytes, except that STI2
      // keeps its palette size (0-16) in the first. An STI1 payload length
      // must match width*height*stride exactly; an STI2 image is checked
      // row by row (checkRows()) -- nothing here is inferred, every field is
      // cross-checked.
      if (path.size() < 4 || path.substr(path.size() - 4) != ".sti" || e.length < 12 || !source_.read(pos, b, 12) ||
          memcmp(b, "STI", 3) || (b[3] != '1' && b[3] != '2')) return false;
      e.compressed = b[3] == '2';
      e.width = u16(b + 4);
      e.height = u16(b + 6);
      e.stride = b[8] ? 3 : 2;
      e.palette = e.compressed ? b[9] : 0;
      if (!e.width || e.width > 240 || !e.height || e.height > 240 || b[8] > 1 || (e.compressed ? b[9] > 16 : b[9]) ||
          b[10] || b[11]) return false;
      if (e.compressed ? !checkRows(e) : e.length != 12 + uint32_t(e.width) * e.height * e.stride) return false;
    }
    const uint32_t length = e.length;
    entries_.push_back(std::move(e));
    pos += length;
  }
  return pos == size;
}
// STI2 images need a spec 2 manifest (#31), so a package says which
// firmware it needs. (Firmware up to 3.1.1 refuses any STI2 image, with
// "Invalid or truncated .stheme package", before it reads the manifest.)
// Every image counts, also one that no layer uses.
bool Package::checkSpec(const Theme& theme, std::string& error) const {
  if (theme.spec >= 2) return true;
  for (size_t i = 0; i < entries_.size(); ++i) {
    const Entry& e = entries_[i];
    if (!e.compressed) continue;
    std::string path(e.pathLength, '\0');
    if (!source_.read(e.offset - e.pathLength, &path[0], path.size())) path = "an image";
    error = path + ": STI2 image needs \"spec\": 2";
    return false;
  }
  return true;
}
// Assets are validated against the parsed manifest only after parseTheme()
// succeeds, into a `candidate` that only replaces `theme` on full success --
// a package that fails asset validation leaves any previously loaded theme
// untouched.
__attribute__((noinline)) bool Package::checkAssets(Theme& candidate, std::string& error) const {
  for (size_t i = 0; i < candidate.layers.size(); ++i) {
    auto& l = candidate.layers[i];
    const std::string field = "layers[" + std::to_string(i) + "].source: ";
    if (l.type != LayerType::Image && l.type != LayerType::Animation) continue;
    for (unsigned frame = 0; frame < l.frames; ++frame) {
      const Entry* e = find(assetPath(l, frame));
      if (!e || !e->stride) {
        error = field + "missing asset: " + assetPath(l, frame);
        return false;
      }
      if (l.type == LayerType::Image) {
        l.width = e->width;
        l.height = e->height;
      } else if (l.width != e->width || l.height != e->height) {
        error = field + "frame dimensions do not match width/height: " + assetPath(l, frame);
        return false;
      }
    }
  }
  return true;
}
// An asset handle is simply an entry's index into `entries_`; `stride` doubles
// as "is this entry an image, not the manifest" -- the manifest entry always
// has stride 0 and so can never be resolved as an asset.
AssetHandle Package::resolve(const std::string& path) {
  size_t i = indexOf(path);
  return i < entries_.size() && entries_[i].stride ? static_cast<AssetHandle>(i) : InvalidAsset;
}
namespace {
// Reads a package byte by byte through a 64-byte window: row() runs while a
// frame is drawn, when little loop stack is left (a whole 240-pixel row was
// a 720-byte buffer), and one storage read per byte would be slow.
class Reader {
 public:
  Reader(Source& source, uint32_t at, uint32_t end) : source_(source), at_(at), end_(end) {}
  bool byte(uint8_t& out) {
    if (pos_ == len_) {
      if (at_ >= end_) return false;
      len_ = static_cast<uint8_t>(std::min<uint32_t>(sizeof(buf_), end_ - at_));
      if (!source_.read(at_, buf_, len_)) return false;
      at_ += len_;
      pos_ = 0;
    }
    out = buf_[pos_++];
    return true;
  }
  bool pixel(uint8_t stride, uint16_t& color, uint8_t& alpha) {
    uint8_t lo, hi, a = 255;
    if (!byte(lo) || !byte(hi) || (stride == 3 && !byte(a))) return false;
    color = lo | (uint16_t(hi) << 8);
    alpha = a;
    return true;
  }
  bool offset(uint32_t& out) {
    uint8_t b[4];
    for (uint8_t& x : b) if (!byte(x)) return false;
    out = u32(b);
    return true;
  }
  // Where the next byte comes from in the package.
  uint32_t position() const { return at_ - len_ + pos_; }
  // Moves on to `to`, keeping what the window holds when `to` is in it.
  void skipTo(uint32_t to) {
    if (to >= at_ - len_ && to <= at_) {
      pos_ = static_cast<uint8_t>(to - (at_ - len_));
    } else {
      at_ = to;
      len_ = pos_ = 0;
    }
  }
 private:
  Source& source_;
  uint32_t at_, end_;
  uint8_t buf_[64];
  uint8_t len_ = 0, pos_ = 0;
};
// Walks an STI2 row's runs up to column `stop` and keeps the pixels from `x`
// on; with `colors` null it only checks them. A run's first byte holds its
// kind in the top two bits and its length minus one in the rest: 00
// transparent (only in an image with alpha), 01 one pixel repeated, 10 that
// many literal pixels; 11 is reserved. With a palette a pixel is an index
// below its size: one byte in a repeat, two per byte in a literal (high
// nibble first, an odd run's unused low nibble 0). A run that goes past the
// row, or data that ends early, fails the row.
bool walkRow(Reader& reader, uint8_t stride, uint8_t palette, int width, const uint16_t* paletteColor,
             const uint8_t* paletteAlpha, int x, int stop, uint16_t* colors, uint8_t* alpha) {
  for (int px = 0; px < stop;) {
    uint8_t head;
    if (!reader.byte(head)) return false;
    const int kind = head >> 6, length = (head & 63) + 1;
    if (kind == 3 || (kind == 0 && stride != 3) || px + length > width) return false;
    uint16_t color = 0;
    uint8_t a = 0, pair = 0;
    for (int i = 0; i < length; ++i) {
      if (kind == 2 || (kind == 1 && i == 0)) {
        if (!palette) {
          if (!reader.pixel(stride, color, a)) return false;
        } else {
          uint8_t index;
          if (kind == 1) {
            if (!reader.byte(index)) return false;
          } else if (i % 2 == 0) {
            if (!reader.byte(pair) || (i + 1 == length && (pair & 15))) return false;
            index = pair >> 4;
          } else {
            index = pair & 15;
          }
          if (index >= palette) return false;
          if (paletteColor) {
            color = paletteColor[index];
            a = paletteAlpha[index];
          }
        }
      }
      if (colors && px + i >= x && px + i < stop) {
        colors[px + i - x] = color;
        alpha[px + i - x] = a;
      }
    }
    px += length;
  }
  return true;
}
}
// An STI2 payload: the 12-byte header, the palette, one u32 offset per row
// (counted from the end of the table), then the rows. Every offset must point
// into the row data without going backwards; rows with equal offsets share
// data. Each distinct row must cover the width exactly without reading past
// the next larger offset (or the end), so row() can trust what it reads.
// This runs at install and at every boot (on the ESP8266 with the watchdog
// on), and also when a package is listed: the table and the rows are each
// read front to back through a 64-byte window, and every 16 rows the check
// pauses for the device. One read per row offset was ~61,000 reads for a
// package of 256 images (#53 review).
bool Package::checkRows(const Entry& e) const {
  const uint32_t table = 12 + uint32_t(e.palette) * e.stride, rows = table + 4u * e.height;
  if (e.length < rows + 1) return false;
  const uint32_t data = e.length - rows;
  Reader offsets(source_, e.offset + table, e.offset + rows);
  Reader runs(source_, e.offset + rows, e.offset + e.length);
  uint32_t start = 0;
  for (uint32_t y = 0; y <= e.height; ++y) {
    if (y % 16 == 0 && y > 0) source_.pause();
    uint32_t at = data;   // after the last row: its limit is the end
    if (y < e.height) {
      if (!offsets.offset(at) || at >= data || at < start) return false;
      if (y > 0 && at == start) continue;   // shares the row above
    }
    if (y > 0) {
      runs.skipTo(e.offset + rows + start);
      if (!walkRow(runs, e.stride, e.palette, e.width, nullptr, nullptr, 0, e.width, nullptr, nullptr) ||
          runs.position() > e.offset + rows + at) return false;
    }
    start = at;
  }
  return true;
}
// Reads exactly the `count` pixels of row `y` starting at column `x`. STI1
// rows are raw pixels (RGB565, plus an alpha byte for a transparent asset);
// STI2 rows are runs (see walkRow()). Runs before `x` are skipped.
bool Package::row(AssetHandle handle, int y, int x, int count, uint16_t* colors, uint8_t* alpha) {
  if (handle >= entries_.size()) return false;
  const Entry* e = &entries_[handle];
  if (!e->stride || x < 0 || y < 0 || y >= e->height || count < 0 || count > 240 || x + count > e->width) return false;
  if (!e->compressed) {
    const uint32_t at = e->offset + 12 + (uint32_t(y) * e->width + x) * e->stride;
    Reader reader(source_, at, at + uint32_t(count) * e->stride);
    for (int i = 0; i < count; ++i) if (!reader.pixel(e->stride, colors[i], alpha[i])) return false;
    return true;
  }
  const uint32_t table = e->offset + 12 + uint32_t(e->palette) * e->stride;
  uint16_t paletteColor[16];
  uint8_t paletteAlpha[16];
  {
    Reader reader(source_, e->offset + 12, table);   // one read: at most 48 bytes
    for (uint8_t i = 0; i < e->palette; ++i) if (!reader.pixel(e->stride, paletteColor[i], paletteAlpha[i])) return false;
  }
  uint8_t b[4];
  if (!source_.read(table + 4u * y, b, 4)) return false;
  Reader reader(source_, table + 4u * e->height + u32(b), e->offset + e->length);
  return walkRow(reader, e->stride, e->palette, e->width, paletteColor, paletteAlpha, x, x + count, colors, alpha);
}
}
#endif
