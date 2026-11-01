// SunTimes.h - sunrise and sunset worked out on the device, the brightness
// curve that follows them, and the rule for combining it with night mode.
//
// Pure logic: no Arduino headers, no clock access, no allocation. Callers
// pass the local date, the UTC offset and the time of day as numbers, so the
// native tests can use any date. Sun times are whole minutes; the curve
// works in seconds so the fade is smooth.
#pragma once
#include <stdint.h>
#include <time.h>

enum SunKind : uint8_t {
  SUN_NORMAL = 0,       // the sun rises and sets this day
  SUN_POLAR_DAY = 1,    // the sun stays up all day
  SUN_POLAR_NIGHT = 2   // the sun stays down all day
};

struct SunTimes {
  SunKind kind;
  int16_t rise;  // minutes since local midnight, 0..1439 (SUN_NORMAL only)
  int16_t set;   // same. May be smaller than rise: the sun sets after midnight.
};

// Sunrise and sunset for the local date (year, month 1..12, day 1..31) at
// latitude/longitude in degrees (north and east are positive).
// utcOffsetMin is the local time zone offset from UTC in minutes, daylight
// saving included (+60 = one hour ahead of UTC). Accuracy is about one
// minute outside the polar circles.
SunTimes sunTimes(float lat, float lon, int year, int month, int day,
                  int utcOffsetMin);

// UTC offset in minutes, from the same moment as local and UTC broken-down
// time (localtime_r and gmtime_r of one time_t).
int sunUtcOffsetMin(const struct tm& local, const struct tm& utc);

struct SunFade {
  uint8_t low;       // backlight at night, 0..255 steps (0 = off)
  uint8_t high;      // backlight by day, 0..255 steps
  uint16_t riseMin;  // sunrise fade, minutes (0 = jump)
  uint16_t setMin;   // sunset fade, minutes (0 = jump)
};

// Backlight steps (0..255) at nowSec seconds since local midnight.
// The fades start AT sunrise and AT sunset. The steps between low and high
// follow a perceptual curve: equal time steps look like equal changes in
// brightness. The endpoints are exactly low and high. If low is above
// high, high wins.
uint8_t sunLevel(const SunTimes& t, int nowSec, const SunFade& f);

enum SunOverlap : uint8_t {
  SUN_OVERLAP_NIGHT_FIRST = 0,  // night mode wins while its window is active
  SUN_OVERLAP_SUN_FIRST = 1,    // night mode is ignored
  SUN_OVERLAP_DARKER = 2        // the lower of the two
};

// Combines the sun level with night mode. nightSteps is night mode's level
// in the same 0..255 steps.
uint8_t sunCombine(SunOverlap mode, bool nightActive, uint8_t nightSteps,
                   uint8_t sunSteps);
