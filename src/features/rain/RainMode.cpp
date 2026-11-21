#include "RainMode.h"
#include <Arduino_GFX_Library.h>
#include <font/glcdfont.h>      // the built-in 5x7 font print() uses (texts are rasterized here)
#include <LittleFS.h>
#include <math.h>
#include <time.h>
#include <new>
#include "Gfx.h"
#include "Clock.h"
#include "RainClient.h"
#include "rain_cities.h"
#if !defined(WITH_RAIN) || WITH_RAIN   // compiled out with -D WITH_RAIN=0

RainMode g_rainMode;

// Radar geometry (square 240x240 panel; the circle leaves the corners empty).
static const int CX = TFT_WIDTH / 2;
static const int CY = TFT_HEIGHT / 2;
static const int RR = 112;                 // outer ring radius, px


// Text with a 1 px black outline: readable over any rain colour (the field
// runs from dark blue to bright yellow and white), without hiding the radar
// the way a filled box would.
// Headers, north and timeline labels in light grey; city names in white.
#define C_RAIN_TEXT gfxTint(0xC618)

// Nothing over the frame ever flickers. The frame's blit covers the whole
// screen, so anything drawn over it afterwards (the scope rings, crosshair,
// city markers, timeline and texts) vanished and came back at every
// animation step; and an outline drawn as the text printed in black once per
// neighbour direction left a black block for ~100 ms. Instead, the layers
// are recorded first as a display list, then replayed onto each pair of
// rows of the frame on its way to the panel (BandGfx below), so the frame
// and everything over it arrive together, row by row, in their final
// colours. Shapes are replayed with Arduino_GFX's own algorithms, and texts
// are rasterized from the library's built-in 5x7 font (the glyphs and
// scaling print() uses), with a 1 px black outline: the screen looks
// exactly as when they were drawn on the panel one after the other.
// Without a frame (black screen), or without heap for the list, the layers
// are drawn on the panel directly.
#define RAIN_OV_MAX 16          // texts on screen: 13 at most (2 header, N, 6 cities, 3 timeline, credit)
#define RAIN_OV_LEN 24
#define RAIN_DL_MAX 96          // shapes + texts (city markers can exceed 24 when crowded)
struct OvText {
  int16_t  x, y;
  uint16_t color;
  uint8_t  size, len;
  char     s[RAIN_OV_LEN];
};
enum DlType : uint8_t { DL_CIRCLE, DL_FILLCIRCLE, DL_VLINE, DL_HLINE, DL_DIAMOND, DL_RRECT, DL_TEXT };
struct DlOp {
  DlType   type;
  uint8_t  text;               // DL_TEXT: index into Overlay::t
  uint16_t color;
  int16_t  x, y, w, h, r;      // per type, as the Arduino_GFX call takes them
  int16_t  y0, y1;             // rows the op can touch: skipped by the bands outside
};
struct Overlay {
  uint8_t n, nt;
  bool    full;                // more ops than fit: the caller falls back to drawing directly
  DlOp    op[RAIN_DL_MAX];
  OvText  t[RAIN_OV_MAX];
};
struct CityCache;
// The mode's state, allocated while rain is in use (viewInit) and freed when
// it is turned off (viewFree): with rain off, nothing of it stays in RAM.
struct RainView {
  // Fingerprint of the settings that shape the frames (home + range): an
  // invalidate() resets the fetch only when they changed. A config save that
  // does not touch the radar (brightness, tickers, carousel, uiScale...) must
  // not cancel a running fetch: a full one takes ~5-6 min, downloads
  // everything again, and the web server barely answers meanwhile.
  float    fetchLat = 0.0f;
  float    fetchLon = 0.0f;
  int16_t  fetchRange = -1;
  uint32_t renderedOk = 0xFFFFFFFF;
  bool     renderedError = false;
  bool     renderedStale = false;
  bool     needRender = true;
  uint8_t  animIdx = 0;              // frame shown in the loop
  uint32_t nextAnimMs = 0;
  // Render cost of the last draw, exposed via /api/status.
  uint32_t drawMs = 0, blitMs = 0, citiesMs = 0;
  Overlay*   ov = nullptr;           // set while the layers are being recorded
  CityCache* cities = nullptr;       // see CityCache; given back by rainModeFree()
};
static RainView* V = nullptr;
// The display list being recorded, if any.
static Overlay* curOv() { return V ? V->ov : nullptr; }

