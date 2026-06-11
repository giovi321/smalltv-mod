#include "Settings.h"
#include <LittleFS.h>

static const char* CONFIG_PATH = "/config.json";

// ---------------------------------------------------------------------------
void Settings::setDefaults() {
  staSsid = "";
  staPass = "";
  apSsid  = DEFAULT_AP_SSID;
  apPass  = DEFAULT_AP_PASS;
  hostname = DEFAULT_HOSTNAME;

  mode = DEFAULT_MODE;
  source = DEFAULT_SOURCE;
  webhookUrl = "";
  usageUrl = "";
  range = DEFAULT_RANGE;
  points = DEFAULT_POINTS;
  pollSec = DEFAULT_POLL_SEC;
  rotateSec = DEFAULT_ROTATE_SEC;
  httpTimeout = DEFAULT_HTTP_TIMEOUT;

  brightness = DEFAULT_BRIGHTNESS;
  autoBrightness = false;
  backlightInverted = TFT_BL_DEFAULT_INVERTED;
  rotation = 0;
  colorInverted = false;

  showName = true;
  showPrice = true;
  showChange = true;
  showChart = true;
  showRangeLabel = true;
  showUpdatedAgo = false;
  showPageDots = true;

  symbolCount = 0;
  for (uint8_t i = 0; i < MAX_SYMBOLS; i++) {
    symbols[i].symbol[0] = 0;
    symbols[i].name[0] = 0;
  }
}

// ---------------------------------------------------------------------------
bool settingsBegin() {
  if (LittleFS.begin()) return true;
  // First boot on a fresh chip: format then mount.
  if (LittleFS.format() && LittleFS.begin()) return true;
  return false;
}

bool loadSettings(Settings& s) {
  s.setDefaults();
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  settingsApplyJson(s, doc.as<JsonObjectConst>());
  return true;
}

bool saveSettings(const Settings& s) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(s, root, /*includeSecrets=*/true);

  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}

void factoryReset(Settings& s) {
  LittleFS.remove(CONFIG_PATH);
  s.setDefaults();
}

// ---------------------------------------------------------------------------
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets) {
  root["hostname"]   = s.hostname;

  // WiFi
  root["staSsid"]    = s.staSsid;
  root["staPassSet"] = s.staPass.length() > 0;
  root["apSsid"]     = s.apSsid;
  root["apPassSet"]  = s.apPass.length() > 0;
  if (includeSecrets) {
    root["staPass"]  = s.staPass;
    root["apPass"]   = s.apPass;
  }

  // Mode + data
  root["mode"]        = (s.mode == MODE_USAGE) ? "usage" : "stocks";
  root["source"]      = (s.source == SRC_YAHOO) ? "yahoo" : "webhook";
  root["webhookUrl"]  = s.webhookUrl;
  root["usageUrl"]    = s.usageUrl;
  root["range"]       = s.range;
  root["points"]      = s.points;
  root["pollSec"]     = s.pollSec;
  root["rotateSec"]   = s.rotateSec;
  root["httpTimeout"] = s.httpTimeout;

  // Display
  root["brightness"]        = s.brightness;
  root["autoBrightness"]    = s.autoBrightness;
  root["backlightInverted"] = s.backlightInverted;
  root["rotation"]          = s.rotation;
  root["colorInverted"]     = s.colorInverted;

  // Show flags
  root["showName"]       = s.showName;
  root["showPrice"]      = s.showPrice;
  root["showChange"]     = s.showChange;
  root["showChart"]      = s.showChart;
  root["showRangeLabel"] = s.showRangeLabel;
  root["showUpdatedAgo"] = s.showUpdatedAgo;
  root["showPageDots"]   = s.showPageDots;

  // Symbols
  JsonArray arr = root["symbols"].to<JsonArray>();
  for (uint8_t i = 0; i < s.symbolCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["symbol"] = s.symbols[i].symbol;
    o["name"]   = s.symbols[i].name;
  }
}

