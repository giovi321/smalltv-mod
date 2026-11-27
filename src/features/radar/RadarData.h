// RadarData.h — runtime (volatile) data for the plane radar.
#pragma once
#include <Arduino.h>
#include "config.h"

// One aircraft as parsed from the feed, plus its polar position relative to home.
struct Aircraft {
  float   lat, lon;
  float   track;       // ground track, degrees (0 = N, 90 = E); NAN if unknown
  float   gs;          // ground speed, knots; NAN if unknown
  int32_t altFt;       // barometric altitude, ft (0 when on ground / unknown)
  char    callsign[9]; // flight id, trimmed
  float   distKm;      // flat-earth distance approximation from home
  float   bearingDeg;  // bearing from home, degrees (0 = N)
};