// Glyph pixel of text t at screen (x, y)? Same layout as Arduino_GFX's
// classic font: 6*size px per character, 5 glyph columns, 8 rows, LSB on top.
static bool ovOn(const OvText& t, int x, int y) {
  const int dx = x - t.x, dy = y - t.y;
  if (dx < 0 || dy < 0 || dy >= 8 * t.size) return false;
  const int cw = 6 * t.size;
  const int ci = dx / cw;
  if (ci >= t.len) return false;
  const int col = (dx - ci * cw) / t.size;
  if (col >= 5) return false;
  const uint8_t line = pgm_read_byte(&font[(uint8_t)t.s[ci] * 5 + col]);
  return (line >> (dy / t.size)) & 1;
}

// 0 = background, 1 = outline (black), 2 = glyph (text colour).
static uint8_t ovClass(const OvText& t, int x, int y) {
  if (ovOn(t, x, y)) return 2;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if ((dx || dy) && ovOn(t, x + dx, y + dy)) return 1;
  return 0;
}

// Writes rows ya..yb of a text with its outline, in runs of final colours.
static void ovDraw(Arduino_GFX* gfx, const OvText& t, int ya, int yb) {
  int x0 = t.x - 1, x1 = t.x + t.len * 6 * t.size;
  int y0 = t.y - 1, y1 = t.y + 8 * t.size;
  if (x0 < 0) x0 = 0;
  if (x1 > TFT_WIDTH - 1) x1 = TFT_WIDTH - 1;
  if (y0 < ya) y0 = ya;
  if (y1 > yb) y1 = yb;
  const uint16_t cols[3] = {0, C_BLACK, t.color};
  gfx->startWrite();
  for (int y = y0; y <= y1; y++) {
    int runX = x0;
    uint8_t runC = ovClass(t, x0, y);
    for (int x = x0 + 1; x <= x1 + 1; x++) {
      uint8_t c = (x <= x1) ? ovClass(t, x, y) : 0xFF;
      if (c == runC) continue;
      if (runC) gfx->writeFastHLine(runX, y, x - runX, cols[runC]);
      runX = x;
      runC = c;
    }
  }
  gfx->endWrite();
}

static void dlDiamond(Arduino_GFX* gfx, int x, int y, int h, uint16_t c) {
  gfx->drawLine(x - h, y, x, y - h, c);
  gfx->drawLine(x, y - h, x + h, y, c);
  gfx->drawLine(x + h, y, x, y + h, c);
  gfx->drawLine(x, y + h, x - h, y, c);
}

static void dlOne(Arduino_GFX* gfx, const DlOp& o) {   // shapes only (texts: ovDraw)
  switch (o.type) {
    case DL_CIRCLE:     gfx->drawCircle(o.x, o.y, o.r, o.color); break;
    case DL_FILLCIRCLE: gfx->fillCircle(o.x, o.y, o.r, o.color); break;
    case DL_VLINE:      gfx->drawFastVLine(o.x, o.y, o.h, o.color); break;
    case DL_HLINE:      gfx->drawFastHLine(o.x, o.y, o.w, o.color); break;
    case DL_DIAMOND:    dlDiamond(gfx, o.x, o.y, o.r, o.color); break;
    case DL_RRECT:      gfx->drawRoundRect(o.x, o.y, o.w, o.h, o.r, o.color); break;
    case DL_TEXT:       break;
  }
}

// Replays the ops that reach rows ya..yb onto gfx (the panel or a band).
static void dlReplay(Arduino_GFX* gfx, const Overlay* ov, int ya, int yb) {
  for (uint8_t i = 0; i < ov->n; i++) {
    const DlOp& o = ov->op[i];
    if (o.y1 < ya || o.y0 > yb) continue;
    if (o.type == DL_TEXT) ovDraw(gfx, ov->t[o.text], ya, yb);
    else dlOne(gfx, o);
  }
}

