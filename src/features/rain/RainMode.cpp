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
static uint16_t g_pal[16];

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

// ---- the static radar background (drawn under, then over, the rain) --------
static void drawBase(Arduino_GFX* gfx) {
  gfx->fillScreen(C_BLACK);
  gfx->drawCircle(CX, CY, RR, C_DGRAY);
  gfx->drawCircle(CX, CY, RR / 2, C_DGRAY);
  gfx->drawFastVLine(CX, CY - RR, 2 * RR, C_DGRAY);
  gfx->drawFastHLine(CX - RR, CY, 2 * RR, C_DGRAY);
  gfxDrawCentered("N", CY - RR + 2, 1, C_GRAY);   // just inside the top of the ring
}

// ---- one frame: LittleFS nibbles -> RGB565 rows -----------------------------
static uint8_t  g_nib[120];
static uint16_t g_row[TFT_WIDTH];

static void blitFrame(uint8_t idx) {
  char path[24];
  snprintf(path, sizeof path, RAIN_DIR "/f%02u.raw", (unsigned)idx);
  File f = LittleFS.open(path, "r");
  if (!f) return;
  Arduino_GFX* gfx = gfxDev();
  uint8_t*  nib = g_nib;
  uint16_t* row = g_row;
  for (int y = 0; y < TFT_HEIGHT; y++) {
    if (f.read(nib, sizeof nib) != sizeof nib) break;
    for (int x = 0; x < TFT_WIDTH; x++) {
      uint8_t lv = (x & 1) ? (nib[x >> 1] & 0x0F) : (nib[x >> 1] >> 4);
      row[x] = g_pal[lv];
    }
    gfx->draw16bitRGBBitmap(0, y, row, TFT_WIDTH, 1);
  }
  f.close();
}

// ---- nearby cities as reference dots (France + Europe, embedded list) ------
static void drawCities(const Settings& s, Arduino_GFX* gfx) {
  if (!s.rain.showCities) return;
  float hLat = rainLat(s), hLon = rainLon(s);
  float range = (float)s.rain.rangeKm;

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

    // declutter : 16 px minimum between two dots
    bool clash = false;
    for (uint8_t k = 0; k < nUsed; k++) {
      int dx = x - used[k].x, dy = y - used[k].y;
      if (dx * dx + dy * dy < 256) { clash = true; break; }
    }
    if (clash) continue;
    if (nUsed < sizeof(used) / sizeof(used[0])) {
      used[nUsed].x = (int16_t)x;
      used[nUsed].y = (int16_t)y;
      nUsed++;
    }

    gfx->fillCircle(x, y, (c.popK > 1000) ? 2 : 1, C_GRAY);
    if (labeled < 6 && c.popK >= 150) {
      labeled++;
      gfx->setTextSize(1);
      gfx->setTextColor(C_GRAY);
      int lw = (int)strlen(c.name) * GFX_FONT_W;
      int lx = x + 4;
      if (lx + lw > TFT_WIDTH - 2) lx = x - 4 - lw;
      if (lx < 2) lx = 2;
      gfx->setCursor(lx, y - 4 < 0 ? 0 : y - 4);
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
    int w = gfxTextW(labs[i].label, 1);
    if (x - w / 2 < 2) x = 2 + w / 2;
    if (x + w / 2 > TFT_WIDTH - 2) x = TFT_WIDTH - 2 - w / 2;
    gfx->setTextSize(1);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(x - w / 2, Y - 14);
    gfx->print(labs[i].label);
  }

  // cursor on the current frame
  int xc = X0 + (int)((int64_t)(rainFrameTime(animIdx) - t0) * (X1 - X0) / (int64_t)(tN - t0));
  gfx->fillTriangle(xc, Y + 1, xc - 4, Y - 5, xc + 4, Y - 5, C_GRAY);
}

// ---- the view ---------------------------------------------------------------
static void drawRain(const Settings& s, uint8_t animIdx) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  uint8_t n = rainCount();

  for (int i = 0; i < 16; i++)
    g_pal[i] = gfxTint(pgm_read_word(&kRainPalette[i]));

  if (n > 0) blitFrame(animIdx);

  drawBase(gfx);
  drawCities(s, gfx);

  // Home marker on top.
  gfx->fillCircle(CX, CY, 3, C_GREEN);

  // Header: range (top-left), current frame time (top-right).
  char hdr[16];
  if (s.rain.unitsMi) snprintf(hdr, sizeof hdr, "%d mi", (int)lroundf(s.rain.rangeKm * 0.621371f));
  else                 snprintf(hdr, sizeof hdr, "%d km", s.rain.rangeKm);
  gfx->setTextSize(1);
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
    gfx->setTextSize(1);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(TFT_WIDTH - gfxTextW(ft, 1) - 3, 3);
    gfx->print(ft);
  } else {
    gfxDrawCentered("Waiting for radar data", CY, 2, C_GRAY);
  }

  drawTimeline(s, gfx, animIdx);

  // Attribution + error dot (bottom corners, outside the ring).
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
