#include "RainMode.h"
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include <math.h>
#include <time.h>
#include "Gfx.h"
#include "Clock.h"
#include "RainClient.h"
#include "rain_cities.h"

RainMode g_rainMode;

// Radar geometry (square 240x240 panel; the circle leaves the corners empty).
static const int CX = TFT_WIDTH / 2;
static const int CY = TFT_HEIGHT / 2;
static const int RR = 112;                 // outer ring radius, px

// Palette tinted through the display colour correction (rebuilt per render).
static uint16_t g_pal[256];

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
// scope ring in the corners, and the longest city name (Châteauroux) would
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
  gfx->drawCircle(CX, CY, RR, C_DGRAY);
  gfx->drawCircle(CX, CY, RR / 2, C_DGRAY);
  gfx->drawFastVLine(CX, CY - RR, 2 * RR, C_DGRAY);
  gfx->drawFastHLine(CX - RR, CY, 2 * RR, C_DGRAY);
  gfxDrawCentered("N", CY - RR + 2, rainTxt(s), C_GRAY);   // just inside the top of the ring
}

// ---- one frame: LittleFS indices -> RGB565 rows -----------------------------
static uint8_t  g_idx[TFT_WIDTH * 2];   // two rows per LittleFS read
static uint16_t g_row[TFT_WIDTH * 2];