// Records one op while the layers are recorded, else draws it now.
static void dlOp(Arduino_GFX* gfx, DlType type, int x, int y, int w, int h, int r, uint16_t color,
                 int y0, int y1) {
  if (Overlay* ov = curOv()) {
    if (ov->n >= RAIN_DL_MAX) { ov->full = true; return; }
    DlOp& o = ov->op[ov->n++];
    o.type = type; o.text = 0; o.color = color;
    o.x = (int16_t)x; o.y = (int16_t)y; o.w = (int16_t)w; o.h = (int16_t)h; o.r = (int16_t)r;
    o.y0 = (int16_t)y0; o.y1 = (int16_t)y1;
    return;
  }
  const DlOp o = {type, 0, color, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r,
                  (int16_t)y0, (int16_t)y1};
  dlOne(gfx, o);   // not recording: draw it now, same code path
}
static void dlCircle(Arduino_GFX* g, int x, int y, int r, uint16_t c)     { dlOp(g, DL_CIRCLE, x, y, 0, 0, r, c, y - r, y + r); }
static void dlFillCircle(Arduino_GFX* g, int x, int y, int r, uint16_t c) { dlOp(g, DL_FILLCIRCLE, x, y, 0, 0, r, c, y - r, y + r); }
static void dlVLine(Arduino_GFX* g, int x, int y, int h, uint16_t c)      { dlOp(g, DL_VLINE, x, y, 0, h, 0, c, y, y + h - 1); }
static void dlHLine(Arduino_GFX* g, int x, int y, int w, uint16_t c)      { dlOp(g, DL_HLINE, x, y, w, 0, 0, c, y, y); }
static void dlDiamondOp(Arduino_GFX* g, int x, int y, int h, uint16_t c)  { dlOp(g, DL_DIAMOND, x, y, 0, 0, h, c, y - h, y + h); }
static void dlRoundRect(Arduino_GFX* g, int x, int y, int w, int h, int r, uint16_t c) {
  dlOp(g, DL_RRECT, x, y, w, h, r, c, y, y + h - 1);
}

static void textOutlined(Arduino_GFX* gfx, int x, int y, const char* str, uint8_t size, uint16_t color) {
  OvText t;
  t.x = (int16_t)x; t.y = (int16_t)y; t.color = color; t.size = size;
  size_t len = strlen(str);
  t.len = (uint8_t)(len < RAIN_OV_LEN ? len : RAIN_OV_LEN);
  memcpy(t.s, str, t.len);
  Overlay* ov = curOv();
  if (!ov) { ovDraw(gfx, t, 0, TFT_HEIGHT - 1); return; }
  if (ov->nt >= RAIN_OV_MAX || ov->n >= RAIN_DL_MAX) { ov->full = true; return; }
  ov->t[ov->nt] = t;
  DlOp& o = ov->op[ov->n++];
  o.type = DL_TEXT; o.text = ov->nt++; o.color = color;
  o.x = t.x; o.y = t.y; o.w = o.h = o.r = 0;
  o.y0 = (int16_t)(y - 1); o.y1 = (int16_t)(y + 8 * size);
}

static void textOutlinedCentered(Arduino_GFX* gfx, const char* str, int y, uint8_t size, uint16_t color) {
  int x = (TFT_WIDTH - gfxTextW(str, size)) / 2;
  textOutlined(gfx, x < 0 ? 0 : x, y, str, size, color);
}

// dist/bearing polar -> screen xy (bearing 0 = N = up, cw).
static void polar(float distPx, float brgDeg, int& x, int& y) {
  float a = brgDeg * (float)PI / 180.0f;
  x = CX + (int)lroundf(distPx * sinf(a));
  y = CY - (int)lroundf(distPx * cosf(a));
}

// Flat-earth home-relative dist/bearing (good enough at radar ranges).
static void geo(float homeLat, float homeLon, float lat, float lon,
                float& distKm, float& brg) {
  float dLat = (lat - homeLat) * 111.0f;
  float dLon = (lon - homeLon) * 111.0f * cosf(homeLat * (float)PI / 180.0f);
  distKm = sqrtf(dLat * dLat + dLon * dLon);
  brg = atan2f(dLon, dLat) * 180.0f / (float)PI;
  if (brg < 0) brg += 360.0f;
}

// UI size from the settings: 0 = small, 1 = medium (default), 2 = large.
// The built-in font only scales in integer steps (6x8, 12x16, 18x24), so
// the main texts map 1:1 onto uiScale: size 1/2/3. Headers and city names
// cap at size 2 (see drawRain / drawCities): a 24 px header would cross the
// scope ring in the corners, and an 11-letter city name would
// span 198 px at size 3. k scales the city squares: medium draws them 3x
// the old 5/3 px markers (15/9 px), large one step bigger again (20/12 px)
// so the level steps are unmistakable.
static uint8_t rainTxt(const Settings& s) { return 1 + s.rain.uiScale; }
static float   rainK(const Settings& s) {
  return (s.rain.uiScale == 0) ? 0.65f : ((s.rain.uiScale == 2) ? 4.0f : 3.0f);
}
static int scaleK(int base, float k) {      // min 2 px so markers stay visible
  int v = (int)lroundf(base * k);
  return v < 2 ? 2 : v;
}

