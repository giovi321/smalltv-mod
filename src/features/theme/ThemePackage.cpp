#include "ThemePackage.h"
#ifdef ARDUINO
#include "config.h"
#endif
#if !defined(ARDUINO) || WITH_THEME
#include <cstring>
#include <utility>
namespace smalltv {
// Both container formats (STH1 packages, STI1 images) are little-endian.
static uint16_t u16(const uint8_t* b) { return b[0] | (uint16_t(b[1]) << 8); }
static uint32_t u32(const uint8_t* b) { return u16(b) | (uint32_t(u16(b + 2)) << 16); }
const Package::Entry* Package::find(const std::string& path) const {
  for (const auto& entry : entries_) if (entry.path == path) return &entry;
  return nullptr;
}
// Reads and validates an .stheme container's index, then its manifest, then
// cross-checks every image/animation layer's declared source against the
// entries actually present -- filling in an image layer's width/height from
// its asset (an image has no declared size of its own) and rejecting an
// animation whose frames disagree with its declared width/height. Nothing is
// decoded beyond the index: pixel data is read later, by row(), only for the
// rows a dirty rectangle actually needs.
bool Package::load(Theme& theme, std::string& error) {
  entries_.clear();
  error = "Invalid or truncated .stheme package";
  uint8_t b[12];
  uint32_t size = source_.size();
  // Header: "STH1" magic, uint16 entry count, uint16 reserved (must be 0).
  if (size < 8 || size > MaxPackage || !source_.read(0, b, 8) || memcmp(b, "STH1", 4) || u16(b + 6)) return false;
  unsigned count = u16(b + 4);
  if (!count || count > MaxEntries) return false;
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
    e.path.resize(len);
    if (!source_.read(pos, &e.path[0], len) || !validPath(e.path) || find(e.path)) return false;
    pos += len;
    e.offset = pos;
    if (e.length > size - pos) return false;
    if (e.path == "theme.json") {
      if (e.length == 0 || e.length > MaxManifest) return false;
    } else {
      // Every non-manifest entry must be a well-formed .sti image: "STI1"
      // magic, width/height, an alpha flag that also selects the pixel
      // stride (2 bytes/pixel opaque, 3 with an alpha byte), 3 reserved
      // zero bytes, and a payload length that matches width*height*stride
      // exactly -- nothing here is inferred, every field is cross-checked.
      if (e.path.size() < 4 || e.path.substr(e.path.size() - 4) != ".sti" || e.length < 12 || !source_.read(pos, b, 12) || memcmp(b, "STI1", 4)) return false;
      e.width = u16(b + 4);
      e.height = u16(b + 6);
      e.stride = b[8] ? 3 : 2;
      if (!e.width || e.width > 240 || !e.height || e.height > 240 || b[8] > 1 || b[9] || b[10] || b[11] ||
          e.length != 12 + uint32_t(e.width) * e.height * e.stride) return false;
    }
    entries_.push_back(std::move(e));
    pos += entries_.back().length;
  }
  if (pos != size) return false;
  const Entry* manifest = find("theme.json");
  if (!manifest) {
    error = "Missing theme.json";
    return false;
  }
  std::string json(manifest->length, '\0');
  if (!source_.read(manifest->offset, &json[0], json.size())) return false;
  Theme candidate;
  if (!parseTheme(json, candidate, error)) return false;
  // Assets are validated against the parsed manifest only after parseTheme()
  // succeeds, into a `candidate` that only replaces `theme` on full success --
  // a package that fails asset validation leaves any previously loaded theme
  // untouched.
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
  theme = std::move(candidate);
  error.clear();
  return true;
}
// An asset handle is simply an entry's index into `entries_`; `stride` doubles
// as "is this entry an image, not the manifest" -- the manifest entry always
// has stride 0 and so can never be resolved as an asset.
AssetHandle Package::resolve(const std::string& path) {
  const Entry* entry = find(path);
  return entry && entry->stride ? static_cast<AssetHandle>(entry - entries_.data()) : InvalidAsset;
}
// Reads exactly the `count` pixels of row `y` starting at column `x`,
// unpacking each pixel from its 2- or 3-byte on-disk form (RGB565, plus an
// alpha byte for a transparent asset) -- never more of the image than that.
bool Package::row(AssetHandle handle, int y, int x, int count, uint16_t* colors, uint8_t* alpha) {
  if (handle >= entries_.size()) return false;
  const Entry* e = &entries_[handle];
  if (!e->stride || x < 0 || y < 0 || y >= e->height || count < 0 || count > 240 || x + count > e->width) return false;
  uint8_t bytes[720];
  if (!source_.read(e->offset + 12 + (uint32_t(y) * e->width + x) * e->stride, bytes, count * e->stride)) return false;
  for (int i = 0; i < count; ++i) {
    colors[i] = u16(bytes + i * e->stride);
    alpha[i] = e->stride == 3 ? bytes[i * 3 + 2] : 255;
  }
  return true;
}
}
#endif
