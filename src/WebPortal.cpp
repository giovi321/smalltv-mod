#include "WebPortal.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "webui.h"
#include "Net.h"
#include "Gfx.h"
#include "OtaUpdate.h"
#include "StockClient.h"
#include "UsageClient.h"
#if WITH_RADAR
#include "RadarClient.h"
#endif
#if WITH_NOTIFY
#include "NotifyMode.h"
#endif
#include "Clock.h"
#include "WgClient.h"
#include "SettingsTransaction.h"
#if WITH_THEME
#include "features/theme/ThemeWeb.h"
#endif
#if WITH_HA
#include "HaScreens.h"
#include "MqttClient.h"
#endif

// Defined in main.cpp — re-init every mode + force a repaint after a config change.
extern void appInvalidate();
extern const char* appResetReason();   // last reset reason (diagnostics)
extern void appApplyBrightness();   // main.cpp: re-resolve effective brightness now

static WebServerClass server(80);
static Settings*        S = nullptr;
static bool             g_reboot = false;
static uint32_t         g_rebootAt = 0;
static bool             g_selfUpdate = false;   // GitHub self-update requested
static String           g_updateMsg;            // last self-update status/error

static void scheduleReboot(uint32_t inMs) {
  g_reboot = true;
  g_rebootAt = millis() + inMs;
}

// ---- optional web UI password ---------------------------------------------
// Off by default. When on, every handler below starts with this line and every
// page and endpoint needs the credentials. Digest, not basic, so the password
// is never sent over a plain-HTTP LAN. Two deliberate exceptions:
//   - the captive-portal probes, which a phone fires before anyone can type a
//     password and which only ever redirect,
//   - /api/usage, the daemon's push endpoint, which has no way to carry
//     credentials and only writes the numbers on the screen.
// Returns true when the request may proceed; on false it has already answered.
static bool requireAuth() {
  if (!S->auth.enabled || !S->auth.pass.length()) return true;
  if (server.authenticate(S->auth.user.c_str(), S->auth.pass.c_str())) return true;
  server.requestAuthentication(HTTPAuthMethod::DIGEST_AUTH, AUTH_REALM,
                               "Authentication required");
  return false;
}

