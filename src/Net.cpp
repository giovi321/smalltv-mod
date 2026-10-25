#include "Net.h"
#include "Platform.h"
#include <DNSServer.h>

static NetMode     g_mode = NET_AP;
static DNSServer   g_dns;
static String      g_hostname;
static String      g_apSsid;
static uint32_t    g_lastReconnect = 0;
static const Settings* g_cfg = nullptr;  // for runtime failover between saved networks
static int8_t      g_curNet = -1;        // settings index of the joined network
static uint32_t    g_downSince = 0;      // 0 = connected; else millis() the drop began

// Why the last join failed. The driver reports a disconnect reason code from
// its event task; the boot loop and the setup-mode retry turn it into a short
// text for the setup screen and /api/status. Reasons 8 (ASSOC_LEAVE) and 36
// (STA_LEAVING, ESP-IDF 5) are our own WiFi.disconnect() or the one inside
// WiFi.begin(), and would otherwise overwrite the real cause.
static volatile uint16_t g_lastReason = 0;
static volatile bool     g_assoc = false;   // associated, maybe still waiting for DHCP
static String      g_failText;
static uint16_t    g_failSeq = 0;           // bumped on every new failure text

// Setup-mode retry: while the AP is up and nobody is on it, try the saved
// networks again in the background (AP+STA), one at a time.
static const uint32_t kJoinBudgetMs   = 30000;  // per network the scan can see
static const uint32_t kUnseenBudgetMs = 8000;   // per hidden / out-of-range network
static const uint32_t kRetryEveryMs   = 90000;  // setup mode: pause between attempts
static uint32_t    g_retryAt = 0;           // millis() of the last attempt's end
static uint32_t    g_attemptStart = 0;      // 0 = no setup-mode attempt running
static bool        g_retryAllowed = true;   // off in safe mode, see netAllowRetry()
// Whether the boot scan saw each network. A retry gets the same budget the boot
// join gave it: a longer one would join here, reboot, fail the shorter boot
// join again, and cycle like that forever.
static bool        g_seen[MAX_WIFI_NETS];

static void onStaDisconnected(uint16_t reason) {
  g_assoc = false;
  if (reason != 8 && reason != 36) g_lastReason = reason;
}

#if defined(SMALLTV_FAMILY_ESP32)
static void netHookEvents() {
  static bool hooked = false;
  if (hooked) return;
  hooked = true;
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    if (e == ARDUINO_EVENT_WIFI_STA_CONNECTED) g_assoc = true;
    else if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
      onStaDisconnected(info.wifi_sta_disconnected.reason);
  });
}
#else
static WiFiEventHandler g_evConnected, g_evDisconnected;   // must stay alive
static void netHookEvents() {
  if (g_evDisconnected) return;
  g_evConnected = WiFi.onStationModeConnected(
      [](const WiFiEventStationModeConnected&) { g_assoc = true; });
  g_evDisconnected = WiFi.onStationModeDisconnected(
      [](const WiFiEventStationModeDisconnected& e) { onStaDisconnected((uint16_t)e.reason); });
}
#endif

// Reason codes shared by the ESP8266 SDK and ESP-IDF (200+ are Espressif's own).
// PSTR: on the ESP8266 a plain literal would sit in RAM, which is heap there.
static const char* reasonText(uint16_t r) {
  switch (r) {
    case 2:                             return PSTR("auth timed out");
    case 15: case 202: case 204:        return PSTR("wrong password?");
    case 201:                           return PSTR("network not found");
    case 210: case 211:                 return PSTR("security mode?");
    case 203:                           return PSTR("router refused");
    case 200:                           return PSTR("signal lost");
    default:                            return PSTR("join failed");
  }
}

