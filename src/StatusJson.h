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
// Call order: writeStatusHead(), then writeStatusTickers() if the build has
// the ticker, writeStatusRadar() if it has the radar, and writeStatusEnd().
#include "JsonStream.h"

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
  const char *mode = "", *ssid = "", *ip = "";
  const char* wifiFail = "";    // left out when empty
  const char* reset = "";
  const char* time = "";        // left out when empty
  const char* tz = "";
  bool connected = false, synced = false, night = false, nightHeld = false, clockFresh = false;
  int rssi = 0;
  uint32_t heap = 0, maxblk = 0, contstk = 0, uptime = 0;
  StatusWg wg;
};
struct StatusTicker {
  const char* symbol = "";
  bool valid = false, error = false;
  uint32_t retryIn = 0;         // written when `error`
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

// Closes the object and sends the last piece.
template <class Out> void writeStatusEnd(Out& out) {
  jsonout::raw(out, PSTR("}"));
  out.flush();
}
}