// ---------------------------------------------------------------------------
static void sendJson(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void handleRoot() {
  if (!requireAuth()) return;
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Content-Encoding", "gzip");   // WEBUI_HTML_GZ is gzip'd (webui.h is generated)
  server.send_P(200, "text/html", (PGM_P)WEBUI_HTML_GZ, WEBUI_HTML_GZ_LEN);
}

static void handleGetConfig() {
  if (!requireAuth()) return;
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(*S, root, /*includeSecrets=*/false);
  // Which features are compiled in (so a lean build hides the tabs it dropped).
  JsonObject feat = root["features"].to<JsonObject>();
  feat["ticker"] = (bool)WITH_TICKER;
  feat["usage"]  = (bool)WITH_USAGE;
  feat["radar"]  = (bool)WITH_RADAR;
  feat["ha"]     = (bool)WITH_HA;
  feat["theme"]  = (bool)WITH_THEME;
  // WireGuard is a per-chip decision rather than a per-feature one: it is
  // compiled only where the image has room for it (the ESP32-C2 build).
#if defined(SMALLTV_WIREGUARD)
  feat["wireguard"] = true;
#else
  feat["wireguard"] = false;
#endif
  // Which chip this build runs on (the UI warns about per-chip limitations).
#if defined(SMALLTV_ESP32C2)
  root["chip"] = "esp32c2";
#elif defined(SMALLTV_ESP32)
  root["chip"] = "esp32";
#else
  root["chip"] = "esp8266";
#endif
  sendJson(doc);
}

static void handleStatus() {
  if (!requireAuth()) return;
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["fw"] = FW_NAME;
  o["version"] = FW_VERSION;
  o["variant"] = FW_VARIANT;   // which release image this is, e.g. "esp8266-lean"
  o["repo"] = REPO_URL;
  if (g_updateMsg.length()) o["updateMsg"] = g_updateMsg;
  o["mode"] = (netMode() == NET_AP) ? "ap" : "sta";
  o["connected"] = netConnected();
  o["ssid"] = netSSID();
  o["ip"] = netIP();
  o["rssi"] = netRSSI();
  o["heap"] = ESP.getFreeHeap();
  o["maxblk"] = platformMaxFreeBlock();     // largest contiguous block (TLS handshake needs one)
  o["contstk"] = platformFreeContStack();   // primary stack headroom (ESP8266)
  o["uptime"] = millis() / 1000;
  o["reset"] = appResetReason();
  o["synced"] = clockSynced();
  { String ts = clockTimeStr(); if (ts.length()) o["time"] = ts; }
  o["tz"]        = S->clock.tz;
  o["night"]     = clockNightActive();   // dimming now
  o["nightHeld"] = clockNightHeld();      // in the window but waiting for a fresh NTP sync
  o["clockFresh"] = clockTrusted();       // last NTP sync within the trust window
  wgStatusJson(o["wg"].to<JsonObject>()); // tunnel state (compiledIn=false where it isn't built)

#if WITH_TICKER
  JsonArray arr = o["tickers"].to<JsonArray>();
  for (uint8_t i = 0; i < stocksCount(); i++) {
    const StockData& d = stockAt(i);
    JsonObject t = arr.add<JsonObject>();
    t["symbol"] = d.symbol;
    t["valid"] = d.valid;
    t["error"] = d.error;
    if (d.error) t["retryIn"] = stockRetryInSec(i);   // seconds to the next attempt
    if (d.valid) {
      t["price"] = d.price;
      float chg, pct;
      bool onRange = false;
      if (stockDisplayChange(d, S->ticker, chg, pct, &onRange)) {
        t["changePct"] = pct;                       // as displayed on the device
        t["basis"] = onRange ? "range" : "day";     // which basis that was
      }
    }
  }
#endif

#if WITH_RADAR
  // An empty scope has several possible causes and the screen shows the same
  // thing for all of them, so report the last poll's actual outcome.
  {
    JsonObject r = o["radar"].to<JsonObject>();
    r["count"] = radarCount();          // aircraft currently plotted
    r["error"] = radarError();
    r["stage"] = radarStageName();      // why the last poll produced what it did
    r["seenAc"] = radarSeenAc();        // aircraft in the last parsed response
    if (radarLastHttp()) r["http"] = radarLastHttp();   // <0 = HTTPClient internal error
    if (radarTlsRx())    r["tlsRx"] = radarTlsRx();     // negotiated BearSSL rx buffer
    if (radarLastTryMs()) r["triedAgo"] = (millis() - radarLastTryMs()) / 1000;
    if (radarLastOkMs())  r["okAgo"]    = (millis() - radarLastOkMs()) / 1000;
    if (radarLastUrl().length()) r["url"] = radarLastUrl();
  }
#endif

  sendJson(doc);
}

// Fingerprint of everything network-identity related: the WiFi list and the
// hostname. Changing any of it needs a reboot, because the connection and the
// mDNS registration are established once at boot.
static String netFingerprint(const Settings& s) {
  String f((int)s.wifiCount);
  for (uint8_t i = 0; i < s.wifiCount; i++) {
    f += '\n';
    f += s.wifi[i].ssid;
    f += '\x01';
    f += s.wifi[i].pass;
  }
  f += '\n';
  f += s.hostname;
  return f;
}

// Everything the tunnel is built from. A change here tears it down and brings
// it back up with the new settings; an unchanged save leaves a working tunnel
// alone.
static String wgFingerprint(const Settings& s) {
  String f(s.wg.enabled ? '1' : '0');
  f += s.wg.privateKey;    f += '\x01';
  f += s.wg.peerPublicKey; f += '\x01';
  f += s.wg.endpointHost;  f += '\x01';
  f += String(s.wg.endpointPort); f += '\x01';
  f += s.wg.address;       f += '\x01';
  f += s.wg.allowedIps;    f += '\x01';
  f += String(s.wg.keepalive);
  return f;
}

static void handlePostConfig() {
  if (!requireAuth()) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }

  String oldNet = netFingerprint(*S);
  String oldWg = wgFingerprint(*S);

  if (!applyAndSaveSettings(*S,
        [&](Settings& settings) { settingsApplyJson(settings, doc.as<JsonObjectConst>()); },
        [](const Settings& settings) { return saveSettings(settings); })) {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"could not save settings\"}");
    return;
  }

  // Live apply (no reboot needed for these)
  clockReapply(*S);         // re-arm SNTP iff the timezone changed
  appApplyBrightness();     // apply effective brightness (respects night/auto/manual)
  gfxApplyColors(*S);       // rotation, panel colour order/inversion, channel gain
  appInvalidate();          // re-init every mode + repaint (covers mode/URL/symbol changes)

  // Rebuild the tunnel when its settings changed. A save that changes nothing
  // still rebuilds while the tunnel is held after repeated crashes: that
  // re-save is the deliberate "I fixed it, try again".
  bool wgChanged = (wgFingerprint(*S) != oldWg) || wgHeld();
  bool wifiChanged = netFingerprint(*S) != oldNet;

  JsonDocument res;
  res["ok"] = true;
  res["reboot"] = wifiChanged;
  sendJson(res);

  // After the response, not before: a browser reaching the device *through* the
  // tunnel would otherwise lose the answer to the very save that rebuilt it.
  if (wgChanged) wgReapply(*S);

  if (wifiChanged) scheduleReboot(800);
}

