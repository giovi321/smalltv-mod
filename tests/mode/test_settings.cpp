// The "mode" key of a save or an import. A save that does not name a mode the
// build knows must leave the stored mode alone. Before, an empty or unknown
// text turned the mode into the stock ticker. Built with the stand-in core
// headers of the fuzz target (see tests/mode/run.sh).
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "Settings.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <string>

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

// Start from a device that stored `mode`, then apply a save.
static uint8_t after(uint8_t stored, const char* json) {
  Settings s;
  s.setDefaults();
  s.mode = stored;
  apply(s, json);
  return s.mode;
}

static void testNamesSetTheMode() {
  CHECK(after(MODE_STOCKS, "{\"mode\":\"theme\"}") == MODE_THEME, "theme");
  CHECK(after(MODE_STOCKS, "{\"mode\":\"radar\"}") == MODE_RADAR, "radar");
  CHECK(after(MODE_STOCKS, "{\"mode\":\"usage\"}") == MODE_USAGE, "usage");
  CHECK(after(MODE_STOCKS, "{\"mode\":\"ha\"}") == MODE_HA, "ha");
  CHECK(after(MODE_STOCKS, "{\"mode\":\"carousel\"}") == MODE_CAROUSEL, "carousel");
  CHECK(after(MODE_RADAR, "{\"mode\":\"stocks\"}") == MODE_STOCKS, "stocks");
  CHECK(after(MODE_RADAR, "{\"mode\":\"Theme\"}") == MODE_THEME, "case is ignored");
}

static void testEmptyMode() {
  // The page sends "" when its mode list lacks the stored mode (a build
  // without that feature). The stored mode stays.
  CHECK(after(MODE_RADAR, "{\"mode\":\"\"}") == MODE_RADAR, "radar kept");
  CHECK(after(MODE_THEME, "{\"mode\":\"\"}") == MODE_THEME, "theme kept");
  CHECK(after(MODE_HA, "{\"mode\":\"\"}") == MODE_HA, "ha kept");
  CHECK(after(MODE_CAROUSEL, "{\"mode\":\"\"}") == MODE_CAROUSEL, "carousel kept");
}

static void testUnknownMode() {
  CHECK(after(MODE_USAGE, "{\"mode\":\"nonsense\"}") == MODE_USAGE, "unknown text kept");
  CHECK(after(MODE_USAGE, "{\"mode\":\"theme2\"}") == MODE_USAGE, "near miss kept");
}

static void testNoModeOrOtherType() {
  CHECK(after(MODE_HA, "{\"brightness\":40}") == MODE_HA, "no mode key");
  CHECK(after(MODE_HA, "{\"mode\":7}") == MODE_HA, "number");
  CHECK(after(MODE_HA, "{\"mode\":null}") == MODE_HA, "null");
}

static void testImport() {
  // An import goes through the same parser, onto the stored settings: an
  // export from a lean build with an empty mode does not move the mode.
  Settings s;
  s.setDefaults();
  apply(s, "{\"mode\":\"radar\",\"hostname\":\"smalltv-test\"}");
  CHECK(s.mode == MODE_RADAR, "stored radar");
  apply(s, "{\"mode\":\"\",\"hostname\":\"smalltv-test-2\"}");
  CHECK(s.mode == MODE_RADAR, "import with empty mode keeps radar");
  CHECK(s.hostname == "smalltv-test-2", "the rest of the import applies");
}

