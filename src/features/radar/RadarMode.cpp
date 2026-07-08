#include "RadarMode.h"
#include <Arduino_GFX_Library.h>
#include <math.h>
#include "Gfx.h"
#include "RadarClient.h"

RadarMode g_radarMode;

#define C_MAGENTA 0xF81F   // speed vector (not in the shared palette)

// Radar geometry (square 240x240 panel; the circle leaves the corners empty).
static const int CX = TFT_WIDTH / 2;
static const int CY = TFT_HEIGHT / 2;
static const int RR = 112;                 // outer ring radius, px

// dist/bearing polar -> screen xy (bearing 0 = N = up, cw).
static void polar(float distPx, float brgDeg, int& x, int& y) {
  float a = brgDeg * (float)PI / 180.0f;
  x = CX + (int)lroundf(distPx * sinf(a));
  y = CY - (int)lroundf(distPx * cosf(a));
}

// Flat-earth home-relative dist/bearing (mirrors RadarClient, for airports).
static void geo(float homeLat, float homeLon, float lat, float lon,
                float& distKm, float& brg) {
  float dLat = (lat - homeLat) * 111.0f;
  float dLon = (lon - homeLon) * 111.0f * cosf(homeLat * (float)PI / 180.0f);
  distKm = sqrtf(dLat * dLat + dLon * dLon);
  brg = atan2f(dLon, dLat) * 180.0f / (float)PI;
  if (brg < 0) brg += 360.0f;
}

// Filled heading triangle centred at (x,y), nose pointing along track (deg, cw N).
static void planeTri(Arduino_GFX* gfx, int x, int y, float trackDeg, uint16_t color) {
  const float L = 12.0f, W = 8.0f, B = 7.0f;  // nose length, half-width, tail setback
  float th = trackDeg * (float)PI / 180.0f;
  float ct = cosf(th), st = sinf(th);
  // local (lx=right, ly=nose) -> screen: sx = x + lx*ct + ly*st; sy = y + lx*st - ly*ct
  int nx = x + (int)lroundf(0 * ct + L * st);
  int ny = y + (int)lroundf(0 * st - L * ct);
  int lx = x + (int)lroundf(-W * ct + (-B) * st);
  int ly = y + (int)lroundf(-W * st - (-B) * ct);
  int rx = x + (int)lroundf(W * ct + (-B) * st);
  int ry = y + (int)lroundf(W * st - (-B) * ct);
  gfx->fillTriangle(nx, ny, lx, ly, rx, ry, color);
}

