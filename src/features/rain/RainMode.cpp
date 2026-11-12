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
// span 198 px at size 3. k scales the city squares - 2x at Large so the
// level steps are unmistakable.
static uint8_t rainTxt(const Settings& s) { return 1 + s.rain.uiScale; }
static float   rainK(const Settings& s) {
  return (s.rain.uiScale == 0) ? 0.65f : ((s.rain.uiScale == 2) ? 2.0f : 1.0f);
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
static uint8_t  g_idx[TFT_WIDTH];
static uint16_t g_row[TFT_WIDTH];

static void blitFrame(uint8_t idx) {
  char path[24];
  snprintf(path, sizeof path, RAIN_DIR "/f%02u.raw", (unsigned)idx);
  File f = LittleFS.open(path, "r");
  if (!f) return;
  Arduino_GFX* gfx = gfxDev();
  uint8_t*  idx8 = g_idx;
  uint16_t* row = g_row;
  for (int y = 0; y < TFT_HEIGHT; y++) {
    if (f.read(idx8, TFT_WIDTH) != TFT_WIDTH) break;
    for (int x = 0; x < TFT_WIDTH; x++) row[x] = g_pal[idx8[x]];
    gfx->draw16bitRGBBitmap(0, y, row, TFT_WIDTH, 1);
  }
  f.close();
}

// ---- nearby cities as reference dots (France + Europe, embedded list) ------
static void drawCities(const Settings& s, Arduino_GFX* gfx) {
  if (!s.rain.showCities) return;
  float hLat = rainLat(s), hLon = rainLon(s);
  float range = (float)s.rain.rangeKm;
  const uint8_t txt = rainTxt(s);
  const uint8_t nt  = txt > 2 ? 2 : txt;   // names cap at 16 px (see rainTxt)
  const float   k   = rainK(s);
  // Declutter fixed for medium/large so the bigger squares don't drop
  // cities; only small gets a tighter grid (its markers are tiny).
  const int     minD = (s.rain.uiScale == 0) ? 10 : 16;

  struct Pt { int16_t x, y; };
  Pt used[24];
  uint8_t nUsed = 0;
  uint8_t labeled = 0;

  for (uint16_t i = 0; i < kRainCityCount; i++) {
    RainCity c;
    memcpy_P(&c, &kRainCities[i], sizeof c);
    float d, b;
    geo(hLat, hLon, c.lat100 / 100.0f, c.lon100 / 100.0f, d, b);
    if (d > range) continue;
    int x, y;
    polar(d / range * RR, b, x, y);

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

    uint8_t sz = (uint8_t)scaleK((c.popK > 1000) ? 5 : 3, k);   // petit carré rouge (pin de carte)
    gfx->fillRect(x - sz / 2, y - sz / 2, sz, sz, C_RED);
    if (labeled < 6 && c.popK >= 20) {
      labeled++;
      gfx->setTextSize(nt);
      gfx->setTextColor(C_GRAY);
      int lw = (int)strlen(c.name) * GFX_FONT_W * nt;
      int lx = x + sz / 2 + 3;
      if (lx + lw > TFT_WIDTH - 2) lx = x - sz / 2 - 3 - lw;
      if (lx < 2) lx = 2;
      int ly = y - 4 * nt;               // keep the taller label centred on the square
      gfx->setCursor(lx, ly < 0 ? 0 : ly);
      gfx->print(c.name);
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

  const int X0 = 6, X1 = TFT_WIDTH - 6, Y = 236;
  const uint8_t txt = rainTxt(s);
  // Taller labels sit higher so they stay clear of the cursor triangle
  // under the line (8/16/24 px glyphs -> labels 14/22/32 px above Y).
  const int labY = (txt == 3) ? Y - 32 : (txt == 2 ? Y - 22 : Y - 14);

  // line + 30-minute ticks
  gfx->drawFastHLine(X0, Y, X1 - X0, C_DGRAY);
  for (uint32_t t = t0 + 1800 - (t0 % 1800); t < tN; t += 1800) {
    int x = X0 + (int)((int64_t)(t - t0) * (X1 - X0) / (int64_t)(tN - t0));
    gfx->drawFastVLine(x, Y - 3, 3, C_DGRAY);
  }

  // labels -2h / -1h / now
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

  // cursor on the current frame, sized with the labels
  int xc = X0 + (int)((int64_t)(rainFrameTime(animIdx) - t0) * (X1 - X0) / (int64_t)(tN - t0));
  int cw = 3 + txt;
  int cy = (txt == 3) ? Y - 7 : (txt == 2 ? Y - 6 : Y - 5);
  gfx->fillTriangle(xc, Y + 1, xc - cw, cy, xc + cw, cy, C_GRAY);
}

// ---- the view ---------------------------------------------------------------
static void drawRain(const Settings& s, uint8_t animIdx) {
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

  // Fond noir, puis la frame (nuages + pluie, composite prémultiplié), puis
  // les cercles/croix PAR-DESSUS le champ. Ordre vécu : drawBase après
  // blitFrame effaçait la frame au fillScreen (flash nuages puis scope vide).
  drawBase(gfx);
  if (n > 0) blitFrame(animIdx);
  drawScope(s, gfx);
  drawCities(s, gfx);

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
  // and collide with the "now" label of the timeline.
  gfx->setTextSize(1);
  gfx->setTextColor(C_DGRAY);
  gfx->setCursor(TFT_WIDTH - gfxTextW("RainViewer", 1) - 2, TFT_HEIGHT - 9);
  gfx->print("RainViewer");
  if (rainError()) gfx->fillCircle(6, TFT_HEIGHT - 7, 4, C_RED);
}

// ---- DisplayMode ------------------------------------------------------------
void RainMode::begin(const Settings& s) {
  rainInit(s);
  renderedOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  needRender_ = true;
  animIdx_ = 0;
}

void RainMode::invalidate(const Settings& s) {
  rainInit(s);
  rainForceRefresh();
  renderedOk_ = 0xFFFFFFFF;
  needRender_ = true;
  animIdx_ = 0;
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
