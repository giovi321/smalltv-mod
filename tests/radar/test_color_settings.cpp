// Radar color settings: defaults, independent updates, validation, and backup.
// Built with the stand-in core headers of the fuzz target (see run_colors.sh).
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

static const char* keys[] = {
  "markerColor", "lineColor", "aircraftColor", "airportColor", "labelColor"
};
static const char* defaults[] = {
  "#ff0000", "#424142", "#848284", "#0082ff", "#848284"
};
static std::string color(const Settings& s, const char* key) {
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>(), false);
  return doc["radar"][key] | "missing";
}

int main() {
  Settings s; s.setDefaults();
  apply(s, R"({"radar":{"rangeKm":25}})");
  for (unsigned i = 0; i < 5; i++) {
    CHECK(color(s, keys[i]) == defaults[i], "%s original palette", keys[i]);
  }
  // Every field accepts a full RGB hex value, independently of the others.
  for (unsigned i = 0; i < 5; i++) {
    s.setDefaults();
    char json[160];
    snprintf(json, sizeof(json), "{\"radar\":{\"%s\":\"#12ABef\"}}", keys[i]);
    apply(s, json);
    CHECK(color(s, keys[i]) == "#12abef", "%s normalized", keys[i]);
    for (unsigned j = 0; j < 5; j++) {
      if (j != i) CHECK(color(s, keys[j]) == defaults[j], "%s independent", keys[j]);
    }
    apply(s, R"({"brightness":75,"radar":{"showLabels":false}})");
    CHECK(color(s, keys[i]) == "#12abef", "%s partial save", keys[i]);
    CHECK(s.brightness == 75 && !s.radar.showLabels, "other settings apply");
    const char* invalid[] = {
      "\"#fff\"", "\"ffffff\"", "\"#12345g\"", "\"#1234567\"",
      "\"# 12345\"", "\"\"", "-1", "16777216", "true", "null", "[]", "{}"
    };
    for (const char* value : invalid) {
      snprintf(json, sizeof(json), "{\"radar\":{\"%s\":%s}}", keys[i], value);
      apply(s, json);
      CHECK(color(s, keys[i]) == "#12abef", "%s invalid %s rejected", keys[i], value);
    }
    for (bool secrets : {false, true}) {
      JsonDocument backup;
      settingsToJson(s, backup.to<JsonObject>(), secrets);
      Settings restored; restored.setDefaults();
      settingsApplyJson(restored, backup.as<JsonObjectConst>());
      CHECK(color(restored, keys[i]) == "#12abef", "%s backup/import", keys[i]);
    }
    snprintf(json, sizeof(json), "{\"radar\":{\"%s\":\"#000000\"}}", keys[i]);
    apply(s, json);
    CHECK(color(s, keys[i]) == "#000000", "%s black accepted", keys[i]);
    snprintf(json, sizeof(json), "{\"radar\":{\"%s\":\"#FFFFFF\"}}", keys[i]);
    apply(s, json);
    CHECK(color(s, keys[i]) == "#ffffff", "%s white accepted", keys[i]);
  }
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
