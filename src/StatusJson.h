#pragma once
// GET /api/status, written straight to the client in small pieces.
//
// Until 3.2.1 the handler built an ArduinoJson document (about 1 KB of small
// blocks), serialized it into one String (another 0.5 KB) and handed that to
// the web server, which copies it once more for lwIP. Six requests arriving
// together, with the 5 connections the server holds waiting, left a 3.2.1
// device with a 648-byte largest block, and a diag build crashed (the Wi-Fi
// driver could not get 384 bytes). Here every value is written the moment it
// is known, through a PieceWriter, and only the piece buffer (128 bytes) and
// one tiny document for the single value being written are alive.
//
// The bytes are exactly the ones 3.2.1 sent: same keys, same order, every
// value serialized by ArduinoJson (escapes, number format). The caller
// supplies the values; this file knows nothing of the firmware, so a native
// test can compare it with the old way, byte for byte.
//
// Call order: writeStatusHead(), then writeStatusCrash() if there is a crash
// record, writeStatusFetch() if the build has themes, writeStatusTickers() if the build has
// the ticker, writeStatusRadar() if it has the radar, and writeStatusEnd().
#include "JsonStream.h"
#include "CrashRecord.h"   // CrashSnapshot: the record of the last crash, written as it is

namespace smalltv {
// The Wi-Fi tunnel (WgClient.cpp). Where it is not built only the first five
// are written, so the web UI can say "not in this build".
struct StatusWg {
  bool compiledIn = false, enabled = false, up = false, held = false, badConfig = false;
  uint8_t stage = 0;
  uint32_t attempts = 0;
  const char* endpointIp = "";   // left out when empty
  long lastHandshake = 0;        // left out when 0
};
// The values behind the first part of the reply. Strings are borrowed: they
// must live until writeStatusHead() returns.
struct StatusData {
  const char *fw = "", *version = "", *variant = "", *repo = "";
  const char* updateMsg = "";   // left out when empty
#ifdef FW_NO_GITHUB_UPDATE
  bool githubUpdateOff = false; // "githubUpdate":false; only a build with the update off has this (BuildInfo.h)
#endif
  const char *mode = "", *ssid = "", *ip = "";
  const char* wifiFail = "";    // left out when empty
  const char* reset = "";
  const char* time = "";        // left out when empty
  const char* tz = "";
  const char* sun = "";         // today's sunrise-sunset; left out when empty
  const char* ha = "";          // why Home Assistant is not as set: memory, safe or reboot; left out when empty
  const char* themeError = "";  // why a theme was refused (not enough memory); left out when empty
  bool connected = false, synced = false, night = false, nightHeld = false, clockFresh = false;
  int rssi = 0;
  int bl = 0;                   // backlight level now, 0..255
  uint32_t heap = 0, maxblk = 0, contstk = 0, uptime = 0;
  StatusWg wg;
};
struct StatusTicker {
  const char* symbol = "";
  bool valid = false, error = false;
  uint32_t retryIn = 0;         // written when `error`
  bool lowHeap = false;         // the error is a TLS heap skip: `reason` follows
  float price = 0;              // written when `valid`
  bool hasChange = false;       // changePct and basis follow price
  float changePct = 0;
  bool onRange = false;         // the change is over the range, not the day
};
struct StatusRadar {
  uint8_t count = 0;
  bool error = false;
  const char* stage = "";
  uint16_t seenAc = 0;
  int http = 0;                 // left out when 0
  uint16_t tlsRx = 0;           // left out when 0
  bool hasTried = false, hasOk = false;
  uint32_t triedAgo = 0, okAgo = 0;
  const char* url = "";         // left out when empty
};
struct StatusRain {
  uint8_t frames = 0;
  bool error = false;
  bool stale = false;           // the set is too old to show (screen says "unavailable")
  const char* stage = "";
  int http = 0;                 // left out when 0
  uint8_t cropK = 0;            // left out when 0
  uint32_t generated = 0;       // left out when 0
  bool hasTried = false, hasOk = false;
  uint32_t triedAgo = 0, okAgo = 0;
  const char* url = "";         // left out when empty
  uint32_t drawMs = 0, blitMs = 0, citiesMs = 0;   // render cost, left out when 0
  uint32_t decodeMs = 0;              // last tile decode, left out when 0
  bool smallMem = false;              // ... in the small-memory mode (written when decodeMs is)
  uint32_t minHeap = 0, minBlk = 0;   // heap low-water mark of the last fetch, left out when 0
  char minWhere = 0;                  // where minBlk was hit: W wind, L list, D download, X decode, S between steps
  uint32_t fsFree = 0;                // LittleFS free bytes at the last room check, left out when 0
  // The wind arrow's request, as "wind":{...}, left out until one was made.
  bool windTried = false;
  const char* windStage = "";
  int windHttp = 0;                   // left out when 0
  bool windHasOk = false;
  uint32_t windOkAgo = 0;
  int16_t windDir = -1;               // the arrow on screen: wind FROM, degrees; left out (with
  uint16_t windKmh = 0;               // its speed) when no arrow is shown
};

namespace statusout {
// `,"key":` (the key is a PSTR literal with its comma and quotes), then the value.
template <class Out> void member(Out& out, const char* key, const char* v) {
  jsonout::raw(out, key);
  jsonout::text(out, v);
}
template <class Out, class T> void member(Out& out, const char* key, T v) {
  jsonout::raw(out, key);
  jsonout::value(out, v);
}
// `"0x40201234"` as a JSON string, lower case, at least `minDigits` digits
// (8: `"0x%08x"`, 1: `"0x%x"`). The digits are computed, not looked up or
// formatted: no string constant takes RAM on the ESP8266, and no stack buffer
// is bigger than the one value.
template <class Out> void hex(Out& out, uint32_t v, unsigned minDigits = 8) {
  unsigned n = 1;   // the digits of v without leading zeros
  while (n < 8 && (v >> (4 * n))) n++;
  if (n < minDigits) n = minDigits < 8 ? minDigits : 8;
  char text[12];
  text[0] = '"';
  text[1] = '0';
  text[2] = 'x';
  for (unsigned i = 0; i < n; i++) {
    const unsigned d = (v >> (4 * (n - 1 - i))) & 15;
    text[3 + i] = char(d < 10 ? '0' + d : 'a' + d - 10);
  }
  text[3 + n] = '"';
  out.write(reinterpret_cast<const uint8_t*>(text), 4 + n);
}
}

// Everything up to and including "wg". No ArduinoJson document holds more
// than the one value being written: a document with an object in it takes a
// 1 KB block of heap in one piece, which the ESP8266 may not have.
template <class Out> void writeStatusHead(Out& out, const StatusData& d) {
  using statusout::member;
  member(out, PSTR("{\"fw\":"), d.fw);
  member(out, PSTR(",\"version\":"), d.version);
  member(out, PSTR(",\"variant\":"), d.variant);
  member(out, PSTR(",\"repo\":"), d.repo);
  if (d.updateMsg[0]) member(out, PSTR(",\"updateMsg\":"), d.updateMsg);
#ifdef FW_NO_GITHUB_UPDATE
  // Only a build with the GitHub update off carries the code (and the text)
  // for this key; every other image is unchanged by the build option.
  if (d.githubUpdateOff) member(out, PSTR(",\"githubUpdate\":"), false);
#endif
  member(out, PSTR(",\"mode\":"), d.mode);
  member(out, PSTR(",\"connected\":"), d.connected);
  member(out, PSTR(",\"ssid\":"), d.ssid);
  member(out, PSTR(",\"ip\":"), d.ip);
  member(out, PSTR(",\"rssi\":"), d.rssi);
  if (d.wifiFail[0]) member(out, PSTR(",\"wifiFail\":"), d.wifiFail);
  member(out, PSTR(",\"heap\":"), d.heap);
  member(out, PSTR(",\"maxblk\":"), d.maxblk);
  member(out, PSTR(",\"contstk\":"), d.contstk);
  member(out, PSTR(",\"uptime\":"), d.uptime);
  member(out, PSTR(",\"reset\":"), d.reset);
  member(out, PSTR(",\"synced\":"), d.synced);
  if (d.time[0]) member(out, PSTR(",\"time\":"), d.time);
  member(out, PSTR(",\"tz\":"), d.tz);
  member(out, PSTR(",\"night\":"), d.night);
  member(out, PSTR(",\"nightHeld\":"), d.nightHeld);
  member(out, PSTR(",\"clockFresh\":"), d.clockFresh);
  if (d.sun[0]) member(out, PSTR(",\"sun\":"), d.sun);
  member(out, PSTR(",\"bl\":"), d.bl);
  if (d.ha[0]) member(out, PSTR(",\"ha\":"), d.ha);
  if (d.themeError[0]) member(out, PSTR(",\"themeError\":"), d.themeError);
  const StatusWg& w = d.wg;
  member(out, PSTR(",\"wg\":{\"compiledIn\":"), w.compiledIn);
  member(out, PSTR(",\"enabled\":"), w.enabled);
  member(out, PSTR(",\"up\":"), w.up);
  member(out, PSTR(",\"held\":"), w.held);
  member(out, PSTR(",\"badConfig\":"), w.badConfig);
  if (w.compiledIn) {
    member(out, PSTR(",\"stage\":"), w.stage);
    member(out, PSTR(",\"attempts\":"), w.attempts);
    if (w.endpointIp[0]) member(out, PSTR(",\"endpointIp\":"), w.endpointIp);
    if (w.lastHandshake) member(out, PSTR(",\"lastHandshake\":"), w.lastHandshake);
  }
  jsonout::raw(out, PSTR("}"));
}

// `,"lastCrash":{...}`, added after the 3.2.1 keys: nothing that read the reply before reads it.
// The record is read where it is, no copy of it is made: the registers are
// written as hex digits here (the cause as `0x%x`, the addresses as `0x%08x`),
// and each detail value is left out when it was not measured, never made up as 0.
template <class Out> void writeStatusCrash(Out& out, const CrashSnapshot& k) {
  using statusout::member;
  const char* const reason = k.reason;   // not the array: member() takes a const char* here
  const char* const fw = k.fw;
  member(out, PSTR(",\"lastCrash\":{\"reason\":"), reason);
  member(out, PSTR(",\"fw\":"), fw);
  if (k.hasRegisters) {
    jsonout::raw(out, PSTR(",\"exccause\":"));
    statusout::hex(out, k.exccause, 1);
    jsonout::raw(out, PSTR(",\"epc\":"));
    statusout::hex(out, k.epc1);
    jsonout::raw(out, PSTR(",\"addr\":"));
    statusout::hex(out, k.excvaddr);
  }
  if (k.uptimeKnown) member(out, PSTR(",\"uptime\":"), k.uptimeS);
  if (k.hasDetail) {   // added after 3.5.0; the keys above are unchanged
    member(out, PSTR(",\"heap\":"), k.heap);
    if (k.hasBlock) member(out, PSTR(",\"maxBlock\":"), k.maxBlock);
    if (k.failSize) {
      member(out, PSTR(",\"failSize\":"), k.failSize);
      jsonout::raw(out, PSTR(",\"failAddr\":"));
      statusout::hex(out, k.failAddr);
    }
    if (k.hasLows) {
      member(out, PSTR(",\"lowHeap\":"), k.lowHeap);
      member(out, PSTR(",\"lowBlk\":"), k.lowBlk);
    }
    jsonout::raw(out, PSTR(",\"trace\":["));
    bool first = true;
    for (uint32_t t : k.trace) {
      if (!t) continue;
      if (!first) jsonout::raw(out, PSTR(","));
      first = false;
      statusout::hex(out, t);
    }
    jsonout::raw(out, PSTR("]"));
  }
  // Outside the detail block: a record made from the hardware watchdog has no
  // detail but still knows how many crashes came before it.
  if (k.more) member(out, PSTR(",\"more\":"), k.more);
  jsonout::raw(out, PSTR("}"));
}

// `,"fetchRefused":N,"fetchLast":"ok|refused|failed"`: how the theme data
// fetches ended since boot (ThemeData.h, FetchStats). `last` is 0 until the
// first fetch has ended, and then nothing is written; else 1 ok, 2 refused for
// memory, 3 failed. Added after the 3.2.1 keys.
template <class Out> void writeStatusFetch(Out& out, uint16_t refused, uint8_t last) {
  if (!last) return;
  statusout::member(out, PSTR(",\"fetchRefused\":"), refused);
  jsonout::raw(out, last == 1 ? PSTR(",\"fetchLast\":\"ok\"") : last == 2 ? PSTR(",\"fetchLast\":\"refused\"") : PSTR(",\"fetchLast\":\"failed\""));
}

// `,"tlsGate":{"free":N,"block":N,"skipped":N}`: what the TLS heap check
// (TlsGate.h) saw at the last handshake it was asked about, and how many it
// skipped since boot. Nothing before the first check (`free` 0). Added after
// the 3.9.0 keys.
template <class Out> void writeStatusTls(Out& out, uint32_t free, uint32_t block, uint16_t skipped) {
  if (!free) return;
  using statusout::member;
  member(out, PSTR(",\"tlsGate\":{\"free\":"), free);
  member(out, PSTR(",\"block\":"), block);
  member(out, PSTR(",\"skipped\":"), skipped);
  jsonout::raw(out, PSTR("}"));
}

// `,"tickers":[...]`, row i from rowAt(i).
template <class Out, class Row> void writeStatusTickers(Out& out, size_t count, Row&& rowAt) {
  using statusout::member;
  jsonout::raw(out, PSTR(",\"tickers\":["));
  for (size_t i = 0; i < count; i++) {
    const StatusTicker t = rowAt(i);
    member(out, i ? PSTR(",{\"symbol\":") : PSTR("{\"symbol\":"), t.symbol);
    member(out, PSTR(",\"valid\":"), t.valid);
    member(out, PSTR(",\"error\":"), t.error);
    if (t.error) member(out, PSTR(",\"retryIn\":"), t.retryIn);
    if (t.error && t.lowHeap) jsonout::raw(out, PSTR(",\"reason\":\"not enough memory for HTTPS\""));
    if (t.valid) {
      member(out, PSTR(",\"price\":"), t.price);
      if (t.hasChange) {
        member(out, PSTR(",\"changePct\":"), t.changePct);
        member(out, PSTR(",\"basis\":"), t.onRange ? "range" : "day");
      }
    }
    jsonout::raw(out, PSTR("}"));
  }
  jsonout::raw(out, PSTR("]"));
}

// `,"radar":{...}`.
template <class Out> void writeStatusRadar(Out& out, const StatusRadar& r) {
  using statusout::member;
  member(out, PSTR(",\"radar\":{\"count\":"), r.count);
  member(out, PSTR(",\"error\":"), r.error);
  member(out, PSTR(",\"stage\":"), r.stage);
  member(out, PSTR(",\"seenAc\":"), r.seenAc);
  if (r.http) member(out, PSTR(",\"http\":"), r.http);
  if (r.tlsRx) member(out, PSTR(",\"tlsRx\":"), r.tlsRx);
  if (r.hasTried) member(out, PSTR(",\"triedAgo\":"), r.triedAgo);
  if (r.hasOk) member(out, PSTR(",\"okAgo\":"), r.okAgo);
  if (r.url[0]) member(out, PSTR(",\"url\":"), r.url);
  jsonout::raw(out, PSTR("}"));
}

// `,"rain":{...}`.
template <class Out> void writeStatusRain(Out& out, const StatusRain& r) {
  using statusout::member;
  member(out, PSTR(",\"rain\":{\"frames\":"), r.frames);
  member(out, PSTR(",\"error\":"), r.error);
  member(out, PSTR(",\"stale\":"), r.stale);
  member(out, PSTR(",\"stage\":"), r.stage);
  if (r.http) member(out, PSTR(",\"http\":"), r.http);
  if (r.cropK) member(out, PSTR(",\"cropK\":"), r.cropK);
  if (r.generated) member(out, PSTR(",\"generated\":"), r.generated);
  if (r.hasTried) member(out, PSTR(",\"triedAgo\":"), r.triedAgo);
  if (r.hasOk) member(out, PSTR(",\"okAgo\":"), r.okAgo);
  if (r.url[0]) member(out, PSTR(",\"url\":"), r.url);
  if (r.drawMs)   member(out, PSTR(",\"drawMs\":"), r.drawMs);
  if (r.blitMs)   member(out, PSTR(",\"blitMs\":"), r.blitMs);
  if (r.citiesMs) member(out, PSTR(",\"citiesMs\":"), r.citiesMs);
  if (r.decodeMs) {
    member(out, PSTR(",\"decodeMs\":"), r.decodeMs);
    member(out, PSTR(",\"smallMem\":"), r.smallMem);
  }
  if (r.minHeap)  member(out, PSTR(",\"minHeap\":"), r.minHeap);
  if (r.minBlk)   member(out, PSTR(",\"minBlk\":"), r.minBlk);
  if (r.minWhere) { char w[2] = {r.minWhere, 0}; member(out, PSTR(",\"minWhere\":"), (const char*)w); }
  if (r.fsFree)   member(out, PSTR(",\"fsFree\":"), r.fsFree);
  if (r.windTried) {
    member(out, PSTR(",\"wind\":{\"stage\":"), r.windStage);
    if (r.windHttp) member(out, PSTR(",\"http\":"), r.windHttp);
    if (r.windHasOk) member(out, PSTR(",\"okAgo\":"), r.windOkAgo);
    if (r.windDir >= 0) {
      member(out, PSTR(",\"dir\":"), r.windDir);
      member(out, PSTR(",\"kmh\":"), r.windKmh);
    }
    jsonout::raw(out, PSTR("}"));
  }
  jsonout::raw(out, PSTR("}"));
}

// Closes the object and sends the last piece.
template <class Out> void writeStatusEnd(Out& out) {
  jsonout::raw(out, PSTR("}"));
  out.flush();
}
}