// Call once an attempt has timed out, before WiFi.disconnect().
static void recordFailure(const String& ssid) {
  char why[24];
  if (g_assoc)            strcpy_P(why, PSTR("no IP (DHCP)"));
  else if (g_lastReason)  strcpy_P(why, reasonText(g_lastReason));
  else                    strcpy_P(why, PSTR("timed out"));
  char buf[64];
  if (g_lastReason && !g_assoc)
    snprintf_P(buf, sizeof(buf), PSTR("%s: %s (%u)"), ssid.c_str(), why, (unsigned)g_lastReason);
  else
    snprintf_P(buf, sizeof(buf), PSTR("%s: %s"), ssid.c_str(), why);
  g_failText = buf;
  g_failSeq++;
  Serial.print(F("[net] join failed, "));
  Serial.println(buf);
}

static void beginJoin(const WifiCred& n) {
  g_lastReason = 0;
  g_assoc = false;
  WiFi.begin(n.ssid.c_str(), n.pass.c_str());
}

static void startAP(const Settings& s) {
  g_mode = NET_AP;
  // With saved networks keep the station side on, so netLoop can retry them.
  // netLoop drives every attempt itself: the driver's own reconnect would keep
  // the radio scanning off the AP channel between attempts, and on the ESP32
  // could restart a join right after netLoop ended it.
  WiFi.setAutoReconnect(false);
  WiFi.mode(s.wifiCount ? WIFI_AP_STA : WIFI_AP);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (s.apPass.length() >= 8) {
    WiFi.softAP(s.apSsid.c_str(), s.apPass.c_str());
  } else {
    WiFi.softAP(s.apSsid.c_str());           // open AP (WPA2 needs >=8 chars)
  }
  g_apSsid = s.apSsid;
  // Captive portal: answer every DNS query with our own IP.
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(53, "*", apIP);
  g_retryAt = millis();
}

void netBegin(const Settings& s, void (*onProgress)(const char*)) {
  g_cfg = &s;
  g_hostname = s.hostname.length() ? s.hostname : String(DEFAULT_HOSTNAME);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  platformSetHostname(g_hostname.c_str());
  netHookEvents();

  if (s.wifiCount == 0) {
    if (onProgress) onProgress("No WiFi saved");
    startAP(s);
    return;
  }

  WiFi.mode(WIFI_STA);

  // Try order: scan once (blocking is fine here, only the boot screen is up)
  // and put the networks the scan can see first, strongest first. Unseen ones
  // (hidden SSIDs or currently out of range) go last, in config order, with a
  // shorter timeout each.
  uint8_t order[MAX_WIFI_NETS];
  bool    seen[MAX_WIFI_NETS];
  if (s.wifiCount == 1) {
    order[0] = 0;
    seen[0] = true;
  } else {
    int32_t rssi[MAX_WIFI_NETS];
    for (uint8_t i = 0; i < s.wifiCount; i++) { rssi[i] = -32768; seen[i] = false; }
    if (onProgress) onProgress("Scanning...");
    int found = WiFi.scanNetworks();
    for (int a = 0; a < found; a++)
      for (uint8_t i = 0; i < s.wifiCount; i++)
        if (WiFi.SSID(a) == s.wifi[i].ssid && WiFi.RSSI(a) > rssi[i]) {
          rssi[i] = WiFi.RSSI(a);
          seen[i] = true;
        }
    WiFi.scanDelete();

    bool used[MAX_WIFI_NETS] = {false};
    for (uint8_t k = 0; k < s.wifiCount; k++) {
      int best = -1;
      for (uint8_t i = 0; i < s.wifiCount; i++) {
        if (used[i]) continue;
        if (best < 0 ||
            (seen[i] && !seen[best]) ||
            (seen[i] == seen[best] && rssi[i] > rssi[best])) best = i;
      }
      used[best] = true;
      order[k] = (uint8_t)best;
    }
  }

  for (uint8_t i = 0; i < s.wifiCount; i++) g_seen[i] = seen[i];

  for (uint8_t k = 0; k < s.wifiCount; k++) {
    const WifiCred& n = s.wifi[order[k]];
    if (onProgress) {
      char msg[48];
      snprintf(msg, sizeof(msg), "WiFi: %s", n.ssid.c_str());
      onProgress(msg);
    }
    beginJoin(n);

    uint32_t budget = seen[order[k]] ? kJoinBudgetMs : kUnseenBudgetMs;
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < budget) {
      delay(200);
      yield();
    }

    if (WiFi.status() == WL_CONNECTED) {
      g_failText = "";
      g_curNet = (int8_t)order[k];
      g_mode = NET_STA;
      if (MDNS.begin(g_hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
#if WITH_USAGE
        // Discoverable usage-push service so the clawdmeter daemon can find and
        // push to every SmallTV on the LAN (no hardcoded host). TXT carries the
        // device id, firmware version, and the push path.
        MDNS.addService("clawdmeter", "tcp", 80);
        MDNS.addServiceTxt("clawdmeter", "tcp", "id",   g_hostname.c_str());
        MDNS.addServiceTxt("clawdmeter", "tcp", "ver",  FW_VERSION);
        MDNS.addServiceTxt("clawdmeter", "tcp", "path", "/api/usage");
#endif
      }
      if (onProgress) onProgress(WiFi.localIP().toString().c_str());
      return;
    }
    recordFailure(n.ssid);
    WiFi.disconnect();
    delay(100);
  }

  if (onProgress) onProgress("WiFi failed -> AP");
  startAP(s);
}

