// Settings for the sun schedule and the device-wide location: defaults,
// limits, the JSON round trip, the move from older config files, the radar's
// "use the device location" switch, and "sun schedule or light sensor, not both".
// Built with the stand-in core headers of the fuzz target (see tests/sun/run.sh).
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "Settings.h"
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>

// Stand-ins the real Settings.cpp links against (as in tests/fuzz/fuzz_parsers.cpp).
FSClass LittleFS;
EspClass ESP;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond, ...)                                                   \
  do {                                                                     \
    g_checks++;                                                            \
    if (!(cond)) {                                                         \
      g_failed++;                                                          \
      printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);               \
      printf(__VA_ARGS__);                                                 \
      printf("\n");                                                        \
    }                                                                      \
  } while (0)

static void apply(Settings& s, const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) { printf("bad test json: %s\n", json); g_failed++; return; }
  settingsApplyJson(s, doc.as<JsonObjectConst>());
}

static void testDefaults() {
  Settings s;
  s.setDefaults();
  CHECK(!s.clock.sunEnabled, "sun off by default");
  CHECK(s.clock.sunLow == 5, "lowest brightness %d", s.clock.sunLow);
  CHECK(s.clock.sunRise == 60 && s.clock.sunSet == 60, "fades %d %d", s.clock.sunRise, s.clock.sunSet);
  CHECK(s.clock.sunOverlap == 0, "overlap: night mode first");
  CHECK(s.clock.lat == 0.0f && s.clock.lon == 0.0f, "no location");
  CHECK(s.radar.useDevice, "radar uses the device location by default");
}

static void testLimits() {
  Settings s;
  s.setDefaults();
  apply(s, "{\"clock\":{\"sunEnabled\":true,\"sunLow\":0,\"sunRise\":0,\"sunSet\":120,\"sunOverlap\":2,\"lat\":52.52,\"lon\":13.41}}");
  CHECK(s.clock.sunEnabled, "enabled");
  CHECK(s.clock.sunLow == 0, "lowest brightness 0 is allowed");
  CHECK(s.clock.sunRise == 0 && s.clock.sunSet == 120, "fades 0 and 120");
  CHECK(s.clock.sunOverlap == 2, "overlap darker");
  CHECK(fabsf(s.clock.lat - 52.52f) < 1e-4f && fabsf(s.clock.lon - 13.41f) < 1e-4f, "location");
  apply(s, "{\"clock\":{\"sunLow\":250,\"sunRise\":999,\"sunSet\":-5,\"sunOverlap\":9,\"lat\":123.0,\"lon\":-400.0}}");
  CHECK(s.clock.sunLow == 100, "lowest brightness capped at 100: %d", s.clock.sunLow);
  CHECK(s.clock.sunRise == 120, "sunrise fade capped at 120: %d", s.clock.sunRise);
  CHECK(s.clock.sunSet == 0, "sunset fade floored at 0: %d", s.clock.sunSet);
  CHECK(s.clock.sunOverlap == 2, "overlap capped at 2: %d", s.clock.sunOverlap);
  CHECK(s.clock.lat == 90.0f, "latitude capped at 90: %f", s.clock.lat);
  CHECK(s.clock.lon == -180.0f, "longitude capped at -180: %f", s.clock.lon);
  // Whole numbers are locations too (a page or a script may send 45, not 45.0).
  apply(s, "{\"clock\":{\"lat\":45,\"lon\":-7}}");
  CHECK(s.clock.lat == 45.0f && s.clock.lon == -7.0f, "integer location: %f %f", s.clock.lat, s.clock.lon);
  apply(s, "{\"clock\":{\"lat\":123.0,\"lon\":-400.0}}");
  // Wrong types are ignored, the old value stays.
  apply(s, "{\"clock\":{\"sunLow\":\"x\",\"sunEnabled\":5,\"lat\":\"north\"}}");
  CHECK(s.clock.sunLow == 100 && s.clock.sunEnabled && s.clock.lat == 90.0f, "wrong types ignored");
  // A partial update leaves the other fields alone.
  apply(s, "{\"clock\":{\"sunRise\":30}}");
  CHECK(s.clock.sunRise == 30 && s.clock.sunSet == 0 && s.clock.sunLow == 100, "partial update");
}

static void testRoundTrip() {
  Settings s;
  s.setDefaults();
  apply(s, "{\"clock\":{\"sunEnabled\":true,\"sunLow\":7,\"sunRise\":45,\"sunSet\":90,\"sunOverlap\":1,\"lat\":-33.87,\"lon\":151.21},"
           "\"radar\":{\"useDevice\":false,\"lat\":1.5,\"lon\":2.5}}");
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>(), true);
  Settings t;
  t.setDefaults();
  settingsApplyJson(t, doc.as<JsonObjectConst>());
  CHECK(t.clock.sunEnabled && t.clock.sunLow == 7 && t.clock.sunRise == 45 &&
        t.clock.sunSet == 90 && t.clock.sunOverlap == 1, "sun settings survive a round trip");
  CHECK(fabsf(t.clock.lat + 33.87f) < 1e-3f && fabsf(t.clock.lon - 151.21f) < 1e-3f, "location survives");
  CHECK(!t.radar.useDevice && t.radar.lat == 1.5f && t.radar.lon == 2.5f, "radar switch and own location survive");
  // The web API output (no secrets) carries them too, for the settings page.
  JsonDocument api;
  settingsToJson(s, api.to<JsonObject>(), false);
  CHECK(api["clock"]["sunEnabled"].as<bool>() == true && api["clock"]["sunRise"].as<int>() == 45 &&
        api["radar"]["useDevice"].is<bool>(), "present in the API output");
}

