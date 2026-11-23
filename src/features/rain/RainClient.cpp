// RainClient.cpp — fetches RainViewer radar frames (past 2h) for the rain radar.
//
// Data flow, every pollSec (15 min by default):
//   1. GET weather-maps.json -> host + 13 past frames {time, path}
//      (+ nowcast frames, currently an empty array — the code tolerates them)
//   2. Per frame: GET {host}{path}/256/7/{lat}/{lon}/2/1_1.png — a 256px
//      RGBA tile centred on the home position (radar is only served at zoom
//      7; higher zooms return map tiles). The centre crop size follows the
//      configured range: K = round(2 * range / kmPerPx), so the 240x240
//      output covers about twice the range ring.
//   3. The custom decoder (RainPng.cpp) crops, upscales and quantizes each
//      frame into /rain/t<time>.raw (240x240, 8-bit indices, 57,600 B).
//   0. With the wind arrow on (rain.wind), first the current wind at the
//      home position from Open-Meteo (fetchWind), one small JSON.
//
// Frame files are named by their radar timestamp, so a frame and its time
// can never disagree. Consecutive lists overlap by all but one or two
// frames (10-minute steps over 2 hours): a poll only downloads the frames
// it does not already have. meta.json lists the frames shown, and it only
// ever lists files that exist with their own times: it is rewritten
// (atomically) after the list step, which drops the frames that left the
// 2-hour window, and after each new frame lands. A failure at any step
// leaves a shorter but consistent set — never new pixels under old times.
// The set is tied to the location and crop it was fetched for; when they
// change, the old frames are dropped rather than shown at the wrong place.
//
// Memory: rain off costs a pointer. The client's state (RainState: the frame
// times of the set, the poll diagnostics, ~0.3 KB) exists while rain is in
// use; the fetch's list (with its path Strings and the tile host) for the
// fetch; the buffers (download buffer, LZ window, decoder state, HTTP client)
// for the step that uses them. Literals stay in flash (PSTR, F(), RAIN_P):
// on the ESP8266 a plain literal would take RAM for good, rain on or off.
#include "RainClient.h"
#include "Platform.h"          // WiFiClient/HTTPClient per platform
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include <memory>
#include <new>
#include "RainPng.h"
#include "RainWind.h"
#if !defined(WITH_RAIN) || WITH_RAIN   // compiled out with -D WITH_RAIN=0

// All the state of the rain client lives in one RainState, allocated while
// rain runs (rainInit, or the first rainService once rain is turned on) and
// freed by rainRemoveAll(): with rain off the client holds a pointer and
// nothing else. ~0.3 KB, of which the frame times of the set are 0.1 KB.

// The fetch is split into steps, one per rainService() call: between two
// steps the main loop animates the screen (a step takes ~2-25 s: the list,
// then one download + decode per frame). Without the split the screen froze
// for ~4.5 min at boot (the web server and the rendering share one task).
enum RainStep : uint8_t { STEP_IDLE, STEP_LIST, STEP_FRAME };

// The frames of the fetch in progress (kept between the JSON pass and the
// download loop): MAX_RAIN_FRAMES entries allocated when the list step
// starts and freed, path Strings included, when the fetch ends
// (fetchAbort) or is dropped (rainInit, rainRemoveAll).
struct RainPending {
  uint32_t time = 0;
  String   path;
  bool     nowcast = false;
  bool     have = false;  // its file is on disk for the current location/crop
};

// What a frame set was fetched for: the tile centre as sent in the URL
// (degrees x 100, RAIN_COORD_DECIMALS) and the crop. Frames of another key
// show another place or scale.
// The palette version too: a frame byte means a colour of one palette.
struct RainKey {
  int32_t la = 0, lo = 0;
  uint8_t k = 0;
  uint8_t pv = 0;
  bool operator==(const RainKey& o) const { return la == o.la && lo == o.lo && k == o.k && pv == o.pv; }
};

struct RainState {
  // Frame set currently on disk (and shown): oldest first, then nowcast frames.
  RainFrame frames[MAX_RAIN_FRAMES];
  uint8_t   count = 0;
  RainKey   key;                    // key of the set listed in meta.json / frames
  uint32_t  generated = 0;          // unix time the server generated the set
  uint32_t  lastOkMs = 0;
  bool      error = false;
  uint32_t  nextPollMs = 0;
  uint32_t  listOkMs = 0;           // last good frame list (or boot, for a set read from disk)

  // Last-poll diagnostics (see RainStage in the header).
  RainStage stage = RAIN_IDLE;
  int       lastHttp = 0;
  uint32_t  lastTryMs = 0;
  String    lastUrl;
  uint8_t   cropK = 240;
  uint32_t  fsFree = 0;             // free bytes at the last room check

  // Heap low-water mark of the current (or last) fetch, sampled in the
  // download loop, the decoder and between steps: the web server only
  // answers between steps, when the peak is already over. minWhere says
  // which part of the fetch hit the lowest largest-block: 'W' wind, 'L' list,
  // 'D' download, 'X' decode, 'S' between steps.
  uint32_t  decodeMs = 0;           // last tile decode, ms
  bool      smallMem = false;       // last tile decoded in the small-memory mode
  uint32_t  minHeap = 0, minBlk = 0;
  char      minWhere = 0;
  char      heapPhase = 'S';
  uint32_t  passStartMs = 0;        // start of the running decode pass (time budget)
  // The radar time of a tile that ran past the budget: later polls skip it
  // without a request (it would cost the same minute every poll and hold
  // back the newer frames). 0 = none.
  uint32_t  slowTime = 0;

  // The running fetch.
  RainStep     step = STEP_IDLE;
  uint8_t      frameIdx = 0;        // next frame of the running fetch
  RainPending* pending = nullptr;   // its list (see RainPending)
  uint8_t      pendingCount = 0;
  String       host;                // its tile host

  // The wind (rain.wind on): the last good answer, per level (clouds, ground;
  // RAIN_WIND_LEVELS). A failed request keeps it: it ages out instead
  // (rainWindNow).
  int16_t   windDir[RAIN_WIND_LEVELS] = {-1, -1};   // from, degrees; -1 = none
  uint16_t  windKmh10[RAIN_WIND_LEVELS] = {0, 0};   // speed, km/h x 10
  uint32_t  windOkMs = 0;           // millis() of the last good answer (0 = never)
  RainStage windStage = RAIN_IDLE;  // outcome of the last wind request
  int       windHttp = 0;
  bool      windDone = false;       // the running fetch made its wind request
};
static RainState* S = nullptr;