// ---- the static radar background (under the rain field) ---------------------
static void drawBase(Arduino_GFX* gfx) {
  gfx->fillScreen(C_BLACK);
}

// ---- the scope lines (over the rain field, like a radar scope) --------------
static void drawScope(const Settings& s, Arduino_GFX* gfx) {
  dlCircle(gfx, CX, CY, RR, C_DGRAY);
  dlCircle(gfx, CX, CY, RR / 2, C_DGRAY);
  dlVLine(gfx, CX, CY - RR, 2 * RR, C_DGRAY);
  dlHLine(gfx, CX - RR, CY, 2 * RR, C_DGRAY);
  textOutlinedCentered(gfx, RAIN_P("N"), CY - RR + 2, rainTxt(s), C_RAIN_TEXT);   // just inside the top of the ring
}

// ---- one frame: LittleFS indices -> RGB565 rows -----------------------------
// A band of the frame (the rows of one LittleFS read) as an Arduino_GFX
// target: the display list is replayed onto it with the library's own
// shape code, which clips to the full screen and lands here; pixels outside
// the band are dropped.
class BandGfx : public Arduino_GFX {
 public:
  BandGfx() : Arduino_GFX(TFT_WIDTH, TFT_HEIGHT) {}
  bool begin(int32_t) override { return true; }
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t c) override {
    if (y >= y0 && y < y0 + rows) buf[(y - y0) * TFT_WIDTH + x] = c;
  }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override {
    if (x < 0 || x >= TFT_WIDTH) return;
    int a = y > y0 ? y : y0, b = (y + h < y0 + rows) ? y + h : y0 + rows;
    for (int i = a; i < b; i++) buf[(i - y0) * TFT_WIDTH + x] = c;
  }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override {
    if (y < y0 || y >= y0 + rows) return;
    int a = x > 0 ? x : 0, b = (x + w < TFT_WIDTH) ? x + w : TFT_WIDTH;
    for (int i = a; i < b; i++) buf[(y - y0) * TFT_WIDTH + i] = c;
  }
  uint16_t* buf = nullptr;
  int16_t   y0 = 0, rows = 0;
};

// Buffers of one blit (~2 KB): malloc'ed for the blit and freed after, so
// the blit holds none of it between renders (and none while the mode is off).
struct BlitBuf {
  uint16_t pal[RAIN_COLOR_COUNT];   // palette tinted through the display colour correction
  uint8_t  idx[TFT_WIDTH * 2];      // two rows per LittleFS read
  uint16_t row[TFT_WIDTH * 2];
  BandGfx  band;
};

// False when there is no heap for the buffers (the caller draws the empty scope).
// `ov`, when set: the layers, replayed onto the rows on their way to the panel.
static bool blitFrame(uint8_t idx, const Overlay* ov) {
  char path[24];
  if (!rainFramePath(idx, path, sizeof path)) return false;
  void* mem = malloc(sizeof(BlitBuf));
  if (!mem) return false;
  BlitBuf* b = new (mem) BlitBuf;   // placement new: BandGfx has a vtable
  b->band.buf = b->row;
  for (int i = 0; i < RAIN_COLOR_COUNT; i++) {
    // Byte-wise copy: 16-bit loads from irom0 raise LoadStoreError on the
    // ESP8266 — memcpy_P works.
    uint8_t col[3];
    memcpy_P(col, kRainColors[i], sizeof col);
    b->pal[i] = gfxTint((uint16_t)(((col[0] >> 3) << 11) | ((col[1] >> 2) << 5) | (col[2] >> 3)));
  }
  File f = LittleFS.open(path, "r");
  if (!f) { b->~BlitBuf(); free(mem); return false; }
  gfxBlitBegin();
  for (int y = 0; y < TFT_HEIGHT; y += 2) {
    int rows = (int)(f.read(b->idx, TFT_WIDTH * 2) / TFT_WIDTH);
    for (int x = 0; x < rows * TFT_WIDTH; x++) {
      uint8_t k = b->idx[x];
      b->row[x] = k < RAIN_COLOR_COUNT ? b->pal[k] : 0;   // a damaged file shows black
    }
    if (ov && rows > 0) {
      b->band.y0 = (int16_t)y;
      b->band.rows = (int16_t)rows;
      dlReplay(&b->band, ov, y, y + rows - 1);
    }
    gfxBlitRow(b->row, rows * TFT_WIDTH);
    if (rows < 2) break;
  }
  gfxBlitEnd();
  f.close();
  b->~BlitBuf();
  free(mem);
  return true;
}

