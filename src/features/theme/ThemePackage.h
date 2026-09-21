#pragma once
#include "ThemeEngine.h"
namespace smalltv {
constexpr uint32_t MaxPackage = 3*1024*1024;
constexpr size_t MaxEntries = 256;
class Source {
 public:
  virtual ~Source() = default;
  virtual uint32_t size() const = 0;
  virtual bool read(uint32_t offset,void* dest,size_t count) = 0;
};
// Container: STH1, u16 count, u16 zero; records: u16 path length,
// u32 payload length, ASCII path, payload. All integers little-endian.
// Image: STI1, u16 width, u16 height, u8 alpha flag, 3 zero bytes,
// row-major little-endian RGB565 pixels (plus alpha8 when flag=1).
class Package : public Assets {
 public:
  explicit Package(Source& source) : source_(source) {}
  bool load(Theme& theme,std::string& error);
  AssetHandle resolve(const std::string& path) override;
  using Assets::row;
  bool row(AssetHandle handle,int y,int x,int count,uint16_t* colors,uint8_t* alpha) override;
 private:
  struct Entry {
    std::string path;
    uint32_t offset=0,length=0;
    uint16_t width=0,height=0;
    uint8_t stride=0;
  };
  Source& source_;
  std::vector<Entry> entries_;
  const Entry* find(const std::string& path) const;
};
}
