#pragma once
#include "ThemeEngine.h"
namespace smalltv {
constexpr uint32_t MaxPackage = 3 * 1024 * 1024;
constexpr size_t MaxEntries = 256;
class Source {
 public:
  virtual ~Source() = default;
  virtual uint32_t size() const = 0;
  virtual bool read(uint32_t offset, void* dest, size_t count) = 0;
};
// Container: STH1, u16 count, u16 zero; records: u16 path length,
// u32 payload length, ASCII path, payload. All integers little-endian.
// Image: STI1, u16 width, u16 height, u8 alpha flag, 3 zero bytes,
// row-major little-endian RGB565 pixels (plus alpha8 when flag=1).
class Package : public Assets {
 public:
  explicit Package(Source& source) : source_(source) {}
  bool load(Theme& theme, std::string& error);
  AssetHandle resolve(const std::string& path) override;
  using Assets::row;
  bool row(AssetHandle handle, int y, int x, int count, uint16_t* colors, uint8_t* alpha) override;
 private:
  // Every animation frame is an entry, so an entry is kept small: its path
  // is not held in RAM (that took ~45 bytes more per frame), only its FNV-1a
  // hash and length. The path itself stays in the package, just before the
  // payload at `offset`, and indexOf() reads it to confirm a match.
  struct Entry {
    uint32_t hash = 0, offset = 0, length = 0;
    uint16_t width = 0, height = 0;
    uint8_t pathLength = 0, stride = 0;
    bool compressed = false;   // STI2: rows as runs (see row())
    uint8_t palette = 0;       // STI2 palette size, 0 for direct colours
  };
  Source& source_;
  // In blocks: in one vector, a package with many frames needed a block of
  // several KiB.
  BlockList<Entry> entries_;
  bool samePath(const Entry& entry, const std::string& path) const;
  bool checkRows(const Entry& entry) const;
  size_t indexOf(const std::string& path) const;
  const Entry* find(const std::string& path) const;
  bool readIndex(std::string& error);
  bool checkSpec(const Theme& theme, std::string& error) const;
  bool checkAssets(Theme& theme, std::string& error) const;
};
}