static void handleScan() {
  if (!requireAuth()) return;
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 25; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["enc"] = !platformScanIsOpen(i);
  }
  WiFi.scanDelete();
  sendJson(doc);
}

static void handleReboot() {
  if (!requireAuth()) return;
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

static void handleFactory() {
  if (!requireAuth()) return;
  factoryReset(*S);
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

// Full settings backup: stream the persisted config.json verbatim. It includes
// the WiFi passwords — same trust domain as typing them into this page.
static void handleExport() {
  if (!requireAuth()) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) { server.send(404, "text/plain", "no config saved yet"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=smalltv-config.json");
  server.streamFile(f, "application/json");
  f.close();
}

// Restore a backup: apply everything, persist, reboot (WiFi/hostname may change).
static void handleImport() {
  if (!requireAuth()) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  if (!applyAndSaveSettings(*S,
        [&](Settings& settings) { settingsApplyJson(settings, doc.as<JsonObjectConst>()); },
        [](const Settings& settings) { return saveSettings(settings); })) {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"could not save settings\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
  scheduleReboot(800);
}

static void handleRefresh() {
  if (!requireAuth()) return;
#if WITH_TICKER
  stocksForceRefresh();
#endif
  server.send(200, "application/json", "{\"ok\":true}");
}

#if WITH_HA
// Purge every HA screen: empties the local store + deletes /ha_screens.json,
// then deletes the broker's retained copies by publishing an empty retained
// payload per known slot — the same delete the MQTT contract defines for a
// zero-length payload. haScreensClearAll() sets the renderer dirty flag, so
// the HA mode repaints on its next tick without an explicit appInvalidate().
// Slots whose retained messages exist only on the broker (the device was
// offline when the screen was deleted) are unknowable from here and survive;
// "broker" in the reply tells the UI whether the broker side was purged at all.
static void handleHaClear() {
  if (!requireAuth()) return;
  char names[HA_MAX_SCREENS][HA_SLOT_LEN];
  uint8_t n = haScreensClearAll(names, HA_MAX_SCREENS);
  bool broker = mqttConnected();
  if (broker) {
    for (uint8_t i = 0; i < n; i++) {
      char topic[128];   // slot is <= 24 chars; hostname is a bounded String, but
                         // the compiler can't prove it — keep the buffer generous
      snprintf(topic, sizeof(topic), "smalltv/%s/screen/%s",
               S->hostname.c_str(), names[i]);
      if (!mqttPublish(topic, "", /*retained=*/true)) broker = false;
    }
  }
  JsonDocument res;
  res["ok"] = true;
  res["cleared"] = n;
  res["broker"] = broker;   // false: local purge done, broker copies may remain
  sendJson(res);
}
#endif

// Check the newest GitHub release against the running version.
static void handleCheckUpdate() {
  if (!requireAuth()) return;
  OtaLatest r = otaCheckLatest(*S);
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["current"] = FW_VERSION;
  o["ok"] = r.ok;
  o["latest"] = r.tag;
  o["newer"] = r.newer;
  if (!r.ok) o["error"] = r.error;
  sendJson(doc);
}

// Trigger the self-update. The actual (blocking) download runs from the loop so
// this response returns first; on success the device reboots into the new image.
static void handleSelfUpdate() {
  if (!requireAuth()) return;
  g_selfUpdate = true;
  g_updateMsg = "starting...";
  server.send(200, "application/json", "{\"ok\":true}");
}

// Push endpoint: the daemon POSTs the usage payload here when the device can't
// reach it (Wi-Fi client isolation). Body is the {s,sr,w,wr,st,ok} contract.
static void handleUsagePush() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_USAGE
  bool ok = usageApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// Attention overlay. Every field is optional and falls back to the preset named
// by "type" (itself defaulting to "info"), so the smallest useful request is
// {"label":"..."} and the largest overrides the lot:
//   {"type":"alert","title":"NAS","label":"<text>","anim":"bang",
//    "color":"#3399ff","ttl":<seconds>,"priority":0..3}
// Never persisted. Behind the password like the rest of the API: unlike the
// daemon's usage push, whatever fires these is a script of your own and can
// send credentials, and taking over the whole screen is not something to leave
// open on a device you deliberately locked.
#if WITH_NOTIFY
static void handleNotify() {
  if (!requireAuth()) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  // "state" is what the endpoint shipped with, before types existed. It named
  // the same two things "type" now does, so it is read as an alias rather than
  // a second field: scripts written against the old API keep working untouched.
  const char* type = doc["type"];
  if (!type) type = doc["state"];

  NotifyRequest r;
  r.type  = type;
  r.title = doc["title"];
  r.label = doc["label"];
  r.anim  = doc["anim"];
  r.color = doc["color"];
  r.ttlSec = doc["ttl"] | (uint32_t)0;              // 0 -> the preset's own hold
  r.prio   = doc["priority"] | (int)-1;             // <0 -> the preset's own priority

  // A full queue answers 429, not 400: it is the one failure worth retrying, and
  // a script cannot back off sensibly if it looks the same as a typo in "type".
  switch (g_notifyMode.request(r)) {
    case NOTIFY_REJECTED:
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad request\"}");
      return;
    case NOTIFY_QUEUE_FULL:
      server.send(429, "application/json", "{\"ok\":false,\"error\":\"queue full\"}");
      return;
    case NOTIFY_ACCEPTED:
      break;
  }

  char body[48];
  snprintf(body, sizeof(body), "{\"ok\":true,\"queued\":%u}",
           (unsigned)g_notifyMode.queued());
  server.send(200, "application/json", body);
}
#endif

// ---- OTA ------------------------------------------------------------------
static void handleUpdateDone() {
  if (!requireAuth()) return;
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : platformUpdateError().c_str());
  if (ok) scheduleReboot(1200);
}

// The upload callback runs while the body streams in, ahead of
// handleUpdateDone, so the password has to be checked here too: guarding only
// the final handler would let an unauthenticated POST write a whole image to
// flash and merely lose the 200 at the end.
static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (S->auth.enabled && S->auth.pass.length() &&
      !server.authenticate(S->auth.user.c_str(), S->auth.pass.c_str())) {
    if (up.status != UPLOAD_FILE_START) Update.end();
    return;
  }
  if (up.status == UPLOAD_FILE_START) {
#if defined(SMALLTV_ESP8266)
    WiFiUDP::stopAll();   // free UDP sockets so the OTA has max contiguous flash/heap
#endif
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSpace)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.end();
  }
  yield();
}

