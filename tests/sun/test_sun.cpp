// Native tests for src/SunTimes.cpp (sunrise and sunset worked out on the
// device, the brightness curve, the overlap with night mode).
// c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc tests/sun/test_sun.cpp src/SunTimes.cpp -o /tmp/smalltv-sun-tests
#include "SunTimes.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_failed = 0;
static int g_checks = 0;
#define CHECK(cond, ...)                                              \
  do {                                                                \
    g_checks++;                                                       \
    if (!(cond)) {                                                    \
      g_failed++;                                                     \
      printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);          \
      printf(__VA_ARGS__);                                            \
      printf("\n");                                                   \
    }                                                                 \
  } while (0)

// ---- computed sun times against the reference table ------------------------
// The table comes from PyEphem and was cross-checked against NOAA's
// equations (tests/sun/gen_reference.py). The tolerance is 1 minute: the
// device uses a short series for the sun's position (a few seconds of
// error) and a fixed 0.833 degree horizon dip, and both sides round to whole
// minutes, so a true difference of a few seconds can show as one minute.
// The table's largest difference is printed by the test.
struct RefRow {
  const char* name;
  float lat, lon;
  int year, month, day, offset;
  int kind, rise, set;
};
static const RefRow kRef[] = {
#include "reference.inc"
};

static int circDiff(int a, int b) {  // distance on the 24 h circle
  int d = ((a - b) % 1440 + 1440) % 1440;
  return d > 720 ? 1440 - d : d;
}

static int signedDiff(int a, int b) {  // a - b on the 24 h circle, -720..720
  int d = ((a - b) % 1440 + 1440) % 1440;
  return d > 720 ? d - 1440 : d;
}

static void testReferenceTable() {
  const int tol = 1;
  int polarDay = 0, polarNight = 0, normal = 0;
  int worst = 0, signedSum = 0, values = 0;
  const int rows = (int)(sizeof(kRef) / sizeof(kRef[0]));
  for (int i = 0; i < rows; i++) {
    const RefRow& r = kRef[i];
    SunTimes t = sunTimes(r.lat, r.lon, r.year, r.month, r.day, r.offset);
    CHECK(t.kind == r.kind, "%s %d-%02d-%02d: kind %d, expected %d", r.name,
          r.year, r.month, r.day, (int)t.kind, r.kind);
    if (r.kind == SUN_POLAR_DAY) polarDay++;
    else if (r.kind == SUN_POLAR_NIGHT) polarNight++;
    else normal++;
    if (r.kind != SUN_NORMAL || t.kind != SUN_NORMAL) continue;
    int dr = circDiff(t.rise, r.rise), ds = circDiff(t.set, r.set);
    if (dr > worst) worst = dr;
    if (ds > worst) worst = ds;
    signedSum += signedDiff(t.rise, r.rise) + signedDiff(t.set, r.set);
    values += 2;
    CHECK(t.rise >= 0 && t.rise < 1440 && t.set >= 0 && t.set < 1440,
          "%s: rise %d set %d out of 0..1439", r.name, t.rise, t.set);
    CHECK(dr <= tol, "%s %d-%02d-%02d: sunrise %d, reference %d", r.name,
          r.year, r.month, r.day, t.rise, r.rise);
    CHECK(ds <= tol, "%s %d-%02d-%02d: sunset %d, reference %d", r.name,
          r.year, r.month, r.day, t.set, r.set);
  }
  // The table must contain what it is meant to check.
  // No bias: a function that rounds down (or up) is half a minute off on
  // average, which one row cannot show but the mean of all rows does.
  double mean = values ? (double)signedSum / values : 99.0;
  CHECK(fabs(mean) < 0.25, "mean signed difference %.3f min over %d values", mean, values);
  CHECK(rows >= 40, "only %d rows", rows);
  CHECK(polarDay >= 2, "polar day rows: %d", polarDay);
  CHECK(polarNight >= 2, "polar night rows: %d", polarNight);
  CHECK(normal >= 35, "normal rows: %d", normal);
  printf("reference table: %d rows (%d normal, %d polar day, %d polar night), "
         "largest difference %d min\n",
         rows, normal, polarDay, polarNight, worst);
}