static void testOldConfigs() {
  // A config from before this feature: nothing new in it.
  Settings s;
  s.setDefaults();
  apply(s, "{\"brightness\":40,\"clock\":{\"tz\":\"Europe/Rome\",\"nightEnabled\":true,\"nightLevel\":10}}");
  CHECK(!s.clock.sunEnabled && s.clock.sunLow == 5 && s.clock.sunRise == 60, "defaults for a config without the keys");
  CHECK(s.clock.nightEnabled && s.clock.nightLevel == 10, "older keys still read");
  // A radar home from before the device-wide location must keep working:
  // coordinates but no switch means "own coordinates".
  Settings r;
  r.setDefaults();
  apply(r, "{\"radar\":{\"lat\":52.3676,\"lon\":4.9041,\"rangeKm\":30}}");
  CHECK(!r.radar.useDevice, "old radar home keeps its own coordinates");
  CHECK(fabsf(r.radar.lat - 52.3676f) < 1e-4f, "old radar latitude");
  // An old config whose radar home was never set follows the device location.
  Settings z;
  z.setDefaults();
  apply(z, "{\"radar\":{\"lat\":0,\"lon\":0,\"rangeKm\":30}}");
  CHECK(z.radar.useDevice, "unset radar home follows the device location");
  // A home on the equator (latitude 0) is still a home: longitude decides too.
  Settings q;
  q.setDefaults();
  apply(q, "{\"radar\":{\"lat\":0,\"lon\":4.9041}}");
  CHECK(!q.radar.useDevice, "old radar home with latitude 0 keeps its own coordinates");
  // And the other way round: a home on the prime meridian (longitude 0).
  Settings p;
  p.setDefaults();
  apply(p, "{\"radar\":{\"lat\":51.5,\"lon\":0}}");
  CHECK(!p.radar.useDevice, "old radar home with longitude 0 keeps its own coordinates");
  // A partial save that does not mention the home must not touch the switch,
  // even when coordinates are stored (a script posting only the range).
  Settings m;
  m.setDefaults();
  m.radar.useDevice = true;
  m.radar.lat = 52.3676f; m.radar.lon = 4.9041f;
  apply(m, "{\"radar\":{\"rangeKm\":50}}");
  CHECK(m.radar.useDevice, "partial save without lat/lon keeps the switch on");
  CHECK(m.radar.rangeKm == 50, "partial save still applies the range");
  CHECK(fabsf(m.radar.lat - 52.3676f) < 1e-4f && fabsf(m.radar.lon - 4.9041f) < 1e-4f,
        "partial save leaves the stored coordinates alone");
  // A partial save that carries one coordinate counts as a home, as in a file.
  Settings n;
  n.setDefaults();
  apply(n, "{\"radar\":{\"lon\":4.9041}}");
  CHECK(!n.radar.useDevice, "a save with only a longitude is a home");
  // A save that carries a 0/0 home leaves the radar on the device location.
  Settings u;
  u.setDefaults();
  apply(u, "{\"radar\":{\"lat\":0,\"lon\":0}}");
  CHECK(u.radar.useDevice, "a save with a 0/0 home stays on the device location");
  // The switch, when present, wins over the guess.
  Settings w;
  w.setDefaults();
  apply(w, "{\"radar\":{\"useDevice\":true,\"lat\":52.3,\"lon\":4.9}}");
  CHECK(w.radar.useDevice, "explicit switch on");
  apply(w, "{\"radar\":{\"useDevice\":false}}");
  CHECK(!w.radar.useDevice, "explicit switch off");
}

static void testRadarLocation() {
  Settings s;
  s.setDefaults();
  s.clock.lat = 10.0f; s.clock.lon = 20.0f;
  s.radar.lat = 1.0f; s.radar.lon = 2.0f;
  s.radar.useDevice = true;
  CHECK(radarLat(s) == 10.0f && radarLon(s) == 20.0f, "device location");
  s.radar.useDevice = false;
  CHECK(radarLat(s) == 1.0f && radarLon(s) == 2.0f, "own location");
}

static void testExclusive() {
  Settings s;
  s.setDefaults();
  apply(s, "{\"autoBrightness\":true}");
  CHECK(s.autoBrightness && !s.clock.sunEnabled, "light sensor alone");
  // Turning the sun schedule on in a later save turns the sensor off.
  apply(s, "{\"clock\":{\"sunEnabled\":true}}");
  CHECK(s.clock.sunEnabled && !s.autoBrightness, "sun on turns the sensor off");
  // And the other way round.
  apply(s, "{\"autoBrightness\":true}");
  CHECK(s.autoBrightness && !s.clock.sunEnabled, "sensor on turns the sun schedule off");
  // Both in one save, as the settings page sends them: only one can be true.
  Settings t;
  t.setDefaults();
  apply(t, "{\"autoBrightness\":true,\"clock\":{\"sunEnabled\":true}}");
  CHECK(!(t.autoBrightness && t.clock.sunEnabled), "never both");
  CHECK(t.clock.sunEnabled, "sun wins when both arrive together");
  // A stored config with both (hand edited): sun wins.
  Settings u;
  u.setDefaults();
  u.autoBrightness = true;
  apply(u, "{\"clock\":{\"sunEnabled\":true,\"sunLow\":4}}");
  CHECK(u.clock.sunEnabled && !u.autoBrightness, "stored sensor switched off");
}

int main() {
  testDefaults();
  testLimits();
  testRoundTrip();
  testOldConfigs();
  testRadarLocation();
  testExclusive();
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