// ---- nearby cities as reference dots (France + Europe, embedded list) ------
// City geometry (projection) depends only on the config, not on the frame:
// computed once per config change, then each render just draws the cached
// list. The 731-city geo loop cost ~82 ms per frame before this cache.
struct RainCityPos {
  int16_t x, y;
  uint16_t popK;
  uint8_t  nameLen;
  const char* name;       // PROGMEM pointer into kRainCities
};
#define RAIN_CITY_CACHE_MAX 64
// ~0.8 KB, malloc'ed on the first render and given back by rainModeFree()
// (mode switched away, big web request): no cache is held while the mode is
// off. It exists only once built (a range with no city at all is still a
// valid cache: n == 0), keyed by what the projection depends on.
struct CityCache {
  float       lat, lon;
  int         range;
  int8_t      scale;
  uint8_t     n;
  RainCityPos pos[RAIN_CITY_CACHE_MAX];
};
void rainModeFree() {
  if (!V) return;
  free(V->cities);
  V->cities = nullptr;
}

static bool buildCityCache(const Settings& s) {
  if (!V->cities) V->cities = (CityCache*)malloc(sizeof(CityCache));
  if (!V->cities) return false;     // no heap: no cities this render
  const float hLat = rainLat(s), hLon = rainLon(s);
  const float range = (float)s.rain.rangeKm;
  CityCache& cc = *V->cities;
  cc.n = 0;
  for (uint16_t i = 0; i < kRainCityCount && cc.n < RAIN_CITY_CACHE_MAX; i++) {
    RainCity c;
    memcpy_P(&c, &kRainCities[i], sizeof c);
    float d, b;
    geo(hLat, hLon, c.lat100 / 100.0f, c.lon100 / 100.0f, d, b);
    if (d > range) continue;
    RainCityPos& p = cc.pos[cc.n++];
    int px, py;
    polar(d / range * RR, b, px, py);
    p.x = (int16_t)px; p.y = (int16_t)py;
    p.popK = c.popK;
    p.name = kRainCities[i].name;
    p.nameLen = (uint8_t)strlen_P(p.name);
  }
  cc.lat = hLat; cc.lon = hLon;      // even with n == 0 the projection is done:
  cc.range = (int)lroundf(range);    // not redone on every render (82 ms for 731 cities)
  cc.scale = (int8_t)s.rain.uiScale;
  return true;
}

static void drawCities(const Settings& s, Arduino_GFX* gfx) {
  if (!s.rain.showCities) return;
  const CityCache* cc = V->cities;
  if (!cc || cc->lat != rainLat(s) || cc->lon != rainLon(s) ||
      cc->range != (int)lroundf((float)s.rain.rangeKm) || cc->scale != (int8_t)s.rain.uiScale)
    if (!buildCityCache(s)) return;
  const uint8_t txt = rainTxt(s);
  const uint8_t nt  = txt > 2 ? 2 : txt;   // names cap at 16 px (see rainTxt)
  const float   k   = rainK(s);
  // Declutter grid per level so the squares never overlap: small keeps a
  // tighter grid (its markers are tiny), large needs more room than medium.
  const int     minD = (s.rain.uiScale == 0) ? 10 : (s.rain.uiScale == 1 ? 16 : 22);

  struct Pt { int16_t x, y; };
  Pt used[24];
  uint8_t nUsed = 0;
  uint8_t labeled = 0;

  for (uint8_t i = 0; i < V->cities->n; i++) {
    const RainCityPos& c = V->cities->pos[i];
    int x = c.x, y = c.y;

    // declutter : minimum distance between two dots
    bool clash = false;
    for (uint8_t j = 0; j < nUsed; j++) {
      int dx = x - used[j].x, dy = y - used[j].y;
      if (dx * dx + dy * dy < minD * minD) { clash = true; break; }
    }
    if (clash) continue;
    if (nUsed < sizeof(used) / sizeof(used[0])) {
      used[nUsed].x = (int16_t)x;
      used[nUsed].y = (int16_t)y;
      nUsed++;
    }

    uint8_t sz = (uint8_t)scaleK((c.popK > 1000) ? 5 : 3, k);   // hollow red diamond (map pin), a square turned 45°
    int16_t h = sz / 2;
    dlDiamondOp(gfx, x, y, h, C_RED);
    if (labeled < 6 && c.popK >= 20) {
      labeled++;
      char name[40];
      uint8_t len = c.nameLen < sizeof name - 1 ? c.nameLen : (uint8_t)(sizeof name - 1);
      memcpy_P(name, c.name, len);   // byte reads only from flash (see blitFrame)
      name[len] = 0;
      int lw = (int)len * GFX_FONT_W * nt;
      int lx = x + sz / 2 + 3;
      if (lx + lw > TFT_WIDTH - 2) lx = x - sz / 2 - 3 - lw;
      if (lx < 2) lx = 2;
      int ly = y - 4 * nt;               // keep the taller label centred on the square
      textOutlined(gfx, lx, ly < 1 ? 1 : ly, name, nt, C_WHITE);
    }
  }
}

