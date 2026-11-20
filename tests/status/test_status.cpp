// GET /api/status is written straight to the client in pieces (StatusJson.h).
// Its bytes must stay exactly what 3.2.1 sent: the web UI, Home Assistant,
// n8n and the device tests all read it. The reference below is the way
// handleStatus() built the reply until 3.2.1 (one ArduinoJson document,
// serialized whole), fed the same values.
#include "StatusJson.h"
#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
using namespace smalltv;

// The "githubUpdate":false key belongs to the build option -DFW_NO_GITHUB_UPDATE.
// Only that build has the field and the code that writes it: every other
// image is byte for byte what it was (the ESP8266's flash and static RAM are
// tight, and an option that is off must cost nothing).
template <class T> static auto hasUpdateOff(int) -> decltype(std::declval<T&>().githubUpdateOff, std::true_type());
template <class T> static std::false_type hasUpdateOff(...);
#ifdef FW_NO_GITHUB_UPDATE
static_assert(decltype(hasUpdateOff<StatusData>(0))::value, "StatusData has no githubUpdateOff in the update-off build");
#else
static_assert(!decltype(hasUpdateOff<StatusData>(0))::value, "StatusData carries githubUpdateOff in a build with the update on");
#endif

static void snap(CrashSnapshot& s, const char* reason, const char* fw) {
  std::snprintf(s.reason, sizeof s.reason, "%s", reason);
  std::snprintf(s.fw, sizeof s.fw, "%s", fw);
}

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
#ifdef FW_NO_GITHUB_UPDATE
  if (d.githubUpdateOff) o["githubUpdate"] = false;
