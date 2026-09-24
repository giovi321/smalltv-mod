#include "Settings.h"
#include "AtomicJson.h"
#include "Platform.h"   // platformChipId() for the unique default hostname
#include <LittleFS.h>

static const char* CONFIG_PATH = "/config.json";

// ===========================================================================
// Ticker slice
// ===========================================================================
static const char* srcToStr(uint8_t s) {
  return (s == SRC_YAHOO) ? "yahoo"
       : (s == SRC_CASH)  ? "cash"
       : (s == SRC_GHUB)  ? "github" : "webhook";
}
static uint8_t srcFromStr(const String& s) {
  return s.equalsIgnoreCase("yahoo")  ? SRC_YAHOO
       : s.equalsIgnoreCase("cash")   ? SRC_CASH
       : s.equalsIgnoreCase("github") ? SRC_GHUB : SRC_WEBHOOK;
}

void TickerSettings::setDefaults() {
  webhookUrl = "";
  range = DEFAULT_RANGE;
  points = DEFAULT_POINTS;
  pollSec = DEFAULT_POLL_SEC;
  rotateSec = DEFAULT_ROTATE_SEC;
  colorInverted = false;
  changeOnRange = true;

  showName = true;
  showPrice = true;
  showChange = true;
  showChart = true;
  showRangeLabel = true;
  showUpdatedAgo = false;
  showPageDots = true;
  showPortfolio = true;   // only visible once a symbol has qty+cost set

  symbolCount = 0;
  for (uint8_t i = 0; i < MAX_SYMBOLS; i++) {
    symbols[i].symbol[0] = 0;
    symbols[i].name[0] = 0;
    symbols[i].source = DEFAULT_SOURCE;
    symbols[i].qty = 0;
    symbols[i].cost = 0;
  }
}

void TickerSettings::toJson(JsonObject o) const {
  o["webhookUrl"]     = webhookUrl;
  o["range"]          = range;
  o["points"]         = points;
  o["pollSec"]        = pollSec;
  o["rotateSec"]      = rotateSec;
  o["colorInverted"]  = colorInverted;
  o["changeOnRange"]  = changeOnRange;
  o["showName"]       = showName;
  o["showPrice"]      = showPrice;
  o["showChange"]     = showChange;
  o["showChart"]      = showChart;
  o["showRangeLabel"] = showRangeLabel;
  o["showUpdatedAgo"] = showUpdatedAgo;
  o["showPageDots"]   = showPageDots;
  o["showPortfolio"]  = showPortfolio;

  JsonArray arr = o["symbols"].to<JsonArray>();
  for (uint8_t i = 0; i < symbolCount; i++) {
    JsonObject e = arr.add<JsonObject>();
    e["symbol"] = symbols[i].symbol;
    e["name"]   = symbols[i].name;
    e["source"] = srcToStr(symbols[i].source);
    e["qty"]    = symbols[i].qty;
    e["cost"]   = symbols[i].cost;
  }
}

