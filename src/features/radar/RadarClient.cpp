#include "RadarClient.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <StreamUtils.h>
#include "WaitingStream.h"
#include "WifiReserve.h"
#include "RadarDeadlineClient.h"
#include "RadarJsonAllocator.h"
#include <math.h>
#include <new>
#include <string.h>

// What the radar holds only while it is in use (its mode, or ticked in the
// carousel): the aircraft list and the TLS session, about 1 KB. On the heap
// so an image with the radar compiled in but switched off does not pay for it
// (#134: that heap is what the ticker's HTTPS fetch is short of).
struct RadarHeap {
  Aircraft   ac[MAX_AIRCRAFT];   // kept sorted nearest-first
  // TLS session resumption: the first handshake stores its parameters here and
  // later connects to the same feed resume without the full ECDHE exchange,
  // which is both the slow part and the allocation peak on this chip. cash.ch
  // has fetched this way for a long time; the radar polls far more often, so it
  // benefits more. Reset alongside the MFLN probe when the provider changes.
  TlsSession session;
};
static RadarHeap* g_heap = nullptr;
static uint8_t  g_count = 0;
static uint32_t g_lastOkMs = 0;
static bool     g_error = false;
static uint32_t g_nextPollMs = 0;

// ESP8266 TLS receive-buffer choice: probe the selected direct provider for
// 512/1024-byte Maximum Fragment Length support, otherwise use 4096 bytes.
// The fallback cannot hold every legal TLS record. Either direct feed can fail
// if its records are larger, or if the remaining ordinary heap is insufficient.
// Plain HTTP on a LAN avoids TLS allocations; it still needs parser/packet RAM.
static uint16_t    g_tlsRx = 0;
// Host g_tlsRx was probed against, compared by pointer: both candidates are the
// same two string literals every time, so identity is enough.
static const char* g_tlsProbedHost = nullptr;
// The probe found no MFLN at 512 or 1024 (ESP8266 only). The 4 KB fallback
// then fails on every poll: both open-data feeds send 16 KB records since
// 2026-11 (#140), and BearSSL cannot read a record larger than its buffer.
// A direct poll is not sent while this holds; the probe runs again after
// RADAR_REPROBE_MS or at the next settings save, in case the server changes.
static bool        g_tlsNoMfln = false;
static uint32_t    g_tlsProbedMs = 0;
static const uint32_t RADAR_REPROBE_MS = 60UL * 60UL * 1000UL;

// Last-poll diagnostics (see RadarStage in the header).
static RadarStage g_stage = RADAR_IDLE;
static int        g_lastHttp = 0;
static const char* g_parseError = "";  // ArduinoJson static error string; no allocation
static uint16_t   g_seenAc = 0;
static uint32_t   g_lastTryMs = 0;
static String     g_lastUrl;

uint8_t         radarCount()      { return g_count; }
const Aircraft& aircraftAt(uint8_t i) { return g_heap->ac[i]; }   // i < radarCount(): 0 without g_heap
uint32_t        radarLastOkMs()   { return g_lastOkMs; }
bool            radarError()      { return g_error; }

RadarStage    radarStage()     { return g_stage; }
int           radarLastHttp()  { return g_lastHttp; }
const char*   radarParseError(){ return g_parseError; }
uint16_t      radarTlsRx()     { return g_tlsRx; }
uint16_t      radarSeenAc()    { return g_seenAc; }
uint32_t      radarLastTryMs() { return g_lastTryMs; }
const String& radarLastUrl()   { return g_lastUrl; }

const char* radarStageName() {
  switch (g_stage) {
    case RADAR_NO_HOME:       return "no home set";
    case RADAR_LOW_HEAP:      return "skipped, low heap";
    case RADAR_CONNECT_FAIL:  return "connect failed";
    case RADAR_ENCODING_ERROR:return "encoding error";
    case RADAR_TIMEOUT:       return "poll timed out";
    case RADAR_HTTP_ERROR:    return "http error";
    case RADAR_PARSE_FAIL:    return "parse failed";
    case RADAR_NO_AC:         return "no aircraft in feed";
    case RADAR_FILTERED_ALL:  return "all filtered out";
    case RADAR_NO_MFLN:       return "server needs 16 KB TLS records";
    case RADAR_OK:            return "ok";
    default:                  return "idle";
  }
}