static bool pendingAlloc() {
  if (!S->pending) S->pending = new (std::nothrow) RainPending[MAX_RAIN_FRAMES];
  return S->pending != nullptr;
}
// The fetch's list goes: entries, path Strings and the tile host.
static void pendingFree() {
  if (!S) return;
  delete[] S->pending;
  S->pending = nullptr;
  S->pendingCount = 0;
  S->host = String();
}

uint32_t rainDecodeMs()  { return S ? S->decodeMs : 0; }
bool     rainSmallMem()  { return S && S->smallMem; }
static void heapReset() { S->minHeap = 0xFFFFFFFF; S->minBlk = 0xFFFFFFFF; S->minWhere = 0; }
void rainHeapSample() {
  if (!S) return;
  uint32_t h = ESP.getFreeHeap();
  uint32_t b = platformMaxFreeBlock();
  if (h < S->minHeap) S->minHeap = h;
  if (b < S->minBlk) { S->minBlk = b; S->minWhere = S->heapPhase; }
}
// The decoder's yield. yield() lets WiFi/lwIP run, and they allocate; at the
// decoder's heap peak (standard ESP8266 image: ~3.5 KB free) a failed
// allocation there is fatal (memcpy to NULL in ROM, seen once). Below
// RAIN_YIELD_MIN_HEAP the decoder only feeds the watchdog: the network
// waits a little (decodes ran up to 14 s without any yield before), the
// device does not crash. ESP32: plenty of heap, always yield.
void rainDecodeYield() {
#if defined(SMALLTV_FAMILY_ESP32)
  yield();
#else
  if (ESP.getFreeHeap() >= RAIN_YIELD_MIN_HEAP) yield();
  else ESP.wdtFeed();
#endif
}
// The decode's heap floor (RAIN_DECODE_MIN_HEAP): the decoder stops under it.
bool rainDecodeHeapLow() { return ESP.getFreeHeap() < RAIN_DECODE_MIN_HEAP; }
// The decode's time budget (RAIN_DECODE_MAX_MS), counted from the start of
// the pass: the decoder stops past it.
bool rainDecodeOverTime() { return S && millis() - S->passStartMs > RAIN_DECODE_MAX_MS; }

uint32_t rainMinHeap() { return (!S || S->minHeap == 0xFFFFFFFF) ? 0 : S->minHeap; }
uint32_t rainMinBlk()  { return (!S || S->minBlk == 0xFFFFFFFF) ? 0 : S->minBlk; }
char     rainMinWhere() { return S ? S->minWhere : 0; }

// The tile host comes from the JSON, over plain HTTP: accept only a RainViewer
// host (<label>.rainviewer.com, letters, digits, '-' and '.'), so a tampered
// list cannot send the device to another server, a port or a path.
static bool rainTileHostOk(const char* h) {
  static const char kDomain[] PROGMEM = "." RAIN_TILE_DOMAIN;
  size_t n = strlen(h), d = sizeof kDomain - 1;
  if (n <= d || n > 64) return false;
  for (size_t i = 0; i < n; i++) {
    char c = h[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!ok) return false;
    if (c == '.' && (i == 0 || h[i - 1] == '.')) return false;
  }
  return h[0] != '-' && strcmp_P(h + n - d, kDomain) == 0;
}