#endif
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
  if (d.sun[0]) o["sun"] = std::string(d.sun);
  o["bl"] = d.bl;
  if (d.ha[0]) o["ha"] = d.ha;
  if (d.themeError[0]) o["themeError"] = d.themeError;   // after "ha", left out when empty
  fillWg(o["wg"].to<JsonObject>(), d.wg);
  if (c.tickersCompiled) {
    JsonArray arr = o["tickers"].to<JsonArray>();
    for (const StatusTicker& d : c.tickers) {
      JsonObject t = arr.add<JsonObject>();
      t["symbol"] = d.symbol;
      t["valid"] = d.valid;
      t["error"] = d.error;
      if (d.error) t["retryIn"] = d.retryIn;
      if (d.error && d.lowHeap) t["reason"] = "not enough memory for HTTPS";
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
  full.d.sun = "06:58-16:42";
  full.d.bl = 255;
  full.d.ha = "memory";
  full.d.themeError = "not enough memory to run this theme";
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
  StatusTicker heap{}; heap.symbol = "XMR-USD"; heap.error = true; heap.lowHeap = true; heap.retryIn = 24;
  StatusTicker stale{}; stale.symbol = "OLD"; stale.valid = true; stale.price = 3.5f; stale.error = true; stale.lowHeap = true; stale.retryIn = 12;
  StatusTicker okHeap{}; okHeap.symbol = "FINE"; okHeap.valid = true; okHeap.price = 1.0f; okHeap.lowHeap = true;   // no error: no reason
  full.tickers = {aapl, btc, nochg, bad, both, wait, heap, stale, okHeap};
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

  // The sun text with a dimmed backlight (a middle level), and a sun text
  // that needs escaping; and no sun text with the backlight fully on.
  Case dim("sun-dim", true, true);
  dim.d.sun = "polar \"day\"";
  dim.d.bl = 37;
  all.push_back(dim);
  Case lit("no-sun-lit", false, false);
  lit.d.sun = "";
  lit.d.bl = 255;
  all.push_back(lit);

  // A build with the GitHub update off (-DFW_NO_GITHUB_UPDATE): the key is
  // there, false, right after the update message. In a build without the
  // flag the same data gives a reply with no key.
  Case off("update-off", true, true);
  off.d.version = "3.7.0-fork";
#ifdef FW_NO_GITHUB_UPDATE
  off.d.githubUpdateOff = true;
#endif
  all.push_back(off);
  Case offMsg("update-off-msg", true, true);
  offMsg.d.updateMsg = "no WiFi at boot";
#ifdef FW_NO_GITHUB_UPDATE
  offMsg.d.githubUpdateOff = true;
#endif
  all.push_back(offMsg);
  return all;
}

int main() {
  // The device's own reply, as saved on 1 November 2026: every byte.
  const char* saved =
      "{\"fw\":\"smalltv-mod\",\"version\":\"3.2.1\",\"variant\":\"esp8266\",\"repo\":\"https://github.com/giovi321/smalltv-mod\","
      "\"mode\":\"sta\",\"connected\":true,\"ssid\":\"home-wifi-01\",\"ip\":\"198.51.100.177\",\"rssi\":-67,\"heap\":6384,"
      "\"maxblk\":3080,\"contstk\":660,\"uptime\":290,\"reset\":\"Software/System restart\",\"synced\":true,"
      "\"time\":\"2026-11-01 11:00\",\"tz\":\"\",\"night\":false,\"nightHeld\":false,\"clockFresh\":true,\"bl\":0,"
      "\"wg\":{\"compiledIn\":false,\"enabled\":false,\"up\":false,\"held\":false,\"badConfig\":false},"
      "\"tickers\":[],\"radar\":{\"count\":0,\"error\":false,\"stage\":\"idle\",\"seenAc\":0}}";
  int checked = 0;
  for (const Case& c : cases()) {
    const std::string want = reference(c);
    if (std::string(c.name) == "idle") {
      if (want != saved) { std::fprintf(stderr, "reference no longer matches the saved reply:\n%s\n%s\n", want.c_str(), saved); return 1; }
    }
    // Where the sun fields sit is fixed: after clockFresh, before wg. themeError comes after ha.
    if (std::string(c.name) == "full" &&
        want.find("\"clockFresh\":false,\"sun\":\"06:58-16:42\",\"bl\":255,\"ha\":\"memory\",\"themeError\":\"not enough memory to run this theme\",\"wg\":{") == std::string::npos) {
      std::fprintf(stderr, "sun, bl, ha and themeError are not between clockFresh and wg:\n%s\n", want.c_str());
      return 1;
    }
    if (std::string(c.name) == "no-sun-lit" && (want.find("\"sun\"") != std::string::npos ||
                                                want.find("\"clockFresh\":true,\"bl\":255,\"wg\":{") == std::string::npos)) {
      std::fprintf(stderr, "an empty sun text must be left out; bl stays:\n%s\n", want.c_str());
      return 1;
    }
    // The key sits straight after the update message (or after repo), and
    // is never written when the update is on.
#ifdef FW_NO_GITHUB_UPDATE
    if (std::string(c.name) == "update-off" &&
        want.find(",\"githubUpdate\":false,\"mode\":") == std::string::npos) {
      std::fprintf(stderr, "githubUpdate is not just before mode:\n%s\n", want.c_str());
      return 1;
    }
    if (std::string(c.name) == "update-off-msg" &&
        want.find("\"updateMsg\":\"no WiFi at boot\",\"githubUpdate\":false,\"mode\":") == std::string::npos) {
      std::fprintf(stderr, "githubUpdate is not right after updateMsg:\n%s\n", want.c_str());
      return 1;
    }
    if (std::string(c.name) != "update-off" && std::string(c.name) != "update-off-msg" &&
        want.find("githubUpdate") != std::string::npos) {
      std::fprintf(stderr, "%s: githubUpdate present although the field is not set\n", c.name);
      return 1;
    }
#else
    // No flag: no reply has the key (the streamed bytes are compared with the
    // reference below).
    if (want.find("githubUpdate") != std::string::npos) {
      std::fprintf(stderr, "%s: githubUpdate in the reference of a build with the update on\n", c.name);
      return 1;
    }
#endif
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
  {  // lastCrash: after the 3.2.1 keys, registers left out when empty
    // The writer reads the platform's record as it is (CrashSnapshot, CrashRecord.h): no copy
    // into a second struct, and the register hex is written digit by digit.
    CrashSnapshot k = {};
    snap(k, "Exception", "3.2.1");
    k.hasRegisters = true; k.exccause = 0x1d; k.epc1 = 0x40201234; k.excvaddr = 0;
    k.uptimeKnown = true; k.uptimeS = 77;
    StringSink s1;
    { PieceWriter<5> out(s1); writeStatusCrash(out, k); out.flush(); }
    assert(s1.text == ",\"lastCrash\":{\"reason\":\"Exception\",\"fw\":\"3.2.1\",\"exccause\":\"0x1d\",\"epc\":\"0x40201234\",\"addr\":\"0x00000000\",\"uptime\":77}");
    // registers are written whatever the values: a small cause is not padded, the addresses are 8 digits
    CrashSnapshot r = {};
    snap(r, "Exception", "3.2.1");
    r.hasRegisters = true; r.exccause = 0; r.epc1 = 0xfedcba98; r.excvaddr = 0x00000a5f;
    StringSink s1b;
    { PieceWriter<128> out(s1b); writeStatusCrash(out, r); out.flush(); }
    assert(s1b.text == ",\"lastCrash\":{\"reason\":\"Exception\",\"fw\":\"3.2.1\",\"exccause\":\"0x0\",\"epc\":\"0xfedcba98\",\"addr\":\"0x00000a5f\"}");
    r.exccause = 0x1c0; r.excvaddr = 0x0;
    StringSink s1c;
    { PieceWriter<128> out(s1c); writeStatusCrash(out, r); out.flush(); }
    assert(s1c.text == ",\"lastCrash\":{\"reason\":\"Exception\",\"fw\":\"3.2.1\",\"exccause\":\"0x1c0\",\"epc\":\"0xfedcba98\",\"addr\":\"0x00000000\"}");
    // the digits agree with the printf the handler used before ("0x%x", "0x%08x") for every length
    const uint32_t samples[] = {0, 1, 9, 10, 15, 16, 255, 256, 0xfff, 0x1000, 0xffff, 0x10000, 0xfffff, 0x100000,
                                0xffffff, 0x1000000, 0xfffffff, 0x10000000, 0x7fffffff, 0x80000000u, 0xffffffffu, 0x40201abc};
    for (uint32_t v : samples) {
      r.exccause = v; r.epc1 = v; r.excvaddr = v;
      char c[12], p[12];
      std::snprintf(c, sizeof c, "0x%x", (unsigned)v);
      std::snprintf(p, sizeof p, "0x%08x", (unsigned)v);
      StringSink sv;
      { PieceWriter<128> out(sv); writeStatusCrash(out, r); out.flush(); }
      const std::string want = std::string(",\"lastCrash\":{\"reason\":\"Exception\",\"fw\":\"3.2.1\",\"exccause\":\"") + c +
                               "\",\"epc\":\"" + p + "\",\"addr\":\"" + p + "\"}";
      if (sv.text != want) { std::printf("FAIL register hex for 0x%x\n got  %s\n want %s\n", (unsigned)v, sv.text.c_str(), want.c_str()); return 1; }
    }
    {  // a minimum above 8 digits is cut at 8: the 12-byte buffer is never overrun
      StringSink sh;
      { PieceWriter<128> out(sh); statusout::hex(out, 0xabc, 20); out.flush(); }
      assert(sh.text == "\"0x00000abc\"");
    }
    CrashSnapshot a = {};
    snap(a, "Abort/out of memory", "3.2.1");
    a.exccause = 28; a.epc1 = 0x40201234;   // not known for an abort (hasRegisters false): left out
    StringSink s2;
    { PieceWriter<128> out(s2); writeStatusCrash(out, a); out.flush(); }
    assert(s2.text == ",\"lastCrash\":{\"reason\":\"Abort/out of memory\",\"fw\":\"3.2.1\"}");
    // with the measured detail: after the keys above, hex computed, unused trace slots left out
    CrashSnapshot d = {};
    snap(d, "Abort/out of memory", "3.2.1");
    d.uptimeKnown = true; d.uptimeS = 91;
    d.hasDetail = true; d.heap = 3456; d.hasBlock = true; d.maxBlock = 576;
    d.failSize = 756; d.failAddr = 0x40201abc;
    d.hasLows = true; d.lowHeap = 3000; d.lowBlk = 512;
    d.trace[0] = 0x40201000; d.trace[1] = 0x4010ffff; d.more = 2;
    StringSink s3;
    { PieceWriter<7> out(s3); writeStatusCrash(out, d); out.flush(); }
    assert(s3.text == ",\"lastCrash\":{\"reason\":\"Abort/out of memory\",\"fw\":\"3.2.1\",\"uptime\":91,\"heap\":3456,\"maxBlock\":576,"
                      "\"failSize\":756,\"failAddr\":\"0x40201abc\",\"lowHeap\":3000,\"lowBlk\":512,"
                      "\"trace\":[\"0x40201000\",\"0x4010ffff\"],\"more\":2}");
    // unknown values are left out, never written as 0: no block, no failed allocation, no lows, no trace
    CrashSnapshot e = {};
    snap(e, "Exception", "3.2.1");
    e.hasDetail = true; e.heap = 7000;
    StringSink s4;
    { PieceWriter<128> out(s4); writeStatusCrash(out, e); out.flush(); }
    assert(s4.text == ",\"lastCrash\":{\"reason\":\"Exception\",\"fw\":\"3.2.1\",\"heap\":7000,\"trace\":[]}");   // no "more" key when 0
    // A record taken from the hardware watchdog has no detail, but it still
    // knows how many crashes came before it: "more" is written outside the
    // detail block, whenever it is not 0 (review of #137, point 1).
    CrashSnapshot h = {};
    snap(h, "Hardware watchdog", "3.2.1");
    h.hasRegisters = true; h.exccause = 28; h.epc1 = 0x40201234;
    h.more = 2;
    StringSink s5;
    { PieceWriter<128> out(s5); writeStatusCrash(out, h); out.flush(); }
    assert(s5.text == ",\"lastCrash\":{\"reason\":\"Hardware watchdog\",\"fw\":\"3.2.1\",\"exccause\":\"0x1c\","
                      "\"epc\":\"0x40201234\",\"addr\":\"0x00000000\",\"more\":2}");
    h.more = 1;   // the smallest non-zero count is written too
    StringSink s6;
    { PieceWriter<5> out(s6); writeStatusCrash(out, h); out.flush(); }
    assert(s6.text.size() > 6 && s6.text.compare(s6.text.size() - 9, 9, "\"more\":1}") == 0);
    h.more = 0;   // none before: no key
    StringSink s7;
    { PieceWriter<128> out(s7); writeStatusCrash(out, h); out.flush(); }
    assert(s7.text.find("more") == std::string::npos);
    // and a record with detail and a count: still once, at the end
    d.more = 1;
    StringSink s8;
    { PieceWriter<128> out(s8); writeStatusCrash(out, d); out.flush(); }
    assert(s8.text.size() > 9 && s8.text.compare(s8.text.size() - 9, 9, "\"more\":1}") == 0 && s8.text.find("more") == s8.text.rfind("more"));
  }
  {  // fetch: nothing before the first theme data fetch has ended, then the
     // count of refusals and how the last one ended
    StringSink none;
    { PieceWriter<128> out(none); writeStatusFetch(out, 0, 0); out.flush(); }
    assert(none.text.empty());
    const char* const lastNames[] = {"ok", "refused", "failed"};
    for (uint8_t last = 1; last <= 3; ++last) {
      StringSink s;
      { PieceWriter<5> out(s); writeStatusFetch(out, 7, last); out.flush(); }
      assert(s.text == std::string(",\"fetchRefused\":7,\"fetchLast\":\"") + lastNames[last - 1] + "\"");
    }
    StringSink top;
    { PieceWriter<128> out(top); writeStatusFetch(out, 65535, 2); out.flush(); }
    assert(top.text == ",\"fetchRefused\":65535,\"fetchLast\":\"refused\"");
    // Where the keys sit in the reply: after lastCrash, before the tickers.
    std::ifstream in("src/WebPortal.cpp");
    assert(in.good());
    std::stringstream file;
    file << in.rdbuf();
    const std::string code = file.str();
    const size_t crash = code.find("smalltv::writeStatusCrash(out, cr);");
    const size_t fetch = code.find("statusFetch(out);");
    const size_t tickers = code.find("smalltv::writeStatusTickers(");
    assert(crash != std::string::npos && fetch != std::string::npos && tickers != std::string::npos && crash < fetch && fetch < tickers);
    // The call sits in a sibling of handleStatus (its frame must not grow), only where themes are built.
    assert(code.find("__attribute__((noinline)) static void statusFetch(") != std::string::npos);
    assert(code.find("smalltv::writeStatusFetch(out, stats.refused, stats.last);") != std::string::npos);
  }
  {  // TLS gate: nothing before the first check, then what it saw and the skips
    StringSink none;
    { PieceWriter<128> out(none); writeStatusTls(out, 0, 0, 0); out.flush(); }
    assert(none.text.empty());
    StringSink s;
    { PieceWriter<5> out(s); writeStatusTls(out, 14736, 12544, 16); out.flush(); }
    assert(s.text == ",\"tlsGate\":{\"free\":14736,\"block\":12544,\"skipped\":16}");
    StringSink top;
    { PieceWriter<128> out(top); writeStatusTls(out, 4294967295u, 4294967295u, 65535); out.flush(); }
    assert(top.text == ",\"tlsGate\":{\"free\":4294967295,\"block\":4294967295,\"skipped\":65535}");
    // After the theme fetch keys, before the tickers, in a sibling of handleStatus.
    std::ifstream in("src/WebPortal.cpp");
    assert(in.good());
    std::stringstream file;
    file << in.rdbuf();
    const std::string code = file.str();
    const size_t fetch = code.find("statusFetch(out);");
    const size_t tls = code.find("  statusTls(out);");
    const size_t tickers = code.find("smalltv::writeStatusTickers(");
    assert(fetch != std::string::npos && tls != std::string::npos && tickers != std::string::npos && fetch < tls && tls < tickers);
    assert(code.find("__attribute__((noinline)) static void statusTls(") != std::string::npos);
  }
  std::printf("status reply tests passed (%d replies, byte for byte)\n", checked);
  return 0;
}