// ---- captive portal -------------------------------------------------------
static void handleNotFound() {
  if (netMode() == NET_AP) {
    // Redirect everything to the config page so the captive portal pops.
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ---------------------------------------------------------------------------
void webPortalBegin(Settings& settings) {
  S = &settings;

  // If the last boot ran a queued GitHub update and failed, surface why
  // (success reboots into the new image before we ever get here).
  g_updateMsg = otaTakeBootResult();

#if WITH_THEME
  themeWebBegin(server, settings, requireAuth);
#endif
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/import", HTTP_POST, handleImport);
  server.on("/api/checkupdate", HTTP_GET, handleCheckUpdate);
  server.on("/api/selfupdate", HTTP_POST, handleSelfUpdate);
  server.on("/api/usage", HTTP_POST, handleUsagePush);   // daemon pushes usage here
#if WITH_HA
  server.on("/api/ha/clear", HTTP_POST, handleHaClear);  // purge HA screens (device + broker retained)
#endif
#if WITH_NOTIFY
  server.on("/api/notify", HTTP_POST, handleNotify);     // full-screen attention overlay
#endif
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);

  // Common captive-portal probe endpoints
  server.on("/generate_204", handleNotFound);
  server.on("/gen_204", handleNotFound);
  server.on("/hotspot-detect.html", handleNotFound);
  server.on("/connecttest.txt", handleNotFound);
  server.onNotFound(handleNotFound);

  server.begin();
}

void webPortalLoop() {
  server.handleClient();

  // Run the GitHub self-update outside the request handler so the browser gets its
  // response first.
  if (g_selfUpdate) {
    g_selfUpdate = false;
#if defined(SMALLTV_ESP8266)
    // RAM-tight chip: verify there is something to install, then queue the
    // download for the next boot (otaBootUpdate in setup(), ~45 KB free) and
    // reboot. A failure there lands back in g_updateMsg via otaTakeBootResult.
    OtaLatest r = otaCheckLatest(*S);
    if (!r.ok)         g_updateMsg = "check failed: " + r.error;
    else if (!r.newer) g_updateMsg = "already up to date (" FW_VERSION ")";
    else if (otaRequestBootUpdate(r.tag.c_str())) {
      g_updateMsg = "updating...";
      scheduleReboot(400);
    } else {
      g_updateMsg = F("could not queue update (storage error)");
    }
#else
    // ESP32 targets: mbedTLS has the RAM to download in place; blocks while it
    // runs and reboots into the new image on success.
    String err = otaUpdateFromGitHub(*S);
    g_updateMsg = err.length() ? err : "updating...";
#endif
  }
}

bool webPortalRebootDue() {
  return g_reboot && (int32_t)(millis() - g_rebootAt) >= 0;
}