// Not tied to the table: properties that must hold anywhere.
static void testSunProperties() {
  // Equator: the day is 12 h (+- 10 min of refraction and sun size).
  for (int m = 1; m <= 12; m++) {
    SunTimes t = sunTimes(0.0f, 0.0f, 2026, m, 15, 0);
    int len = ((t.set - t.rise) % 1440 + 1440) % 1440;
    CHECK(t.kind == SUN_NORMAL && len > 710 && len < 730,
          "equator month %d: day length %d", m, len);
  }
  // Moving 15 degrees east at the same clock offset: sun comes 1 h earlier.
  SunTimes a = sunTimes(48.0f, 0.0f, 2026, 4, 10, 0);
  SunTimes b = sunTimes(48.0f, 15.0f, 2026, 4, 10, 0);
  CHECK(circDiff(a.rise - 60, b.rise) <= 1, "east shift: %d vs %d", a.rise, b.rise);
  CHECK(circDiff(a.set - 60, b.set) <= 1, "east shift: %d vs %d", a.set, b.set);
  // The clock offset only shifts the printed time.
  SunTimes c = sunTimes(48.0f, 0.0f, 2026, 4, 10, 120);
  CHECK(circDiff(a.rise + 120, c.rise) <= 1, "offset: %d vs %d", a.rise, c.rise);
  // Northern summer day is longer than winter day, southern the other way.
  SunTimes s1 = sunTimes(50.0f, 8.0f, 2026, 6, 21, 120);
  SunTimes w1 = sunTimes(50.0f, 8.0f, 2026, 12, 21, 60);
  CHECK((s1.set - s1.rise) > (w1.set - w1.rise) + 300, "north long/short");
  SunTimes s2 = sunTimes(-34.0f, 151.0f, 2026, 12, 21, 660);
  SunTimes w2 = sunTimes(-34.0f, 151.0f, 2026, 6, 21, 600);
  CHECK((s2.set - s2.rise) > (w2.set - w2.rise) + 200, "south long/short");
  // The poles: no division by zero, a clear answer.
  CHECK(sunTimes(90.0f, 0.0f, 2026, 6, 21, 0).kind == SUN_POLAR_DAY, "north pole june");
  CHECK(sunTimes(90.0f, 0.0f, 2026, 12, 21, 0).kind == SUN_POLAR_NIGHT, "north pole dec");
  CHECK(sunTimes(-90.0f, 0.0f, 2026, 12, 21, 0).kind == SUN_POLAR_DAY, "south pole dec");
  CHECK(sunTimes(-90.0f, 0.0f, 2026, 6, 21, 0).kind == SUN_POLAR_NIGHT, "south pole june");
  // Where the polar periods start and end, day by day over 2026 at Tromso,
  // against the reference (PyEphem). The days right at an edge may differ by
  // one, so one wrong day is allowed; the mutation tests show that a shifted
  // limit moves the edges by more than that.
  static const char kYear[] =
#include "year.inc"
      ;
  int wrong = 0, changes = 0;
  char prev = 0;
  for (int doy = 0; doy < 365; doy++) {
    int mon = 1, day = doy + 1;
    static const int ml[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    while (day > ml[mon - 1]) { day -= ml[mon - 1]; mon++; }
    SunKind k = sunTimes(69.65f, 18.96f, 2026, mon, day, 60).kind;
    char c = k == SUN_NORMAL ? 'N' : k == SUN_POLAR_DAY ? 'D' : 'X';
    if (c != kYear[doy]) wrong++;
    if (prev && c != prev) changes++;
    prev = c;
  }
  CHECK(sizeof(kYear) == 366, "year table has %d letters", (int)sizeof(kYear) - 1);
  CHECK(wrong <= 1, "Tromso 2026: %d days of the year differ from the reference", wrong);
  CHECK(changes == 4, "Tromso: %d changes of kind in a year, expected 4", changes);
}

static void testUtcOffset() {
  struct tm l = {}, u = {};
  l.tm_year = u.tm_year = 126; l.tm_mon = u.tm_mon = 5;
  // Same day: Berlin in summer is UTC+2.
  l.tm_mday = u.tm_mday = 21; l.tm_yday = u.tm_yday = 171;
  l.tm_hour = 14; l.tm_min = 30; u.tm_hour = 12; u.tm_min = 30;
  CHECK(sunUtcOffsetMin(l, u) == 120, "+2h: %d", sunUtcOffsetMin(l, u));
  // Local time is on the next day (Sydney, +10 at 20:15 UTC).
  l.tm_mday = 22; l.tm_yday = 172; l.tm_hour = 6; l.tm_min = 15;
  u.tm_mday = 21; u.tm_yday = 171; u.tm_hour = 20; u.tm_min = 15;
  CHECK(sunUtcOffsetMin(l, u) == 600, "+10h next day: %d", sunUtcOffsetMin(l, u));
  // Local time is on the previous day (UTC-5).
  l.tm_mday = 20; l.tm_yday = 170; l.tm_hour = 22; l.tm_min = 0;
  u.tm_mday = 21; u.tm_yday = 171; u.tm_hour = 3; u.tm_min = 0;
  CHECK(sunUtcOffsetMin(l, u) == -300, "-5h previous day: %d", sunUtcOffsetMin(l, u));
  // Half hour zone (India, +5:30) and +14.
  l.tm_mday = 21; l.tm_yday = 171; l.tm_hour = 18; l.tm_min = 0;
  u.tm_hour = 12; u.tm_min = 30;
  CHECK(sunUtcOffsetMin(l, u) == 330, "+5:30: %d", sunUtcOffsetMin(l, u));
  // New year's eve: local is in the next year.
  struct tm l2 = {}, u2 = {};
  l2.tm_year = 127; l2.tm_mon = 0; l2.tm_mday = 1; l2.tm_yday = 0; l2.tm_hour = 1;
  u2.tm_year = 126; u2.tm_mon = 11; u2.tm_mday = 31; u2.tm_yday = 364; u2.tm_hour = 23;
  CHECK(sunUtcOffsetMin(l2, u2) == 120, "year change: %d", sunUtcOffsetMin(l2, u2));
  // ... and local is in the previous year.
  CHECK(sunUtcOffsetMin(u2, l2) == -120, "year change back: %d", sunUtcOffsetMin(u2, l2));
}

// ---- brightness curve ------------------------------------------------------
static SunTimes day(int rise, int set) {
  SunTimes t = {SUN_NORMAL, (int16_t)rise, (int16_t)set};
  return t;
}
static int at(const SunTimes& t, int h, int m, int s, const SunFade& f) {
  return sunLevel(t, h * 3600 + m * 60 + s, f);
}
// Expected level halfway, worked out here in the plain way: mix the two ends
// in perceived brightness, then undo the 2.2 power.
static int perceptual(int low, int high, double frac) {
  double lo = pow(low / 255.0, 1 / 2.2), hi = pow(high / 255.0, 1 / 2.2);
  return (int)floor(255.0 * pow(lo + (hi - lo) * frac, 2.2) + 0.5);
}

static void testLevelEndpoints() {
  SunTimes t = day(360, 1080);              // 06:00 .. 18:00
  SunFade f = {13, 255, 60, 60};
  CHECK(at(t, 3, 0, 0, f) == 13, "night %d", at(t, 3, 0, 0, f));
  CHECK(at(t, 5, 59, 59, f) == 13, "just before sunrise");
  CHECK(at(t, 6, 0, 0, f) == 13, "at sunrise = low, exactly");
  CHECK(at(t, 7, 0, 0, f) == 255, "end of sunrise fade = high, exactly: %d", at(t, 7, 0, 0, f));
  // (one second before the end, the level has already rounded to 255)
  CHECK(at(t, 6, 59, 0, f) < 255 && at(t, 6, 59, 0, f) > 200, "one minute before the end of the fade");
  CHECK(at(t, 12, 0, 0, f) == 255, "noon");
  CHECK(at(t, 18, 0, 0, f) == 255, "at sunset the fall starts at high");
  CHECK(at(t, 19, 0, 0, f) == 13, "end of sunset fade = low, exactly: %d", at(t, 19, 0, 0, f));
  CHECK(at(t, 18, 59, 0, f) > 13 && at(t, 18, 59, 0, f) < 30, "one minute before the end of the sunset fade");
  CHECK(at(t, 18, 1, 0, f) < 255 && at(t, 18, 1, 0, f) > 200, "one minute into the sunset fade");
  CHECK(at(t, 23, 59, 59, f) == 13, "late night");
  CHECK(at(t, 0, 0, 0, f) == 13, "midnight");
}

static void testLevelPerceptual() {
  SunTimes t = day(360, 1080);
  SunFade f = {13, 255, 60, 60};
  CHECK(at(t, 6, 30, 0, f) == perceptual(13, 255, 0.5), "rise mid: %d vs %d",
        at(t, 6, 30, 0, f), perceptual(13, 255, 0.5));
  CHECK(at(t, 18, 30, 0, f) == perceptual(13, 255, 0.5), "set mid: %d vs %d",
        at(t, 18, 30, 0, f), perceptual(13, 255, 0.5));
  CHECK(at(t, 6, 15, 0, f) == perceptual(13, 255, 0.25), "quarter: %d vs %d",
        at(t, 6, 15, 0, f), perceptual(13, 255, 0.25));
  CHECK(at(t, 18, 45, 0, f) == perceptual(13, 255, 0.25), "falling quarter: %d vs %d",
        at(t, 18, 45, 0, f), perceptual(13, 255, 0.25));
  // And clearly not a straight line in backlight steps (that would be 134).
  CHECK(at(t, 6, 30, 0, f) < 110, "mid-fade level %d is too bright for a perceptual curve",
        at(t, 6, 30, 0, f));
  // A different range, and low = 0 (screen off at night).
  SunFade g = {0, 200, 90, 45};
  CHECK(at(t, 6, 45, 0, g) == perceptual(0, 200, 0.5), "low 0 rise: %d vs %d",
        at(t, 6, 45, 0, g), perceptual(0, 200, 0.5));
  CHECK(at(t, 18, 22, 30, g) == perceptual(0, 200, 0.5), "low 0 set: %d vs %d",
        at(t, 18, 22, 30, g), perceptual(0, 200, 0.5));
  CHECK(at(t, 3, 0, 0, g) == 0, "low 0 at night");
  CHECK(at(t, 12, 0, 0, g) == 200, "high 200 at noon");
  // The two fades are independent: 90 minutes up, 45 down.
  CHECK(at(t, 7, 28, 0, g) < 200 && at(t, 7, 28, 0, g) > 180 && at(t, 7, 30, 0, g) == 200,
        "rise fade is 90 min: %d", at(t, 7, 28, 0, g));
  CHECK(at(t, 18, 40, 0, g) > 0 && at(t, 18, 40, 0, g) < 20 && at(t, 18, 45, 0, g) == 0,
        "set fade is 45 min: %d", at(t, 18, 40, 0, g));
}

static void testLevelMonotonic() {
  // Every second of the day, for several fade lengths: never goes the wrong
  // way, and never jumps by more than one step per second (for fades of
  // 30 minutes or more; the largest change per second is 0.12 steps).
  static const int fades[] = {30, 60, 120};
  static const int lows[] = {0, 5, 13, 100};
  for (int fi = 0; fi < 3; fi++) {
    for (int li = 0; li < 4; li++) {
      SunTimes t = day(430, 1020);
      SunFade f = {(uint8_t)lows[li], 255, (uint16_t)fades[fi], (uint16_t)fades[fi]};
      int prev = sunLevel(t, 0, f);
      for (int s = 1; s < 86400; s++) {
        int cur = sunLevel(t, s, f);
        int riseStart = 430 * 60, riseEnd = riseStart + fades[fi] * 60;
        int setStart = 1020 * 60, setEnd = setStart + fades[fi] * 60;
        if (s > riseStart && s <= riseEnd)
          CHECK(cur >= prev, "rising fade went down at %d: %d -> %d", s, prev, cur);
        if (s > setStart && s <= setEnd)
          CHECK(cur <= prev, "falling fade went up at %d: %d -> %d", s, prev, cur);
        if (s <= riseStart || (s > riseEnd && s <= setStart) || s > setEnd)
          CHECK(cur == prev, "level changed outside a fade at %d: %d -> %d", s, prev, cur);
        CHECK(abs(cur - prev) <= 1, "jump at %d: %d -> %d", s, prev, cur);
        prev = cur;
      }
    }
  }
}

static void testLevelNoFade() {
  SunTimes t = day(360, 1080);
  SunFade f = {20, 255, 0, 0};
  CHECK(at(t, 5, 59, 59, f) == 20, "before sunrise");
  CHECK(at(t, 6, 0, 0, f) == 255, "at sunrise: jump to high");
  CHECK(at(t, 17, 59, 59, f) == 255, "before sunset");
  CHECK(at(t, 18, 0, 0, f) == 20, "at sunset: jump to low");
  SunFade g = {20, 255, 0, 60};   // only the sunset is faded
  CHECK(at(t, 6, 0, 0, g) == 255, "rise jump, set fade");
  CHECK(at(t, 18, 30, 0, g) == perceptual(20, 255, 0.5), "set fade only");
}

static void testLevelEdgeSettings() {
  SunTimes t = day(360, 1080);
  // Lowest brightness above the normal one: the normal one wins everywhere.
  SunFade f = {200, 100, 60, 60};
  for (int h = 0; h < 24; h++)
    CHECK(at(t, h, 30, 0, f) == 100, "low > high at %d h: %d", h, at(t, h, 30, 0, f));
  // Equal: flat.
  SunFade e = {77, 77, 60, 60};
  for (int h = 0; h < 24; h++)
    CHECK(at(t, h, 15, 0, e) == 77, "low == high at %d h", h);
  // Polar day and polar night ignore the clock.
  SunTimes pd = {SUN_POLAR_DAY, 0, 0}, pn = {SUN_POLAR_NIGHT, 0, 0};
  SunFade g = {9, 240, 60, 60};
  for (int h = 0; h < 24; h++) {
    CHECK(at(pd, h, 0, 0, g) == 240, "polar day %d h", h);
    CHECK(at(pn, h, 0, 0, g) == 9, "polar night %d h", h);
  }
}

static void testLevelAcrossMidnight() {
  // Reykjavik in June: sunrise 02:55, sunset 00:03 (the next calendar day).
  SunTimes t = day(175, 3);
  SunFade f = {10, 255, 60, 60};
  CHECK(at(t, 12, 0, 0, f) == 255, "noon");
  CHECK(at(t, 23, 0, 0, f) == 255, "23:00 is still day");
  CHECK(at(t, 0, 3, 0, f) == 255, "at sunset after midnight");
  CHECK(at(t, 0, 33, 0, f) == perceptual(10, 255, 0.5), "mid sunset fade after midnight: %d",
        at(t, 0, 33, 0, f));
  CHECK(at(t, 1, 3, 0, f) == 10, "fade over");
  CHECK(at(t, 2, 54, 59, f) == 10, "just before sunrise");
  CHECK(at(t, 3, 25, 0, f) == perceptual(10, 255, 0.5), "mid sunrise fade: %d", at(t, 3, 25, 0, f));
  CHECK(at(t, 3, 55, 0, f) == 255, "sunrise fade over");
  // Sunrise before midnight in local time and sunset after it (far east of
  // the time zone's meridian).
  SunTimes u = day(1380, 700);        // 23:00 .. 11:40 the next day
  CHECK(at(u, 23, 0, 0, f) == 10, "rise at 23:00 starts low");
  CHECK(at(u, 5, 0, 0, f) == 255, "05:00 is day");
  CHECK(at(u, 15, 0, 0, f) == 10, "15:00 is night");
}

static void testFadesLongerThanTheDay() {
  // A day of 100 minutes with fades of 120: no step ever jumps, and the
  // level never reaches high (it takes the lower of the two fades).
  SunTimes t = day(600, 700);
  SunFade f = {10, 255, 120, 120};
  int prev = sunLevel(t, 0, f), top = 0;
  for (int s = 1; s < 86400; s++) {
    int cur = sunLevel(t, s, f);
    CHECK(abs(cur - prev) <= 1, "jump at %d: %d -> %d", s, prev, cur);
    if (cur > top) top = cur;
    prev = cur;
  }
  CHECK(top < 255 && top > 100, "highest level %d", top);
  // A night of 60 minutes with a sunset fade of 120: the fall is cut at
  // sunrise, so the level is already low then and does not jump.
  SunTimes n = day(0, 1380);          // night 23:00 .. 24:00
  SunFade g = {10, 255, 30, 120};
  prev = sunLevel(n, 0, g);
  for (int s = 1; s < 86400; s++) {
    int cur = sunLevel(n, s, g);
    CHECK(abs(cur - prev) <= 1, "short night, jump at %d: %d -> %d", s, prev, cur);
    prev = cur;
  }
  CHECK(sunLevel(n, 86399, g) <= 12, "end of a short night: %d", sunLevel(n, 86399, g));
}

// ---- overlap with night mode ----------------------------------------------
static void testCombine() {
  // night mode off: the sun level, in every mode
  for (int m = 0; m < 3; m++) {
    CHECK(sunCombine((SunOverlap)m, false, 5, 200) == 200, "mode %d, night off, night darker", m);
    CHECK(sunCombine((SunOverlap)m, false, 250, 100) == 100, "mode %d, night off, night brighter", m);
  }
  // night mode active, night level darker than the sun level
  CHECK(sunCombine(SUN_OVERLAP_NIGHT_FIRST, true, 5, 200) == 5, "night first, night darker");
  CHECK(sunCombine(SUN_OVERLAP_SUN_FIRST, true, 5, 200) == 200, "sun first, night darker");
  CHECK(sunCombine(SUN_OVERLAP_DARKER, true, 5, 200) == 5, "darker, night darker");
  // night mode active, night level brighter than the sun level
  CHECK(sunCombine(SUN_OVERLAP_NIGHT_FIRST, true, 250, 100) == 250, "night first, night brighter");
  CHECK(sunCombine(SUN_OVERLAP_SUN_FIRST, true, 250, 100) == 100, "sun first, night brighter");
  CHECK(sunCombine(SUN_OVERLAP_DARKER, true, 250, 100) == 100, "darker, night brighter");
  // equal and zero
  CHECK(sunCombine(SUN_OVERLAP_DARKER, true, 0, 100) == 0, "darker, night off-level 0");
  CHECK(sunCombine(SUN_OVERLAP_DARKER, true, 100, 0) == 0, "darker, sun level 0");
  CHECK(sunCombine(SUN_OVERLAP_NIGHT_FIRST, true, 0, 100) == 0, "night first, level 0");
}

int main() {
  testReferenceTable();
  testSunProperties();
  testUtcOffset();
  testLevelEndpoints();
  testLevelPerceptual();
  testLevelMonotonic();
  testLevelNoFade();
  testLevelEdgeSettings();
  testLevelAcrossMidnight();
  testFadesLongerThanTheDay();
  testCombine();
  printf("%d checks, %d failed\n", g_checks, g_failed);
  if (g_failed) return 1;
  printf("sun tests passed\n");
  return 0;
}
