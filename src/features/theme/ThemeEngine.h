#pragma once
// Portable, declarative V1 model. No Arduino, filesystem, or notification policy.
#include "BlockList.h"
#include "Languages.h"
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
// The RAM all non-English texts of one theme may take together (spec 2
// localized text): each entry's size plus its text where it spills to the heap.
constexpr size_t MaxTranslationBytes = 1024;
constexpr size_t MaxFonts = 4;

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

// Every number a layer stores fits in 16 bits: the parser limits coordinates
// to -240..479, sizes to 240 and a scroll pause to 10,000 ms. A running theme
// keeps every layer and its state in RAM, and on the ESP8266 the 16-bit
// fields and 1-byte enums are what leave room for the web server and WiFi.
using Coord = int16_t;

enum class LayerType : uint8_t { Text, Image, Animation, Shape };
// Arc, Hand and Ticks are spec 2 (#31). Angles are whole degrees, 0 at 12 o'clock
// and growing clockwise.
enum class Shape : uint8_t { Rectangle, Circle, Line, Arc, Hand, Ticks };
enum class ScrollMode : uint8_t { Loop, Bounce };

// Declared on a text layer to scroll content wider than its viewport. `width`
// is the viewport, not the text; `gap` only matters in loop mode, where it is
// the blank run between the end of one copy and the start of the next.
struct Scroll {
  bool enabled = false;
  Coord width = 0, speed = 0, pauseMs = 1000, gap = 24;
  ScrollMode mode = ScrollMode::Loop;
};

