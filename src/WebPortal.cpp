#include "WebPortal.h"
#include "Platform.h"
#include "WebGuard.h"
#include "UpdateBuffer.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "webui.h"
#include "StatusJson.h"
#include "Net.h"
#include "Gfx.h"
#include "OtaUpdate.h"
#include "StockClient.h"
#include "UsageClient.h"
#if WITH_RADAR
#include "RadarClient.h"
#endif
#if WITH_RAIN
#include "RainClient.h"
#include "RainMode.h"
#endif
#if WITH_NOTIFY
#include "NotifyMode.h"
#endif
#include "Clock.h"
#include "WgClient.h"
#if WITH_THEME
#include "features/theme/ThemeWeb.h"
#endif
#include "SettingsTransaction.h"
#if WITH_HA
#include "HaBoot.h"
#include "HaScreens.h"
#include "MqttClient.h"
#endif

// Defined in main.cpp — re-init every mode + force a repaint after a config change.
extern void appInvalidate();
extern void appRelease();
extern const char* appResetReason();   // last reset reason (diagnostics)
extern void appApplyBrightness();   // main.cpp: re-resolve effective brightness now
extern int  appBacklightSteps();    // main.cpp: backlight level now, 0..255

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

// ---- CSRF and DNS-rebinding guard ------------------------------------------
// Every handler goes through requireAuth(), which calls this first (the usage
// push calls it directly). The two upload callbacks (OTA, theme install) run
// before their final handler, so they only ask guardAllowed() and write nothing
// when it says no; the final handler then answers 403 through requireAuth().
// Rules in WebGuard.h.
//   - Host: checked on every request, but not in AP mode. There the phone's
//     captive-portal probe arrives with whatever name it is checking
//     (connectivitycheck.gstatic.com, captive.apple.com, ...) and must get the
//     redirect; the network is the device's own, so nothing can rebind to it.
//   - Origin / Sec-Fetch-Site: checked on anything that is not GET/HEAD/OPTIONS.
//     A request carrying neither (curl, Home Assistant, n8n, the daemon) passes.
// guardReject() returns the refusal text, or nullptr when the request may
// proceed. It answers nothing, so it is safe inside an upload callback.
static const char* guardReject() {
  String host = server.hostHeader();
  if (netMode() != NET_AP) {
    const char* name = S->hostname.length() ? S->hostname.c_str() : DEFAULT_HOSTNAME;
    if (!webguard::hostAllowed(host.c_str(), name)) return "Forbidden: unexpected Host header";
  }
  HTTPMethod m = server.method();
  if (m != HTTP_GET && m != HTTP_HEAD && m != HTTP_OPTIONS) {
    String origin = server.header("Origin");
    String site   = server.header("Sec-Fetch-Site");
    if (!webguard::originAllowed(origin.c_str(), site.c_str(), host.c_str()))
      return "Forbidden: cross-site request";
  }
  return nullptr;
}

static bool guardAllowed() { return guardReject() == nullptr; }

// Returns true when the request may proceed; on false it has already answered.
static bool guardRequest() {
  const char* why = guardReject();
  if (!why) return true;
  server.send(403, "text/plain", why);
  return false;
}

