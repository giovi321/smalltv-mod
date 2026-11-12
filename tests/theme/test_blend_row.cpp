// Assets::blendRow(): an image row drawn over the pixels below it, so render()
// needs no colour and alpha row buffers (they were 720 bytes of the 4 KB loop
// stack, TRAPS T12). The answer must be the same, pixel for pixel, as reading
// the row with row() and blending it by hand.
#include "ThemePackage.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace smalltv;
struct Memory : Source {
  std::string data;
  uint32_t size() const override { return data.size(); }
  bool read(uint32_t offset, void* dest, size_t count) override {
    if (offset > data.size() || count > data.size() - offset) return false;
    memcpy(dest, data.data() + offset, count);
    return true;
  }
};
static void u16(std::string& s, unsigned n) { s += char(n); s += char(n >> 8); }
static void u32(std::string& s, unsigned n) { u16(s, n); u16(s, n >> 16); }
static void entry(std::string& s, const std::string& path, const std::string& data) {
  u16(s, path.size()); u32(s, data.size()); s += path; s += data;
}
// The oracle: each 5/6/5 channel is (fg * a + bg * (255 - a) + 127) / 255.
static uint16_t over(uint16_t bg, uint16_t fg, unsigned a) {
  const unsigned b = 255 - a;
  return uint16_t(((((fg >> 11) * a + (bg >> 11) * b + 127) / 255) << 11) |
                  (((((fg >> 5) & 63) * a + ((bg >> 5) & 63) * b + 127) / 255) << 5) |
                  ((fg & 31) * a + (bg & 31) * b + 127) / 255);
}
static const char* manifest =
    R"({"spec":2,"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"layers":[{"id":"bg","type":"image","x":0,"y":0,"source":"images/bg.png"}]})";
