#include "RadarClient.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <math.h>

static Aircraft g_ac[MAX_AIRCRAFT];   // kept sorted nearest-first
static uint8_t  g_count = 0;
static uint32_t g_lastOkMs = 0;
static bool     g_error = false;
static uint32_t g_nextPollMs = 0;

// TLS receive-buffer size for the direct-feed handshake, chosen by probing the
// server's Maximum Fragment Length support. MFLN at 512/1024 lets BearSSL use a
// tiny buffer (a big heap win on the ESP8266); otherwise we fall back to 4 KB and
// hope the records fit. Cloudflare-fronted hosts offer neither, which is what
// took direct adsb.fi off the ESP8266 — adsb.lol or the webhook path instead.
static uint16_t    g_tlsRx = 0;
// Host g_tlsRx was probed against, compared by pointer: both candidates are the
// same two string literals every time, so identity is enough.
static const char* g_tlsProbedHost = nullptr;

// TLS session resumption: the first handshake stores its parameters here and
// later connects to the same feed resume without the full ECDHE exchange,
// which is both the slow part and the allocation peak on this chip. cash.ch
// has fetched this way for a long time; the radar polls far more often, so it
// benefits more. Reset alongside the MFLN probe when the provider changes.
static TlsSession g_tlsSession;

// Last-poll diagnostics (see RadarStage in the header).
static RadarStage g_stage = RADAR_IDLE;
static int        g_lastHttp = 0;
static uint16_t   g_seenAc = 0;
static uint32_t   g_lastTryMs = 0;
static String     g_lastUrl;

uint8_t         radarCount()      { return g_count; }
const Aircraft& aircraftAt(uint8_t i) { return g_ac[i]; }
uint32_t        radarLastOkMs()   { return g_lastOkMs; }
bool            radarError()      { return g_error; }

RadarStage    radarStage()     { return g_stage; }
int           radarLastHttp()  { return g_lastHttp; }
uint16_t      radarTlsRx()     { return g_tlsRx; }
uint16_t      radarSeenAc()    { return g_seenAc; }
uint32_t      radarLastTryMs() { return g_lastTryMs; }
const String& radarLastUrl()   { return g_lastUrl; }

const char* radarStageName() {
  switch (g_stage) {
    case RADAR_NO_HOME:       return "no home set";
    case RADAR_LOW_HEAP:      return "skipped, low heap";
    case RADAR_CONNECT_FAIL:  return "connect failed";
    case RADAR_HTTP_ERROR:    return "http error";
    case RADAR_PARSE_FAIL:    return "parse failed";
    case RADAR_NO_AC:         return "no aircraft in feed";
    case RADAR_FILTERED_ALL:  return "all filtered out";
    case RADAR_OK:            return "ok";
    default:                  return "idle";
  }
}

void radarInit(const Settings& s) {
  (void)s;
  g_count = 0;
  g_error = false;
  g_lastOkMs = 0;
  g_nextPollMs = millis();
  g_stage = RADAR_IDLE;
  g_lastHttp = 0;
  g_seenAc = 0;
  g_lastTryMs = 0;
  g_lastUrl = "";
}

void radarForceRefresh() { g_nextPollMs = millis(); }

// ---- geo: flat-earth projection around home (good enough at radar ranges) --
static void geo(float homeLat, float homeLon, float lat, float lon,
                float& distKm, float& brg) {
  float dLat = (lat - homeLat) * 111.0f;                              // km north
  float dLon = (lon - homeLon) * 111.0f * cosf(homeLat * (float)PI / 180.0f); // km east
  distKm = sqrtf(dLat * dLat + dLon * dLon);
  brg = atan2f(dLon, dLat) * 180.0f / (float)PI;                      // 0 = N, 90 = E
  if (brg < 0) brg += 360.0f;
}