// ---- optional web UI password ---------------------------------------------
// Off by default. While a password is stored, every handler below starts with
// this line and every page and endpoint needs the credentials. Digest, not basic, so the password
// is never sent over a plain-HTTP LAN. Two deliberate exceptions:
//   - the captive-portal probes, which a phone fires before anyone can type a
//     password and which only ever redirect,
//   - /api/usage, the daemon's push endpoint, which has no way to carry
//     credentials and only writes the numbers on the screen.
// Returns true when the request may proceed; on false it has already answered.
static bool requireAuth() {
  if (!guardRequest()) return false;
  if (!S->auth.pass.length()) return true;   // a stored password is the lock
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
  // The full settings need about 4 KB of heap to send, which a running theme
  // on the ESP8266 does not leave free; the theme reloads on its next frame.
  appRelease();
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(*S, root, /*includeSecrets=*/false);
  // Which features are compiled in (so a lean build hides the tabs it dropped).
  JsonObject feat = root["features"].to<JsonObject>();
  feat["ticker"] = (bool)WITH_TICKER;
  feat["usage"]  = (bool)WITH_USAGE;
  feat["radar"]  = (bool)WITH_RADAR;
  feat["rain"]   = (bool)WITH_RAIN;
  feat["ha"]     = (bool)WITH_HA;
  feat["theme"]  = (bool)WITH_THEME;
  // WireGuard is a per-chip decision rather than a per-feature one: it is
  // compiled only where the image has room for it (the ESP32-C2 build).
#if defined(SMALLTV_WIREGUARD)
  feat["wireguard"] = true;
#else
  feat["wireguard"] = false;
#endif
  // Physical button: only present in some boards, so the UI hides its card otherwise.
#ifdef BTN_PIN
  feat["button"] = true;
#else
  feat["button"] = false;
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

// The pieces of the status reply, to the client as they come, with a pause
// after each (CONTRIBUTING: a loop over input-sized data yields).
struct StatusSink : smalltv::Sink {
  bool write(const char* data, size_t count) override {
    server.sendContent(data, count);
    yield();
    return true;
  }
};

#if WITH_TICKER
static smalltv::StatusTicker statusTicker(size_t i) {
  const StockData& d = stockAt(i);
  smalltv::StatusTicker t;
  t.symbol = d.symbol;
  t.valid = d.valid;
  t.error = d.error;
  if (d.error) t.retryIn = stockRetryInSec(i);   // seconds to the next attempt
  if (d.valid) {
    t.price = d.price;
    float chg, pct;
    bool onRange = false;
    if (stockDisplayChange(d, S->ticker, chg, pct, &onRange)) {
      t.hasChange = true;
      t.changePct = pct;                           // as displayed on the device
      t.onRange = onRange;                         // which basis that was
    }
  }
  return t;
}
#endif

// GET /api/status. The reply is written in 128-byte pieces as the values are
// read, never built whole (StatusJson.h): its length is not known before the
// last value, and uptime and heap change meanwhile, so it goes out chunked.
// 3.2.1 built a document and a String of it; six requests at once then left
// the ESP8266 with a 648-byte largest block, and a diag build crashed.
static void handleStatus() {
  if (!requireAuth()) return;
  const String ssid = netSSID(), ip = netIP();
  const String clockText = clockTimeStr();
  smalltv::StatusData d;
  d.fw = FW_NAME;
  d.version = FW_VERSION;
  d.variant = FW_VARIANT;   // which release image this is, e.g. "esp8266-lean"
  d.repo = REPO_URL;
  d.updateMsg = g_updateMsg.c_str();
  d.mode = (netMode() == NET_AP) ? "ap" : "sta";
  d.connected = netConnected();
  d.ssid = ssid.c_str();
  d.ip = ip.c_str();
  d.rssi = netRSSI();
  d.wifiFail = netFailReason();
  d.heap = ESP.getFreeHeap();
  d.maxblk = platformMaxFreeBlock();     // largest contiguous block (TLS handshake needs one)
  d.contstk = platformFreeContStack();   // primary stack headroom (ESP8266)
  d.uptime = millis() / 1000;
  d.reset = appResetReason();
  d.synced = clockSynced();
  d.time = clockText.c_str();
  d.tz = S->clock.tz.c_str();
  d.night = clockNightActive();          // dimming now
  d.nightHeld = clockNightHeld();        // in the window but waiting for a fresh NTP sync
  d.clockFresh = clockTrusted();         // last NTP sync within the trust window
  char sunText[24] = "";
  if (!clockSunText(*S, sunText, sizeof(sunText))) sunText[0] = 0;
  d.sun = sunText;                       // today's sunrise-sunset; left out when empty
  d.bl = appBacklightSteps();            // backlight level now, 0..255
  d.wg = wgStatus();                     // tunnel state (compiledIn=false where it isn't built)
#if WITH_HA
  // Only when Home Assistant is not as set: no room at boot, safe mode, or
  // switched on or off since boot (a restart does it). Left out otherwise.
  const char* haNote = haProblem(haWanted(*S));
  d.ha = haNote ? haNote : "";
#endif

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  StatusSink sink;
  smalltv::PieceWriter<128> out(sink);
  smalltv::writeStatusHead(out, d);
  PlatformCrashRecord cr;   // last crash, kept in RTC memory until cleared or power-cycled
  if (platformCrashRecordGet(cr)) {
    char cause[12], epc[12], addr[12];
    snprintf(cause, sizeof cause, "0x%x", (unsigned)cr.exccause);
    snprintf(epc, sizeof epc, "0x%08x", (unsigned)cr.epc1);
    snprintf(addr, sizeof addr, "0x%08x", (unsigned)cr.excvaddr);
    smalltv::StatusCrash k;
    k.reason = cr.reason;
    k.fw = cr.fw;
    if (cr.hasRegisters) { k.exccause = cause; k.epc = epc; k.addr = addr; }
    k.hasUptime = cr.uptimeKnown;
    k.uptime = cr.uptimeS;
    smalltv::writeStatusCrash(out, k);
  }
#if WITH_TICKER
  smalltv::writeStatusTickers(out, stocksCount(), statusTicker);
#endif
#if WITH_RADAR
  // An empty scope has several possible causes and the screen shows the same
  // thing for all of them, so report the last poll's actual outcome.
  smalltv::StatusRadar r;
  r.count = radarCount();          // aircraft currently plotted
  r.error = radarError();
  r.stage = radarStageName();      // why the last poll produced what it did
  r.seenAc = radarSeenAc();        // aircraft in the last parsed response
  r.http = radarLastHttp();        // <0 = HTTPClient internal error
  r.tlsRx = radarTlsRx();          // negotiated BearSSL rx buffer
  if (radarLastTryMs()) { r.hasTried = true; r.triedAgo = (millis() - radarLastTryMs()) / 1000; }
  if (radarLastOkMs())  { r.hasOk = true;    r.okAgo    = (millis() - radarLastOkMs()) / 1000; }
  r.url = radarLastUrl().c_str();
  smalltv::writeStatusRadar(out, r);
#endif
#if WITH_RAIN
  smalltv::StatusRain rn;
  rn.frames = rainCount();           // frames currently on disk
  rn.error = rainError();
  rn.stage = rainStageName();        // why the last poll produced what it did
  rn.http = rainLastHttp();          // <0 = HTTPClient internal error
  rn.cropK = rainCropK();            // centre crop of the last poll
  rn.generated = rainGenerated();    // unix time the server generated the set
  if (rainLastTryMs()) { rn.hasTried = true; rn.triedAgo = (millis() - rainLastTryMs()) / 1000; }
  if (rainLastOkMs())  { rn.hasOk = true;    rn.okAgo    = (millis() - rainLastOkMs()) / 1000; }
  rn.url = rainLastUrl().c_str();
  rn.drawMs = rainDrawMs();
  rn.blitMs = rainBlitMs();
  rn.citiesMs = rainCitiesMs();
  smalltv::writeStatusRain(out, rn);
#endif
  smalltv::writeStatusEnd(out);
  server.sendContent("", 0);   // the last chunk
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

// A settings body that could not be parsed. Running out of heap is reported
// as that: on the ESP8266 it is the usual cause, and "bad json" sent people
// looking for a syntax error that was not there.
static void sendParseError(DeserializationError err) {
  if (err == DeserializationError::NoMemory)
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"not enough memory\"}");
  else
    server.send(400, "application/json", String("{\"ok\":false,\"error\":\"bad json (") + err.c_str() + ")\"}");
}

static void handlePostConfig() {
  if (!requireAuth()) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }

  // Parsing the page's full settings and building the document for the file
  // need about 7 KB of heap. On the ESP8266 that is only free with the
  // running theme released; appInvalidate() below brings it back.
  appRelease();
  JsonDocument doc;
  if (DeserializationError err = deserializeJson(doc, server.arg("plain"))) {
    sendParseError(err);
    return;
  }

  String oldNet = netFingerprint(*S);
  String oldWg = wgFingerprint(*S);

  // The request is freed once applied, before saveSettings() builds its own
  // document: the two together do not fit on the ESP8266.
  if (!applyAndSaveSettings(*S,
        [&](Settings& settings) { settingsApplyJson(settings, doc.as<JsonObjectConst>()); doc.clear(); },
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
#if WITH_HA
  // Switching Home Assistant on or off (filling or clearing the broker host)
  // takes effect at the next restart, when its memory is taken or given back.
  // Tell the page, so it says so; it does not restart the device by itself.
  if (const char* haNote = haProblem(haWanted(*S))) res["ha"] = haNote;
#endif
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

static void handleCrashClear() {
  if (!requireAuth()) return;
  platformCrashRecordClear();
  server.send(200, "application/json", "{\"ok\":true}");
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
  appRelease();   // as in handlePostConfig(); the reboot below restores it
  JsonDocument doc;
  if (DeserializationError err = deserializeJson(doc, server.arg("plain"))) {
    sendParseError(err);
    return;
  }
  if (!applyAndSaveSettings(*S,
        [&](Settings& settings) { settingsApplyJson(settings, doc.as<JsonObjectConst>()); doc.clear(); },
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
#if WITH_RAIN
  rainForceRefresh();
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
  if (!guardRequest()) return;
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_USAGE
  bool ok = usageApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// Prompt activity from Claude Code hooks: /api/activity?state=running|done|idle.
// A query argument rather than JSON so a hook is a single curl with no quoting.
// Behind the password like /api/notify, for the same reason.
static void handleActivity() {
  if (!requireAuth()) return;
#if WITH_USAGE
  bool ok = usageSetActivity(server.arg("state"));
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
#if defined(SMALLTV_FAMILY_ESP8266)
// The core's updater asks for one buffer in one piece and gives up if the
// heap has none (UpdateBuffer.h). A refused start is remembered here, so the
// final handler can say why instead of "OK" or a generic failure.
static bool g_updateRefused = false;
static uint32_t g_refusedFree = 0;
static uint32_t g_refusedBlock = 0;
#endif

static void handleUpdateDone() {
  if (!requireAuth()) return;
#if defined(SMALLTV_FAMILY_ESP8266)
  if (g_updateRefused) {
    g_updateRefused = false;
    char msg[200];
    updatebuf::refusalText(msg, sizeof(msg), g_refusedFree, g_refusedBlock);
    server.sendHeader("Connection", "close");
    server.send(500, "text/plain", msg);
    return;
  }
#endif
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
  if (!guardAllowed()) {
    if (up.status != UPLOAD_FILE_START) Update.end();
    return;
  }
  if (S->auth.pass.length() &&
      !server.authenticate(S->auth.user.c_str(), S->auth.pass.c_str())) {
    if (up.status != UPLOAD_FILE_START) Update.end();
    return;
  }
  if (up.status == UPLOAD_FILE_START) {
#if defined(SMALLTV_FAMILY_ESP8266)
    WiFiUDP::stopAll();   // free UDP sockets so the OTA has max contiguous flash/heap
    g_updateRefused = false;
    // Read after stopAll(): these are the numbers Update.begin() will see.
    uint32_t freeNow = ESP.getFreeHeap();
    uint32_t blockNow = ESP.getMaxFreeBlockSize();
    if (!updatebuf::uploadCanStart(freeNow, blockNow)) {
      g_updateRefused = true;
      g_refusedFree = freeNow;
      g_refusedBlock = blockNow;
      return;
    }
#endif
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSpace)) Update.printError(Serial);
  }
#if defined(SMALLTV_FAMILY_ESP8266)
  else if (g_updateRefused) {
    // Refused at the start: nothing was begun, so the chunks, the end and
    // the abort have nothing to act on.
  }
#endif
  else if (up.status == UPLOAD_FILE_WRITE) {
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
  themeWebBegin(server, settings, requireAuth, guardAllowed);
#endif
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/crash/clear", HTTP_POST, handleCrashClear);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/import", HTTP_POST, handleImport);
  server.on("/api/checkupdate", HTTP_GET, handleCheckUpdate);
  server.on("/api/selfupdate", HTTP_POST, handleSelfUpdate);
  server.on("/api/usage", HTTP_POST, handleUsagePush);   // daemon pushes usage here
  server.on("/api/activity", HTTP_POST, handleActivity); // Claude Code hooks
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

  // Headers the guard reads; the server drops all others.
#if defined(SMALLTV_FAMILY_ESP8266)
  server.collectHeaders("Origin", "Sec-Fetch-Site");   // variadic, keeps Authorization
#else
  static const char* kGuardHeaders[] = {"Origin", "Sec-Fetch-Site"};
  server.collectHeaders(kGuardHeaders, 2);
#endif

  server.begin();
}

void webPortalLoop() {
  server.handleClient();

  // Run the GitHub self-update outside the request handler so the browser gets its
  // response first.
  if (g_selfUpdate) {
    g_selfUpdate = false;
#if defined(SMALLTV_FAMILY_ESP8266)
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
