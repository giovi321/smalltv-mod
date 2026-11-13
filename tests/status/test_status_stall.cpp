// GET /api/status to a client that stops reading. The reply is written to the
// client by a ChunkedSink (JsonStream.h): it writes the chunk framing itself
// so it sees a short write, which sendContent() does not report. After the
// first refused piece nothing more is written, and the connection is given
// up once (abandon). A good client gets the same bytes the web server's own
// chunked sendContent() sends: "<hex size>\r\n<piece>\r\n" for each piece
// (the last chunk, "0\r\n\r\n", is still sent by the web server).
#include "StatusJson.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace smalltv;

// The client as the sink sees it: takes `room` bytes, then refuses the rest
// (a short write, as WiFiClient::write() gives after its send timeout).
struct FakeClient {
  size_t room;
  std::string received;
  size_t calls = 0, callsAfterShort = 0, abandoned = 0;
  bool wasShort = false;
  explicit FakeClient(size_t r) : room(r) {}
  size_t write(const uint8_t* data, size_t count) {
    calls++;
    if (wasShort) callsAfterShort++;
    const size_t n = count < room ? count : room;
    received.append(reinterpret_cast<const char*>(data), n);
    room -= n;
    if (n < count) wasShort = true;
    return n;
  }
  void abandon() { abandoned++; }
};

static StatusData sample() {
  StatusData d;
  d.fw = "smalltv-mod";
  d.version = "3.3.0";
  d.variant = "esp8266";
  d.repo = "https://example.com/smalltv-mod";
  d.mode = "sta";
  d.connected = true;
  d.ssid = "home-wifi-01";
  d.ip = "198.51.100.7";
  d.rssi = -61;
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

// The reply as the handler writes it (no ticker, no radar), to any Sink.
template <size_t Piece> static bool reply(Sink& sink) {
  PieceWriter<Piece> out(sink);
  writeStatusHead(out, sample());
  writeStatusEnd(out);
  return endReply(out, sink);
}

struct StringSink : Sink {
  std::string text;
  bool write(const char* data, size_t count) override {
    text.append(data, count);
    return true;
  }
};

// The wire bytes, worked out here from the plain text, not by the code
// under test: every piece of `piece` bytes behind its hex size line.
static std::string chunked(const std::string& body, size_t piece) {
  std::string wire;
  for (size_t at = 0; at < body.size(); at += piece) {
    const std::string part = body.substr(at, piece);
    char head[24];
    std::snprintf(head, sizeof head, "%zx\r\n", part.size());
    wire += head + part + "\r\n";
  }
  return wire;
}

template <size_t Piece> static void checkPiece() {
  StringSink plain;
  assert(reply<Piece>(plain));
  const std::string body = plain.text;
  const std::string wire = chunked(body, Piece);
  assert(body.size() > 300);   // more than one piece even at 300 bytes

  // A good client: the exact framing, nothing given up.
  FakeClient good(1000000);
  ChunkedSink<FakeClient> sink(good);
  assert(reply<Piece>(sink));
  if (good.received != wire) {
    std::fprintf(stderr, "piece %zu: wire bytes differ\n want %s\n got  %s\n", Piece, wire.c_str(), good.received.c_str());
    std::exit(1);
  }
  assert(good.abandoned == 0);
  assert(!good.wasShort);

  // A client that takes `room` bytes and refuses the rest, for every room
  // from nothing to one byte short of the whole reply: it may stall inside a
  // size line, inside a piece or before the CRLF. The first short write ends
  // all writing, and the connection is given up once.
  for (size_t room = 0; room < wire.size(); room++) {
    FakeClient stalled(room);
    ChunkedSink<FakeClient> to(stalled);
    if (reply<Piece>(to)) {
      std::fprintf(stderr, "piece %zu room %zu: a refused reply counted as sent\n", Piece, room);
      std::exit(1);
    }
    if (stalled.callsAfterShort != 0 || stalled.abandoned != 1 || stalled.received != wire.substr(0, room)) {
      std::fprintf(stderr, "piece %zu room %zu: %zu writes after the short one, abandoned %zu\n", Piece, room,
                   stalled.callsAfterShort, stalled.abandoned);
      std::exit(1);
    }
  }
  // Room for exactly the whole reply: not a refusal.
  FakeClient exact(wire.size());
  ChunkedSink<FakeClient> to(exact);
  assert(reply<Piece>(to));
  assert(exact.abandoned == 0 && !exact.wasShort);
}

// An empty write must send nothing: a zero-size chunk ("0\r\n\r\n") would end
// the chunked reply for the client.
static void checkEmptyWrite() {
  FakeClient client(1000000);
  ChunkedSink<FakeClient> sink(client);
  assert(sink.write("", 0));
  assert(client.calls == 0);
  assert(client.received.empty());
  assert(client.abandoned == 0);
  // And the sink still works afterwards.
  assert(sink.write("ab", 2));
  assert(client.received == "2\r\nab\r\n");
}

int main() {
  checkEmptyWrite();
  checkPiece<10>();    // size line "a": the digit ten
  checkPiece<160>();   // size line "a0": the digit ten behind another
  checkPiece<128>();
  checkPiece<17>();    // size lines "11"
  checkPiece<300>();   // size line "12c": a letter in the hex
  std::printf("status stall tests passed\n");
  return 0;
}