static std::string packageOf(const std::string& image) {
  std::string s = "STH1";
  u16(s, 2); u16(s, 0);
  entry(s, "theme.json", manifest);
  entry(s, "images/bg.png.sti", image);
  return s;
}
// Backgrounds with every channel at different values from pixel to pixel.
static uint16_t background(int kind, int i) {
  if (kind == 0) return 0x0000;
  if (kind == 1) return 0xffff;
  return uint16_t((i * 2711 + 0x1357) ^ (i * 40503));
}
// Draws every row of the image in `bytes` at many windows, over three
// backgrounds, and compares blendRow() with row() plus the oracle.
// Returns the number of comparisons.
static int compareImage(const std::string& bytes, int width, int height) {
  Memory m;
  m.data = packageOf(bytes);
  Package p(m);
  Theme theme;
  std::string error;
  assert(p.load(theme, error));
  const AssetHandle handle = p.resolve("images/bg.png.sti");
  assert(handle != InvalidAsset);
  const int windows[][2] = {{0, width}, {1, width - 1}, {0, 1}, {width - 1, 1}, {3, width - 7}, {width / 2, width - width / 2}, {5, 0}};
  int compared = 0;
  for (int y = 0; y < height; ++y) {
    for (const auto& w : windows) {
      const int x = w[0], count = w[1];
      if (count < 0) continue;   // the 5-pixel image has no window of width - 7
      uint16_t colors[240];
      uint8_t alpha[240];
      assert(p.row(handle, y, x, count, colors, alpha));
      for (int kind = 0; kind < 3; ++kind) {
        uint16_t pixels[246];
        for (int i = 0; i < 246; ++i) pixels[i] = background(kind, i);
        assert(p.blendRow(handle, y, x, count, pixels));
        for (int i = 0; i < count; ++i) assert(pixels[i] == over(background(kind, i), colors[i], alpha[i]));
        // Nothing past the pixels asked for is touched.
        for (int i = count; i < 246; ++i) assert(pixels[i] == background(kind, i));
        ++compared;
      }
    }
    // The same refusals as row(): outside the image, or too many pixels.
    uint16_t pixels[246] = {};
    assert(!p.blendRow(handle, y, 1, width, pixels));
    assert(!p.blendRow(handle, y, -1, 1, pixels));
    assert(!p.blendRow(handle, -1, 0, 1, pixels));
  }
  uint16_t pixels[8] = {};
  assert(!p.blendRow(handle, height, 0, 1, pixels));
  assert(!p.blendRow(InvalidAsset, 0, 0, 1, pixels));
  return compared;
}
static std::string fileOf(const std::string& name) {
  std::ifstream in("tests/theme/sti/" + name, std::ios::binary);
  assert(in);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
// The shared test images (STI1 and STI2, direct and with a palette, with and
// without alpha): 70 pixels wide, so rows cross the 64-byte read window.
static void testSharedImages() {
  int compared = 0;
  const struct { const char* name; int width, height; } files[] = {
      {"alpha-sti1.sti", 70, 5}, {"alpha-sti2-direct.sti", 70, 5}, {"alpha-sti2-indexed.sti", 70, 5},
      {"opaque-sti1.sti", 70, 4}, {"opaque-sti2-direct.sti", 70, 4}, {"opaque-sti2-indexed.sti", 70, 4}};
  for (const auto& f : files) compared += compareImage(fileOf(f.name), f.width, f.height);
  assert(compared == 3 * 7 * (5 + 5 + 5 + 4 + 4 + 4));
}
// A hand-made STI2 row of 5 pixels with every run kind: transparent, literal
// (full alpha and half alpha) and a repeat. A transparent pixel leaves what is
// below it alone, a half-transparent one mixes.
static void testRunKinds() {
  std::string row;
  row += char(0x01);                                                       // 2 transparent
  row += char(0x81); u16(row, 0xf800); row += char(255); u16(row, 0x07e0); row += char(128);   // 2 literal
  row += char(0x40); u16(row, 0x001f); row += char(255);                   // 1 repeated
  std::string image = "STI2";
  u16(image, 5); u16(image, 1); image += char(1); image.append(3, '\0');
  u32(image, 0);
  image += row;
  assert(compareImage(image, 5, 1) == 3 * 6);
  Memory m;
  m.data = packageOf(image);
  Package p(m);
  Theme theme;
  std::string error;
  assert(p.load(theme, error));
  uint16_t pixels[5] = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555};
  assert(p.blendRow(p.resolve("images/bg.png.sti"), 0, 0, 5, pixels));
  assert(pixels[0] == 0x1111 && pixels[1] == 0x2222 && pixels[2] == 0xf800 && pixels[3] == over(0x4444, 0x07e0, 128) && pixels[4] == 0x001f);
}
// ---- render() draws images through blendRow(), not through row() ----
struct Pixels : Display {
  uint16_t data[240 * 240]{};
  uint8_t glyphColumn(uint8_t, uint8_t) const override { return 0; }
  void row(int x, int y, const uint16_t* p, int n) override { for (int i = 0; i < n; ++i) data[y * 240 + x + i] = p[i]; }
};
// One image pixel per column and row: colour 0xf800 at alpha = 40 * column + 7 * row.
static uint8_t patternAlpha(int column, int row) { return uint8_t(40 * column + 7 * row); }
static void patternRow(int y, int x, int count, uint16_t* c, uint8_t* a) {
  for (int i = 0; i < count; ++i) { c[i] = 0xf800; a[i] = patternAlpha(x + i, y); }
}
struct Both : Assets {   // answers both calls; counts which one render() uses
  int rowCalls = 0, blendCalls = 0;
  AssetHandle resolve(const std::string&) override { return 0; }
  bool row(AssetHandle, int y, int x, int count, uint16_t* c, uint8_t* a) override { ++rowCalls; patternRow(y, x, count, c, a); return true; }
  bool blendRow(AssetHandle, int y, int x, int count, uint16_t* pixels) override {
    ++blendCalls;
    uint16_t c[8]; uint8_t a[8];
    patternRow(y, x, count, c, a);
    for (int i = 0; i < count; ++i) pixels[i] = over(pixels[i], c[i], a[i]);
    return true;
  }
};
struct OnlyRow : Assets {   // an Assets that knows nothing of blendRow(): the base class serves it
  AssetHandle resolve(const std::string&) override { return 0; }
  bool row(AssetHandle, int y, int x, int count, uint16_t* c, uint8_t* a) override { patternRow(y, x, count, c, a); return true; }
};
struct FailsLate : Assets {   // reads fail from column 16 on
  AssetHandle resolve(const std::string&) override { return 0; }
  bool row(AssetHandle, int y, int x, int count, uint16_t* c, uint8_t* a) override {
    if (x >= 16) return false;
    patternRow(y, x, count, c, a);
    return true;
  }
};
static Theme imageTheme() {
  Theme theme;
  theme.spec = 2; theme.background = 0x07e0;
  Layer image;
  image.type = LayerType::Image; image.x = 10; image.y = 5; image.width = 6; image.height = 3; image.source = "img";
  theme.layers = {image};
  return theme;
}
static void testRenderUsesBlendRow() {
  Both assets;
  Pixels d;
  Engine e;
  e.setTheme(imageTheme(), 0);
  assert(render(e, e.update(0, nullptr), assets, d));
  assert(assets.blendCalls == 3 && assets.rowCalls == 0);   // one call per image row, never row()
  for (int y = 0; y < 8; ++y)
    for (int x = 8; x < 18; ++x) {
      const bool inside = y >= 5 && x >= 10 && x < 16;
      assert(d.data[y * 240 + x] == (inside ? over(0x07e0, 0xf800, patternAlpha(x - 10, y - 5)) : 0x07e0));
    }
}
static void testBaseClassBlendRow() {
  OnlyRow assets;
  Pixels d;
  Engine e;
  e.setTheme(imageTheme(), 0);
  assert(render(e, e.update(0, nullptr), assets, d));
  for (int y = 5; y < 8; ++y)
    for (int x = 10; x < 16; ++x) assert(d.data[y * 240 + x] == over(0x07e0, 0xf800, patternAlpha(x - 10, y - 5)));
  // A window longer than the base class's own piece, and one that does not fill it.
  for (int count : {1, 15, 16, 17, 33, 240}) {
    uint16_t pixels[240];
    for (int i = 0; i < 240; ++i) pixels[i] = background(2, i);
    assert(assets.blendRow(0, 4, 3, count, pixels));
    for (int i = 0; i < 240; ++i) assert(pixels[i] == (i < count ? over(background(2, i), 0xf800, patternAlpha(3 + i, 4)) : background(2, i)));
  }
}
// A read that fails in the second piece fails the whole row.
static void testBaseClassFailure() {
  FailsLate assets;
  uint16_t pixels[40] = {};
  assert(assets.blendRow(0, 0, 0, 16, pixels));
  assert(!assets.blendRow(0, 0, 0, 17, pixels));
  assert(!assets.blendRow(0, 0, 0, 40, pixels));
}
int main() {
  testSharedImages();
  testBaseClassFailure();
  testRunKinds();
  testRenderUsesBlendRow();
  testBaseClassBlendRow();
  std::cout << "blend row tests passed\n";
}
