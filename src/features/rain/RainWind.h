// RainWind.h — the wind arrow of the rain radar: data rules and geometry.
//
// Plain functions, no Arduino dependency, so the host tests check them as
// they are (tests/rain/test_frameset.cpp).
//
// Weather convention: a wind direction is where the wind comes FROM, in
// degrees clockwise from true north (0 = from the north, 90 = from the east).
// The arrow shows where the rain is heading, so it points the other way:
// a 306° wind (from the north-west) draws an arrow towards 126° (south-east).
// The scope has north up and is never rotated: an arrow at angle a points
// along (sin a, -cos a) on screen (y grows downwards).
#pragma once
#include <math.h>
#include <stdint.h>

// Which wind the arrow shows (RainSettings::wind).
#define RAIN_WIND_OFF    0
#define RAIN_WIND_CLOUDS 1   // 850 hPa (~1,500 m): the wind that moves the rain clouds
#define RAIN_WIND_GROUND 2   // 10 m above ground
#define RAIN_WIND_LEVELS 2   // levels fetched (clouds, ground), index = setting - 1

// A value as Open-Meteo sends it, accepted only within bounds: a direction in
// 0..360 and a speed in 0..RAIN_WIND_MAX_KMH. Anything else (missing, NaN,
// out of range) means no arrow for that level.
#define RAIN_WIND_MAX_KMH 500.0f
inline bool rainWindValueOk(float dirFrom, float kmh) {
  return dirFrom >= 0.0f && dirFrom <= 360.0f && kmh >= 0.0f && kmh <= RAIN_WIND_MAX_KMH;
}

// Arrow angle on screen (degrees clockwise from north, 0..359) for a wind
// coming from dirFrom.
inline int rainWindHeading(float dirFrom) {
  int a = (int)lroundf(dirFrom) + 180;
  a %= 360;
  return a < 0 ? a + 360 : a;
}

// The arrow's corners: the head (tip and the two base corners) and the
// shaft (a quad from the tail to the head's base), each as two triangles
// once drawn. Centred on (cx, cy), `half` px from the centre to the tip.
struct RainArrow {
  int16_t tipX, tipY, b1x, b1y, b2x, b2y;      // head
  int16_t s1x, s1y, s2x, s2y, s3x, s3y, s4x, s4y;  // shaft: tail-left, tail-right, base-right, base-left
};

inline void rainWindArrow(int cx, int cy, int half, int headingDeg, RainArrow& a) {
  const float r = (float)headingDeg * 3.14159265f / 180.0f;
  const float dx = sinf(r), dy = -cosf(r);      // along the arrow
  const float px = -dy, py = dx;                // across it
  const float h = (float)half;
  const float head = h * 0.9f;                  // head length
  const float hw = h * 0.55f;                   // head half-width
  const float sw = h >= 10.0f ? 1.5f : 1.0f;    // shaft half-width
  auto R = [](float v) { return (int16_t)lroundf(v); };
  const float tx = cx + h * dx, ty = cy + h * dy;               // tip
  const float bx = tx - head * dx, by = ty - head * dy;         // head base centre
  const float ex = cx - h * dx, ey = cy - h * dy;               // tail
  a.tipX = R(tx); a.tipY = R(ty);
  a.b1x = R(bx + hw * px); a.b1y = R(by + hw * py);
  a.b2x = R(bx - hw * px); a.b2y = R(by - hw * py);
  a.s1x = R(ex + sw * px); a.s1y = R(ey + sw * py);
  a.s2x = R(ex - sw * px); a.s2y = R(ey - sw * py);
  a.s3x = R(bx - sw * px); a.s3y = R(by - sw * py);
  a.s4x = R(bx + sw * px); a.s4y = R(by + sw * py);
}