void TickerSettings::fromJson(JsonObjectConst o) {
  // Legacy (pre-2.4) configs carried one global "source"; it becomes the
  // default for any symbol that doesn't carry its own.
  uint8_t legacySrc = DEFAULT_SOURCE;
  if (o["source"].is<const char*>()) legacySrc = srcFromStr(o["source"].as<String>());

  if (o["webhookUrl"].is<const char*>()) webhookUrl = o["webhookUrl"].as<String>();
  if (o["range"].is<const char*>())      range = o["range"].as<String>();
  if (o["points"].is<int>())             points = constrain((int)o["points"], 0, MAX_SPARK_POINTS);
  // Clamped at both ends: these land in a uint16_t, and the web UI's own
  // min/max are only advisory (the page POSTs JSON, it does not submit a form).
  if (o["pollSec"].is<int>())            pollSec = constrain((int)o["pollSec"], 10, 3600);
  if (o["rotateSec"].is<int>())          rotateSec = constrain((int)o["rotateSec"], 2, 3600);
  if (o["colorInverted"].is<bool>())     colorInverted = o["colorInverted"];
  if (o["changeOnRange"].is<bool>())     changeOnRange = o["changeOnRange"];

  if (o["showName"].is<bool>())       showName = o["showName"];
  if (o["showPrice"].is<bool>())      showPrice = o["showPrice"];
  if (o["showChange"].is<bool>())     showChange = o["showChange"];
  if (o["showChart"].is<bool>())      showChart = o["showChart"];
  if (o["showRangeLabel"].is<bool>()) showRangeLabel = o["showRangeLabel"];
  if (o["showUpdatedAgo"].is<bool>()) showUpdatedAgo = o["showUpdatedAgo"];
  if (o["showPageDots"].is<bool>())   showPageDots = o["showPageDots"];
  if (o["showPortfolio"].is<bool>())  showPortfolio = o["showPortfolio"];

  if (o["symbols"].is<JsonArrayConst>()) {
    JsonArrayConst arr = o["symbols"].as<JsonArrayConst>();
    symbolCount = 0;
    for (JsonObjectConst e : arr) {
      if (symbolCount >= MAX_SYMBOLS) break;
      const char* sym = e["symbol"] | "";
      if (!sym[0]) continue;                 // skip blank rows
      SymbolCfg& dst = symbols[symbolCount];
      strlcpy(dst.symbol, sym, MAX_SYMBOL_LEN);
      strlcpy(dst.name, e["name"] | "", MAX_NAME_LEN);
      dst.source = e["source"].is<const char*>()
                     ? srcFromStr(e["source"].as<String>()) : legacySrc;
      dst.qty  = e["qty"].as<float>();     // absent -> 0
      dst.cost = e["cost"].as<float>();
      if (dst.qty < 0)  dst.qty = 0;
      if (dst.cost < 0) dst.cost = 0;
      symbolCount++;
    }
  }
}

// ===========================================================================
// Usage slice
// ===========================================================================
void UsageSettings::setDefaults() {
  usageUrl = "";
  pollSec = DEFAULT_POLL_SEC;
}

void UsageSettings::toJson(JsonObject o) const {
  o["usageUrl"] = usageUrl;
  o["pollSec"]  = pollSec;
}

void UsageSettings::fromJson(JsonObjectConst o) {
  if (o["usageUrl"].is<const char*>()) usageUrl = o["usageUrl"].as<String>();
  if (o["pollSec"].is<int>())          pollSec = constrain((int)o["pollSec"], 10, 3600);
}

// ===========================================================================
// Clock / night mode slice
// ===========================================================================
static uint16_t hhmmToMin(const char* s, uint16_t fallback) {
  if (!s || !s[0]) return fallback;
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return fallback;
  if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
  return (uint16_t)(h * 60 + m);
}
static String minToHhmm(uint16_t v) {
  if (v > 1439) v = 0;
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", (unsigned)(v / 60), (unsigned)(v % 60));
  return String(b);
}

void ClockSettings::setDefaults() {
  tz            = DEFAULT_TZ_NAME;
  tzPosix       = DEFAULT_TZ_POSIX;
  nightEnabled  = DEFAULT_NIGHT_ENABLED;
  nightStartMin = DEFAULT_NIGHT_START_MIN;
  nightEndMin   = DEFAULT_NIGHT_END_MIN;
  nightLevel    = DEFAULT_NIGHT_LEVEL;
}

void ClockSettings::toJson(JsonObject o) const {
  o["tz"]           = tz;
  o["tzPosix"]      = tzPosix;
  o["nightEnabled"] = nightEnabled;
  o["nightStart"]   = minToHhmm(nightStartMin);
  o["nightEnd"]     = minToHhmm(nightEndMin);
  o["nightLevel"]   = nightLevel;
}

void ClockSettings::fromJson(JsonObjectConst o) {
  if (o["tz"].is<const char*>())          tz = o["tz"].as<String>();
  if (o["tzPosix"].is<const char*>())     tzPosix = o["tzPosix"].as<String>();
  if (o["nightEnabled"].is<bool>())       nightEnabled = o["nightEnabled"];
  if (o["nightStart"].is<const char*>())  nightStartMin = hhmmToMin(o["nightStart"], nightStartMin);
  if (o["nightEnd"].is<const char*>())    nightEndMin   = hhmmToMin(o["nightEnd"], nightEndMin);
  if (o["nightLevel"].is<int>())          nightLevel = constrain((int)o["nightLevel"], 0, 100);
}

// ===========================================================================
// Web UI password slice
// ===========================================================================
void AuthSettings::setDefaults() {
  enabled = false;
  user = DEFAULT_AUTH_USER;
  pass = "";
}