void netLoop() {
  if (g_mode == NET_AP) {
    g_dns.processNextRequest();
    // Retry the saved networks in the background. Joining moves the radio to
    // the router's channel, which drops anyone on the setup AP, so wait while
    // a client is connected. A successful join reboots: the normal boot path
    // then brings everything up in station mode.
    if (!g_retryAllowed || !g_cfg || !g_cfg->wifiCount) return;
    bool busy = WiFi.softAPgetStationNum() > 0;
    if (g_attemptStart) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("[net] setup-mode retry joined, rebooting"));
        delay(100);
        ESP.restart();
      }
      uint32_t budget = g_seen[g_curNet] ? kJoinBudgetMs : kUnseenBudgetMs;
      if (busy || millis() - g_attemptStart > budget) {
        if (!busy) recordFailure(g_cfg->wifi[g_curNet].ssid);
        WiFi.disconnect();
        g_attemptStart = 0;
        g_retryAt = millis();
      }
    } else if (!busy && millis() - g_retryAt > kRetryEveryMs) {
      g_curNet = (int8_t)((g_curNet + 1) % g_cfg->wifiCount);
      beginJoin(g_cfg->wifi[g_curNet]);
      g_attemptStart = millis();
      if (!g_attemptStart) g_attemptStart = 1;   // 0 means idle
    }
    return;
  }
  // STA: keep mDNS alive, nudge reconnect if we dropped. After a long outage
  // rotate through the other saved networks. Never scan here — it would block
  // the display loop and the web server; WiFi.begin is fire-and-forget and its
  // status is picked up on later passes.
  platformMdnsUpdate();
  if (WiFi.status() == WL_CONNECTED) {
    g_downSince = 0;
    return;
  }
  if (!g_downSince) g_downSince = millis();
  if (millis() - g_lastReconnect > 10000) {
    g_lastReconnect = millis();
    if (g_cfg && g_cfg->wifiCount > 1 && millis() - g_downSince > 45000) {
      g_curNet = (int8_t)((g_curNet + 1) % g_cfg->wifiCount);
      beginJoin(g_cfg->wifi[g_curNet]);
      g_downSince = millis();   // give this candidate its own window before rotating on
    } else {
      WiFi.reconnect();         // first ~45 s: keep nudging the current network
    }
  }
}

NetMode netMode()      { return g_mode; }
bool    netConnected() { return g_mode == NET_STA && WiFi.status() == WL_CONNECTED; }

String netIP() {
  return (g_mode == NET_AP) ? WiFi.softAPIP().toString()
                            : WiFi.localIP().toString();
}

String netSSID() {
  return (g_mode == NET_AP) ? g_apSsid : WiFi.SSID();
}

int netRSSI() {
  return (g_mode == NET_STA) ? WiFi.RSSI() : 0;
}

void        netAllowRetry(bool on) { g_retryAllowed = on; }
const char* netFailReason() { return g_failText.c_str(); }
uint16_t    netFailSeq()    { return g_failSeq; }
