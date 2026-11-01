// Heap needed to produce GET /api/status, counted the way the ESP8266 heap
// (umm_malloc) charges it: whole 8-byte blocks plus a 4-byte header. Build
// with -m32 so pointers and strings have the device's sizes.
//
// Until 3.2.1 the reply was one ArduinoJson document, serialized into a
// String and sent: document, String and (inside the web server) a copy for
// lwIP existed at once. Six such replies waiting behind each other, with the
// 5 connections the server keeps waiting, took the heap of a 3.2.1 device
// down to a 648-byte largest block. Now the reply goes out in 128-byte
// pieces and nothing holds more than a piece.
#include "StatusJson.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <string>
using namespace smalltv;

static size_t live = 0, peak = 0, largest = 0, calls = 0;
static bool counting = false;
static size_t charge(void* p) { return p ? (malloc_usable_size(p) + 4 + 7) / 8 * 8 : 0; }
static void* track(void* p, size_t requested) {
  if (counting && p) {
    live += charge(p);
    calls++;
    if (live > peak) peak = live;
    if (requested > largest) largest = requested;
  }
  return p;
}
static void untrack(void* p) { if (counting && p) live -= charge(p); }
extern "C" {
void* __real_malloc(size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
void* __wrap_malloc(size_t n) { return track(__real_malloc(n), n); }
void __wrap_free(void* p) { untrack(p); __real_free(p); }
void* __wrap_realloc(void* p, size_t n) {
  untrack(p);
  return track(__real_realloc(p, n), n);
}
}
void* operator new(size_t n) {
  void* p = __wrap_malloc(n);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { __wrap_free(p); }
void operator delete(void* p, size_t) noexcept { __wrap_free(p); }

// The values of a busy device: 3 tickers, a radar with a long URL, the tunnel.
static const char* const Symbols[] = {"AAPL", "BTC-USD", "NOPE"};
static StatusData busyData() {
  StatusData d;
  d.fw = "smalltv-mod"; d.version = "3.2.1"; d.variant = "esp8266";
  d.repo = "https://github.com/giovi321/smalltv-mod";
  d.updateMsg = "up to date";
  d.mode = "sta"; d.connected = true;
  d.ssid = "home-wifi-01"; d.ip = "198.51.100.177"; d.rssi = -67;
  d.heap = 6384; d.maxblk = 3080; d.contstk = 660; d.uptime = 290;
  d.reset = "Software/System restart";
  d.synced = true; d.time = "2026-11-01 11:00"; d.tz = "Europe/Berlin";
  d.clockFresh = true;
  d.wg.compiledIn = d.wg.enabled = d.wg.up = true;
  d.wg.stage = 3; d.wg.attempts = 2; d.wg.endpointIp = "203.0.113.9"; d.wg.lastHandshake = 1793532000;
  return d;
}
static StatusTicker tickerAt(size_t i) {
  StatusTicker t{};
  t.symbol = Symbols[i];
  t.valid = i != 2;
  t.error = i == 2;
  t.retryIn = 45;
  t.price = 189.37f + i;
  t.hasChange = i == 0;
  t.changePct = -1.25f;
  return t;
}
static StatusRadar busyRadar() {
  StatusRadar r;
  r.count = 12; r.stage = "ok"; r.seenAc = 301; r.http = 200; r.tlsRx = 4096;
  r.hasTried = r.hasOk = true; r.triedAgo = 3; r.okAgo = 125;
  r.url = "https://api.adsb.lol/v2/point/48.1234/11.5678/25";
  return r;
}
static void fillWg(JsonObject o, const StatusWg& w) {
  o["compiledIn"] = w.compiledIn; o["enabled"] = w.enabled; o["up"] = w.up; o["held"] = w.held; o["badConfig"] = w.badConfig;
  o["stage"] = w.stage; o["attempts"] = w.attempts; o["endpointIp"] = w.endpointIp; o["lastHandshake"] = w.lastHandshake;
}

// 3.2.1's way: the document, then the whole reply in one String.
static size_t oldWay() {
  const StatusData d = busyData();
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["fw"] = d.fw; o["version"] = d.version; o["variant"] = d.variant; o["repo"] = d.repo;
  o["updateMsg"] = std::string(d.updateMsg);
  o["mode"] = d.mode; o["connected"] = d.connected;
  o["ssid"] = std::string(d.ssid); o["ip"] = std::string(d.ip); o["rssi"] = d.rssi;
  o["heap"] = d.heap; o["maxblk"] = d.maxblk; o["contstk"] = d.contstk; o["uptime"] = d.uptime;
  o["reset"] = d.reset; o["synced"] = d.synced; o["time"] = std::string(d.time);
  o["tz"] = std::string(d.tz); o["night"] = d.night; o["nightHeld"] = d.nightHeld; o["clockFresh"] = d.clockFresh;
  fillWg(o["wg"].to<JsonObject>(), d.wg);
  JsonArray arr = o["tickers"].to<JsonArray>();
  for (size_t i = 0; i < 3; i++) {
    const StatusTicker s = tickerAt(i);
    JsonObject t = arr.add<JsonObject>();
    t["symbol"] = s.symbol; t["valid"] = s.valid; t["error"] = s.error;
    if (s.error) t["retryIn"] = s.retryIn;
    if (s.valid) { t["price"] = s.price; if (s.hasChange) { t["changePct"] = s.changePct; t["basis"] = "day"; } }
  }
  const StatusRadar rr = busyRadar();
  JsonObject r = o["radar"].to<JsonObject>();
  r["count"] = rr.count; r["error"] = rr.error; r["stage"] = rr.stage; r["seenAc"] = rr.seenAc;
  r["http"] = rr.http; r["tlsRx"] = rr.tlsRx; r["triedAgo"] = rr.triedAgo; r["okAgo"] = rr.okAgo;
  r["url"] = std::string(rr.url);
  std::string out;
  serializeJson(doc, out);
  return out.size();
}

struct CountingSink : Sink {
  size_t bytes = 0, longest = 0;
  bool write(const char*, size_t count) override {
    bytes += count;
    if (count > longest) longest = count;
    return true;
  }
};

static size_t newWay(CountingSink& sink) {
  const StatusData d = busyData();
  const StatusRadar radar = busyRadar();
  PieceWriter<128> out(sink);
  writeStatusHead(out, d);
  writeStatusTickers(out, 3, tickerAt);
  writeStatusRadar(out, radar);
  writeStatusEnd(out);
  return sink.bytes;
}

struct Usage { size_t peak, largest, bytes, calls; };
template <class F> static Usage measure(F&& body) {
  live = peak = largest = calls = 0;
  counting = true;
  const size_t bytes = body();
  counting = false;
  return Usage{peak, largest, bytes, calls};
}

// The new way must stay this small. Budgets are judgements, not measured
// limits: 1 KiB is the biggest single request the fragmented device heap is
// known to grant (TRAPS T10); the peak budget leaves room for the 128-byte
// piece buffer and ArduinoJson's few small blocks, and would fail at once if
// the reply were ever collected whole again.
constexpr size_t PeakBudget = 512;
constexpr size_t LargestBudget = 256;

int main() {
  const Usage was = measure([] { return oldWay(); });
  CountingSink sink;
  const Usage now = measure([&] { return newWay(sink); });
  std::printf("3.2.1: reply %zu B, peak heap %zu B, largest request %zu B, %zu allocations\n", was.bytes, was.peak, was.largest, was.calls);
  std::printf("now:   reply %zu B, peak heap %zu B, largest request %zu B, %zu allocations, longest piece %zu B\n", now.bytes, now.peak, now.largest, now.calls, sink.longest);
  int failed = 0;
  // The instrument can see the bad case: the whole reply was held at once.
  if (was.peak < was.bytes + 512) { std::fprintf(stderr, "the counter does not see the old way's copies\n"); failed++; }
  // Same reply either way.
  if (now.bytes != was.bytes) { std::fprintf(stderr, "reply length differs: %zu vs %zu\n", now.bytes, was.bytes); failed++; }
  if (now.peak > PeakBudget) { std::fprintf(stderr, "peak heap %zu B is over the budget of %zu B\n", now.peak, PeakBudget); failed++; }
  if (now.largest > LargestBudget) { std::fprintf(stderr, "largest request %zu B is over %zu B\n", now.largest, LargestBudget); failed++; }
  if (now.peak * 4 > was.peak) { std::fprintf(stderr, "peak heap %zu B is not far below 3.2.1's %zu B\n", now.peak, was.peak); failed++; }
  if (sink.longest > 128) { std::fprintf(stderr, "a piece of %zu B\n", sink.longest); failed++; }
  if (failed) return 1;
  std::printf("status memory tests passed\n");
  return 0;
}