// ---- the timeline at the bottom: T-2h -> now --------------------------------
static void drawTimeline(const Settings& s, Arduino_GFX* gfx, uint8_t animIdx) {
  uint8_t n = rainCount();
  if (n < 2) return;
  uint32_t t0 = rainFrameTime(0);
  uint32_t tN = rainFrameTime(n - 1);
  if (tN <= t0) return;

  // Outlined pill across the screen, above the RainViewer attribution
  // (y >= 231), with a dot riding the current frame time on it.
  const int X0 = 6, X1 = TFT_WIDTH - 6, RR = 4, DR = 3, Y = 226;
  const uint8_t txt = rainTxt(s);
  // Black rims on both sides of the outline keep it visible over bright rain.
  dlRoundRect(gfx, X0 - 1, Y - RR - 1, X1 - X0 + 2, 2 * RR + 2, RR + 1, C_BLACK);
  dlRoundRect(gfx, X0 + 1, Y - RR + 1, X1 - X0 - 2, 2 * RR - 2, RR - 1, C_BLACK);
  dlRoundRect(gfx, X0, Y - RR, X1 - X0, 2 * RR, RR, C_GRAY);

  // labels -2h / -1h / now, just above the pill (glyphs are 8/16/24 px)
  const int labY = Y - RR - 8 * txt - 1;
  struct { PGM_P label; uint32_t t; } labs[3] = {
    {PSTR("-2h"), tN - 7200}, {PSTR("-1h"), tN - 3600}, {PSTR("now"), tN}
  };
  for (uint8_t i = 0; i < 3; i++) {
    if (labs[i].t < t0) continue;
    const RainStr label(labs[i].label);
    int x = X0 + (int)((int64_t)(labs[i].t - t0) * (X1 - X0) / (int64_t)(tN - t0));
    int w = gfxTextW(label, txt);
    if (x - w / 2 < 2) x = 2 + w / 2;
    if (x + w / 2 > TFT_WIDTH - 2) x = TFT_WIDTH - 2 - w / 2;
    textOutlined(gfx, x - w / 2, labY, label, txt, C_RAIN_TEXT);
  }

  // dot on the current frame, vertically centred on the pill
  int xc = X0 + (int)((int64_t)(rainFrameTime(animIdx) - t0) * (X1 - X0) / (int64_t)(tN - t0));
  if (xc < X0 + DR) xc = X0 + DR;   // keep the dot fully on the pill
  if (xc > X1 - DR) xc = X1 - DR;
  dlFillCircle(gfx, xc, Y, DR + 1, C_BLACK);
  dlFillCircle(gfx, xc, Y, DR, C_WHITE);
}

// ---- the view ---------------------------------------------------------------
uint32_t rainDrawMs()   { return V ? V->drawMs : 0; }
uint32_t rainBlitMs()   { return V ? V->blitMs : 0; }
uint32_t rainCitiesMs() { return V ? V->citiesMs : 0; }

// Too old to show (the provider stopped answering, or changed): the clock's
// time when it is set, else 0 and RainClient falls back to fetch ages.
static bool rainStaleNow(const Settings& s) {
  uint32_t now = clockSynced() ? (uint32_t)time(nullptr) : 0;
  return rainStale(s.rain.pollSec, now);
}