// Radar color defaults, partial updates, validation and backup/import.
static std::string radarColor(const Settings& s, const char* key = "lineColor") {
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>(), false);
  return doc["radar"][key] | "missing";
}
static void testRadarColor() {
  Settings s; s.setDefaults();
  CHECK(radarColor(s) == "#424142", "radar color persistence");
  apply(s, R"({"radar":{"rangeKm":25}})");
  CHECK(radarColor(s) == "#424142", "radar color persistence");
  apply(s, R"({"radar":{"uiColor":"#12ABef"}})");
  CHECK(radarColor(s) == "#12abef", "radar color persistence");
  apply(s, R"({"brightness":75,"radar":{"showLabels":false}})");
  CHECK(radarColor(s) == "#12abef", "radar color persistence");
  CHECK(s.brightness == 75 && !s.radar.showLabels, "radar color persistence");
  JsonDocument backup;
  settingsToJson(s, backup.to<JsonObject>(), true);
  Settings restored; restored.setDefaults();
  settingsApplyJson(restored, backup.as<JsonObjectConst>());
  CHECK(radarColor(restored) == "#12abef", "radar color persistence");
  const char* invalid[] = {
    R"({"radar":{"uiColor":"#fff"}})",
    R"({"radar":{"uiColor":"ffffff"}})",
    R"({"radar":{"uiColor":"#12345g"}})",
    R"({"radar":{"uiColor":"#1234567"}})",
    R"({"radar":{"uiColor":"# 12345"}})",
    R"({"radar":{"uiColor":""}})",
    R"({"radar":{"uiColor":-1}})",
    R"({"radar":{"uiColor":16777216}})",
    R"({"radar":{"uiColor":true}})",
    R"({"radar":{"uiColor":null}})",
    R"({"radar":{"uiColor":[]}})"
  };
  for (const char* json : invalid) { apply(s, json); CHECK(radarColor(s) == "#12abef", "radar color persistence"); }
  apply(s, R"({"radar":{"uiColor":"#000000"}})"); CHECK(radarColor(s) == "#000000", "radar color persistence");
  apply(s, R"({"radar":{"uiColor":"#FFFFFF"}})"); CHECK(radarColor(s) == "#ffffff", "radar color persistence");
}

static void testSeparateRadarColors() {
  Settings s; s.setDefaults();
  CHECK(radarColor(s, "lineColor") == "#424142", "default grid");
  CHECK(radarColor(s, "aircraftColor") == "#848284", "default aircraft info");
  CHECK(radarColor(s, "labelColor") == "#848284", "default general text");
  CHECK(radarColor(s, "airportColor") == "#0082ff", "default airport blue");
  apply(s, R"({"radar":{"uiColor":"#123456"}})");
  for (const char* key : {"lineColor", "aircraftColor", "airportColor", "labelColor"})
    CHECK(radarColor(s, key) == "#123456", "legacy color migrates to every group");
  apply(s, R"({"radar":{"lineColor":"#000000","aircraftColor":"#12ABef","airportColor":"#bb0011","labelColor":"#FFFFFF"}})");
  CHECK(radarColor(s, "lineColor") == "#000000", "independent grid");
  CHECK(radarColor(s, "aircraftColor") == "#12abef", "independent aircraft info");
  CHECK(radarColor(s, "labelColor") == "#ffffff", "independent labels");
  CHECK(radarColor(s, "airportColor") == "#bb0011", "independent airports");
  apply(s, R"({"radar":{"lineColor":"#010203"}})");
  CHECK(radarColor(s, "lineColor") == "#010203", "partial grid update");
  CHECK(radarColor(s, "aircraftColor") == "#12abef", "aircraft info preserved");
  CHECK(radarColor(s, "labelColor") == "#ffffff", "labels preserved");
  CHECK(radarColor(s, "airportColor") == "#bb0011", "airport color preserved");
  apply(s, R"({"radar":{"lineColor":"#12345g","aircraftColor":false,"airportColor":"#xyzxyz","labelColor":"# 12345"}})");
  CHECK(radarColor(s, "lineColor") == "#010203", "invalid grid rejected");
  CHECK(radarColor(s, "aircraftColor") == "#12abef", "invalid aircraft info rejected");
  CHECK(radarColor(s, "labelColor") == "#ffffff", "invalid labels rejected");
  CHECK(radarColor(s, "airportColor") == "#bb0011", "invalid airport color rejected");
  JsonDocument backup; settingsToJson(s, backup.to<JsonObject>(), true);
  Settings restored; restored.setDefaults(); settingsApplyJson(restored, backup.as<JsonObjectConst>());
  CHECK(radarColor(restored, "lineColor") == "#010203", "grid backup/import");
  CHECK(radarColor(restored, "aircraftColor") == "#12abef", "aircraft backup/import");
  CHECK(radarColor(restored, "labelColor") == "#ffffff", "label backup/import");
  CHECK(radarColor(restored, "airportColor") == "#bb0011", "airport backup/import");
  apply(s, R"({"radar":{"uiColor":"#222222","lineColor":"#333333"}})");
  CHECK(radarColor(s, "lineColor") == "#333333", "specific color overrides legacy");
  CHECK(radarColor(s, "aircraftColor") == "#222222", "legacy fallback for other groups");
}

static void testStartupColor() {
  Settings s; s.setDefaults();
  CHECK(s.startupTextColor == 0x848284, "original startup gray");
  apply(s, R"({"startupTextColor":"#ffffff"})");
  CHECK(s.startupTextColor == 0xffffff, "startup color accepted");
  apply(s, R"({"brightness":90,"startupTextColor":"#12345g"})");
  CHECK(s.startupTextColor == 0xffffff, "invalid startup color preserves value");
  JsonDocument backup; settingsToJson(s, backup.to<JsonObject>(), true);
  CHECK(std::string(backup["startupTextColor"].as<const char*>()) == "#ffffff", "startup exported");
  Settings restored; restored.setDefaults(); settingsApplyJson(restored, backup.as<JsonObjectConst>());
  CHECK(restored.startupTextColor == 0xffffff, "startup backup/import");
}

static void testMarkerColor() {
  Settings s; s.setDefaults();
  CHECK(radarColor(s, "markerColor") == "#ff0000", "original marker red");
  apply(s, R"({"radar":{"markerColor":"#12ABef"}})");
  CHECK(radarColor(s, "markerColor") == "#12abef", "marker color accepted");
  CHECK(radarColor(s, "aircraftColor") == "#848284", "marker independent of text");
  apply(s, R"({"radar":{"uiColor":"#222222","markerColor":"#12345g"}})");
  CHECK(radarColor(s, "markerColor") == "#12abef", "invalid marker and legacy text preserve marker");
  apply(s, R"({"brightness":90})");
  CHECK(radarColor(s, "markerColor") == "#12abef", "partial save preserves marker");
  JsonDocument backup; settingsToJson(s, backup.to<JsonObject>(), true);
  Settings restored; restored.setDefaults(); settingsApplyJson(restored, backup.as<JsonObjectConst>());
  CHECK(radarColor(restored, "markerColor") == "#12abef", "marker backup/import");
}

int main() {
  testNamesSetTheMode();
  testEmptyMode();
  testUnknownMode();
  testNoModeOrOtherType();
  testImport();
  testRadarColor();
  testSeparateRadarColors();
  testStartupColor();
  testMarkerColor();
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