void AuthSettings::toJson(JsonObject o, bool includeSecrets) const {
  // Never enable in the saved config without a password to check against; a
  // blank one would lock the page with a credential nobody can supply.
  o["enabled"] = enabled && pass.length() > 0;
  o["user"]    = user;
  o["passSet"] = pass.length() > 0;
  if (includeSecrets) o["pass"] = pass;
}

void AuthSettings::fromJson(JsonObjectConst o) {
  if (o["user"].is<const char*>()) user = o["user"].as<String>();
  // Blank keeps the stored password, as everywhere else in this file.
  if (o["pass"].is<const char*>()) {
    String p = o["pass"].as<String>();
    if (p.length()) pass = p;
  }
  if (o["enabled"].is<bool>()) enabled = o["enabled"];
  if (user.length() >= MAX_AUTH_USER_LEN) user.remove(MAX_AUTH_USER_LEN - 1);
  if (pass.length() >= MAX_AUTH_PASS_LEN) pass.remove(MAX_AUTH_PASS_LEN - 1);
  if (!user.length()) user = DEFAULT_AUTH_USER;
  // Same guard as toJson, for a config imported by hand.
  if (!pass.length()) enabled = false;
}

// ===========================================================================
// WireGuard slice
// ===========================================================================
void WgSettings::setDefaults() {
  enabled = false;
  privateKey = "";
  peerPublicKey = "";
  endpointHost = "";
  endpointPort = DEFAULT_WG_PORT;
  address = "";
  allowedIps = "";
  keepalive = DEFAULT_WG_KEEPALIVE;
}

void WgSettings::toJson(JsonObject o, bool includeSecrets) const {
  o["enabled"]        = enabled;
  o["peerPublicKey"]  = peerPublicKey;
  o["endpointHost"]   = endpointHost;
  o["endpointPort"]   = endpointPort;
  o["address"]        = address;
  o["allowedIps"]     = allowedIps;
  o["keepalive"]      = keepalive;
  // The private key follows the same rule as the WiFi passwords: it reaches
  // the config file and the settings export, never the web API.
  o["privateKeySet"]  = privateKey.length() > 0;
  if (includeSecrets) o["privateKey"] = privateKey;
}

void WgSettings::fromJson(JsonObjectConst o) {
  if (o["enabled"].is<bool>())              enabled = o["enabled"];
  if (o["peerPublicKey"].is<const char*>()) peerPublicKey = o["peerPublicKey"].as<String>();
  if (o["endpointHost"].is<const char*>())  endpointHost = o["endpointHost"].as<String>();
  if (o["endpointPort"].is<int>())          endpointPort = constrain((int)o["endpointPort"], 1, 65535);
  if (o["address"].is<const char*>())       address = o["address"].as<String>();
  if (o["allowedIps"].is<const char*>())    allowedIps = o["allowedIps"].as<String>();
  if (o["keepalive"].is<int>())             keepalive = constrain((int)o["keepalive"], 0, 3600);
  // Blank keeps the stored key, so the web UI can save the rest of the form
  // without ever holding the secret. Clearing it takes a factory reset or an
  // imported config that carries a new one.
  if (o["privateKey"].is<const char*>()) {
    String p = o["privateKey"].as<String>();
    if (p.length()) privateKey = p;
  }
  if (peerPublicKey.length() >= MAX_WG_KEY_LEN)   peerPublicKey.remove(MAX_WG_KEY_LEN - 1);
  if (privateKey.length()    >= MAX_WG_KEY_LEN)   privateKey.remove(MAX_WG_KEY_LEN - 1);
  if (endpointHost.length()  >= MAX_WG_HOST_LEN)  endpointHost.remove(MAX_WG_HOST_LEN - 1);
  if (allowedIps.length()    >= MAX_WG_ALLOWED_LEN) allowedIps.remove(MAX_WG_ALLOWED_LEN - 1);
}

// ===========================================================================
// Panel colour slice
// ===========================================================================
void DisplaySettings::setDefaults() {
  colorOrder = DEFAULT_COLOR_ORDER;
  invert     = DEFAULT_COLOR_INVERT;
  rGain = gGain = bGain = DEFAULT_COLOR_GAIN;
}