// Everything over the frame: scope, cities, markers, texts. Run once per
// pass (see RainPass): shapes are skipped while the texts are collected.
static void drawLayers(const Settings& s, Arduino_GFX* gfx, uint8_t animIdx, uint8_t n, bool stale) {
  const uint8_t txt = rainTxt(s);
  // Headers cap at size 2: at size 3 (18x24) "50 km" / "12:30" would cross
  // the scope ring — the corner caps only fit ~3 glyphs at that size.
  const uint8_t ht = txt > 2 ? 2 : txt;

  drawScope(s, gfx);
  uint32_t tCities = millis();
  drawCities(s, gfx);
  V->citiesMs = millis() - tCities;

  // Home marker on top.
  dlFillCircle(gfx, CX, CY, 3, C_GREEN);

  // Header: range (top-left), current frame time (top-right).
  char hdr[16];
  if (s.rain.unitsMi) snprintf_P(hdr, sizeof hdr, PSTR("%d mi"), (int)lroundf(s.rain.rangeKm * 0.621371f));
  else                 snprintf_P(hdr, sizeof hdr, PSTR("%d km"), s.rain.rangeKm);
  textOutlined(gfx, 3, 3, hdr, ht, C_RAIN_TEXT);

  if (n > 0) {
    char ft[10];
    uint32_t t = rainFrameTime(animIdx);
    if (clockSynced()) {
      time_t tt = (time_t)t;
      struct tm tmv;
      localtime_r(&tt, &tmv);
      snprintf_P(ft, sizeof ft, PSTR("%02d:%02d"), tmv.tm_hour, tmv.tm_min);
    } else {
      int32_t d = (int32_t)(rainFrameTime(n - 1) - t);
      if (d <= 0) snprintf_P(ft, sizeof ft, PSTR("now"));
      else if (d < 3600) snprintf_P(ft, sizeof ft, PSTR("-%ldm"), (long)(d / 60));
      else snprintf_P(ft, sizeof ft, PSTR("-%ldh"), (long)(d / 3600));
    }
    textOutlined(gfx, TFT_WIDTH - gfxTextW(ft, ht) - 3, 3, ft, ht, C_RAIN_TEXT);
  } else if (stale || (rainError() && !rainFetching())) {
    // Old frames or none, and the last fetch did not bring new ones: say so
    // rather than loop an old picture (the free API may change or stop).
    // Two 16 px lines above the home marker, the reason just below it.
    gfxDrawCentered(RAIN_P("Radar data"), CY - 44, 2, C_YELLOW);
    gfxDrawCentered(RAIN_P("unavailable"), CY - 26, 2, C_YELLOW);
    char stage[32];
    if (rainError()) gfxDrawCentered(rainStageName(stage, sizeof stage), CY + 10, 1, C_GRAY);
  } else {
    gfxDrawCentered(RAIN_P("Waiting for"), CY - 44, 2, C_GRAY);
    gfxDrawCentered(RAIN_P("radar data"), CY - 26, 2, C_GRAY);
  }

  if (n > 0) drawTimeline(s, gfx, animIdx);

  // Attribution + error dot (bottom corners, outside the ring). The
  // attribution stays at size 1 on purpose: at size 2 it would span 132 px
  // and crowd the timeline pill.
  const RainStr credit(PSTR("RainViewer"));
  textOutlined(gfx, TFT_WIDTH - gfxTextW(credit, 1) - 2, TFT_HEIGHT - 9, credit, 1, C_GRAY);
  if (rainError()) dlFillCircle(gfx, 6, TFT_HEIGHT - 7, 4, C_RED);
}

static void drawRain(const Settings& s, uint8_t animIdx) {
  uint32_t tDraw = millis();
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  // A stale set is not drawn at all: the scope says the data is unavailable.
  const bool stale = rainStaleNow(s);
  uint8_t n = stale ? 0 : rainCount();

  // Over a frame, the layers are recorded first and go out inside the blit
  // (see Overlay). Without heap for the list they are drawn after it.
  Overlay* ov = nullptr;
  if (n > 0) {
    ov = (Overlay*)malloc(sizeof(Overlay));
    if (ov) {
      ov->n = 0;
      ov->nt = 0;
      ov->full = false;
      V->ov = ov;
      drawLayers(s, gfx, animIdx, n, stale);
      V->ov = nullptr;
      if (ov->full) { free(ov); ov = nullptr; }   // incomplete list: draw directly
    }
  }

  // No black background when a frame exists: it covers the whole screen, and
  // a fillScreen before the blit flashed black on every animation step. The
  // black is only for the "Waiting for radar data" state. Order matters:
  // drawBase after blitFrame would erase the frame with its fillScreen.
  uint32_t tBlit = millis();
  bool blitted = n > 0 && blitFrame(animIdx, ov);
  if (!blitted) drawBase(gfx);
  V->blitMs = millis() - tBlit;

  if (!ov) drawLayers(s, gfx, animIdx, n, stale);              // drawn on the panel
  else if (!blitted) dlReplay(gfx, ov, 0, TFT_HEIGHT - 1);      // no frame: over black
  free(ov);
  V->drawMs = millis() - tDraw;
}