// The radar is in use when it is the mode, or ticked in the carousel.
static bool radarWanted(const Settings& s) {
  return s.mode == MODE_RADAR || (s.mode == MODE_CAROUSEL && s.carouselRadar);
}

// Takes the heap part when there is none. False when it does not fit.
static bool radarHeapTake() {
  if (!g_heap) g_heap = new (std::nothrow) RadarHeap();
  return g_heap != nullptr;
}

static void radarHeapGive() {
  delete g_heap;
  g_heap = nullptr;
  g_tlsRx = 0;                 // the session went with it: probe again
  g_tlsProbedHost = nullptr;
  g_tlsNoMfln = false;
}

void radarInit(const Settings& s) {
  // At boot and on every settings save: taken while the boot heap is still
  // whole, given back as soon as the radar is switched off.
  if (radarWanted(s)) radarHeapTake();
  else                radarHeapGive();
  // A feed found without MFLN is probed again after a save: the user may have
  // switched the source, or wants to try the server again now.
  if (g_tlsNoMfln) {
    g_tlsRx = 0;
    g_tlsProbedHost = nullptr;
    g_tlsNoMfln = false;
  }
  g_count = 0;
  g_error = false;
  g_lastOkMs = 0;
  g_nextPollMs = millis();
  g_stage = RADAR_IDLE;
  g_lastHttp = 0;
  g_parseError = "";
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
static void insertNearest(Aircraft* list, uint8_t& count, const Aircraft& t) {
  if (count == MAX_AIRCRAFT && t.distKm >= list[count - 1].distKm) return;
  uint8_t i = (count < MAX_AIRCRAFT) ? count : (uint8_t)(MAX_AIRCRAFT - 1);
  while (i > 0 && list[i - 1].distKm > t.distKm) { list[i] = list[i - 1]; i--; }
  list[i] = t;
  if (count < MAX_AIRCRAFT) count++;
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
static bool reprobeDue() { return (uint32_t)(millis() - g_tlsProbedMs) >= RADAR_REPROBE_MS; }
// The current direct feed was probed without MFLN, and it is not time to ask
// it again: the poll is not sent, whatever the heap.
static bool noMflnHolds(const Settings& s) {
  return g_tlsNoMfln && g_tlsProbedHost == directHost(s) && !reprobeDue();
}
// Re-probed when the provider changes, since each host answers for itself.
// Neither adsb.lol nor adsb.fi negotiates MFLN since 2026-11 (#140).
static void probeTls(const Settings& s) {
#if defined(SMALLTV_FAMILY_ESP8266)
  const char* host = directHost(s);
  if (g_tlsRx && g_tlsProbedHost == host && !(g_tlsNoMfln && reprobeDue())) return;
  g_tlsProbedHost = host;
  g_tlsProbedMs = millis();
  g_heap->session = TlsSession();   // stored params are per-host; drop them with it
  g_tlsNoMfln = false;
  if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 512))       g_tlsRx = 512;
  else if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 1024)) g_tlsRx = 1024;
  else { g_tlsRx = 4096; g_tlsNoMfln = true; }
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