// The frame path comes from the same JSON and goes into the URL right after
// the host: accept only '/' then letters, digits, '/', '_' and '-', at most
// RAIN_PATH_MAX characters. Anything else could move the request elsewhere
// ("@evil.example/x" reads as credentials + host, ":8080" as a port) or
// carry a query or fragment. RainViewer's paths: /v2/radar/<id>.
static bool rainTilePathOk(const char* p) {
  size_t n = strlen(p);
  if (n < 2 || n > RAIN_PATH_MAX || p[0] != '/') return false;
  for (size_t i = 0; i < n; i++) {
    char c = p[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '/' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}
// Every frame path of the list, checked before anything of the list is kept:
// one bad path refuses the whole list, like a foreign host.
static bool rainListPathsOk(JsonArrayConst frames) {
  for (JsonObjectConst f : frames)
    if (!rainTilePathOk(f[F("path")] | "")) return false;
  return true;
}

// Download buffer of one tile: malloc'ed for the download only (not on the
// cont stack, which has no room for it, and not static, which would hold it
// for good).
#define RAIN_DL_BUF 512

// Same for the HTTP client (~150 B, too much for the cont stack): allocated
// for one request and deleted after it, its Strings with it, so nothing of
// it stays between fetches or during a tile's decode.
static HTTPClient* newHttp() { return new (std::nothrow) HTTPClient(); }

// The readers below answer for "nothing yet" while rain is not running (no
// state): /api/status asks them whatever the mode.
uint8_t   rainCount()                { return S ? S->count : 0; }
uint32_t  rainFrameTime(uint8_t i)   { return (S && i < S->count) ? S->frames[i].time : 0; }
bool      rainFrameNowcast(uint8_t i) { return S && i < S->count && S->frames[i].nowcast; }
uint32_t  rainLastOkMs()             { return S ? S->lastOkMs : 0; }
bool      rainError()                { return S && S->error; }
uint32_t  rainGenerated()            { return S ? S->generated : 0; }
bool      rainFetching()             { return S && S->step != STEP_IDLE; }

// Too old to show? Judged on the last frame list RainViewer sent, not on the
// frames downloaded so far: a first fetch brings the oldest frames first,
// and the list is what proves the service still answers. With the clock
// set, by the time the server generated that list; without it, by how long
// ago it arrived (or the boot, for a set read back from disk whose age is
// unknown then).
bool rainStale(uint32_t pollSec, uint32_t nowUnix) {
  if (!S || S->count == 0) return false;
  const uint32_t limit = pollSec + RAIN_STALE_SEC;
  if (nowUnix && S->generated) return nowUnix > S->generated && nowUnix - S->generated > limit;
  return millis() - S->listOkMs > limit * 1000UL;
}

RainStage rainStage()          { return S ? S->stage : RAIN_IDLE; }
int       rainLastHttp()       { return S ? S->lastHttp : 0; }
uint8_t   rainCropK()          { return S ? S->cropK : 0; }
uint32_t  rainLastTryMs()      { return S ? S->lastTryMs : 0; }
const char* rainLastUrl()      { return S ? S->lastUrl.c_str() : ""; }

// The stage names, in RainStage order, in one flash string (no table of
// pointers, no literal in RAM).
static const char kStageNames[] PROGMEM =
    "idle\0fetching\0no home set\0connect failed\0http error\0parse failed\0"
    "no frames in feed\0png decode failed\0out of memory\0filesystem error\0"
    "storage full\0low memory, tile skipped\0decode too slow, tile skipped\0ok";
static_assert(RAIN_OK == 13, "kStageNames follows RainStage");
static const char* stageName(RainStage st, char* buf, size_t n) {
  uint8_t k = (uint8_t)st <= RAIN_OK ? (uint8_t)st : 0;
  PGM_P p = kStageNames;
  while (k--) while (pgm_read_byte(p++)) {}
  strncpy_P(buf, p, n - 1);
  buf[n - 1] = 0;
  return buf;
}
const char* rainStageName(char* buf, size_t n) { return stageName(rainStage(), buf, n); }

static void timePath(char* out, size_t n, uint32_t time) {
  snprintf_P(out, n, PSTR(RAIN_DIR "/t%lu.raw"), (unsigned long)time);
}

bool rainFramePath(uint8_t i, char* out, size_t n) {
  if (!S || i >= S->count) return false;
  timePath(out, n, S->frames[i].time);
  return true;
}


static uint8_t cropFor(float lat, uint16_t rangeKm);
static RainKey keyFor(const Settings& s) {
  RainKey k;
  k.la = (int32_t)lroundf(rainLat(s) * 100.0f);
  k.lo = (int32_t)lroundf(rainLon(s) * 100.0f);
  k.k = cropFor(rainLat(s), s.rain.rangeKm);
  k.pv = RAIN_PALETTE_VERSION;
  return k;
}

// The URL scheme (a build macro), from flash.
static String scheme() { return String(RAIN_USE_HTTPS ? F("https://") : F("http://")); }

// Rebuild S->frames from /rain/meta.json so a reboot keeps showing the last
// set without waiting for the next poll. Frames of another location or crop
// are not shown, and a listed frame whose file is missing is skipped.
static void loadMeta(const Settings& s) {
  S->count = 0;
  S->generated = 0;
  S->key = RainKey();
  File f = LittleFS.open(RAIN_PATH("/meta.json"), "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  RainKey k;
  k.la = doc[F("la")] | (int32_t)0;
  k.lo = doc[F("lo")] | (int32_t)0;
  k.k = doc[F("k")] | (uint8_t)0;
  k.pv = doc[F("pv")] | (uint8_t)0;   // absent before the published palette: frames dropped
  if (!(k == keyFor(s))) return;   // also a meta.json from before the key
  S->key = k;
  S->generated = doc[F("generated")].as<uint32_t>();
  uint8_t nc = doc[F("nowcast")].as<uint8_t>();
  JsonArrayConst times = doc[F("times")].as<JsonArrayConst>();
  uint8_t pos = 0;
  for (JsonVariantConst t : times) {
    bool nowcast = (pos + nc >= (uint8_t)times.size());   // nowcast frames come last
    pos++;
    if (S->count >= MAX_RAIN_FRAMES) break;
    uint32_t time = t.as<uint32_t>();
    char path[24];
    timePath(path, sizeof path, time);
    if (!LittleFS.exists(path)) continue;
    S->frames[S->count].time = time;
    S->frames[S->count].nowcast = nowcast;
    S->count++;
  }
}

void rainInit(const Settings& s) {
  // The state, on first use (boot, or rain turned on). No heap for it: rain
  // stays idle, the next rainService() tries again.
  if (!S) S = new (std::nothrow) RainState();
  if (!S) return;
  // Abort of a running fetch (mode or settings change): the step machine
  // starts over.
  S->step = STEP_IDLE;
  S->frameIdx = 0;
  pendingFree();
  loadMeta(s);
  S->error = false;
  S->lastOkMs = 0;
  S->listOkMs = millis();
  S->nextPollMs = millis();   // poll as soon as the mode becomes active
  S->stage = RAIN_IDLE;
  S->lastHttp = 0;
  S->lastTryMs = 0;
  S->lastUrl = String();
  // The wind is the old location's too (rainInit runs on a change of home):
  // never shown over the new one, even if the next wind request fails.
  for (uint8_t l = 0; l < RAIN_WIND_LEVELS; l++) { S->windDir[l] = -1; S->windKmh10[l] = 0; }
  S->windOkMs = 0;
  S->windStage = RAIN_IDLE;
  S->windHttp = 0;
  S->windDone = false;
  // /rain is created by the first fetch (rainService), not here: rainInit
  // runs at boot for every image, rain on or off.
}

// A fetch already running? Its frames are fresh: do not arm a second poll
// that would start as soon as this one ends (a /api/refresh during a fetch
// used to trigger two full fetches back to back).
void rainForceRefresh() {
  if (!S || S->step != STEP_IDLE) return;   // no state: the first service polls at once
  S->nextPollMs = millis();
}

// Nothing to give back: the LZ window only exists inside fetchFrame(), for
// the decode of one tile, so no web request or page switch ever finds it
// allocated. Kept for the callers (big web requests, mode switches).
void rainFreeForRequest() {}

// Crop size for the configured range at this latitude: the 240px output
// must span about twice the range ring.
static uint8_t cropFor(float lat, uint16_t rangeKm) {
  float kmPerPx = 40075.0f * cosf(lat * (float)PI / 180.0f) / 32768.0f;  // z7, 256 px
  float k = 2.0f * (float)rangeKm / kmPerPx;
  int ki = (int)lroundf(k);
  if (ki < 8) ki = 8;
  if (ki > TFT_WIDTH) ki = TFT_WIDTH;
  return (uint8_t)ki;
}

// ---- one HTTP GET + parse of the frame list --------------------------------
static bool fetchFrameList(const Settings& s) {
  String url = scheme() + F(RAIN_API_HOST) + F(RAIN_API_PATH);
  S->lastUrl = url;

  WiFiClient client;
  std::unique_ptr<HTTPClient> httpOwner(newHttp());
  if (!httpOwner) { S->stage = RAIN_NO_MEM; return false; }
  HTTPClient& http = *httpOwner;
  // HTTP/1.0: the answer cannot be chunked, and the JSON is tiny.
  http.useHTTP10(true);
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(client, url)) { S->stage = RAIN_CONNECT_FAIL; return false; }
  http.addHeader(F("Accept"), F("application/json"));
  http.setUserAgent(F(RAIN_USER_AGENT));
  // No redirects: a 3xx could send the request to any host (the list
  // answers 200 directly; a move would show up as an HTTP error).
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  int code = http.GET();
  S->lastHttp = code;
  if (code != HTTP_CODE_OK) {
    http.end();
    S->stage = (code < 0) ? RAIN_CONNECT_FAIL : RAIN_HTTP_ERROR;
    return false;
  }

  // Filter so only the fields we need materialise: the doc is parsed while
  // the HTTP connection's buffers are allocated and, on the ESP8266, the
  // heap margin at that point is a few KB, so every unexpected field is a
  // crash risk. (The LZ window is not allocated here: it only exists
  // during a tile's decode, in fetchFrame().)
  JsonDocument filter;
  filter[F("host")] = true;
  filter[F("generated")] = true;
  {
    JsonObject f = filter[F("radar")][F("past")][0].to<JsonObject>();
    f[F("time")] = true;
    f[F("path")] = true;
  }
  {
    JsonObject f = filter[F("radar")][F("nowcast")][0].to<JsonObject>();
    f[F("time")] = true;
    f[F("path")] = true;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { S->stage = RAIN_PARSE_FAIL; return false; }

  const char* host = doc[F("host")] | "";
  if (strncmp_P(host, PSTR("https://"), 8) == 0) host += 8;
  else if (strncmp_P(host, PSTR("http://"), 7) == 0) host += 7;
  if (!rainTileHostOk(host)) { S->stage = RAIN_PARSE_FAIL; return false; }
  JsonArrayConst past = doc[F("radar")][F("past")].as<JsonArrayConst>();
  JsonArrayConst nowcast = doc[F("radar")][F("nowcast")].as<JsonArrayConst>();
  if (!rainListPathsOk(past) || !rainListPathsOk(nowcast)) { S->stage = RAIN_PARSE_FAIL; return false; }
  S->host = host;

  S->generated = doc[F("generated")].as<uint32_t>();
  S->pendingCount = 0;
  for (JsonObjectConst f : past) {
    if (S->pendingCount >= MAX_RAIN_FRAMES) break;
    S->pending[S->pendingCount].time = f[F("time")].as<uint32_t>();
    S->pending[S->pendingCount].path = f[F("path")].as<String>();
    S->pending[S->pendingCount].nowcast = false;
    S->pendingCount++;
  }
  // The nowcast array is empty since 2026-01-01; keep the code path so the
  // future frames appear automatically if RainViewer ever restores them.
  for (JsonObjectConst f : nowcast) {
    if (S->pendingCount >= MAX_RAIN_FRAMES) break;
    S->pending[S->pendingCount].time = f[F("time")].as<uint32_t>();
    S->pending[S->pendingCount].path = f[F("path")].as<String>();
    S->pending[S->pendingCount].nowcast = true;
    S->pendingCount++;
  }

  if (S->pendingCount == 0) { S->stage = RAIN_NO_FRAMES; return false; }
  S->listOkMs = millis();
  return true;
}

// ---- the wind: one GET to Open-Meteo ----------------------------------------
// The answer has no Content-Length (HTTP/1.0, the body runs to the close):
// the parser reads it through this cap, so a server that never stops costs
// RAIN_WIND_MAX_BYTES at most, then the JSON fails to parse.
class CappedStream : public Stream {
 public:
  CappedStream(Stream& in, size_t cap) : in_(in), left_(cap) {}
  int available() override { return left_ ? in_.available() : 0; }
  int read() override {
    if (!left_) return -1;
    int c = in_.read();
    if (c >= 0) left_--;
    return c;
  }
  int peek() override { return left_ ? in_.peek() : -1; }
  size_t write(uint8_t) override { return 0; }
 private:
  Stream& in_;
  size_t  left_;
};

// Both levels are asked for in one request (the setting picks the one shown,
// so changing it needs no new request), in km/h (miles are a display matter).
// A level missing from the answer or out of bounds is dropped on its own;
// with neither level usable the answer counts as a parse failure.
static bool fetchWind(const Settings& s) {
  String url = scheme() + F(RAIN_WIND_HOST) + F(RAIN_WIND_PATH);
  url += F("?latitude=");
  url += String(rainLat(s), RAIN_COORD_DECIMALS);   // rounded, as for the tiles
  url += F("&longitude=");
  url += String(rainLon(s), RAIN_COORD_DECIMALS);
  url += F("&current=wind_speed_850hPa,wind_direction_850hPa,wind_speed_10m,wind_direction_10m"
           "&wind_speed_unit=kmh");

  WiFiClient client;
  std::unique_ptr<HTTPClient> httpOwner(newHttp());
  if (!httpOwner) { S->windStage = RAIN_NO_MEM; return false; }
  HTTPClient& http = *httpOwner;
  http.useHTTP10(true);   // never chunked: the body as it is, up to the close
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(client, url)) { S->windStage = RAIN_CONNECT_FAIL; return false; }
  url = String();
  http.addHeader(F("Accept"), F("application/json"));
  http.setUserAgent(F(RAIN_USER_AGENT));
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);   // as for RainViewer

  int code = http.GET();
  S->windHttp = code;
  if (code != HTTP_CODE_OK) {
    http.end();
    S->windStage = (code < 0) ? RAIN_CONNECT_FAIL : RAIN_HTTP_ERROR;
    return false;
  }
  int len = http.getSize();
  if (len > RAIN_WIND_MAX_BYTES) { http.end(); S->windStage = RAIN_HTTP_ERROR; return false; }

  JsonDocument filter;
  {
    JsonObject f = filter[F("current")].to<JsonObject>();
    f[F("wind_speed_850hPa")] = true;
    f[F("wind_direction_850hPa")] = true;
    f[F("wind_speed_10m")] = true;
    f[F("wind_direction_10m")] = true;
  }
  JsonDocument doc;
  CappedStream body(http.getStream(), RAIN_WIND_MAX_BYTES);
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  http.end();
  if (err) { S->windStage = RAIN_PARSE_FAIL; return false; }

  JsonObjectConst cur = doc[F("current")].as<JsonObjectConst>();
  int16_t dir[RAIN_WIND_LEVELS];
  uint16_t kmh10[RAIN_WIND_LEVELS];
  bool any = false;
  for (uint8_t l = 0; l < RAIN_WIND_LEVELS; l++) {
    JsonVariantConst d = l ? cur[F("wind_direction_10m")] : cur[F("wind_direction_850hPa")];
    JsonVariantConst v = l ? cur[F("wind_speed_10m")] : cur[F("wind_speed_850hPa")];
    dir[l] = -1;
    kmh10[l] = 0;
    if (!d.is<float>() || !v.is<float>()) continue;   // missing or null
    const float df = d.as<float>(), vf = v.as<float>();
    if (!rainWindValueOk(df, vf)) continue;
    dir[l] = (int16_t)(lroundf(df) % 360);
    kmh10[l] = (uint16_t)lroundf(vf * 10.0f);
    any = true;
  }
  if (!any) { S->windStage = RAIN_PARSE_FAIL; return false; }
  for (uint8_t l = 0; l < RAIN_WIND_LEVELS; l++) { S->windDir[l] = dir[l]; S->windKmh10[l] = kmh10[l]; }
  S->windOkMs = millis();
  S->windStage = RAIN_OK;
  return true;
}

bool rainWindNow(const Settings& s, int16_t& dirFrom, float& kmh) {
  // Same rule as the arrow everywhere (screen and /api/status).
  if (!S || s.rain.wind == RAIN_WIND_OFF || s.rain.wind > RAIN_WIND_LEVELS || !S->windOkMs) return false;
  if (millis() - S->windOkMs > ((uint32_t)s.rain.pollSec + RAIN_STALE_SEC) * 1000UL) return false;
  const uint8_t l = s.rain.wind - 1;
  if (S->windDir[l] < 0) return false;
  if (S->windKmh10[l] < (uint16_t)lroundf(RAIN_WIND_MIN_KMH * 10.0f)) return false;   // no meaningful direction
  dirFrom = S->windDir[l];
  kmh = S->windKmh10[l] / 10.0f;
  return true;
}
uint32_t rainWindOkMs() { return S ? S->windOkMs : 0; }
int      rainWindHttp() { return S ? S->windHttp : 0; }
bool     rainWindTried() { return S && S->windStage != RAIN_IDLE; }
const char* rainWindStageName(char* buf, size_t n) { return stageName(S ? S->windStage : RAIN_IDLE, buf, n); }

// ---- small-memory decode: the pixel history on LittleFS ---------------------
// One file per segment (/rain/h<seg>), written whole and closed, so a read
// never meets a file still being written (LittleFS would copy the partly
// written block on every switch between reading and writing). The decoder
// keeps at most 4; whatever is left is removed when the decode ends, and
// purgeFrames() removes any a reset left behind.
static void histPath(char* out, size_t n, uint8_t seg) {
  snprintf_P(out, n, PSTR(RAIN_DIR "/h%u"), (unsigned)seg);
}

class FsHistory : public RainHistory {
 public:
  ~FsHistory() override {
    if (rd_) rd_.close();
    for (uint8_t k = 0; k < 32; k++)
      if (live_ & (1UL << k)) { char p[24]; histPath(p, sizeof p, k); LittleFS.remove(p); }
  }
  bool put(uint8_t seg, const uint8_t* px) override {
    if (seg >= 32) return false;
    char p[24];
    histPath(p, sizeof p, seg);
    File f = LittleFS.open(p, "w");
    if (!f) return false;
    live_ |= 1UL << seg;
    bool ok = f.write(px, RAIN_RING_PIXELS) == RAIN_RING_PIXELS;
    f.close();
    return ok;
  }
  bool get(uint8_t seg, uint16_t off, uint8_t* px, uint16_t n) override {
    if (seg >= 32 || !(live_ & (1UL << seg))) return false;
    if (!rd_ || rdSeg_ != seg) {
      if (rd_) rd_.close();
      char p[24];
      histPath(p, sizeof p, seg);
      rd_ = LittleFS.open(p, "r");
      rdSeg_ = seg;
      if (!rd_) return false;
    }
    return rd_.seek(off) && rd_.read(px, n) == (int)n;
  }
  void drop(uint8_t seg) override {
    if (seg >= 32) return;
    if (rd_ && rdSeg_ == seg) rd_.close();
    char p[24];
    histPath(p, sizeof p, seg);
    LittleFS.remove(p);
    live_ &= ~(1UL << seg);
  }

 private:
  uint32_t live_ = 0;     // segments on disk
  File     rd_;
  uint8_t  rdSeg_ = 0xFF;
};

// Free bytes on LittleFS. The ESP32 core reports them directly, the
// ESP8266 core only through info(FSInfo&) (same as ThemeWeb's freeBytes()).
// `block` gets the filesystem's block size (files take whole blocks).
static size_t fsFreeBytes(size_t* block = nullptr) {
#if defined(SMALLTV_ESP8266)
  FSInfo info;
  if (!LittleFS.info(info)) return 0;
  if (block) *block = info.blockSize ? info.blockSize : 8192;
  return info.totalBytes > info.usedBytes ? info.totalBytes - info.usedBytes : 0;
#else
  if (block) *block = 4096;   // the ESP32 cores' LittleFS
  size_t t = LittleFS.totalBytes(), u = LittleFS.usedBytes();
  return t > u ? t - u : 0;
#endif
}
uint32_t rainFsFree() { return S ? S->fsFree : 0; }

static bool publishSet(const RainKey& key);

// Room for the fetch of pending frame i, whose PNG is pngLen bytes: the PNG,
// the frame, up to 4 history segments (small-memory mode) and meta.tmp, in
// whole blocks, plus RAIN_FS_KEEP_FREE. Short of it, the oldest frames of
// the set (older than frame i) are dropped one by one: meta.json first, so
// it never lists a deleted file, then the file. False when even that is not
// enough (or meta.json cannot be written): nothing is downloaded.
static bool hasRoom(size_t pngLen) {
  size_t block = 8192;
  size_t free = fsFreeBytes(&block);
  S->fsFree = (uint32_t)free;
  auto blocks = [block](size_t n) { return (n + block - 1) / block * block; };
  size_t need = blocks(pngLen) + blocks(RAIN_FRAME_BYTES) + 4 * blocks(RAIN_RING_PIXELS) + block;
  return free >= need + RAIN_FS_KEEP_FREE;
}

// Pending frame i is older than a frame of the set: on a full filesystem it
// could only take the place of a newer one.
static bool olderThanSet(uint8_t i) {
  for (uint8_t j = 0; j < S->pendingCount; j++)
    if (S->pending[j].have && S->pending[j].time > S->pending[i].time) return true;
  return false;
}

static bool makeRoom(uint8_t i, size_t pngLen) {
  for (;;) {
    if (hasRoom(pngLen)) return true;
    uint8_t oldest = 0;
    while (oldest < S->pendingCount && !S->pending[oldest].have) oldest++;
    if (oldest >= S->pendingCount || S->pending[oldest].time >= S->pending[i].time) return false;
    S->pending[oldest].have = false;
    if (!publishSet(S->key)) return false;
    char path[24];
    timePath(path, sizeof path, S->pending[oldest].time);
    LittleFS.remove(path);
  }
}

// ---- one frame: download to /rain/tmp.png, decode to /rain/t<time>.raw ----
static bool fetchFrame(const Settings& s, const String& host, uint8_t i) {
  String url = scheme() + host;
  url += S->pending[i].path;
  url += '/';
  url += RAIN_TILE_SIZE;
  url += '/';
  url += RAIN_ZOOM;
  url += '/';
  // Rounded position only (privacy: plain HTTP), see RAIN_COORD_DECIMALS.
  url += String(rainLat(s), RAIN_COORD_DECIMALS);
  url += '/';
  url += String(rainLon(s), RAIN_COORD_DECIMALS);
  url += F("/2/1_1.png");
  S->lastUrl = url;
  S->pending[i].path = String();   // the path now lives in the URL: free its
                                  // buffer before the decode (tight heap)

  WiFiClient client;
  std::unique_ptr<HTTPClient> httpOwner(newHttp());
  if (!httpOwner) { S->stage = RAIN_NO_MEM; return false; }
  HTTPClient& http = *httpOwner;
  http.useHTTP10(true);
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(client, url)) { S->stage = RAIN_CONNECT_FAIL; return false; }
  http.setUserAgent(F(RAIN_USER_AGENT));
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);   // as for the list

  S->heapPhase = 'D';
  rainHeapSample();
  int code = http.GET();
  rainHeapSample();
  S->lastHttp = code;
  if (code != HTTP_CODE_OK) {
    http.end();
    S->stage = (code < 0) ? RAIN_CONNECT_FAIL : RAIN_HTTP_ERROR;
    return false;
  }

  // Room for this tile before a byte of it is written (RAIN_FS_KEEP_FREE),
  // sized by its Content-Length (real tiles: 3-12 KB), RAIN_PNG_SIZE_GUESS
  // without one. The download stops at that size: never more on disk than
  // the room checked for it.
  size_t pngLen;
  {
    int len = http.getSize();
    pngLen = (len > 0 && len <= RAIN_PNG_MAX) ? (size_t)len : (size_t)RAIN_PNG_SIZE_GUESS;
    if (len > RAIN_PNG_MAX) { http.end(); S->stage = RAIN_HTTP_ERROR; return false; }
    if (!makeRoom(i, pngLen)) { http.end(); S->stage = RAIN_FS_FULL; return false; }
  }

  // Stream to /rain/tmp.png (sequential writes; ~3-12 KB).
  File tmp = LittleFS.open(RAIN_PATH("/tmp.png"), "w");
  if (!tmp) { http.end(); S->stage = RAIN_FS_FAIL; return false; }
  uint8_t* dlBuf = (uint8_t*)malloc(RAIN_DL_BUF);
  if (!dlBuf) { http.end(); tmp.close(); S->stage = RAIN_NO_MEM; return false; }
  Stream& in = http.getStream();
  uint32_t total = 0;
  // readBytes: same signature on the ESP8266 and ESP32 cores (Stream::read
  // differs between them).
  int n;
  bool full = false, tooBig = false;
  while ((n = (int)in.readBytes(dlBuf, RAIN_DL_BUF)) > 0) {
    rainHeapSample();
    if (total + (uint32_t)n > pngLen) { tooBig = true; break; }   // more than announced / reserved
    if (tmp.write(dlBuf, (size_t)n) != (size_t)n) { full = true; break; }   // FS full
    total += (uint32_t)n;
  }
  free(dlBuf);
  http.end();
  httpOwner.reset();   // its Strings are not needed for the decode
  tmp.close();
  if (tooBig) { LittleFS.remove(RAIN_PATH("/tmp.png")); S->stage = RAIN_HTTP_ERROR; return false; }
  if (full) { LittleFS.remove(RAIN_PATH("/tmp.png")); S->stage = RAIN_FS_FULL; return false; }
  if (total == 0) { LittleFS.remove(RAIN_PATH("/tmp.png")); S->stage = RAIN_HTTP_ERROR; return false; }

  url = String();   // free the URL's buffer before the decode

  // Decode: centre crop + bilinear upscale + quantization into the frame.
  // Decoded into a temporary file: the frame in place is replaced (atomic
  // rename) only on success, so a failure here never destroys a good frame
  // (a .raw opened with "w" before the decode stayed empty after a failed
  // decode, and the animation showed a black frame).
  //
  // The LZ window is allocated here, once the download's TCP buffers are
  // gone, and freed right after the decode: the download is the heap peak
  // of the fetch (largest free block ~600 B measured on the ESP8266 lean
  // image with the window held across it), and the decoder needs nothing
  // during it. Only the missing frames are fetched (1-2 per poll), so the
  // malloc/free cycles stay few. The full 8 KB window first; if the heap
  // cannot give it (or the decoder's ~5 KB work area after it), the
  // small-memory mode: a 2 KB ring and the history on LittleFS. Same output.
  // A pass that would take free heap under RAIN_DECODE_MIN_HEAP stops
  // (the decoder checks it) and counts as no memory: the small-memory pass
  // follows; under the floor there too, the tile is skipped and the set kept.
  char path[24];
  timePath(path, sizeof path, S->pending[i].time);
  uint32_t t0 = millis();
  bool ok = false, noMem = true, lowHeap = false, slow = false, shortFrame = false;
  for (int small = 0; small < 2 && noMem; small++) {
    uint8_t* win = (uint8_t*)malloc(small ? RAIN_RING_PIXELS : RAIN_WINDOW_PIXELS);
    if (!win) continue;
    File src = LittleFS.open(RAIN_PATH("/tmp.png"), "r");
    File dst = LittleFS.open(RAIN_PATH("/tmp.raw"), "w");
    if (!src || !dst) {
      if (src) src.close();
      if (dst) dst.close();
      free(win);
      LittleFS.remove(RAIN_PATH("/tmp.png"));
      LittleFS.remove(RAIN_PATH("/tmp.raw"));
      S->stage = RAIN_FS_FAIL;
      return false;
    }
    S->heapPhase = 'X';
    S->passStartMs = millis();   // each pass has the whole budget
    if (small) {
      FsHistory hist;
      ok = rainDecodeToFs(src, dst, RAIN_TILE_SIZE, S->cropK, win, &hist);
    } else {
      ok = rainDecodeToFs(src, dst, RAIN_TILE_SIZE, S->cropK, win);
    }
    S->heapPhase = 'S';
    lowHeap = !ok && rainDecodeLowHeap();
    slow = !ok && rainDecodeTimedOut();   // not retried: the other pass is not faster
    noMem = !ok && (rainDecodeNoMem() || lowHeap);
    S->smallMem = small != 0;
    free(win);
    src.close();
    // A frame is only kept whole: the decoder checks each of its writes,
    // and the size is checked once more after the close (a write the file
    // system accepted but could not commit, LittleFS full).
    if (ok && dst.size() != RAIN_FRAME_BYTES) { ok = false; shortFrame = true; }
    if (!ok && rainDecodeWriteFail()) shortFrame = true;
    dst.close();
  }
  S->decodeMs = millis() - t0;
  LittleFS.remove(RAIN_PATH("/tmp.png"));   // the tile is not needed any more: give its blocks back
  if (!ok) {
    LittleFS.remove(RAIN_PATH("/tmp.raw"));   // never leave a truncated .raw behind
    if (slow) S->slowTime = S->pending[i].time;
    S->stage = slow ? RAIN_SLOW_TILE : lowHeap ? RAIN_LOW_HEAP : noMem ? RAIN_NO_MEM
            : shortFrame ? RAIN_FS_FULL : RAIN_PNG_FAIL;
    return false;
  }
  if (!LittleFS.rename(RAIN_PATH("/tmp.raw"), path)) {
    LittleFS.remove(RAIN_PATH("/tmp.raw"));
    S->stage = RAIN_FS_FAIL;
    return false;
  }
  return true;
}

