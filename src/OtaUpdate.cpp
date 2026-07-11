#include "OtaUpdate.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"

#if defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32)
#include <HTTPUpdate.h>
#endif

#if defined(SMALLTV_ESP8266)
// Prefer MFLN so BearSSL can run with a tiny buffer; fall back to 4 KB.
static uint16_t probeMfln(const char* host) {
  if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 512))  return 512;
  if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 1024)) return 1024;
  return 4096;
}
#endif

// "a.b.c" -> a*10000 + b*100 + c, for a simple newer-than comparison.
static long verNum(const char* v) {
  int a = 0, b = 0, c = 0;
  sscanf(v, "%d.%d.%d", &a, &b, &c);
  return (long)a * 10000 + (long)b * 100 + c;
}

OtaLatest otaCheckLatest(const Settings& s) {
  OtaLatest r;
  if (ESP.getFreeHeap() < 20000) { r.error = F("low heap"); return r; }

  String url = F("https://");
  url += F(GH_API_HOST);
  url += F("/repos/");
  url += F(REPO_OWNER);
  url += "/";
  url += F(REPO_NAME);
  url += F("/releases/latest");

  // GitHub over TLS on this chip occasionally stalls a stream read (truncated
  // JSON -> "parse failed") or drops the connection; a couple of quick retries
  // clear the transient. A 403 with the rate-limit budget exhausted is NOT
  // retryable — surface a clear message so the user waits instead of hammering
  // the API (which is what turns an occasional hiccup into a persistent failure).
  const int kAttempts = 3;
  for (int attempt = 1; attempt <= kAttempts; attempt++) {
    r.tag = ""; r.url = ""; r.newer = false;   // clear partial state from any prior attempt
    bool retryable = false;

    SecureClient client;
    client.setInsecure();
#if defined(SMALLTV_ESP8266)
    client.setBufferSizes(probeMfln(GH_API_HOST), 512);
#endif

    HTTPClient http;
    // A stalled stream truncates into a "parse failed"; the retries below clear
    // that, so keep the per-attempt timeout modest to stay responsive (this runs
    // in the ESP32 web handler) rather than blocking long on each failing try.
    http.setTimeout(s.httpTimeout);
    http.setReuse(false);
    http.setUserAgent(F(FW_NAME));                 // GitHub rejects requests with no UA
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // HTTP/1.0 forbids chunked responses. The body is parsed straight off
    // getStream(), which neither core de-chunks (same fix as StockClient v2.4.1).
    http.useHTTP10(true);
    const char* hdrKeys[] = { "x-ratelimit-remaining" };
    http.collectHeaders(hdrKeys, 1);

    if (!http.begin(client, url)) {
      r.error = F("connect failed"); retryable = true;
    } else {
      http.addHeader("Accept", "application/vnd.github+json");
      int code = http.GET();
      if (code == 403 && http.header("x-ratelimit-remaining") == "0") {
        r.error = F("GitHub rate limit, try again later");   // not retryable
      } else if (code != HTTP_CODE_OK) {
        r.error = "HTTP " + String(code);
        retryable = (code >= 500);                            // server-side -> transient
      } else {
        // Keep only the fields we need; the releases payload is large.
        JsonDocument filter;
        filter["tag_name"] = true;
        JsonObject fa = filter["assets"][0].to<JsonObject>();
        fa["name"] = true;
        fa["browser_download_url"] = true;

        JsonDocument doc;
        DeserializationError err =
            deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        if (err) {
          r.error = F("parse failed"); retryable = true;      // truncated/stalled stream
        } else {
          r.tag = (const char*)(doc["tag_name"] | "");
          for (JsonObjectConst a : doc["assets"].as<JsonArrayConst>()) {
            if (strcmp(a["name"] | "", UPDATE_ASSET) == 0) {
              r.url = (const char*)(a["browser_download_url"] | "");
              break;
            }
          }
          if (r.tag.length() == 0 || r.url.length() == 0) {
            r.error = F("no matching asset");                 // not retryable
          } else {
            String latest = r.tag;
            if (latest.startsWith("v")) latest.remove(0, 1);
            r.newer = verNum(latest.c_str()) > verNum(FW_VERSION);
            r.error = "";
            r.ok = true;
          }
        }
      }
      http.end();
    }

    if (r.ok || !retryable) return r;
    if (attempt < kAttempts) delay(500);           // brief backoff before the next try
  }
  return r;   // r.error holds the last (retryable) error after all attempts
}

