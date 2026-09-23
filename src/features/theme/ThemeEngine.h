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

// Integer pixel rectangle. w/h <= 0 means empty, not "invalid" -- callers rely
// on that to skip drawing without a separate validity flag.
struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  Rect() = default;
  Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
  bool empty() const { return w <= 0 || h <= 0; }
};
Rect intersect(Rect a, Rect b);
Rect unite(Rect a, Rect b);
bool validId(const std::string& id);
bool validPath(const std::string& path);

enum class LayerType { Text, Image, Animation, Shape };
enum class Shape { Rectangle, Circle, Line };
enum class ScrollMode { Loop, Bounce };

// Declared on a text layer to scroll content wider than its viewport. `width`
// is the viewport, not the text; `gap` only matters in loop mode, where it is
// the blank run between the end of one copy and the start of the next.
struct Scroll {
  bool enabled = false;
  int width = 0, speed = 0, pauseMs = 1000, gap = 24;
  ScrollMode mode = ScrollMode::Loop;
};

// Every property a `bind` object may target. Which ones apply to which layer
// type is enforced by `applicable()` in the .cpp, not by this enum.
enum class BoundProperty {
  X, Y, Width, Height, Radius, CornerRadius, X2, Y2, Size, StrokeWidth,
  ScrollWidth, ScrollSpeed, Color, Fill, Stroke
};

// Linear map from a fetched number to an integer property. See
// resolveNumericBinding() for the exact rounding/clamping rule.
struct NumericBinding {
  double input0 = 0, input1 = 1;
  int output0 = 0, output1 = 1;
  bool clamp = true;
};
// One threshold in a ColorBinding. Stops must be inserted in strictly
// increasing `at` order; the parser enforces this, resolveColorBinding()
// relies on it.
struct ColorStop {
  double at = 0;
  uint16_t value = 0;
};
struct ColorBinding {
  std::vector<ColorStop> stops;
};
// One `bind` entry: a single property driven by a single declared data field,
// through either a numeric mapping or a color-stop table (never both).
struct Binding {
  BoundProperty property = BoundProperty::X;
  std::string source;
  bool color = false;
  NumericBinding numeric;
  ColorBinding colors;
};
// Per-frame result of applying a layer's bindings to the latest fetched
// values, falling back to the static Layer fields wherever no binding (or a
// binding with a missing/non-finite source) applies. Never persisted; the
// parsed Theme stays immutable.
struct ResolvedLayer {
  int x = 0, y = 0, width = 0, height = 0, x2 = 0, y2 = 0, radius = 0, cornerRadius = 0;
  int size = 16, strokeWidth = 1, scrollWidth = 0, scrollSpeed = 0;
  uint16_t color = 0xffff, fill = 0, stroke = 0;
};
bool parseFiniteNumber(const std::string& value, double& number);
int resolveNumericBinding(const NumericBinding& binding, double value, int lo, int hi);
uint16_t resolveColorBinding(const ColorBinding& binding, double value);

// One parsed manifest layer. Fields that do not apply to `type`/`shape` keep
// their defaults and are simply never read by the renderer.
struct Layer {
  std::string id, value, source;
  LayerType type = LayerType::Text;
  Shape shape = Shape::Rectangle;
  int x = 0, y = 0, width = 0, height = 0, x2 = 0, y2 = 0, radius = 0;
  int size = 16, anchorX = 0, anchorY = 0, strokeWidth = 1, cornerRadius = 0;
  uint16_t color = 0xffff, fill = 0, stroke = 0;
  bool hasFill = false, hasStroke = false, loop = true;
  uint16_t frames = 1, fps = 1;
  Scroll scroll;
  std::vector<Binding> bindings;
};
struct ThemeDataField {
  std::string id, path;
};
// One periodic JSON endpoint declared by a theme. `interval` is seconds
// between refreshes; `insecureTls` is the explicit opt-in an HTTPS source
// needs because this firmware's TLS path does not validate certificates.
struct ThemeDataSource {
  std::string id, url;
  uint32_t interval = 300;
  bool insecureTls = false;
  std::vector<ThemeDataField> fields;
};
// One `<source>.<field>` value as fetched, always a string; the engine parses
// it back to a number only where a binding or clock token needs one.
struct ThemeValue {
  std::string key, value;
};
inline bool operator==(const ThemeValue& a, const ThemeValue& b) { return a.key == b.key && a.value == b.value; }
// The immutable parsed manifest. Engine::setTheme() takes this by value and
// never mutates it afterward; fetched values only ever affect LayerState.
struct Theme {
  std::string id, name, author, version;
  uint16_t background = 0;
  std::vector<Layer> layers;
  std::vector<ThemeDataSource> data;
};
bool parseTheme(const std::string& json, Theme& out, std::string& error);
std::string expandText(const std::string& value, const tm* time);
std::string expandText(const std::string& value, const tm* time, const std::vector<ThemeValue>& values);
std::string assetPath(const Layer& layer, uint16_t frame);

// Per-layer runtime state: the current resolved geometry/color, the expanded
// text, the animation/scroll schedule, and the last painted bounds (needed to
// erase a layer's old footprint when it moves, shrinks, or changes content).
struct LayerState {
  std::string text;
  uint16_t frame = 0;
  Rect bounds;
  ResolvedLayer resolved;
  uint32_t lastMs = 0, phase = 0;
  // Loop/bounce scroll schedule. `scrollDirection` is -1 while advancing
  // toward the far edge (or continuously, in loop mode) and +1 on a bounce
  // leg's way back. See advanceScroll() in the .cpp for the timing model.
  int scrollOffset = 0, scrollDirection = -1;
  uint32_t scrollPhase = 0, scrollLastMs = 0, scrollPauseUntil = 0;
  bool finished = false;
};

// Owns the parsed Theme and per-layer runtime state. update() is the only
// place that advances time, animation, and scroll, and computes the dirty
// rectangles render() must repaint; it never touches a Display or Assets
// itself, so the engine stays testable without either.
class Engine {
 public:
  void setTheme(Theme theme, uint32_t now);
  void setValues(std::vector<ThemeValue> values);
  // Forces the next update() to report a full-canvas dirty rectangle, e.g.
  // after a notification overlay restores the theme. It never resets scroll
  // progress, only what gets repainted.
  void invalidate() { full_ = true; }
  std::vector<Rect> update(uint32_t now, const tm* time);
  const Theme& theme() const { return theme_; }
  const std::vector<LayerState>& states() const { return states_; }
 private:
  void resolveLayer(size_t index);
  Theme theme_;
  std::vector<LayerState> states_;
  std::vector<ThemeValue> values_;
  bool full_ = true, hadTime_ = false, valuesChanged_ = false;
  tm lastTime_{};
};

// Assets deliver RGB565 and 8-bit opacity; a row never exceeds 240 pixels.
using AssetHandle = uint16_t;
constexpr AssetHandle InvalidAsset = 0xffff;
// Row-based asset provider so render() never needs a whole image or animation
// frame in memory at once -- only the pixel rows a dirty rectangle actually
// touches are read.
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
// Font/output sink render() writes into. glyphColumn() returns the built-in
// bitmap font's column bits for one character; row() streams one already
// composited scanline to the physical display (or, on desktop, a buffer).
class Display {
 public:
  virtual ~Display() = default;
  virtual uint8_t glyphColumn(uint8_t character, uint8_t column) const = 0;
  virtual void row(int x, int y, const uint16_t* pixels, int count) = 0;
};
// Rebuilds each dirty row in layer order; no display readback or full framebuffer.
bool render(const Engine& engine, const std::vector<Rect>& dirty, Assets& assets, Display& display);
}