// ---- publish the frames on disk: S->frames + meta.json ----------------------
// S->frames becomes the frames of the pending list whose files exist, in the
// list's order, and meta.json says the same (tmp + rename, so a power cut
// cannot leave a truncated one). S->frames follows the disk even when the
// write fails; loadMeta() skips listed frames whose file is gone, so an
// older meta.json can never show a frame under the wrong time.
static bool publishSet(const RainKey& key) {
  S->key = key;
  S->count = 0;
  for (uint8_t i = 0; i < S->pendingCount; i++) {
    if (!S->pending[i].have) continue;
    S->frames[S->count].time = S->pending[i].time;
    S->frames[S->count].nowcast = S->pending[i].nowcast;
    S->count++;
  }
  bool ok = false;
  File m = LittleFS.open(RAIN_PATH("/meta.tmp"), "w");
  if (m) {
    JsonDocument doc;
    doc[F("generated")] = S->generated;
    doc[F("la")] = key.la;
    doc[F("lo")] = key.lo;
    doc[F("k")] = key.k;
    doc[F("pv")] = key.pv;
    uint8_t nc = 0;
    JsonArray times = doc[F("times")].to<JsonArray>();
    for (uint8_t i = 0; i < S->count; i++) {
      times.add(S->frames[i].time);
      if (S->frames[i].nowcast) nc++;
    }
    doc[F("nowcast")] = nc;
    const size_t len = measureJson(doc);
    ok = len > 0 && serializeJson(doc, m) == len;   // short write = FS full: keep the old meta.json
    m.close();
    ok = ok && LittleFS.rename(RAIN_PATH("/meta.tmp"), RAIN_PATH("/meta.json"));
    if (!ok) LittleFS.remove(RAIN_PATH("/meta.tmp"));
  }
  return ok;
}