// Keep the array sorted ascending by distance, holding at most MAX_AIRCRAFT.
static void insertNearest(const Aircraft& t) {
  if (g_count == MAX_AIRCRAFT && t.distKm >= g_ac[g_count - 1].distKm) return;
  uint8_t i = (g_count < MAX_AIRCRAFT) ? g_count : (uint8_t)(MAX_AIRCRAFT - 1);
  while (i > 0 && g_ac[i - 1].distKm > t.distKm) { g_ac[i] = g_ac[i - 1]; i--; }
  g_ac[i] = t;
  if (g_count < MAX_AIRCRAFT) g_count++;
}

static void trimTail(char* s) {
  int n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

// ---- which open-data feed the "direct" sources point at --------------------
static const char* directHost(const Settings& s) {
  return (s.radar.source == RADAR_SRC_ADSBLOL) ? ADSB_LOL_HOST : ADSB_FI_HOST;
}
static const char* directPath(const Settings& s) {
  return (s.radar.source == RADAR_SRC_ADSBLOL) ? ADSB_LOL_PATH : ADSB_FI_PATH;
}

// ---- probe MFLN once so TLS can use the smallest safe buffer ---------------
// Re-probed when the provider changes: the two hosts answer differently
// (adsb.lol negotiates MFLN, adsb.fi behind Cloudflare does not).
static void probeTls(const Settings& s) {
#if defined(SMALLTV_FAMILY_ESP8266)
  const char* host = directHost(s);
  if (g_tlsRx && g_tlsProbedHost == host) return;
  g_tlsProbedHost = host;
  g_tlsSession = TlsSession();   // stored params are per-host; drop them with it
  if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 512))       g_tlsRx = 512;
  else if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 1024)) g_tlsRx = 1024;
  else                                                                         g_tlsRx = 4096;
#else
  (void)s;
#endif
}

// ---- URL builders ----------------------------------------------------------
static uint16_t rangeNm(uint16_t km) {
  uint16_t nm = (uint16_t)lroundf(km / 1.852f) + 1;   // +1 so the ring edge is covered
  return nm < 1 ? 1 : nm;
}

static String buildDirectUrl(const Settings& s) {
  String u = F("https://");
  u += directHost(s);
  u += directPath(s);
  u += String(radarLat(s), 4);
  u += F("/lon/");
  u += String(radarLon(s), 4);
  u += F("/dist/");
  u += String(rangeNm(s.radar.rangeKm));
  return u;
}

static String buildWebhookUrl(const Settings& s) {
  String u = s.radar.webhookUrl;
  char sep = (u.indexOf('?') >= 0) ? '&' : '?';
  u += sep;
  u += "lat=" + String(radarLat(s), 4);
  u += "&lon=" + String(radarLon(s), 4);
  u += "&dist=" + String(s.radar.rangeKm);   // webhook works in km
  return u;
}

// ---- parse the open-data / webhook "ac" array ------------------------------
static bool parseAdsb(const Settings& s, Stream& stream) {
  // Filter to just the fields we plot; applied to every element of "ac".
  JsonDocument filter;
  JsonObject fe = filter["ac"][0].to<JsonObject>();
  fe["lat"] = true;
  fe["lon"] = true;
  fe["track"] = true;
  fe["gs"] = true;
  fe["flight"] = true;
  fe["hex"] = true;
  fe["alt_baro"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, stream, DeserializationOption::Filter(filter));
  if (err) { g_stage = RADAR_PARSE_FAIL; return false; }

  JsonArrayConst ac = doc["ac"].as<JsonArrayConst>();
  if (ac.isNull()) { g_stage = RADAR_NO_AC; return false; }

  g_seenAc = 0;
  for (JsonObjectConst a : ac) { (void)a; g_seenAc++; }

  g_count = 0;
  for (JsonObjectConst a : ac) {
    if (!a["lat"].is<float>() && !a["lat"].is<int>()) continue;
    if (!a["lon"].is<float>() && !a["lon"].is<int>()) continue;

    Aircraft t;
    t.lat = a["lat"].as<float>();
    t.lon = a["lon"].as<float>();
    t.track = (a["track"].is<float>() || a["track"].is<int>()) ? a["track"].as<float>() : NAN;
    t.gs    = (a["gs"].is<float>()    || a["gs"].is<int>())    ? a["gs"].as<float>()    : NAN;
    t.altFt = a["alt_baro"].is<int>() ? a["alt_baro"].as<int>() : 0;  // "ground" => 0

    // Optional: drop ground/low traffic below the configured altitude threshold.
    if (s.radar.minAltFt > 0 && t.altFt < (int32_t)s.radar.minAltFt) continue;

    const char* fl = a["flight"] | (a["hex"] | "");
    strlcpy(t.callsign, fl, sizeof(t.callsign));
    trimTail(t.callsign);

    geo(radarLat(s), radarLon(s), t.lat, t.lon, t.distKm, t.bearingDeg);
    insertNearest(t);
  }

  g_lastOkMs = millis();
  g_error = false;
  // A response that parsed but plotted nothing is a filter result, not a
  // network fault; g_seenAc says whether the feed had traffic to begin with.
  g_stage = (g_count > 0) ? RADAR_OK
          : (g_seenAc > 0) ? RADAR_FILTERED_ALL : RADAR_NO_AC;
  return true;
}