// One-byte lookahead keeps top-level framing outside ArduinoJson. Numeric
// metadata is presented as a complete scalar without consuming its delimiter.
class RadarJsonInput {
 public:
  explicit RadarJsonInput(Stream& stream) : stream_(stream) {}
  int peek() {
    if (held_ == -2) {
      char c;
      held_ = stream_.readBytes(&c, 1) == 1 ? static_cast<uint8_t>(c) : -1;
    }
    return held_;
  }
  int read() {
    int c = peek();
    if (number_ && (c == ',' || c == '}' || c == ']' || c == ' ' ||
                    c == '\t' || c == '\r' || c == '\n')) return -1;
    if (c >= 0) held_ = -2;
    return c;
  }
  size_t readBytes(char* bytes, size_t size) {
    size_t n = 0;
    while (n < size) {
      int c = read();
      if (c < 0) break;
      bytes[n++] = static_cast<char>(c);
    }
    return n;
  }
  int nonSpace() {
    int c;
    while ((c = peek()) == ' ' || c == '\t' || c == '\r' || c == '\n') read();
    return c;
  }
  void numberMode(bool enabled) { number_ = enabled; }
 private:
  Stream& stream_;
  int held_ = -2;
  bool number_ = false;
};

static bool parseFailure(const char* error) {
  g_parseError = error;
  g_stage = RADAR_PARSE_FAIL;
  return false;
}

static bool consumeJson(RadarJsonInput& input, char expected) {
  int c = input.nonSpace();
  if (c != expected) return parseFailure(c < 0 ? "IncompleteInput" : "InvalidInput");
  input.read();
  return true;
}

static bool skipJsonValue(RadarJsonInput& input, JsonDocument& doc) {
  int c = input.nonSpace();
  if (c < 0) return parseFailure("IncompleteInput");
  // Skip composite/string metadata without retaining input-sized values.
  // Parse primitive values normally, then return numeric lookahead to framing.
  bool number = c == '-' || (c >= '0' && c <= '9');
  input.numberMode(number);
  DeserializationError err = (c == '{' || c == '[' || c == '"')
      ? deserializeJson(doc, input, DeserializationOption::Filter(JsonVariantConst()),
                        DeserializationOption::NestingLimit(9))
      : deserializeJson(doc, input, DeserializationOption::NestingLimit(9));
  input.numberMode(false);
  if (err) return parseFailure(err.c_str());
  return true;
}

static void collectAircraft(const Settings& s, JsonObjectConst a,
                            Aircraft* pending, uint8_t& count) {
  if (!a["lat"].is<float>() && !a["lat"].is<int>()) return;
  if (!a["lon"].is<float>() && !a["lon"].is<int>()) return;
  Aircraft t;
  t.lat = a["lat"].as<float>();
  t.lon = a["lon"].as<float>();
  t.track = (a["track"].is<float>() || a["track"].is<int>()) ? a["track"].as<float>() : NAN;
  t.gs = (a["gs"].is<float>() || a["gs"].is<int>()) ? a["gs"].as<float>() : NAN;
  t.altFt = a["alt_baro"].is<int>() ? a["alt_baro"].as<int>() : 0;
  if (s.radar.minAltFt > 0 && t.altFt < (int32_t)s.radar.minAltFt) return;
  const char* flight = a["flight"] | (a["hex"] | "");
  strlcpy(t.callsign, flight, sizeof(t.callsign));
  trimTail(t.callsign);
  geo(radarLat(s), radarLon(s), t.lat, t.lon, t.distKm, t.bearingDeg);
  insertNearest(pending, count, t);
}

static bool parseAircraftArray(const Settings& s, RadarJsonInput& input,
                               JsonDocument& doc, JsonDocument& filter,
                               Aircraft* pending, uint8_t& count, uint16_t& seen) {
  if (!consumeJson(input, '[')) return false;
  count = 0; seen = 0;
  if (input.nonSpace() == ']') { input.read(); return true; }
  for (;;) {
    if (input.nonSpace() != '{')
      return parseFailure(input.nonSpace() < 0 ? "IncompleteInput" : "InvalidInput");
    DeserializationError err = deserializeJson(doc, input,
        DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8));
    if (err) return parseFailure(err.c_str());
    if (seen < UINT16_MAX) ++seen;
    collectAircraft(s, doc.as<JsonObjectConst>(), pending, count);
    int c = input.nonSpace();
    if (c == ']') { input.read(); return true; }
    if (!consumeJson(input, ',')) return false;
  }
}