// Is /rain/<name> a frame file of the published set?
static bool inSet(const char* name) {
  for (uint8_t i = 0; i < S->count; i++) {
    char path[24];
    timePath(path, sizeof path, S->frames[i].time);
    if (strcmp(path + sizeof(RAIN_DIR), name) == 0) return true;   // skip "/rain/"
  }
  return false;
}

// Remove every .raw file outside the published set: frames that left the
// 2-hour window, frames of a previous location, the fNN.raw files of older
// firmware, a tmp.raw or tmp.png left by a power cut. A few names per pass, so the
// directory is never changed while it is read.
// A small-memory history segment ("h<digits>") a reset left behind.
static bool endsWithRaw(const char* n) {
  size_t l = strlen(n);
  return l >= 4 && strcmp_P(n + l - 4, PSTR(".raw")) == 0;
}
static bool isHistName(const char* n) {
  if (n[0] != 'h' || !n[1]) return false;
  for (const char* c = n + 1; *c; c++) if (*c < '0' || *c > '9') return false;
  return true;
}

// all = true: every file of /rain (rain turned off).
static bool purgeName(const char* n, bool all) {
  return all || (endsWithRaw(n) && !inSet(n)) || isHistName(n) || strcmp_P(n, PSTR("tmp.png")) == 0;
}