// ---- DisplayMode ------------------------------------------------------------
// Rain is in use when it is the mode, or ticked in the carousel. Otherwise its
// frames are deleted (rainRemoveAll) and its state freed, at boot and after
// every settings save: rain off costs no heap and almost no static RAM.
static bool rainInUse(const Settings& s) {
  return s.mode == MODE_RAIN || (s.mode == MODE_CAROUSEL && s.carouselRain);
}

// The view for rain in use: fingerprint of the settings that shape the
// frames, a full repaint, the loop from its first frame. False: no heap,
// the next service() tries again.
static bool viewInit(const Settings& s) {
  if (!V) V = new (std::nothrow) RainView();
  if (!V) return false;
  V->fetchLat = rainLat(s);
  V->fetchLon = rainLon(s);
  V->fetchRange = (int16_t)lroundf((float)s.rain.rangeKm);
  V->renderedOk = 0xFFFFFFFF;
  V->renderedError = false;
  V->needRender = true;
  V->animIdx = 0;
  return true;
}
static void viewFree() {
  rainModeFree();
  delete V;
  V = nullptr;
}

void RainMode::begin(const Settings& s) {
  if (!rainInUse(s)) {
    rainRemoveAll();     // also any /rain an earlier boot left behind
    viewFree();
    return;
  }
  rainInit(s);
  viewInit(s);
}

void RainMode::invalidate(const Settings& s) {
  if (!rainInUse(s)) {   // turned off: give the disk and the heap back
    rainRemoveAll();
    viewFree();
    return;
  }
  if (!V) {              // turned on: as at boot, the first service() fetches
    rainInit(s);
    viewInit(s);
    return;
  }
  // Re-init + immediate re-fetch only when what shapes the frames changed
  // (home or range: the crop and the centring). Any other config save (mode,
  // brightness, tickers, uiScale...) leaves the running fetch alone: the
  // rendering reads the Settings live and the city cache re-keys itself.
  // Without this guard every POST /api/config and every press of the "Home"
  // button cancelled a running fetch and started a new one at once (~5-6 min
  // of fetch restarted from zero, the web server barely answering).
  const float lat = rainLat(s), lon = rainLon(s);
  const int16_t range = (int16_t)lroundf((float)s.rain.rangeKm);
  if (lat != V->fetchLat || lon != V->fetchLon || range != V->fetchRange) {
    V->fetchLat = lat;
    V->fetchLon = lon;
    V->fetchRange = range;
    rainInit(s);
    rainForceRefresh();
    V->animIdx = 0;
  }
  V->renderedOk = 0xFFFFFFFF;
  V->needRender = true;
}

void RainMode::wake(const Settings& s) {
  if (V) V->needRender = true;   // repaint only
}

static void render(const Settings& s) {
  if (rainLat(s) == 0.0f && rainLon(s) == 0.0f) {
    gfxMessage(RAIN_P("Rain radar"), RAIN_P("Set home location"), C_YELLOW);
    return;
  }
  uint8_t n = rainCount();
  if (n > 0 && V->animIdx >= n) V->animIdx = 0;
  drawRain(s, V->animIdx);
}

void RainMode::service(const Settings& s) {
  if (!V && !viewInit(s)) return;   // no heap for the view yet: try again next tick
  // A fetch step may need every byte (the ESP8266 standard image): the city
  // cache goes back first, the next render rebuilds it.
  if (rainFetching()) rainModeFree();
  rainService(s);

  uint32_t ok = rainLastOkMs();
  bool err = rainError();
  bool stale = rainStaleNow(s);       // flips with time alone, checked every tick
  if (ok != V->renderedOk || err != V->renderedError || stale != V->renderedStale) {
    V->renderedOk = ok;
    V->renderedError = err;
    V->renderedStale = stale;
    V->needRender = true;
  }

  // Animation: loop T-2h -> now between refreshes, one frame per animSec
  // (none while the set is too old to show).
  uint8_t n = rainCount();
  if (!stale && n > 1 && (int32_t)(millis() - V->nextAnimMs) >= 0) {
    V->animIdx = (uint8_t)((V->animIdx + 1) % n);
    V->nextAnimMs = millis() + (uint32_t)s.rain.animSec * 1000UL;
    V->needRender = true;
  }

  if (V->needRender) {
    render(s);
    V->needRender = false;
  }
}

#endif  // WITH_RAIN