void DisplaySettings::toJson(JsonObject o) const {
  o["colorOrder"] = (colorOrder == COLOR_ORDER_RGB) ? "rgb"
                  : (colorOrder == COLOR_ORDER_BGR) ? "bgr" : "auto";
  o["invert"] = invert;
  o["rGain"]  = rGain;
  o["gGain"]  = gGain;
  o["bGain"]  = bGain;
}

void DisplaySettings::fromJson(JsonObjectConst o) {
  if (o["colorOrder"].is<const char*>()) {
    String c = o["colorOrder"].as<String>();
    colorOrder = c.equalsIgnoreCase("rgb") ? COLOR_ORDER_RGB
               : c.equalsIgnoreCase("bgr") ? COLOR_ORDER_BGR : COLOR_ORDER_AUTO;
  }
  if (o["invert"].is<bool>()) invert = o["invert"];
  if (o["rGain"].is<int>()) rGain = constrain((int)o["rGain"], MIN_COLOR_GAIN, MAX_COLOR_GAIN);
  if (o["gGain"].is<int>()) gGain = constrain((int)o["gGain"], MIN_COLOR_GAIN, MAX_COLOR_GAIN);
  if (o["bGain"].is<int>()) bGain = constrain((int)o["bGain"], MIN_COLOR_GAIN, MAX_COLOR_GAIN);
}

// ===========================================================================
// Home Assistant / MQTT slice
// ===========================================================================
void HaSettings::setDefaults() {
  brokerHost = "";
  brokerPort = DEFAULT_HA_BROKER_PORT;
  brokerUser = "";
  brokerPass = "";
  dwellSec   = DEFAULT_HA_DWELL_SEC;
}

void HaSettings::toJson(JsonObject o, bool includeSecrets) const {
  o["brokerHost"] = brokerHost;
  o["brokerPort"] = brokerPort;
  o["brokerUser"] = brokerUser;
  o["passSet"]    = brokerPass.length() > 0;
  if (includeSecrets) o["brokerPass"] = brokerPass;
  o["dwellSec"]   = dwellSec;
}

void HaSettings::fromJson(JsonObjectConst o) {
  if (o["brokerHost"].is<const char*>()) brokerHost = o["brokerHost"].as<String>();
  if (o["brokerPort"].is<int>())         brokerPort = constrain((int)o["brokerPort"], 1, 65535);
  if (o["brokerUser"].is<const char*>()) brokerUser = o["brokerUser"].as<String>();
  // Blank keeps the stored password, as everywhere else in this file.
  if (o["brokerPass"].is<const char*>()) {
    String p = o["brokerPass"].as<String>();
    if (p.length()) brokerPass = p;
  }
  if (o["dwellSec"].is<int>()) dwellSec = constrain((int)o["dwellSec"], HA_DWELL_MIN_SEC, HA_DWELL_MAX_SEC);
  if (brokerHost.length() >= MAX_HA_HOST_LEN) brokerHost.remove(MAX_HA_HOST_LEN - 1);
  if (brokerUser.length() >= MAX_HA_USER_LEN) brokerUser.remove(MAX_HA_USER_LEN - 1);
  if (brokerPass.length() >= MAX_HA_PASS_LEN) brokerPass.remove(MAX_HA_PASS_LEN - 1);
}

// ===========================================================================
// Radar slice
// ===========================================================================
void RadarSettings::setDefaults() {
  lat = DEFAULT_RADAR_LAT;
  lon = DEFAULT_RADAR_LON;
  source = DEFAULT_RADAR_SRC;
  webhookUrl = "";
  rangeKm = DEFAULT_RADAR_RANGE_KM;
  pollSec = DEFAULT_RADAR_POLL_SEC;
  unitsMi = false;
  showLabels = true;
  showVectors = true;
  showRimDots = true;
  uiScale = 1;            // medium
  minAltFt = 0;           // show all
  airportCount = 0;
  for (uint8_t i = 0; i < MAX_AIRPORTS; i++) {
    airports[i].icao[0] = 0;
    airports[i].lat = airports[i].lon = 0;
  }
}