static void blitFrame(uint8_t idx) {
  char path[24];
  snprintf(path, sizeof path, RAIN_DIR "/f%02u.raw", (unsigned)idx);
  File f = LittleFS.open(path, "r");
  if (!f) return;
  uint8_t*  idx8 = g_idx;
  uint16_t* row = g_row;
  gfxBlitBegin();
  for (int y = 0; y < TFT_HEIGHT; y += 2) {
    int rows = (int)(f.read(idx8, TFT_WIDTH * 2) / TFT_WIDTH);
    for (int x = 0; x < rows * TFT_WIDTH; x++) row[x] = g_pal[idx8[x]];
    gfxBlitRow(row, rows * TFT_WIDTH);
    if (rows < 2) break;
  }
  gfxBlitEnd();
  f.close();
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
static RainCityPos g_cityCache[RAIN_CITY_CACHE_MAX];
static uint8_t     g_cityCacheN = 0;
static bool        g_cacheBuilt = false;   // distinct de N : une portée sans
                                           // aucune ville doit rester valide
static float       g_cacheLat = NAN, g_cacheLon = NAN;
static int         g_cacheRange = -1;
static int8_t      g_cacheScale = -1;

static void buildCityCache(const Settings& s) {
  const float hLat = rainLat(s), hLon = rainLon(s);
  const float range = (float)s.rain.rangeKm;
  g_cityCacheN = 0;
  for (uint16_t i = 0; i < kRainCityCount && g_cityCacheN < RAIN_CITY_CACHE_MAX; i++) {
    RainCity c;
    memcpy_P(&c, &kRainCities[i], sizeof c);
    float d, b;
    geo(hLat, hLon, c.lat100 / 100.0f, c.lon100 / 100.0f, d, b);
    if (d > range) continue;
    RainCityPos& p = g_cityCache[g_cityCacheN++];
    int px, py;
    polar(d / range * RR, b, px, py);
    p.x = (int16_t)px; p.y = (int16_t)py;
    p.popK = c.popK;
    p.name = kRainCities[i].name;
    p.nameLen = (uint8_t)strlen_P(p.name);
  }
  g_cacheLat = hLat; g_cacheLon = hLon;
  g_cacheRange = (int)lroundf(range);
  g_cacheScale = (int8_t)s.rain.uiScale;
  g_cacheBuilt = true;    // même avec N == 0, la projection est faite : ne
                          // pas la refaire à chaque rendu (82 ms × 731 villes)
}

static void drawCities(const Settings& s, Arduino_GFX* gfx) {
  if (!s.rain.showCities) return;
  if (!g_cacheBuilt || g_cacheLat != rainLat(s) || g_cacheLon != rainLon(s) ||
      g_cacheRange != (int)lroundf((float)s.rain.rangeKm) || g_cacheScale != (int8_t)s.rain.uiScale)
    buildCityCache(s);
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

  for (uint8_t i = 0; i < g_cityCacheN; i++) {
    const RainCityPos& c = g_cityCache[i];
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

    uint8_t sz = (uint8_t)scaleK((c.popK > 1000) ? 5 : 3, k);   // losange rouge évidé (pin de carte), carré pivoté de 45°
    int16_t h = sz / 2;
    gfx->drawLine(x - h, y, x, y - h, C_RED);
    gfx->drawLine(x, y - h, x + h, y, C_RED);
    gfx->drawLine(x + h, y, x, y + h, C_RED);
    gfx->drawLine(x, y + h, x - h, y, C_RED);
    if (labeled < 6 && c.popK >= 20) {
      labeled++;
      gfx->setTextSize(nt);
      gfx->setTextColor(C_DGRAY);   // dim grey: names stay quiet on the field
      int lw = (int)c.nameLen * GFX_FONT_W * nt;
      int lx = x + sz / 2 + 3;
      if (lx + lw > TFT_WIDTH - 2) lx = x - sz / 2 - 3 - lw;
      if (lx < 2) lx = 2;
      int ly = y - 4 * nt;               // keep the taller label centred on the square
      gfx->setCursor(lx, ly < 0 ? 0 : ly);
      for (uint8_t ci = 0; ci < c.nameLen; ci++) gfx->write(pgm_read_byte(c.name + ci));
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
  gfx->drawRoundRect(X0, Y - RR, X1 - X0, 2 * RR, RR, C_DGRAY);

  // labels -2h / -1h / now, just above the pill (glyphs are 8/16/24 px)
  const int labY = Y - RR - 8 * txt - 1;
  struct { const char* label; uint32_t t; } labs[3] = {
    {"-2h", tN - 7200}, {"-1h", tN - 3600}, {"now", tN}
  };
  for (uint8_t i = 0; i < 3; i++) {
    if (labs[i].t < t0) continue;
    int x = X0 + (int)((int64_t)(labs[i].t - t0) * (X1 - X0) / (int64_t)(tN - t0));
    int w = gfxTextW(labs[i].label, txt);
    if (x - w / 2 < 2) x = 2 + w / 2;
    if (x + w / 2 > TFT_WIDTH - 2) x = TFT_WIDTH - 2 - w / 2;
    gfx->setTextSize(txt);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(x - w / 2, labY);
    gfx->print(labs[i].label);
  }

  // dot on the current frame, vertically centred on the pill
  int xc = X0 + (int)((int64_t)(rainFrameTime(animIdx) - t0) * (X1 - X0) / (int64_t)(tN - t0));
  if (xc < X0 + DR) xc = X0 + DR;   // keep the dot fully on the pill
  if (xc > X1 - DR) xc = X1 - DR;
  gfx->fillCircle(xc, Y, DR, C_GRAY);
}

// ---- the view ---------------------------------------------------------------
// Render cost counters of the last draw, exposed via /api/status.
static uint32_t g_drawMs = 0, g_blitMs = 0, g_citiesMs = 0;
uint32_t rainDrawMs()   { return g_drawMs; }
uint32_t rainBlitMs()   { return g_blitMs; }
uint32_t rainCitiesMs() { return g_citiesMs; }

static void drawRain(const Settings& s, uint8_t animIdx) {
  uint32_t tDraw = millis();
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  uint8_t n = rainCount();
  const uint8_t txt = rainTxt(s);
  // Headers cap at size 2: at size 3 (18x24) "50 km" / "12:30" would cross
  // the scope ring — the corner caps only fit ~3 glyphs at that size.
  const uint8_t ht = txt > 2 ? 2 : txt;

  for (int i = 0; i < RAIN_COLOR_COUNT; i++) {
    // Byte-wise copy: 16-bit loads from irom0 raise LoadStoreError on the
    // ESP8266 (crash vécu sur l'Ultra) — memcpy_P works.
    uint8_t col[3];
    memcpy_P(col, kRainColors[i], sizeof col);
    g_pal[i] = gfxTint((uint16_t)(((col[0] >> 3) << 11) | ((col[1] >> 2) << 5) | (col[2] >> 3)));
  }

  // Pas de fond noir quand une frame existe : elle recouvre tout l'écran et
  // le fillScreen d'avant-blit faisait un flash noir à chaque animation. Le
  // noir ne sert que pour l'état « Waiting for radar data ». Ordre vécu :
  // drawBase après blitFrame effaçait la frame au fillScreen (bug 10).
  uint32_t tBlit = millis();
  if (n == 0) drawBase(gfx);
  if (n > 0) blitFrame(animIdx);
  g_blitMs = millis() - tBlit;
  drawScope(s, gfx);
  uint32_t tCities = millis();
  drawCities(s, gfx);
  g_citiesMs = millis() - tCities;

  // Home marker on top.
  gfx->fillCircle(CX, CY, 3, C_GREEN);

  // Header: range (top-left), current frame time (top-right).
  char hdr[16];
  if (s.rain.unitsMi) snprintf(hdr, sizeof hdr, "%d mi", (int)lroundf(s.rain.rangeKm * 0.621371f));
  else                 snprintf(hdr, sizeof hdr, "%d km", s.rain.rangeKm);
  gfx->setTextSize(ht);
  gfx->setTextColor(C_GRAY);
  gfx->setCursor(3, 3);
  gfx->print(hdr);

  if (n > 0) {
    char ft[10];
    uint32_t t = rainFrameTime(animIdx);
    if (clockSynced()) {
      time_t tt = (time_t)t;
      struct tm tmv;
      localtime_r(&tt, &tmv);
      snprintf(ft, sizeof ft, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    } else {
      int32_t d = (int32_t)(rainFrameTime(n - 1) - t);
      if (d <= 0) snprintf(ft, sizeof ft, "now");
      else if (d < 3600) snprintf(ft, sizeof ft, "-%ldm", (long)(d / 60));
      else snprintf(ft, sizeof ft, "-%ldh", (long)(d / 3600));
    }
    gfx->setTextSize(ht);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(TFT_WIDTH - gfxTextW(ft, ht) - 3, 3);
    gfx->print(ft);
  } else {
    gfxDrawCentered("Waiting for radar data", CY, 2, C_GRAY);
  }

  drawTimeline(s, gfx, animIdx);

  // Attribution + error dot (bottom corners, outside the ring). The
  // attribution stays at size 1 on purpose: at size 2 it would span 132 px
  // and crowd the timeline pill.
  gfx->setTextSize(1);
  gfx->setTextColor(C_DGRAY);
  gfx->setCursor(TFT_WIDTH - gfxTextW("RainViewer", 1) - 2, TFT_HEIGHT - 9);
  gfx->print("RainViewer");
  if (rainError()) gfx->fillCircle(6, TFT_HEIGHT - 7, 4, C_RED);
  g_drawMs = millis() - tDraw;
}

// ---- DisplayMode ------------------------------------------------------------
void RainMode::begin(const Settings& s) {
  rainInit(s);
  fetchLat_ = rainLat(s);
  fetchLon_ = rainLon(s);
  fetchRange_ = (int16_t)lroundf((float)s.rain.rangeKm);
  renderedOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  needRender_ = true;
  animIdx_ = 0;
}

void RainMode::invalidate(const Settings& s) {
  // Re-init + re-fetch immédiat seulement si ce qui façonne les frames a
  // changé (home ou portée — le crop et le centrage). Tout autre save de
  // config (mode, luminosité, tickers, uiScale…) laisse le fetch en cours
  // tranquille : le rendu lit les Settings en direct et le cache des villes
  // se re-clé de lui-même. Avant ce garde-fou, chaque POST /api/config et
  // chaque appui sur le bouton « Home » annulaient un fetch en cours et en
  // relançaient un immédiatement (~5-6 min de fetch recommencées de zéro,
  // HTTP quasi muet pendant ce temps).
  const float lat = rainLat(s), lon = rainLon(s);
  const int16_t range = (int16_t)lroundf((float)s.rain.rangeKm);
  if (lat != fetchLat_ || lon != fetchLon_ || range != fetchRange_) {
    fetchLat_ = lat;
    fetchLon_ = lon;
    fetchRange_ = range;
    rainInit(s);
    rainForceRefresh();
    animIdx_ = 0;
  }
  renderedOk_ = 0xFFFFFFFF;
  needRender_ = true;
}

void RainMode::render(const Settings& s) {
  if (rainLat(s) == 0.0f && rainLon(s) == 0.0f) {
    gfxMessage("Rain radar", "Set home location", C_YELLOW);
    return;
  }
  uint8_t n = rainCount();
  if (n > 0 && animIdx_ >= n) animIdx_ = 0;
  drawRain(s, animIdx_);
}

void RainMode::service(const Settings& s) {
  rainService(s);

  uint32_t ok = rainLastOkMs();
  bool err = rainError();
  if (ok != renderedOk_ || err != renderedError_) {
    renderedOk_ = ok;
    renderedError_ = err;
    needRender_ = true;
  }

  // Animation: loop T-2h -> now between refreshes, one frame per animSec.
  uint8_t n = rainCount();
  if (n > 1 && (int32_t)(millis() - nextAnimMs_) >= 0) {
    animIdx_ = (uint8_t)((animIdx_ + 1) % n);
    nextAnimMs_ = millis() + (uint32_t)s.rain.animSec * 1000UL;
    needRender_ = true;
  }

  if (needRender_) {
    render(s);
    needRender_ = false;
  }
}
