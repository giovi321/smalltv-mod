#include "WebPortal.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "webui.h"
#include "Net.h"
#include "Display.h"
#include "StockClient.h"

// Defined in main.cpp — re-init usage client + force a repaint after a config change.
extern void appInvalidate();

static ESP8266WebServer server(80);
static Settings*        S = nullptr;
static bool             g_reboot = false;
static uint32_t         g_rebootAt = 0;

static void scheduleReboot(uint32_t inMs) {
  g_reboot = true;
  g_rebootAt = millis() + inMs;
}

// ---------------------------------------------------------------------------
static void sendJson(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", WEBUI_HTML);
}

static void handleGetConfig() {
  JsonDocument doc;
  settingsToJson(*S, doc.to<JsonObject>(), /*includeSecrets=*/false);
  sendJson(doc);
}

static void handleStatus() {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["fw"] = FW_NAME;
  o["version"] = FW_VERSION;
  o["mode"] = (netMode() == NET_AP) ? "ap" : "sta";
  o["connected"] = netConnected();
  o["ssid"] = netSSID();
  o["ip"] = netIP();
  o["rssi"] = netRSSI();
  o["heap"] = ESP.getFreeHeap();
  o["uptime"] = millis() / 1000;

  JsonArray arr = o["tickers"].to<JsonArray>();
  for (uint8_t i = 0; i < stocksCount(); i++) {
    const StockData& d = stockAt(i);
    JsonObject t = arr.add<JsonObject>();
    t["symbol"] = d.symbol;
    t["valid"] = d.valid;
    t["error"] = d.error;
    if (d.valid) {
      t["price"] = d.price;
      if (d.hasChange) t["changePct"] = d.changePct;
    }
  }
  sendJson(doc);
}

static void handlePostConfig() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }

  String oldSsid = S->staSsid, oldPass = S->staPass;
  uint8_t oldRot = S->rotation;

  settingsApplyJson(*S, doc.as<JsonObjectConst>());
  saveSettings(*S);

  // Live apply (no reboot needed for these)
  displaySetBrightness(S->brightness, S->backlightInverted);
  if (S->rotation != oldRot) displaySetRotation(S->rotation);
  stocksInit(*S);
  stocksForceRefresh();
  appInvalidate();          // re-init usage client + repaint (covers mode/URL changes)

  bool wifiChanged = (S->staSsid != oldSsid) || (S->staPass != oldPass);

  JsonDocument res;
  res["ok"] = true;
  res["reboot"] = wifiChanged;
  sendJson(res);

  if (wifiChanged) scheduleReboot(800);
}

static void handleScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 25; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["enc"] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
  }
  WiFi.scanDelete();
  sendJson(doc);
}

static void handleReboot() {
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

static void handleFactory() {
  factoryReset(*S);
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

static void handleRefresh() {
  stocksForceRefresh();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---- OTA ------------------------------------------------------------------
static void handleUpdateDone() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.getErrorString().c_str());
  if (ok) scheduleReboot(1200);
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    WiFiUDP::stopAll();
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

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
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
}

bool webPortalRebootDue() {
  return g_reboot && (int32_t)(millis() - g_rebootAt) >= 0;
}
