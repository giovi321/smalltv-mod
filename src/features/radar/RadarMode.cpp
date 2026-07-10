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

// Marker radius scaled by the UI-size factor (min 2 px so it stays visible).
static int scaleR(float base, float k) {
  int v = (int)lroundf(base * k);
  return v < 2 ? 2 : v;
}

// Filled heading triangle centred at (x,y), nose pointing along track (deg, cw N).
// `k` scales the triangle with the UI-size setting.
static void planeTri(Arduino_GFX* gfx, int x, int y, float trackDeg, float k, uint16_t color) {
  const float L = 12.0f * k, W = 8.0f * k, B = 7.0f * k;  // nose, half-width, tail setback
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

// Label bounding box + AABB overlap test, used to declutter callsigns: labels
// are placed nearest-first and any that would collide with one already drawn are
// dropped (the plane's triangle still shows).
struct LblBox { int16_t x, y, w, h; };
static bool boxHit(const LblBox& a, const LblBox& b) {
  return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
           a.y + a.h <= b.y || b.y + b.h <= a.y);
}

// ---- the radar view --------------------------------------------------------
static void drawRadar(const Settings& s) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  const float range = (float)s.radar.rangeKm;
  const uint8_t sc = s.radar.uiScale;                             // 0=small,1=med,2=large
  const uint8_t txt = (sc == 0) ? 1 : 2;                          // built-in font scale
  const float   k = (sc == 0) ? 0.65f : (sc == 2 ? 1.5f : 1.0f);  // marker geometry scale
  const int lblDX = scaleR(9, k);
  gfx->fillScreen(C_BLACK);

  // Range rings + crosshair + N marker.
  gfx->drawCircle(CX, CY, RR, C_DGRAY);
  gfx->drawCircle(CX, CY, RR / 2, C_DGRAY);
  gfx->drawFastVLine(CX, CY - RR, 2 * RR, C_DGRAY);
  gfx->drawFastHLine(CX - RR, CY, 2 * RR, C_DGRAY);
  gfxDrawCentered("N", CY - RR + 2, txt, C_GRAY);   // just inside the top of the ring

  // Home-area airports (projected like aircraft).
  int ad = scaleR(5, k);
  for (uint8_t i = 0; i < s.radar.airportCount; i++) {
    float d, b;
    geo(s.radar.lat, s.radar.lon, s.radar.airports[i].lat, s.radar.airports[i].lon, d, b);
    if (d > range) continue;
    int x, y;
    polar(d / range * RR, b, x, y);
    // diamond marker
    gfx->drawLine(x, y - ad, x + ad, y, C_BLUE);
    gfx->drawLine(x + ad, y, x, y + ad, C_BLUE);
    gfx->drawLine(x, y + ad, x - ad, y, C_BLUE);
    gfx->drawLine(x - ad, y, x, y - ad, C_BLUE);
    if (s.radar.airports[i].icao[0]) {
      gfx->setTextSize(txt);
      gfx->setTextColor(C_BLUE);
      gfx->setCursor(x + ad + 3, y - ad - 1);
      gfx->print(s.radar.airports[i].icao);
    }
  }

  // Aircraft, nearest first.
  uint8_t n = radarCount();
  LblBox  lbl[MAX_AIRCRAFT];    // labels already placed (for declutter)
  uint8_t nLbl = 0;
  for (uint8_t i = 0; i < n; i++) {
    const Aircraft& a = aircraftAt(i);

    if (a.distKm > range) {                 // beyond the ring -> bearing dot on rim
      if (!s.radar.showRimDots) continue;
      int x, y;
      polar(RR, a.bearingDeg, x, y);
      gfx->fillCircle(x, y, scaleR(3, k), C_RED);
      continue;
    }

    int x, y;
    polar(a.distKm / range * RR, a.bearingDeg, x, y);

    if (s.radar.showVectors && !isnan(a.gs) && !isnan(a.track)) {
      float th = a.track * (float)PI / 180.0f;
      float len = constrain(a.gs * 0.08f, 6.0f, 24.0f) * k;
      int ex = x + (int)lroundf(sinf(th) * len);
      int ey = y - (int)lroundf(cosf(th) * len);
      gfx->drawLine(x, y, ex, ey, C_MAGENTA);
    }

    if (!isnan(a.track)) planeTri(gfx, x, y, a.track, k, C_RED);
    else                 gfx->fillCircle(x, y, scaleR(4, k), C_RED);

    if (s.radar.showLabels && a.callsign[0]) {
      int lw = (int)strlen(a.callsign) * 6 * txt;
      int lh = 8 * txt + (a.altFt > 0 ? 8 : 0);
      // Right of the marker by default; flip to its left when the label would
      // run off the panel, and clamp as a last resort so it never clips.
      int lx = x + lblDX;
      if (lx + lw > TFT_WIDTH - 2) lx = x - lblDX - lw;
      if (lx < 2) lx = 2;
      if (lx + lw > TFT_WIDTH - 2) lx = TFT_WIDTH - 2 - lw;
      LblBox box = {(int16_t)lx, (int16_t)(y - (txt == 1 ? 4 : 8)),
                    (int16_t)lw, (int16_t)lh};
      bool clash = false;
      for (uint8_t j = 0; j < nLbl; j++) if (boxHit(box, lbl[j])) { clash = true; break; }
      if (!clash) {                          // skip labels that would collide
        if (nLbl < MAX_AIRCRAFT) lbl[nLbl++] = box;
        gfx->setTextSize(txt);
        gfx->setTextColor(C_GRAY);
        gfx->setCursor(box.x, box.y);
        gfx->print(a.callsign);
        if (a.altFt > 0) {
          char fl[8];
          snprintf(fl, sizeof(fl), "FL%03d", (int)(a.altFt / 100));
          gfx->setTextSize(1);
          gfx->setCursor(box.x, y + (txt == 1 ? 6 : 10));
          gfx->print(fl);
        }
      }
    }
  }

  // Home marker on top.
  gfx->fillCircle(CX, CY, scaleR(4, k), C_GREEN);

  // Overlays: range (top-left), aircraft count (top-right), error dot.
  char hdr[16];
  if (s.radar.unitsMi) snprintf(hdr, sizeof(hdr), "%d mi", (int)lroundf(s.radar.rangeKm * 0.621371f));
  else                 snprintf(hdr, sizeof(hdr), "%d km", s.radar.rangeKm);
  gfx->setTextSize(txt);
  gfx->setTextColor(C_GRAY);
  gfx->setCursor(3, 3);
  gfx->print(hdr);

  char cnt[10];
  snprintf(cnt, sizeof(cnt), "%d ac", n);
  gfx->setTextSize(txt);
  gfx->setTextColor(C_GRAY);
  gfx->setCursor(TFT_WIDTH - gfxTextW(cnt, txt) - 3, 3);
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