#if defined(SMALLTV_FAMILY_ESP8266)
static size_t radarFreeHeap() { return ESP.getFreeHeap(); }
static size_t radarMaxBlock() { return platformMaxFreeBlock(); }
#endif

// Keep parser locals out of the TLS connection frame (upstream #149).
__attribute__((noinline)) static bool parseAdsb(const Settings& s, Stream& stream) {
  // Bound one aircraft, the field filter, and fixed candidate storage together.
  // No allocation grows with the aircraft count, on either ESP family.
#if defined(SMALLTV_FAMILY_ESP8266)
  // Keep the measured SDK reserve in total heap; only the actual parser
  // allocation, including its header, needs to fit in the largest block.
  RadarJsonAllocator allocator(4 * 1024, WifiReserveFreeHeap, radarFreeHeap,
                               malloc, realloc, free, radarMaxBlock);
#else
  RadarJsonAllocator allocator(4 * 1024);
#endif
  struct Pending { Aircraft values[MAX_AIRCRAFT]; };
  void* memory = allocator.allocate(sizeof(Pending));
  if (!memory) return parseFailure("NoMemory");
  auto release = [&allocator](Pending* p) { p->~Pending(); allocator.deallocate(p); };
  std::unique_ptr<Pending, decltype(release)> pending(new(memory) Pending, release);
  JsonDocument filter(&allocator);
  for (const char* key : {"lat", "lon", "track", "gs", "flight", "hex", "alt_baro"})
    filter[key] = true;
  if (filter.overflowed()) return parseFailure("NoMemory");
  JsonDocument doc(&allocator);
  RadarJsonInput input(stream);
  bool sawAc = false, sawAircraft = false, primary = false, found = false;
  uint8_t count = 0;
  uint16_t seen = 0;
  if (!consumeJson(input, '{')) return false;
  if (input.nonSpace() == '}') { g_stage = RADAR_NO_AC; return false; }
  for (;;) {
    if (input.nonSpace() != '"')
      return parseFailure(input.nonSpace() < 0 ? "IncompleteInput" : "InvalidInput");
    DeserializationError err = deserializeJson(doc, input);
    if (err) return parseFailure(err.c_str());
    JsonString key = doc.as<JsonString>();
    bool ac = key.size() == 2 && memcmp(key.c_str(), "ac", 2) == 0;
    bool aircraft = key.size() == 8 && memcmp(key.c_str(), "aircraft", 8) == 0;
    doc.clear();
    if (!consumeJson(input, ':')) return false;
    if ((ac && sawAc) || (aircraft && sawAircraft)) return parseFailure("InvalidInput");
    if (ac) sawAc = true;
    if (aircraft) sawAircraft = true;
    if ((ac || (aircraft && !primary)) && input.nonSpace() == '[') {
      if (!parseAircraftArray(s, input, doc, filter, pending->values, count, seen)) return false;
      found = true;
      if (ac) primary = true;  // API ac takes precedence, whichever key arrives first.
    } else if (!skipJsonValue(input, doc)) return false;
    int c = input.nonSpace();
    if (c == '}') { input.read(); break; }
    if (!consumeJson(input, ',')) return false;
  }
  if (!found) { g_stage = RADAR_NO_AC; return false; }
  // Replace cached results only after the root object is complete. Invalid or
  // incomplete arrays or trailing metadata leave the previous results intact.
  memcpy(g_heap->ac, pending->values, count * sizeof(Aircraft));
  g_count = count;
  g_seenAc = seen;
  g_lastOkMs = millis();
  g_error = false;
  g_stage = count ? RADAR_OK : seen ? RADAR_FILTERED_ALL : RADAR_NO_AC;
  return true;
}