void RadarSettings::toJson(JsonObject o) const {
  o["lat"]         = lat;
  o["lon"]         = lon;
  o["source"]      = (source == RADAR_SRC_WEBHOOK) ? "webhook"
                   : (source == RADAR_SRC_ADSBLOL) ? "adsblol" : "adsbfi";
  o["webhookUrl"]  = webhookUrl;
  o["rangeKm"]     = rangeKm;
  o["pollSec"]     = pollSec;
  o["unitsMi"]     = unitsMi;
  o["showLabels"]  = showLabels;
  o["showVectors"] = showVectors;
  o["showRimDots"] = showRimDots;
  o["uiScale"]     = uiScale;
  o["minAltFt"]    = minAltFt;

  JsonArray arr = o["airports"].to<JsonArray>();
  for (uint8_t i = 0; i < airportCount; i++) {
    JsonObject e = arr.add<JsonObject>();
    e["icao"] = airports[i].icao;
    e["lat"]  = airports[i].lat;
    e["lon"]  = airports[i].lon;
  }
}

void RadarSettings::fromJson(JsonObjectConst o) {
  if (o["lat"].is<float>() || o["lat"].is<int>()) lat = o["lat"].as<float>();
  if (o["lon"].is<float>() || o["lon"].is<int>()) lon = o["lon"].as<float>();
  if (o["source"].is<const char*>()) {
    String src = o["source"].as<String>();
    // "direct" is the pre-2.11 name for "whichever direct feed we default to".
    // Configs written back then predate the adsb.fi Cloudflare move, so they
    // resolve to the platform default rather than pinning adsb.fi.
    if      (src.equalsIgnoreCase("webhook")) source = RADAR_SRC_WEBHOOK;
    else if (src.equalsIgnoreCase("adsblol")) source = RADAR_SRC_ADSBLOL;
    else if (src.equalsIgnoreCase("adsbfi"))  source = RADAR_SRC_ADSBFI;
    else                                      source = DEFAULT_RADAR_SRC;
  }
  if (o["webhookUrl"].is<const char*>()) webhookUrl = o["webhookUrl"].as<String>();
  if (o["rangeKm"].is<int>())    rangeKm = constrain((int)o["rangeKm"], 1, 500);
  if (o["pollSec"].is<int>())    pollSec = constrain((int)o["pollSec"], 3, 3600);
  if (o["unitsMi"].is<bool>())   unitsMi = o["unitsMi"];
  if (o["showLabels"].is<bool>())  showLabels = o["showLabels"];
  if (o["showVectors"].is<bool>()) showVectors = o["showVectors"];
  if (o["showRimDots"].is<bool>()) showRimDots = o["showRimDots"];
  if (o["uiScale"].is<int>())      uiScale = constrain((int)o["uiScale"], 0, 2);
  if (o["minAltFt"].is<int>())     minAltFt = constrain((int)o["minAltFt"], 0, 60000);

  if (o["airports"].is<JsonArrayConst>()) {
    JsonArrayConst arr = o["airports"].as<JsonArrayConst>();
    airportCount = 0;
    for (JsonObjectConst e : arr) {
      if (airportCount >= MAX_AIRPORTS) break;
      const char* ic = e["icao"] | "";
      if (!ic[0]) continue;                  // skip blank rows
      Airport& dst = airports[airportCount];
      strlcpy(dst.icao, ic, MAX_ICAO_LEN);
      dst.lat = e["lat"].as<float>();
      dst.lon = e["lon"].as<float>();
      airportCount++;
    }
  }
}

// ===========================================================================
// Top-level settings
// ===========================================================================
void Settings::setDefaults() {
  wifiCount = 0;
  for (uint8_t i = 0; i < MAX_WIFI_NETS; i++) {
    wifi[i].ssid = "";
    wifi[i].pass = "";
  }
  apSsid  = DEFAULT_AP_SSID;
  apPass  = DEFAULT_AP_PASS;
  // Unique per device so several SmallTVs on one network don't collide on
  // mDNS out of the box. A hostname saved in config.json overrides this.
  hostname = String(DEFAULT_HOSTNAME) + "-" + String(platformChipId() & 0xFFFF, HEX);

  mode = DEFAULT_MODE;
  themeId = "";
  carouselSec = DEFAULT_CAROUSEL_SEC;
  carouselTicker = carouselUsage = carouselRadar = carouselHa = true;
  httpTimeout = DEFAULT_HTTP_TIMEOUT;

  brightness = DEFAULT_BRIGHTNESS;
  autoBrightness = false;
  backlightInverted = TFT_BL_DEFAULT_INVERTED;
  rotation = 0;

  ticker.setDefaults();
  usage.setDefaults();
  radar.setDefaults();
  ha.setDefaults();
  clock.setDefaults();
  display.setDefaults();
  wg.setDefaults();
  auth.setDefaults();
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

  // Never truncate the working settings when a full disk or interrupted
  // save prevents writing the replacement.
  return saveJsonAtomically(LittleFS, doc, CONFIG_PATH, "/config.json.tmp");
}

