#include "OtaUpdate.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include "config.h"

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
  if (ESP.getFreeHeap() < 16000) { r.error = F("low heap"); return r; }

  SecureClient client;
  client.setInsecure();
#if defined(SMALLTV_ESP8266)
  client.setBufferSizes(probeMfln(GH_API_HOST), 512);
#endif

  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  http.setUserAgent(F(FW_NAME));                 // GitHub rejects requests with no UA
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  String url = F("https://");
  url += F(GH_API_HOST);
  url += F("/repos/");
  url += F(REPO_OWNER);
  url += "/";
  url += F(REPO_NAME);
  url += F("/releases/latest");

  if (!http.begin(client, url)) { r.error = F("connect failed"); return r; }
  http.addHeader("Accept", "application/vnd.github+json");

  int code = http.GET();
  if (code != HTTP_CODE_OK) { r.error = "HTTP " + String(code); http.end(); return r; }

  // Keep only the fields we need; the releases payload is large.
  JsonDocument filter;
  filter["tag_name"] = true;
  JsonObject fa = filter["assets"][0].to<JsonObject>();
  fa["name"] = true;
  fa["browser_download_url"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { r.error = F("parse failed"); return r; }

  r.tag = (const char*)(doc["tag_name"] | "");
  for (JsonObjectConst a : doc["assets"].as<JsonArrayConst>()) {
    if (strcmp(a["name"] | "", UPDATE_ASSET) == 0) {
      r.url = (const char*)(a["browser_download_url"] | "");
      break;
    }
  }
  if (r.tag.length() == 0 || r.url.length() == 0) { r.error = F("no matching asset"); return r; }

  String latest = r.tag;
  if (latest.startsWith("v")) latest.remove(0, 1);
  r.newer = verNum(latest.c_str()) > verNum(FW_VERSION);
  r.ok = true;
  return r;
}

String otaUpdateFromGitHub(const Settings& s) {
#if defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32)
  // No ESP32 release assets are published (local builds only), and the ESP8266
  // asset is not a valid image for these chips. Use the manual firmware upload
  // (Update tab) instead. otaCheckLatest() still reports the newest version.
  (void)s;
  return F("GitHub self-update is off on this build - use manual firmware upload");
#else
  OtaLatest r = otaCheckLatest(s);
  if (!r.ok) return "check failed: " + r.error;
  if (!r.newer) return "already up to date (" FW_VERSION ")";
  if (ESP.getFreeHeap() < 22000) return F("not enough free heap for a TLS update");

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  // The github.com asset URL redirects to a CDN host that may send large TLS
  // records. If it does not offer MFLN, give BearSSL a full-size receive buffer.
  uint16_t mf = probeMfln("objects.githubusercontent.com");
  client.setBufferSizes(mf > 1024 ? 16384 : mf, 512);

  ESPhttpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  ESPhttpUpdate.rebootOnUpdate(true);

  t_httpUpdate_return ret = ESPhttpUpdate.update(client, r.url);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      return "download failed: " + ESPhttpUpdate.getLastErrorString();
    case HTTP_UPDATE_NO_UPDATES:
      return F("server reported no update");
    case HTTP_UPDATE_OK:
      return "";   // success — rebootOnUpdate restarts into the new image
  }
  return F("unknown result");
#endif
}