static void purgeFrames(bool all = false) {
  for (int pass = 0; pass < 16; pass++) {
    char names[4][24];
    unsigned found = 0;
#if defined(SMALLTV_ESP8266)
    Dir dir = LittleFS.openDir(RAIN_PATH(""));
    while (found < 4 && dir.next()) {
      String n = dir.fileName();
      if (purgeName(n.c_str(), all))
        snprintf_P(names[found++], sizeof names[0], PSTR("%s"), n.c_str());
    }
#else
    File dir = LittleFS.open(RAIN_PATH(""), "r");
    for (File f = dir.openNextFile(); found < 4 && f; f = dir.openNextFile()) {
      String n = f.name();
      n = n.substring(n.lastIndexOf('/') + 1);
      if (!f.isDirectory() && purgeName(n.c_str(), all))
        snprintf_P(names[found++], sizeof names[0], PSTR("%s"), n.c_str());
    }
    dir.close();
#endif
    bool removed = false;
    for (unsigned i = 0; i < found; i++) {
      char path[32];
      snprintf_P(path, sizeof path, PSTR(RAIN_DIR "/%s"), names[i]);
      removed |= LittleFS.remove(path);
    }
    if (found < 4 || !removed) break;
  }
}

// Rain turned off (mode or carousel box): the frames go, so they never hold
// ~800 KB of LittleFS for a feature nobody shows (a theme install or a
// settings save would find the disk full). The next fetch, once rain is
// back, starts from an empty set.
void rainRemoveAll() {
  // The state goes with the frames (the running fetch and its list too): with
  // rain off nothing of the client stays in RAM. rainInit() or the next
  // rainService() starts again from an empty set.
  pendingFree();
  delete S;
  S = nullptr;
  if (!LittleFS.exists(RAIN_PATH(""))) return;
  purgeFrames(true);
  LittleFS.rmdir(RAIN_PATH(""));
}