// ---- one HTTP(S) GET + parse ----------------------------------------------
static bool fetchUrl(const Settings& s, const String& url, bool direct) {
  g_parseError = "";
  g_lastHttp = 0;
  g_seenAc = 0;
  const uint32_t started = millis();
  bool https = url.startsWith("https://");

  std::unique_ptr<NetClient> client;
  if (https) {
    // The handshake needs one contiguous block, not total free heap: 16 KB is
    // the same floor the cash.ch fetch uses for this identical handshake shape.
    // (The old total-heap >= 18000 test passed on fragmented heaps that then
    // failed the allocation, and failed healthy ones that would have worked.)
    if (direct && noMflnHolds(s)) { g_stage = RADAR_NO_MFLN; return false; }
    if (platformMaxFreeBlock() < 16000) { g_stage = RADAR_LOW_HEAP; return false; }
    probeTls(s);
    // Without MFLN the handshake to an open-data feed fails on every poll
    // (#140): say so, and send nothing until the probe finds MFLN again.
    if (direct && g_tlsNoMfln) { g_stage = RADAR_NO_MFLN; return false; }
    client.reset(platformMakeSecureClient(g_tlsRx, &g_heap->session));   // no cert validation (public read-only API)
  } else {
    client.reset(new WiFiClient());
  }

  RadarDeadlineClient limited(*client, started, s.httpTimeout);
  HTTPClient http;
  // adsb.lol rejects HTTP/1.0 with 426. Use HTTP/1.1 and decode chunked
  // bodies below: getStream() exposes raw framing on both Arduino cores.
  http.useHTTP10(false);
  const char* headers[] = {"Transfer-Encoding"};
  http.collectHeaders(headers, 1);
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(limited, url)) { g_stage = RADAR_CONNECT_FAIL; return false; }
  http.addHeader("Accept", "application/json");
  http.setUserAgent(F(ADSB_USER_AGENT));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int code = http.GET();
  g_lastHttp = code;
  if (code != HTTP_CODE_OK) {
    http.end();
    // A negative code is an HTTPClient internal error (connection
    // refused, read timeout, connection lost) rather than a server reply.
    g_stage = limited.expired() ? RADAR_TIMEOUT
        : code == HTTPC_ERROR_ENCODING ? RADAR_ENCODING_ERROR
        : (code < 0) ? RADAR_CONNECT_FAIL : RADAR_HTTP_ERROR;
    return false;
  }

  if (limited.expired()) { http.end(); g_stage = RADAR_TIMEOUT; return false; }
  WaitingStream body(http.getStream(), limited.remaining());
  bool ok;
  if (http.header("Transfer-Encoding").equalsIgnoreCase("chunked")) {
    ChunkDecodingStream decoded(body);
    ok = parseAdsb(s, decoded);
  } else {
    ok = parseAdsb(s, body);
  }
  if (!ok && limited.expired()) { g_stage = RADAR_TIMEOUT; g_parseError = ""; }
  http.end();
  return ok;
}

// ---------------------------------------------------------------------------
void radarService(const Settings& s) {
  // No home set yet -> nothing to fetch (the mode shows a prompt instead).
  if (radarLat(s) == 0.0f && radarLon(s) == 0.0f) { g_stage = RADAR_NO_HOME; return; }

  if ((int32_t)(millis() - g_nextPollMs) < 0) return;
  g_nextPollMs = millis() + (uint32_t)s.radar.pollSec * 1000UL;

  // Not taken at the save that switched the radar on (no room then): again
  // now, and the poll is skipped like any short heap while it does not fit.
  if (!radarHeapTake()) { g_stage = RADAR_LOW_HEAP; g_error = true; return; }

  bool useWebhook = (s.radar.source == RADAR_SRC_WEBHOOK) && (s.radar.webhookUrl.length() >= 8);
  String url = useWebhook ? buildWebhookUrl(s) : buildDirectUrl(s);
  g_lastUrl = url;
  g_lastTryMs = millis();
  if (!fetchUrl(s, url, !useWebhook)) g_error = true;   // keep stale aircraft, flag the error
}