void factoryReset(Settings& s) {
  LittleFS.remove(CONFIG_PATH);
  s.setDefaults();
}

// ---------------------------------------------------------------------------
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets) {
  root["hostname"]   = s.hostname;

  // WiFi networks. Passwords only reach the config file, never the web API.
  JsonArray wf = root["wifi"].to<JsonArray>();
  for (uint8_t i = 0; i < s.wifiCount; i++) {
    JsonObject e = wf.add<JsonObject>();
    e["ssid"]    = s.wifi[i].ssid;
    e["passSet"] = s.wifi[i].pass.length() > 0;
    if (includeSecrets) e["pass"] = s.wifi[i].pass;
  }
  // Legacy mirror of the primary network, kept for one release so a firmware
  // downgrade still finds its WiFi in config.json.
  root["staSsid"]    = s.wifiCount ? s.wifi[0].ssid : "";
  root["staPassSet"] = s.wifiCount && s.wifi[0].pass.length() > 0;
  root["apSsid"]     = s.apSsid;
  root["apPassSet"]  = s.apPass.length() > 0;
  if (includeSecrets) {
    root["staPass"]  = s.wifiCount ? s.wifi[0].pass : "";
    root["apPass"]   = s.apPass;
  }

  // Mode + shared HTTP/display
  root["themeId"] = s.themeId;
  root["mode"]              = (s.mode == MODE_THEME)    ? "theme"
                            : (s.mode == MODE_RADAR)    ? "radar"
                            : (s.mode == MODE_USAGE)    ? "usage"
                            : (s.mode == MODE_HA)       ? "ha"
                            : (s.mode == MODE_CAROUSEL) ? "carousel" : "stocks";
  root["carouselSec"]       = s.carouselSec;
  root["carouselTicker"]    = s.carouselTicker;
  root["carouselUsage"]     = s.carouselUsage;
  root["carouselRadar"]     = s.carouselRadar;
  root["carouselHa"]        = s.carouselHa;
  root["httpTimeout"]       = s.httpTimeout;
  root["brightness"]        = s.brightness;
  root["autoBrightness"]    = s.autoBrightness;
  root["backlightInverted"] = s.backlightInverted;
  root["rotation"]          = s.rotation;

  // Feature slices
  s.ticker.toJson(root["ticker"].to<JsonObject>());
  s.usage.toJson(root["usage"].to<JsonObject>());
  s.radar.toJson(root["radar"].to<JsonObject>());
  s.ha.toJson(root["ha"].to<JsonObject>(), includeSecrets);
  s.clock.toJson(root["clock"].to<JsonObject>());
  s.display.toJson(root["display"].to<JsonObject>());
  s.wg.toJson(root["wg"].to<JsonObject>(), includeSecrets);
  s.auth.toJson(root["auth"].to<JsonObject>(), includeSecrets);
}

