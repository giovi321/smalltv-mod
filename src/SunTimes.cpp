#include "SunTimes.h"
#include <math.h>

// ---- sun times --------------------------------------------------------------
// Low-precision solar position from the Astronomical Almanac (good to about
// 0.01 degree, i.e. a few seconds of time), the same family of formulas as
// NOAA's solar calculator. Everything is float: the ESP8266 has no double
// hardware, and a minute of accuracy needs far less than 24 bits.

static const float kDeg = 0.017453292f;   // radians per degree
static const float kHorizon = -0.833f;    // sun's centre at sunrise: 34' refraction + 16' radius

// Days since 1970-01-01 of a calendar date (proleptic Gregorian).
static int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  int32_t yoe = y - era * 400;
  int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static float wrap360(float x) {
  x = fmodf(x, 360.0f);
  return x < 0 ? x + 360.0f : x;
}

// Declination (radians) and equation of time (minutes) at n days after
// 2000-01-01 12:00 UTC.
static void sunPosition(float n, float& decl, float& eqt) {
  float L = wrap360(280.460f + 0.9856474f * n);
  float g = wrap360(357.528f + 0.9856003f * n);
  float lam = (L + 1.915f * sinf(g * kDeg) + 0.020f * sinf(2 * g * kDeg)) * kDeg;
  float eps = (23.439f - 0.0000004f * n) * kDeg;
  decl = asinf(sinf(eps) * sinf(lam));
  float ra = atan2f(cosf(eps) * sinf(lam), cosf(lam)) / kDeg;   // -180..180
  float diff = L - ra;                                           // wrap to -180..180
  while (diff > 180.0f) diff -= 360.0f;
  while (diff < -180.0f) diff += 360.0f;
  eqt = 4.0f * diff;
}

// cos of the hour angle at which the sun's centre is at the horizon.
// > 1: never rises; < -1: never sets.
static float cosHourAngle(float lat, float decl) {
  float num = sinf(kHorizon * kDeg) - sinf(lat * kDeg) * sinf(decl);
  float den = cosf(lat * kDeg) * cosf(decl);
  if (den < 1e-6f) return num > 0 ? 2.0f : -2.0f;   // at a pole
  return num / den;
}

SunTimes sunTimes(float lat, float lon, int year, int month, int day,
                  int utcOffsetMin) {
  // n at local midnight (the day number is exact, the fraction is added later)
  const float base = (float)(daysFromCivil(year, month, day) - 10957) - 0.5f;
  const float off = (float)utcOffsetMin;
  float decl, eqt;
  SunTimes out = {SUN_NORMAL, 0, 0};

  // Is there a sunrise at all? Look at solar noon, when the sun is highest.
  float noon = 720.0f;
  for (int i = 0; i < 2; i++) {
    sunPosition(base + (noon - off) / 1440.0f, decl, eqt);
    noon = 720.0f - 4.0f * lon - eqt + off;
  }
  float c = cosHourAngle(lat, decl);
  if (c > 1.0f) { out.kind = SUN_POLAR_NIGHT; return out; }
  if (c < -1.0f) { out.kind = SUN_POLAR_DAY; return out; }

  // The sun's position changes during the day, so work out the hour angle
  // again at the time found, twice.
  for (int ev = 0; ev < 2; ev++) {
    const float sign = ev == 0 ? -1.0f : 1.0f;
    float t = noon;
    for (int i = 0; i < 3; i++) {
      sunPosition(base + (t - off) / 1440.0f, decl, eqt);
      float cc = cosHourAngle(lat, decl);
      if (cc > 1.0f) cc = 1.0f;
      if (cc < -1.0f) cc = -1.0f;
      float h = acosf(cc) / kDeg;                       // degrees
      t = 720.0f - 4.0f * lon - eqt + off + sign * 4.0f * h;
    }
    int m = (int)floorf(t + 0.5f) % 1440;
    if (m < 0) m += 1440;
    if (ev == 0) out.rise = (int16_t)m; else out.set = (int16_t)m;
  }
  return out;
}

int sunUtcOffsetMin(const struct tm& local, const struct tm& utc) {
  int32_t days = daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) -
                 daysFromCivil(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
  return (int)(days * 1440 + (local.tm_hour * 60 + local.tm_min) -
               (utc.tm_hour * 60 + utc.tm_min));
}

// ---- brightness curve -------------------------------------------------------
static const float kGamma = 2.2f;

uint8_t sunLevel(const SunTimes& t, int nowSec, const SunFade& f) {
  const uint8_t hi = f.high;
  const uint8_t lo = f.low > f.high ? f.high : f.low;
  if (t.kind == SUN_POLAR_DAY) return hi;
  if (t.kind == SUN_POLAR_NIGHT) return lo;
  if (lo == hi) return hi;

  const int32_t daySec = 86400;
  const int32_t lenSec = (((int32_t)t.set - t.rise) % 1440 + 1440) % 1440 * 60;
  const int32_t nightSec = daySec - lenSec;
  // seconds since sunrise, 0..86399
  int32_t d = ((int32_t)nowSec - (int32_t)t.rise * 60) % daySec;
  if (d < 0) d += daySec;

  const int32_t up = (int32_t)f.riseMin * 60;
  int32_t down = (int32_t)f.setMin * 60;
  if (down > nightSec) down = nightSec;   // the fall ends at the next sunrise

  // r: share of the sunrise fade done. Wins until the fall starts.
  float r = d >= up ? 1.0f : (float)d / (float)up;   // up == 0: always 1
  float share = r;
  if (d >= lenSec) {
    // after sunset: the lower of the rising and the falling share
    int32_t e = d - lenSec;
    float g = e >= down ? 0.0f : 1.0f - (float)e / (float)down;   // down == 0: always 0
    if (g < share) share = g;
  }
  if (share <= 0.0f) return lo;
  if (share >= 1.0f) return hi;

  // Mix in perceived brightness, then undo the power.
  const float inv = 1.0f / kGamma;
  float pl = powf(lo / 255.0f, inv), ph = powf(hi / 255.0f, inv);
  float v = 255.0f * powf(pl + (ph - pl) * share, kGamma) + 0.5f;
  int out = (int)v;
  if (out < lo) out = lo;
  if (out > hi) out = hi;
  return (uint8_t)out;
}

uint8_t sunCombine(SunOverlap mode, bool nightActive, uint8_t nightSteps,
                   uint8_t sunSteps) {
  if (!nightActive || mode == SUN_OVERLAP_SUN_FIRST) return sunSteps;
  if (mode == SUN_OVERLAP_NIGHT_FIRST) return nightSteps;
  return nightSteps < sunSteps ? nightSteps : sunSteps;
}
