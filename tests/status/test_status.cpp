// GET /api/status is written straight to the client in pieces (StatusJson.h).
// Its bytes must stay exactly what 3.2.1 sent: the web UI, Home Assistant,
// n8n and the device tests all read it. The reference below is the way
// handleStatus() built the reply until 3.2.1 (one ArduinoJson document,
// serialized whole), fed the same values.
#include "StatusJson.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace smalltv;

// wgStatusJson() as 3.2.1 had it (WgClient.cpp), for a build with and
// without the tunnel.
static void fillWg(JsonObject o, const StatusWg& w) {
  o["compiledIn"] = w.compiledIn;
  o["enabled"] = w.enabled;
  o["up"] = w.up;
  o["held"] = w.held;
  o["badConfig"] = w.badConfig;
  if (!w.compiledIn) return;
  o["stage"] = w.stage;
  o["attempts"] = w.attempts;
  if (w.endpointIp[0]) o["endpointIp"] = w.endpointIp;
  if (w.lastHandshake) o["lastHandshake"] = w.lastHandshake;
}

static StatusData idleData();
static StatusRadar idleRadar();
struct Case {
  Case(const char* n, bool tickers, bool radarOn) : name(n), d(idleData()), tickersCompiled(tickers), radarCompiled(radarOn), radar(idleRadar()) {}
  const char* name;
  StatusData d;
  bool tickersCompiled;
  std::vector<StatusTicker> tickers;
  bool radarCompiled;
  StatusRadar radar;
};

static const char* basisOf(const StatusTicker& t) { return t.onRange ? "range" : "day"; }