// Apply only the keys that are present (partial update friendly). Accepts both
// the nested layout and the legacy flat layout (feature keys at the top level).
void settingsApplyJson(Settings& s, JsonObjectConst root) {
  if (root["hostname"].is<const char*>()) s.hostname = root["hostname"].as<String>();

  if (root["wifi"].is<JsonArrayConst>()) {
    // The list is authoritative when present (order = try priority, missing
    // row = deletion). A blank password keeps the stored one, matched by SSID
    // so rows survive reordering.
    WifiCred old[MAX_WIFI_NETS];
    uint8_t oldCount = s.wifiCount;
    for (uint8_t i = 0; i < oldCount; i++) old[i] = s.wifi[i];

    s.wifiCount = 0;
    for (JsonObjectConst e : root["wifi"].as<JsonArrayConst>()) {
      if (s.wifiCount >= MAX_WIFI_NETS) break;
      const char* ssid = e["ssid"] | "";
      if (!ssid[0]) continue;                // skip blank rows
      WifiCred& dst = s.wifi[s.wifiCount];
      dst.ssid = ssid;
      const char* pass = e["pass"] | "";
      dst.pass = pass;
      if (!pass[0])
        for (uint8_t i = 0; i < oldCount; i++)
          if (old[i].ssid == dst.ssid) { dst.pass = old[i].pass; break; }
      s.wifiCount++;
    }
  } else if (root["staSsid"].is<const char*>()) {
    // Legacy single-network layout (pre-2.4 config.json or an old cached web
    // page): it becomes/updates the primary network, extras stay untouched.
    String ssid = root["staSsid"].as<String>();
    if (ssid.length()) {
      s.wifi[0].ssid = ssid;
      if (root["staPass"].is<const char*>()) {
        String p = root["staPass"].as<String>();
        if (p.length() > 0) s.wifi[0].pass = p;   // blank = keep
      }
      if (s.wifiCount < 1) s.wifiCount = 1;
    }
  }
  if (root["apSsid"].is<const char*>()) s.apSsid = root["apSsid"].as<String>();
  // AP password: apply as-is when present (empty allowed => open AP).
  if (root["apPass"].is<const char*>()) s.apPass = root["apPass"].as<String>();

  if (root["mode"].is<const char*>()) {
    String m = root["mode"].as<String>();
    s.mode = m.equalsIgnoreCase("theme")    ? MODE_THEME
           : m.equalsIgnoreCase("radar")    ? MODE_RADAR
           : m.equalsIgnoreCase("usage")    ? MODE_USAGE
           : m.equalsIgnoreCase("ha")       ? MODE_HA
           : m.equalsIgnoreCase("carousel") ? MODE_CAROUSEL : MODE_STOCKS;
  }
  if (root["themeId"].is<const char*>()) {
    String id = root["themeId"].as<String>();
    bool valid = id.length() <= 48;
    for (size_t i = 0; i < id.length(); ++i) {
      char c = id[i];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) valid = false;
    }
    if (valid) s.themeId = id;
  }
  if (root["carouselSec"].is<int>())      s.carouselSec = constrain((int)root["carouselSec"], 5, 3600);
  if (root["carouselTicker"].is<bool>())  s.carouselTicker = root["carouselTicker"];
  if (root["carouselUsage"].is<bool>())   s.carouselUsage = root["carouselUsage"];
  if (root["carouselRadar"].is<bool>())   s.carouselRadar = root["carouselRadar"];
  if (root["carouselHa"].is<bool>())      s.carouselHa = root["carouselHa"];

  if (root["httpTimeout"].is<int>())        s.httpTimeout = constrain((int)root["httpTimeout"], 1000, 20000);
  if (root["brightness"].is<int>())         s.brightness = constrain((int)root["brightness"], 0, 100);
  if (root["autoBrightness"].is<bool>())    s.autoBrightness = root["autoBrightness"];
  if (root["backlightInverted"].is<bool>()) s.backlightInverted = root["backlightInverted"];
  if (root["rotation"].is<int>())           s.rotation = (uint8_t)(((int)root["rotation"]) & 3);

  // Feature slices: prefer the nested object; fall back to the top level so a
  // legacy flat config.json (or a legacy POST) still applies. The old shared
  // "pollSec" thus seeds both ticker and usage cadence on first upgrade.
  JsonObjectConst t = root["ticker"].is<JsonObjectConst>() ? root["ticker"].as<JsonObjectConst>() : root;
  s.ticker.fromJson(t);
  JsonObjectConst u = root["usage"].is<JsonObjectConst>() ? root["usage"].as<JsonObjectConst>() : root;
  s.usage.fromJson(u);
  // Radar has no legacy flat layout; only apply when its nested object is present.
  if (root["radar"].is<JsonObjectConst>()) s.radar.fromJson(root["radar"].as<JsonObjectConst>());
  // HA has no legacy flat layout either; only apply when its object is present.
  if (root["ha"].is<JsonObjectConst>()) s.ha.fromJson(root["ha"].as<JsonObjectConst>());
  if (root["clock"].is<JsonObjectConst>()) s.clock.fromJson(root["clock"].as<JsonObjectConst>());
  if (root["display"].is<JsonObjectConst>()) s.display.fromJson(root["display"].as<JsonObjectConst>());
  if (root["wg"].is<JsonObjectConst>()) s.wg.fromJson(root["wg"].as<JsonObjectConst>());
  if (root["auth"].is<JsonObjectConst>()) s.auth.fromJson(root["auth"].as<JsonObjectConst>());
}