// Next pending frame still to download, from `from` on (S->pendingCount = none).
static uint8_t nextMissing(uint8_t from) {
  while (from < S->pendingCount && S->pending[from].have) from++;
  return from;
}

// The fetch reached the end of the list: the set on disk is complete.
static void fetchDone() {
  S->lastOkMs = millis();
  S->error = false;
  S->stage = RAIN_OK;
}

// End of the running fetch (success or failure): reset the step machine and
// arm the next poll from now. Without this, a pollSec shorter than a full
// fetch (~5-6 min on an ESP8266) chained fetches back to back: the heap under
// constant pressure (out-of-memory aborts) and the web server barely
// answering. The frames on disk stay in place: after a failure the animation
// keeps looping the previous set.
static void fetchAbort(const Settings& s) {
  S->step = STEP_IDLE;
  S->frameIdx = 0;
  pendingFree();   // the list and its Strings are not kept between fetches
  S->nextPollMs = millis() + (uint32_t)s.rain.pollSec * 1000UL;
}

// ---------------------------------------------------------------------------
void rainService(const Settings& s) {
  if (!S) rainInit(s);   // rain just turned on (or no heap for it before)
  if (!S) return;
  // No home set yet -> nothing to fetch (the mode shows a prompt instead).
  if (rainLat(s) == 0.0f && rainLon(s) == 0.0f) { S->stage = RAIN_NO_HOME; return; }

  if (S->step == STEP_IDLE) {
    if ((int32_t)(millis() - S->nextPollMs) < 0) return;
    // Provisional value, re-armed at the END of the fetch by fetchAbort():
    // the next poll starts pollSec after the fetch ends, not after it starts.
    // It only stays in place if rainInit() interrupts the fetch.
    S->nextPollMs = millis() + (uint32_t)s.rain.pollSec * 1000UL;
    S->lastTryMs = millis();
    S->cropK = cropFor(rainLat(s), s.rain.rangeKm);
    S->stage = RAIN_FETCHING;
    heapReset();
    rainHeapSample();
    S->windDone = false;
    S->step = STEP_LIST;
    return;   // the requests go out on the next calls: the screen animates first
  }

  // One step per call: the main loop animates the screen in between.
  S->heapPhase = 'S';
  rainHeapSample();
  // The wind, once per fetch, in a step of its own: first, before the list;
  // or, when the arrow is turned on during a fetch, between two steps of it
  // (the arrow then appears within this fetch, not the next one). A failure
  // only costs the arrow (the last good wind ages out), never the radar.
  if (s.rain.wind != RAIN_WIND_OFF && !S->windDone) {
    S->windDone = true;
    S->heapPhase = 'W';
    fetchWind(s);
    rainHeapSample();
    S->heapPhase = 'S';
    return;
  }
  if (S->step == STEP_LIST) {
    // /rain exists only while rain is in use (created here, deleted by
    // rainRemoveAll()).
    if (!LittleFS.exists(RAIN_PATH("")) && !LittleFS.mkdir(RAIN_PATH(""))) {
      S->error = true; S->stage = RAIN_FS_FAIL; fetchAbort(s); return;
    }
    if (!pendingAlloc()) { S->error = true; S->stage = RAIN_NO_MEM; fetchAbort(s); return; }
    S->heapPhase = 'L';
    bool listed = fetchFrameList(s);
    rainHeapSample();
    S->heapPhase = 'S';
    if (!listed) { S->error = true; fetchAbort(s); return; }   // keep the old frames
    // Keep the frames already on disk for this location and crop, drop the
    // rest: the set shrinks to the frames both lists share, each under its
    // own time, and grows again as the new frames land.
    const RainKey key = keyFor(s);
    const bool sameKey = (key == S->key);
    for (uint8_t i = 0; i < S->pendingCount; i++) {
      char path[24];
      timePath(path, sizeof path, S->pending[i].time);
      S->pending[i].have = sameKey && LittleFS.exists(path);
    }
    // Purge only once the new meta.json is down: if it cannot be written,
    // the old one still lists the old frames, so they must stay.
    if (!publishSet(key)) { S->error = true; S->stage = RAIN_FS_FAIL; fetchAbort(s); return; }
    purgeFrames();
    S->frameIdx = nextMissing(0);
    if (S->frameIdx >= S->pendingCount) { fetchDone(); fetchAbort(s); return; }   // nothing new
    S->step = STEP_FRAME;
    return;
  }
  if (S->step == STEP_FRAME) {
    // Short of room, the frames older than the set (dropped earlier to make
    // room for newer ones) are skipped without a request: fetching them
    // would only push the newer frames out again.
    if (olderThanSet(S->frameIdx) && !hasRoom(RAIN_PNG_SIZE_GUESS)) {
      while (S->frameIdx < S->pendingCount && olderThanSet(S->frameIdx)) S->frameIdx = nextMissing(S->frameIdx + 1);
      if (S->frameIdx >= S->pendingCount) { fetchDone(); fetchAbort(s); return; }
      return;   // the next missing frame on the next call
    }
    // The tile that ran past the decode's time budget in an earlier poll:
    // skipped without a request, the newer frames still come.
    if (S->slowTime && S->pending[S->frameIdx].time == S->slowTime) {
      S->frameIdx = nextMissing(S->frameIdx + 1);
      if (S->frameIdx >= S->pendingCount) { fetchDone(); fetchAbort(s); }
      return;
    }
    // Fail fast on the first bad frame: the stale set stays on disk and the
    // animation keeps looping it; the next poll (15 min) retries everything.
    if (!fetchFrame(s, S->host, S->frameIdx)) { S->error = true; fetchAbort(s); return; }
    S->pending[S->frameIdx].have = true;
    if (!publishSet(S->key)) { S->error = true; S->stage = RAIN_FS_FAIL; fetchAbort(s); return; }
    S->frameIdx = nextMissing(S->frameIdx + 1);
    if (S->frameIdx >= S->pendingCount) { fetchDone(); fetchAbort(s); }
    return;
  }
}

#endif  // WITH_RAIN