// Apply only the keys that are present (partial update friendly).
void settingsApplyJson(Settings& s, JsonObjectConst root) {
  if (root["hostname"].is<const char*>()) s.hostname = root["hostname"].as<String>();

  if (root["staSsid"].is<const char*>()) s.staSsid = root["staSsid"].as<String>();
  // Station password: apply only if a non-empty value is supplied (blank = keep).
  if (root["staPass"].is<const char*>()) {
    String p = root["staPass"].as<String>();
    if (p.length() > 0) s.staPass = p;
  }
  if (root["apSsid"].is<const char*>()) s.apSsid = root["apSsid"].as<String>();
  // AP password: apply as-is when present (empty allowed => open AP).
  if (root["apPass"].is<const char*>()) s.apPass = root["apPass"].as<String>();

  if (root["mode"].is<const char*>()) {
    String m = root["mode"].as<String>();
    s.mode = m.equalsIgnoreCase("usage") ? MODE_USAGE : MODE_STOCKS;
  }
  if (root["source"].is<const char*>()) {
    String src = root["source"].as<String>();
    s.source = src.equalsIgnoreCase("yahoo") ? SRC_YAHOO : SRC_WEBHOOK;
  }
  if (root["webhookUrl"].is<const char*>()) s.webhookUrl = root["webhookUrl"].as<String>();
  if (root["usageUrl"].is<const char*>())   s.usageUrl = root["usageUrl"].as<String>();
  if (root["range"].is<const char*>())      s.range = root["range"].as<String>();
  if (root["points"].is<int>())             s.points = constrain((int)root["points"], 0, MAX_SPARK_POINTS);
  if (root["pollSec"].is<int>())            s.pollSec = max(10, (int)root["pollSec"]);
  if (root["rotateSec"].is<int>())          s.rotateSec = max(2, (int)root["rotateSec"]);
  if (root["httpTimeout"].is<int>())        s.httpTimeout = constrain((int)root["httpTimeout"], 1000, 20000);

  if (root["brightness"].is<int>())         s.brightness = constrain((int)root["brightness"], 0, 100);
  if (root["autoBrightness"].is<bool>())    s.autoBrightness = root["autoBrightness"];
  if (root["backlightInverted"].is<bool>()) s.backlightInverted = root["backlightInverted"];
  if (root["rotation"].is<int>())           s.rotation = (uint8_t)(((int)root["rotation"]) & 3);
  if (root["colorInverted"].is<bool>())     s.colorInverted = root["colorInverted"];

  if (root["showName"].is<bool>())       s.showName = root["showName"];
  if (root["showPrice"].is<bool>())      s.showPrice = root["showPrice"];
  if (root["showChange"].is<bool>())     s.showChange = root["showChange"];
  if (root["showChart"].is<bool>())      s.showChart = root["showChart"];
  if (root["showRangeLabel"].is<bool>()) s.showRangeLabel = root["showRangeLabel"];
  if (root["showUpdatedAgo"].is<bool>()) s.showUpdatedAgo = root["showUpdatedAgo"];
  if (root["showPageDots"].is<bool>())   s.showPageDots = root["showPageDots"];

  if (root["symbols"].is<JsonArrayConst>()) {
    JsonArrayConst arr = root["symbols"].as<JsonArrayConst>();
    s.symbolCount = 0;
    for (JsonObjectConst o : arr) {
      if (s.symbolCount >= MAX_SYMBOLS) break;
      const char* sym = o["symbol"] | "";
      if (!sym[0]) continue;                 // skip blank rows
      SymbolCfg& dst = s.symbols[s.symbolCount];
      strlcpy(dst.symbol, sym, MAX_SYMBOL_LEN);
      strlcpy(dst.name, o["name"] | "", MAX_NAME_LEN);
      s.symbolCount++;
    }
  }
}
