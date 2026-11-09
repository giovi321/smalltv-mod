#pragma once
#include "Arduino.h"
struct Settings {
  struct Radar {
    unsigned source = 1, rangeKm = 20, minAltFt = 0, pollSec = 10;
    String webhookUrl = "http://receiver/aircraft.json";
  } radar;
  unsigned httpTimeout = 100;
};
inline float radarLat(const Settings&) { return 39.7f; }
inline float radarLon(const Settings&) { return -105.1f; }