// Every property a `bind` object may target. Which ones apply to which layer
// type is enforced by `applicable()` in the .cpp, not by this enum.
enum class BoundProperty : uint8_t {
  X, Y, Width, Height, Radius, CornerRadius, X2, Y2, Size, StrokeWidth,
  ScrollWidth, ScrollSpeed, Color, Fill, Stroke, Angle, Sweep
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
// A `source` that names a value the engine knows (spec 2, #31) instead of a
// declared data field. `time.*` comes from the clock handed to update();
// `device.*` is optional and comes from the firmware (setDeviceValues()), so
// the binding keeps its name in `source` and has no enum entry of its own.
enum class BuiltIn : uint8_t {
  None, TimeSecond, TimeMinute, TimeHour, TimeHour12, TimeSecondOfDay, TimeDay, TimeMonth, TimeWeekday, TimeMoon, Device
};
// "time.minute" and the like give their BuiltIn; "device.<name>" for a name
// the spec lists gives Device. Anything else, including a data field, is None.
BuiltIn builtInSource(const std::string& source);
// A `time.*` source's value for a broken-down time.
double timeValue(BuiltIn which, const tm& time);
// One `bind` entry: a single property driven by a single declared data field,
// through either a numeric mapping or a color-stop table (never both).
struct Binding {
  BoundProperty property = BoundProperty::X;
  BuiltIn builtin = BuiltIn::None;
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
  Coord x = 0, y = 0, width = 0, height = 0, x2 = 0, y2 = 0, radius = 0, cornerRadius = 0;
  Coord size = 16, strokeWidth = 1, scrollWidth = 0, scrollSpeed = 0;
  uint16_t color = 0xffff, fill = 0, stroke = 0;
};
bool parseFiniteNumber(const std::string& value, double& number);
int resolveNumericBinding(const NumericBinding& binding, double value, int lo, int hi);
uint16_t resolveColorBinding(const ColorBinding& binding, double value);

// One parsed manifest layer. Fields that do not apply to `type`/`shape` keep
// their defaults and are simply never read by the renderer. The parser checks
// each layer's `id` (valid, unique) but does not keep it: nothing reads it
// once the theme is parsed, and every running layer would carry it in RAM.
struct Layer {
  std::string value, source;
  LayerType type = LayerType::Text;
  Shape shape = Shape::Rectangle;
  Coord x = 0, y = 0, width = 0, height = 0, x2 = 0, y2 = 0, radius = 0;
  // The rotation shapes (spec 2) reuse fields rather than add their own, since
  // every running layer carries a Layer and a LayerState: arc is radius, x2 =
  // start, y2 = sweep; hand is radius = length, cornerRadius = tail, x2 = angle;
  // ticks is radius, x2 = count, y2 = length. A binding writes the resolved x2/y2.
  Coord size = 16, strokeWidth = 1, cornerRadius = 0;
  uint8_t anchorX = 0, anchorY = 0;
  uint16_t color = 0xffff, fill = 0, stroke = 0;
  bool hasFill = false, hasStroke = false, loop = true;
  bool timeBound = false;   // some binding reads a time.* source
  // Spec 2: this layer's other languages are Theme::translations[firstTranslation]
  // and the translationCount - 1 after it; 0 for a layer with a single text.
  uint16_t firstTranslation = 0;
  uint8_t translationCount = 0;
  uint8_t font = 0;   // 1 + index into Theme::fonts; 0 is the built-in font
  uint16_t frames = 1, fps = 1;
  Scroll scroll;
  std::vector<Binding> bindings;
};
// One language's text of a localized text layer.
struct LocalizedText {
  uint8_t language = 0;   // index of Languages.h, never 0 (English is `value`)
  std::string text;
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
// A package font (spec 2, #31), as far as the engine needs it to lay out
// text: every glyph's advance, read from `fonts/<name>.stf` when the package
// is loaded. The bitmaps stay in the package (Assets::glyphRow()), and the
// font's line height becomes the `size` of every text layer that uses it.
struct ThemeFont {
  std::string name;
  uint8_t first = 0, fallback = 0;
  std::vector<uint8_t> advances;   // from code point `first` on; 0: no glyph
  // The code point drawn for byte `c`: `c` itself, or the fallback glyph.
  uint8_t glyph(uint8_t c) const {
    return c >= first && size_t(c - first) < advances.size() && advances[c - first] ? c : fallback;
  }
  int advance(uint8_t c) const {
    const size_t at = size_t(glyph(c) - first);
    return at < advances.size() ? advances[at] : 0;
  }
};
// The immutable parsed manifest. Engine::setTheme() takes this by value and
// never mutates it afterward; fetched values only ever affect LayerState.
struct Theme {
  uint8_t spec = 1;   // manifest version; 2 allows STI2 images (#31)
  std::string id, name, author, version;
  uint16_t background = 0;
  BlockList<Layer> layers;
  // The non-English texts of localized layers, in blocks (issue #30) reserved
  // for exactly their number, at most MaxTranslationBytes of RAM in all;
  // `value` holds the English one.
  BlockList<LocalizedText> translations;
  uint16_t translationBytes = 0;   // their cost so far, as parseTextValue() counts it
  std::vector<ThemeDataSource> data;
  std::vector<ThemeFont> fonts;
};
// Random-access bytes of a manifest. parseTheme() reads it forward in small
// pieces, several times, so a package's manifest never has to be copied into
// RAM as a whole; on the ESP8266 that copy alone could exceed free heap.
class ManifestSource {
 public:
  virtual ~ManifestSource() = default;
  virtual size_t size() const = 0;
  virtual bool read(size_t offset, char* out, size_t count) = 0;
};
bool parseTheme(const std::string& json, Theme& out, std::string& error);
bool parseTheme(ManifestSource& json, Theme& out, std::string& error);
std::string expandText(const std::string& value, const tm* time);
std::string expandText(const std::string& value, const tm* time, const std::vector<ThemeValue>& values);
std::string expandText(const std::string& value, const tm* time, const std::vector<ThemeValue>& values, const std::vector<ThemeValue>& device);
// `language` picks the month and weekday names ({MON}, {WD}, ...); 0 is English.
std::string expandText(const std::string& value, const tm* time, const std::vector<ThemeValue>& values, uint8_t language);
std::string expandText(const std::string& value, const tm* time, const std::vector<ThemeValue>& values, const std::vector<ThemeValue>& device, uint8_t language);
// The text of a layer in `language`: its translation, else the English value.
const std::string& layerText(const Theme& theme, const Layer& layer, uint8_t language);
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
  // Values a firmware provides for `device.*` sources, keyed "device.<name>".
  // A name that is not set leaves its bindings on the layer's static value,
  // like a fetched field that is missing.
  void setDeviceValues(std::vector<ThemeValue> values);
  // The device language (an index of Languages.h). Only a spec 2 theme follows it.
  void setLanguage(uint8_t language);
  // Forces the next update() to report a full-canvas dirty rectangle, e.g.
  // after a notification overlay restores the theme. It never resets scroll
  // progress, only what gets repainted.
  void invalidate() { full_ = true; }
  std::vector<Rect> update(uint32_t now, const tm* time);
  const Theme& theme() const { return theme_; }
  const BlockList<LayerState>& states() const { return states_; }
 private:
  void resolveLayer(size_t index);
  Theme theme_;
  BlockList<LayerState> states_;
  std::vector<ThemeValue> values_, device_;
  bool full_ = true, hadTime_ = false, valuesChanged_ = false, languageChanged_ = false;
  uint8_t language_ = 0;
  tm lastTime_{};
};

// Assets deliver RGB565 and 8-bit opacity; a row never exceeds 240 pixels.
using AssetHandle = uint16_t;
constexpr AssetHandle InvalidAsset = 0xffff;
// The widest ink of one package-font glyph: a glyph lies within its advance,
// and an advance is at most 120 (the package check, Package::checkFont()).
constexpr size_t MaxGlyphWidth = 120;
// Alpha-composites an RGB565 foreground pixel over a background one, blending
// each of the 5/6/5 channels independently with rounding (+127 before the
// /255 integer division). Used for image/animation alpha and a package font's
// coverage -- shapes and built-in font text are fully opaque where drawn.
inline uint16_t blend(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a == 255) return fg;
  if (a == 0) return bg;
  unsigned b = 255 - a;
  return ((((fg >> 11) * a + (bg >> 11) * b + 127) / 255) << 11) |
    (((((fg >> 5) & 63) * a + ((bg >> 5) & 63) * b + 127) / 255) << 5) |
    (((fg & 31) * a + (bg & 31) * b + 127) / 255);
}
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
  // Like row(), but blends the pixels over `pixels[0..count)` (blend()) instead
  // of handing them back, so the caller needs no colour and alpha buffers
  // (720 bytes of the loop stack while a frame is drawn). The base class
  // does it with row() in small pieces; a provider that can read and blend
  // in one pass overrides it.
  virtual bool blendRow(AssetHandle handle, int y, int x, int count, uint16_t* pixels);
  // One row of a package font's glyph: line `y` of the line box, as 0-255
  // coverage for the `width` pixels from `left` within the glyph's advance;
  // `width` is 0 where the glyph has no ink. `coverage` holds MaxGlyphWidth
  // bytes, and no glyph is wider.
  virtual bool glyphRow(AssetHandle, uint8_t, int, uint8_t*, int&, int&) { return false; }
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