// ---- the radar view --------------------------------------------------------
static void drawRadar(const Settings& s) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  const float range = (float)s.radar.rangeKm;
  gfx->fillScreen(C_BLACK);

  // Range rings + crosshair + N marker.
  gfx->drawCircle(CX, CY, RR, C_DGRAY);
  gfx->drawCircle(CX, CY, RR / 2, C_DGRAY);
  gfx->drawFastVLine(CX, CY - RR, 2 * RR, C_DGRAY);
  gfx->drawFastHLine(CX - RR, CY, 2 * RR, C_DGRAY);
  gfxDrawCentered("N", CY - RR + 2, 2, C_GRAY);   // just inside the top of the ring

  // Home-area airports (projected like aircraft).
  for (uint8_t i = 0; i < s.radar.airportCount; i++) {
    float d, b;
    geo(s.radar.lat, s.radar.lon, s.radar.airports[i].lat, s.radar.airports[i].lon, d, b);
    if (d > range) continue;
    int x, y;
    polar(d / range * RR, b, x, y);
    // diamond marker
    gfx->drawLine(x, y - 5, x + 5, y, C_BLUE);
    gfx->drawLine(x + 5, y, x, y + 5, C_BLUE);
    gfx->drawLine(x, y + 5, x - 5, y, C_BLUE);
    gfx->drawLine(x - 5, y, x, y - 5, C_BLUE);
    if (s.radar.airports[i].icao[0]) {
      gfx->setTextSize(2);
      gfx->setTextColor(C_BLUE);
      gfx->setCursor(x + 8, y - 7);
      gfx->print(s.radar.airports[i].icao);
    }
  }

  // Aircraft, nearest first.
  uint8_t n = radarCount();
  for (uint8_t i = 0; i < n; i++) {
    const Aircraft& a = aircraftAt(i);

    if (a.distKm > range) {                 // beyond the ring -> bearing dot on rim
      if (!s.radar.showRimDots) continue;
      int x, y;
      polar(RR, a.bearingDeg, x, y);
      gfx->fillCircle(x, y, 3, C_RED);
      continue;
    }

    int x, y;
    polar(a.distKm / range * RR, a.bearingDeg, x, y);

    if (s.radar.showVectors && !isnan(a.gs) && !isnan(a.track)) {
      float th = a.track * (float)PI / 180.0f;
      float len = constrain(a.gs * 0.08f, 6.0f, 24.0f);
      int ex = x + (int)lroundf(sinf(th) * len);
      int ey = y - (int)lroundf(cosf(th) * len);
      gfx->drawLine(x, y, ex, ey, C_MAGENTA);
    }

    if (!isnan(a.track)) planeTri(gfx, x, y, a.track, C_RED);
    else                 gfx->fillCircle(x, y, 4, C_RED);

    if (s.radar.showLabels && a.callsign[0]) {
      gfx->setTextSize(2);
      gfx->setTextColor(C_GRAY);
      gfx->setCursor(x + 9, y - 8);
      gfx->print(a.callsign);
      if (a.altFt > 0) {
        char fl[8];
        snprintf(fl, sizeof(fl), "FL%03d", (int)(a.altFt / 100));
        gfx->setTextSize(1);
        gfx->setCursor(x + 9, y + 10);
        gfx->print(fl);
      }
    }
  }

  // Home marker on top.
  gfx->fillCircle(CX, CY, 4, C_GREEN);

  // Overlays: range (top-left), aircraft count (top-right), error dot.
  char hdr[16];
  if (s.radar.unitsMi) snprintf(hdr, sizeof(hdr), "%d mi", (int)lroundf(s.radar.rangeKm * 0.621371f));
  else                 snprintf(hdr, sizeof(hdr), "%d km", s.radar.rangeKm);
  gfx->setTextSize(2);
  gfx->setTextColor(C_GRAY);
  gfx->setCursor(3, 3);
  gfx->print(hdr);

  char cnt[10];
  snprintf(cnt, sizeof(cnt), "%d ac", n);
  gfx->setTextSize(2);
  gfx->setTextColor(C_GRAY);
  gfx->setCursor(TFT_WIDTH - gfxTextW(cnt, 2) - 3, 3);
  gfx->print(cnt);

  if (radarError()) gfx->fillCircle(6, TFT_HEIGHT - 7, 4, C_RED);
}

// ---- DisplayMode ----------------------------------------------------------
void RadarMode::begin(const Settings& s) {
  radarInit(s);
  renderedOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  needRender_ = true;
}

void RadarMode::invalidate(const Settings& s) {
  radarInit(s);
  radarForceRefresh();
  renderedOk_ = 0xFFFFFFFF;
  needRender_ = true;
}

void RadarMode::render(const Settings& s) {
  if (s.radar.lat == 0.0f && s.radar.lon == 0.0f) {
    gfxMessage("Plane radar", "Set home location", C_YELLOW);
    return;
  }
  drawRadar(s);
}

void RadarMode::service(const Settings& s) {
  radarService(s);

  uint32_t ok = radarLastOkMs();
  bool err = radarError();
  if (ok != renderedOk_ || err != renderedError_) {
    renderedOk_ = ok;
    renderedError_ = err;
    needRender_ = true;
  }

  if (needRender_) {
    render(s);
    needRender_ = false;
  }
}