String otaUpdateFromGitHub(const Settings& s) {
#if defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32)
  OtaLatest r = otaCheckLatest(s);
  if (!r.ok) return "check failed: " + r.error;
  if (!r.newer) return "already up to date (" FW_VERSION ")";
  if (ESP.getFreeHeap() < 22000) return F("not enough free heap for a TLS update");

  // mbedTLS manages its own buffers; each target pulls its own release asset
  // (UPDATE_ASSET in config.h). The two-slot OTA layout makes this atomic, so a
  // failed/interrupted download just leaves the running image untouched — retry
  // once on a transient stream stall before giving up.
  String lastErr;
  for (int attempt = 1; attempt <= 2; attempt++) {
    SecureClient client;
    client.setInsecure();

    HTTPUpdate up;
    up.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    up.rebootOnUpdate(true);

    t_httpUpdate_return ret = up.update(client, r.url);
    switch (ret) {
      case HTTP_UPDATE_OK:         return "";                     // reboots into the new image
      case HTTP_UPDATE_NO_UPDATES: return F("server reported no update");
      case HTTP_UPDATE_FAILED:     lastErr = up.getLastErrorString(); break;
    }
    if (attempt < 2) delay(1000);
  }
  return "download failed after retry: " + lastErr;
#else
  (void)s;
  return F("internal error: the ESP8266 updates at boot");   // WebPortal never calls this here
#endif
}

// ---- update-at-boot (ESP8266) ----------------------------------------------
// The asset download needs a full 16 KB BearSSL receive buffer (github.com and
// release-assets.githubusercontent.com offer no MFLN), which does not fit next
// to the running features. The web UI queues the request in LittleFS and
// reboots; this runs early in setup() with the heap still free. The request is
// consumed BEFORE the attempt, so a crash or failure can never boot-loop.
#if defined(SMALLTV_ESP8266)
static const char* OTA_REQ_PATH = "/ota.req";
static const char* OTA_MSG_PATH = "/ota.msg";

bool otaBootRequested() { return LittleFS.exists(OTA_REQ_PATH); }

bool otaRequestBootUpdate(const char* tag) {
  File f = LittleFS.open(OTA_REQ_PATH, "w");
  if (!f) return false;                     // storage full/broken -> caller must not reboot
  f.print(tag ? tag : "");
  f.close();
  return true;
}

static void otaBootResult(const String& msg) {
  File f = LittleFS.open(OTA_MSG_PATH, "w");
  if (f) { f.print(msg); f.close(); }
}

String otaTakeBootResult() {
  if (!LittleFS.exists(OTA_MSG_PATH)) return String();
  File f = LittleFS.open(OTA_MSG_PATH, "r");
  String m = f ? f.readString() : String();
  if (f) f.close();
  LittleFS.remove(OTA_MSG_PATH);
  return m;
}

void otaBootUpdate(const Settings& s) {
  LittleFS.remove(OTA_REQ_PATH);            // consume first: one attempt per request
  if (WiFi.status() != WL_CONNECTED) { otaBootResult(F("no WiFi at boot")); return; }

  OtaLatest r = otaCheckLatest(s);          // re-resolve the asset URL fresh
  if (!r.ok)    { otaBootResult("check failed: " + r.error); return; }
  if (!r.newer) { otaBootResult(F("already up to date (" FW_VERSION ")")); return; }

  // Honest guard: rx + tx buffers plus BearSSL engine/stack-thunk overhead.
  const uint32_t need = 16384 + 512 + 8000;
  if (ESP.getFreeHeap() < need || ESP.getMaxFreeBlockSize() < 16384 + 1024) {
    otaBootResult("not enough heap even at boot (" + String(ESP.getFreeHeap()) +
                  " free, need " + String(need) + ")");
    return;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(16384, 512);        // no MFLN on the CDN -> full-size records

  ESPhttpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  ESPhttpUpdate.rebootOnUpdate(true);

  // Retry once on a transient stream stall — the buffers are still free at boot
  // and the request was already consumed, so a retry can't boot-loop.
  t_httpUpdate_return ret = HTTP_UPDATE_FAILED;
  for (int attempt = 1; attempt <= 2; attempt++) {
    ret = ESPhttpUpdate.update(client, r.url);
    if (ret == HTTP_UPDATE_OK || ret == HTTP_UPDATE_NO_UPDATES) break;  // OK reboots; NO_UPDATES is final
    if (attempt < 2) delay(1000);
  }
  if (ret == HTTP_UPDATE_NO_UPDATES)
    otaBootResult(F("server reported no update"));
  else if (ret != HTTP_UPDATE_OK)
    otaBootResult("download failed: " + ESPhttpUpdate.getLastErrorString());
  // HTTP_UPDATE_OK: rebootOnUpdate restarts into the new image
}
#else
bool   otaBootRequested() { return false; }
bool   otaRequestBootUpdate(const char*) { return false; }
void   otaBootUpdate(const Settings&) {}
String otaTakeBootResult() { return String(); }
#endif
