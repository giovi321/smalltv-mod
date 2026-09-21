#pragma once
// Portable, declarative V1 model. No Arduino, filesystem, or notification policy.
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace smalltv {
constexpr int CanvasSize = 240;
constexpr size_t MaxManifest = 16384;
constexpr size_t MaxLayers = 32;
constexpr size_t MaxDataSources = 4;
constexpr size_t MaxDataFields = 8;
struct Rect {
  int x=0, y=0, w=0, h=0;
  Rect() = default;
  Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
  bool empty() const { return w<=0 || h<=0; }
};
Rect intersect(Rect a, Rect b);
Rect unite(Rect a, Rect b);
bool validId(const std::string& id);
bool validPath(const std::string& path);
enum class LayerType { Text, Image, Animation, Shape };
enum class Shape { Rectangle, Circle, Line };
struct Layer {
  std::string id, value, source;
  LayerType type=LayerType::Text;
  Shape shape=Shape::Rectangle;
  int x=0, y=0, width=0, height=0, x2=0, y2=0, radius=0;
  int size=16, anchorX=0, anchorY=0, strokeWidth=1;
  uint16_t color=0xffff, fill=0, stroke=0;
  bool hasFill=false, hasStroke=false, loop=true;
  uint16_t frames=1, fps=1;
};
struct ThemeDataField { std::string id, path; };
struct ThemeDataSource { std::string id, url; uint32_t interval=300; std::vector<ThemeDataField> fields; };
struct ThemeValue { std::string key, value; };
inline bool operator==(const ThemeValue& a,const ThemeValue& b) { return a.key==b.key&&a.value==b.value; }
struct Theme {
  std::string id, name, author, version;
  uint16_t background=0;
  std::vector<Layer> layers;
  std::vector<ThemeDataSource> data;
};
bool parseTheme(const std::string& json, Theme& out, std::string& error);
std::string expandText(const std::string& value, const tm* time);
std::string expandText(const std::string& value, const tm* time, const std::vector<ThemeValue>& values);
std::string assetPath(const Layer& layer, uint16_t frame);
struct LayerState {
  std::string text;
  uint16_t frame=0;
  Rect bounds;
  uint32_t lastMs=0, phase=0;
  bool finished=false;
};
class Engine {
 public:
  void setTheme(Theme theme, uint32_t now);
  void setValues(std::vector<ThemeValue> values);
  void invalidate() { full_=true; }
  std::vector<Rect> update(uint32_t now, const tm* time);
  const Theme& theme() const { return theme_; }
  const std::vector<LayerState>& states() const { return states_; }
 private:
  Theme theme_;
  std::vector<LayerState> states_;
  std::vector<ThemeValue> values_;
  bool full_=true, hadTime_=false, valuesChanged_=false;
  tm lastTime_{};
};
// Assets deliver RGB565 and 8-bit opacity; a row never exceeds 240 pixels.
using AssetHandle = uint16_t;
constexpr AssetHandle InvalidAsset = 0xffff;
class Assets {
 public:
  virtual ~Assets() = default;
  // Handles belong to this asset provider and stay valid until it is reloaded.
  virtual AssetHandle resolve(const std::string& path) = 0;
  virtual bool row(AssetHandle handle, int y, int x, int count,
                   uint16_t* colors, uint8_t* alpha) = 0;
  bool row(const std::string& path, int y, int x, int count,
           uint16_t* colors, uint8_t* alpha) {
    return row(resolve(path), y, x, count, colors, alpha);
  }
};
class Display {
 public:
  virtual ~Display() = default;
  virtual uint8_t glyphColumn(uint8_t character, uint8_t column) const = 0;
  virtual void row(int x, int y, const uint16_t* pixels, int count) = 0;
};
// Rebuilds each dirty row in layer order; no display readback or full framebuffer.
bool render(const Engine& engine, const std::vector<Rect>& dirty, Assets& assets, Display& display);
}