// ---- one HTTP(S) GET + parse ----------------------------------------------
static bool fetchUrl(const Settings& s, const String& url) {
  bool https = url.startsWith("https://");

  std::unique_ptr<NetClient> client;
  if (https) {
    // The handshake needs one contiguous block, not total free heap: 16 KB is
    // the same floor the cash.ch fetch uses for this identical handshake shape.
    // (The old total-heap >= 18000 test passed on fragmented heaps that then
    // failed the allocation, and failed healthy ones that would have worked.)
    if (platformMaxFreeBlock() < 16000) { g_stage = RADAR_LOW_HEAP; return false; }
    probeTls(s);
    client.reset(platformMakeSecureClient(g_tlsRx, &g_tlsSession));   // no cert validation (public read-only API)
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  // HTTP/1.0: a 1.0 response cannot be chunked, and parseAdsb reads the raw
  // stream. Cloudflare (fronting adsb.fi) answers 1.1 requests chunked, and the
  // chunk-size framing then reaches ArduinoJson, which reads the first hex
  // length as a bare number and reports a valid document with no "ac" in it —
  // an empty scope with HTTP 200 and no error anywhere.
  http.useHTTP10(true);
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(*client, url)) { g_stage = RADAR_CONNECT_FAIL; return false; }
  http.addHeader("Accept", "application/json");
  http.setUserAgent(F(ADSB_USER_AGENT));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int code = http.GET();
  g_lastHttp = code;
  if (code != HTTP_CODE_OK) {
    http.end();
    // A negative code is an ESP8266HTTPClient internal error (connection
    // refused, read timeout, connection lost) rather than a server reply.
    g_stage = (code < 0) ? RADAR_CONNECT_FAIL : RADAR_HTTP_ERROR;
    return false;
  }

  bool ok = parseAdsb(s, http.getStream());
  http.end();
  return ok;
}

// ---------------------------------------------------------------------------
void radarService(const Settings& s) {
  // No home set yet -> nothing to fetch (the mode shows a prompt instead).
  if (radarLat(s) == 0.0f && radarLon(s) == 0.0f) { g_stage = RADAR_NO_HOME; return; }

  if ((int32_t)(millis() - g_nextPollMs) < 0) return;
  g_nextPollMs = millis() + (uint32_t)s.radar.pollSec * 1000UL;

  bool useWebhook = (s.radar.source == RADAR_SRC_WEBHOOK) && (s.radar.webhookUrl.length() >= 8);
  String url = useWebhook ? buildWebhookUrl(s) : buildDirectUrl(s);
  g_lastUrl = url;
  g_lastTryMs = millis();
  if (!fetchUrl(s, url)) g_error = true;   // keep stale aircraft, flag the error
}