// 3.2.1's handleStatus(), field by field.
static std::string reference(const Case& c) {
  const StatusData& d = c.d;
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["fw"] = d.fw;
  o["version"] = d.version;
  o["variant"] = d.variant;
  o["repo"] = d.repo;
  if (d.updateMsg[0]) o["updateMsg"] = std::string(d.updateMsg);
  o["mode"] = d.mode;
  o["connected"] = d.connected;
  o["ssid"] = std::string(d.ssid);
  o["ip"] = std::string(d.ip);
  o["rssi"] = d.rssi;
  if (d.wifiFail[0]) o["wifiFail"] = d.wifiFail;
  o["heap"] = d.heap;
  o["maxblk"] = d.maxblk;
  o["contstk"] = d.contstk;
  o["uptime"] = d.uptime;
  o["reset"] = d.reset;
  o["synced"] = d.synced;
  if (d.time[0]) o["time"] = std::string(d.time);
  o["tz"] = std::string(d.tz);
  o["night"] = d.night;
  o["nightHeld"] = d.nightHeld;
  o["clockFresh"] = d.clockFresh;
  fillWg(o["wg"].to<JsonObject>(), d.wg);
  if (c.tickersCompiled) {
    JsonArray arr = o["tickers"].to<JsonArray>();
    for (const StatusTicker& d : c.tickers) {
      JsonObject t = arr.add<JsonObject>();
      t["symbol"] = d.symbol;
      t["valid"] = d.valid;
      t["error"] = d.error;
      if (d.error) t["retryIn"] = d.retryIn;
      if (d.valid) {
        t["price"] = d.price;
        if (d.hasChange) {
          t["changePct"] = d.changePct;
          t["basis"] = basisOf(d);
        }
      }
    }
  }
  if (c.radarCompiled) {
    const StatusRadar& s = c.radar;
    JsonObject r = o["radar"].to<JsonObject>();
    r["count"] = s.count;
    r["error"] = s.error;
    r["stage"] = s.stage;
    r["seenAc"] = s.seenAc;
    if (s.http) r["http"] = s.http;
    if (s.tlsRx) r["tlsRx"] = s.tlsRx;
    if (s.hasTried) r["triedAgo"] = s.triedAgo;
    if (s.hasOk) r["okAgo"] = s.okAgo;
    if (s.url[0]) r["url"] = std::string(s.url);
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

struct StringSink : Sink {
  std::string text;
  size_t pieces = 0, longest = 0;
  bool write(const char* data, size_t count) override {
    text.append(data, count);
    pieces++;
    if (count > longest) longest = count;
    return true;
  }
};

template <size_t Piece> static std::string streamed(const Case& c, StringSink& sink) {
  PieceWriter<Piece> out(sink);
  writeStatusHead(out, c.d);
  if (c.tickersCompiled) writeStatusTickers(out, c.tickers.size(), [&](size_t i) { return c.tickers[i]; });
  if (c.radarCompiled) writeStatusRadar(out, c.radar);
  writeStatusEnd(out);
  return sink.text;
}

static StatusData idleData() {
  StatusData d;
  d.fw = "smalltv-mod";
  d.version = "3.2.1";
  d.variant = "esp8266";
  d.repo = "https://github.com/giovi321/smalltv-mod";
  d.mode = "sta";
  d.connected = true;
  d.ssid = "home-wifi-01";
  d.ip = "198.51.100.177";
  d.rssi = -67;
  d.heap = 6384;
  d.maxblk = 3080;
  d.contstk = 660;
  d.uptime = 290;
  d.reset = "Software/System restart";
  d.synced = true;
  d.time = "2026-11-01 11:00";
  d.tz = "";
  d.clockFresh = true;
  return d;
}

static StatusRadar idleRadar() {
  StatusRadar r;
  r.stage = "idle";
  return r;
}

static std::vector<Case> cases() {
  std::vector<Case> all;
  // Saved from the device (Pixel Room, nothing configured).
  Case idle("idle", true, true);
  all.push_back(idle);

  // Every optional field present, text that needs escaping, every ticker
  // kind, large and negative numbers.
  Case full("full", true, true);
  full.d.updateMsg = "update failed: HTTP \"404\"";
  full.d.wifiFail = "wrong password\n(try again)";
  full.d.ssid = "caf\xc3\xa9 \"WiFi\" \\ 5G";
  full.d.rssi = -91;
  full.d.heap = 4294967295u;
  full.d.maxblk = 0;
  full.d.uptime = 4000000000u;
  full.d.tz = "Europe/Rome";
  full.d.mode = "ap";
  full.d.connected = false;
  full.d.synced = false;
  full.d.time = "2026-11-01 11:00";   // every optional field is present in this one
  full.d.night = true;
  full.d.nightHeld = true;
  full.d.clockFresh = false;
  full.d.wg.compiledIn = true;
  full.d.wg.enabled = full.d.wg.up = true;
  full.d.wg.stage = 3;
  full.d.wg.attempts = 7;
  full.d.wg.endpointIp = "203.0.113.9";
  full.d.wg.lastHandshake = 1793532000;
  StatusTicker aapl{}; aapl.symbol = "AAPL"; aapl.valid = true; aapl.price = 189.37f; aapl.hasChange = true; aapl.changePct = -1.25f;
  StatusTicker btc{};  btc.symbol = "BTC-USD"; btc.valid = true; btc.price = 67123.5f; btc.hasChange = true; btc.changePct = 12.5f; btc.onRange = true;
  StatusTicker nochg{}; nochg.symbol = "^GSPC"; nochg.valid = true; nochg.price = 0.0001f;
  StatusTicker bad{};  bad.symbol = "NOPE"; bad.error = true; bad.retryIn = 45;
  StatusTicker both{}; both.symbol = "KEPT"; both.valid = true; both.error = true; both.retryIn = 0; both.price = 12.0f; both.hasChange = true; both.changePct = 0.0f;
  StatusTicker wait{}; wait.symbol = "SOON";
  full.tickers = {aapl, btc, nochg, bad, both, wait};
  full.radar.count = 12;
  full.radar.error = true;
  full.radar.stage = "all filtered out";
  full.radar.seenAc = 301;
  full.radar.http = -11;
  full.radar.tlsRx = 4096;
  full.radar.hasTried = full.radar.hasOk = true;
  full.radar.triedAgo = 3;
  full.radar.okAgo = 125;
  full.radar.url = "https://api.adsb.lol/v2/point/48.1/11.5/25?x=\"1\"";
  all.push_back(full);

  // A lean build: no ticker, no radar. The keys are not there at all.
  Case lean("lean", false, false);
  lean.d.wg.compiledIn = true;   // the tunnel's own fields, none of them optional ones
  all.push_back(lean);

  // Radar with a zero in every optional field that is left out at zero.
  Case zeros("zeros", true, true);
  zeros.d.time = "";   // and the clock not yet synced: no "time" key
  zeros.radar.hasTried = true;
  zeros.radar.triedAgo = 0;
  zeros.radar.hasOk = true;
  zeros.radar.okAgo = 0;
  all.push_back(zeros);
  return all;
}

int main() {
  // The device's own reply, as saved on 1 November 2026: every byte.
  const char* saved =
      "{\"fw\":\"smalltv-mod\",\"version\":\"3.2.1\",\"variant\":\"esp8266\",\"repo\":\"https://github.com/giovi321/smalltv-mod\","
      "\"mode\":\"sta\",\"connected\":true,\"ssid\":\"home-wifi-01\",\"ip\":\"198.51.100.177\",\"rssi\":-67,\"heap\":6384,"
      "\"maxblk\":3080,\"contstk\":660,\"uptime\":290,\"reset\":\"Software/System restart\",\"synced\":true,"
      "\"time\":\"2026-11-01 11:00\",\"tz\":\"\",\"night\":false,\"nightHeld\":false,\"clockFresh\":true,"
      "\"wg\":{\"compiledIn\":false,\"enabled\":false,\"up\":false,\"held\":false,\"badConfig\":false},"
      "\"tickers\":[],\"radar\":{\"count\":0,\"error\":false,\"stage\":\"idle\",\"seenAc\":0}}";
  int checked = 0;
  for (const Case& c : cases()) {
    const std::string want = reference(c);
    if (std::string(c.name) == "idle") {
      if (want != saved) { std::fprintf(stderr, "reference no longer matches the saved reply:\n%s\n%s\n", want.c_str(), saved); return 1; }
    }
    StringSink big, small;
    const std::string a = streamed<128>(c, big);
    const std::string b = streamed<5>(c, small);   // pieces that split every token
    if (a != want || b != want) {
      std::fprintf(stderr, "%s: bytes differ\n want %s\n got  %s\n 5 B  %s\n", c.name, want.c_str(), a.c_str(), b.c_str());
      return 1;
    }
    // Pieces: all but the last are full, none empty.
    assert(big.longest <= 128 && small.longest <= 5);
    assert(big.pieces == (want.size() + 127) / 128);
    assert(small.pieces == (want.size() + 4) / 5);
    checked++;
  }
  std::printf("status reply tests passed (%d replies, byte for byte)\n", checked);
  return 0;
}
