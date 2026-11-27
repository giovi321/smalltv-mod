// Compile the real firmware client against deterministic network/heap stand-ins.
#include "features/radar/RadarClient.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
static_assert(sizeof(void*) == 4, "receiver capacity test requires 32-bit pointers");
unsigned long ticks = 0;
EspClass ESP;
Stream* HTTPClient::body = nullptr;
int HTTPClient::status = 200;
unsigned HTTPClient::requests = 0;
bool HTTPClient::chunked = false;
bool HTTPClient::http10 = true;
unsigned HTTPClient::headerDelay = 0;
const char* HTTPClient::transferEncoding = "chunked";
class Body : public Stream {
 public:
  explicit Body(std::string bytes, unsigned gap = 0) : bytes_(bytes), gap_(gap) {}
  int read() override {
    if (pos_ == bytes_.size()) return -1;
    if (gap_ && pos_ && pos_ % gap_ == 0 && !waited_) { waited_ = true; return -1; }
    waited_ = false;
    return static_cast<unsigned char>(bytes_[pos_++]);
  }
  int peek() override { return pos_ < bytes_.size() ? static_cast<unsigned char>(bytes_[pos_]) : -1; }
  int available() override { return int(bytes_.size() - pos_); }
 private:
  std::string bytes_; size_t pos_ = 0; unsigned gap_; bool waited_ = false;
};
static void poll(const Settings& s, const std::string& bytes, bool chunked = false, unsigned gap = 0) {
  ticks += 10;
  Body body(bytes, gap); HTTPClient::body = &body; HTTPClient::chunked = chunked;
  radarForceRefresh(); radarService(s);
}
static std::string chunks(const std::string& text, size_t step = 7) {
  std::string out;
  for (size_t pos = 0; pos < text.size(); pos += step) {
    size_t size = text.size() - pos; if (size > step) size = step;
    char header[24]; snprintf(header, sizeof(header), "%x\r\n", unsigned(size));
    out += header; out += text.substr(pos, size); out += "\r\n";
  }
  return out + "0\r\n\r\n";
}
static void failedPoll(const Settings& s, const std::string& text, const char* error = nullptr, bool chunked = false) {
  unsigned count = radarCount(); uint32_t last = radarLastOkMs();
  std::string cached(reinterpret_cast<const char*>(g_heap->ac), count * sizeof(Aircraft));
  poll(s, text, chunked);
  assert(radarError() && radarStage() == RADAR_PARSE_FAIL);
  if (error) assert(std::string(radarParseError()) == error);
  assert(radarCount() == count && radarLastOkMs() == last && radarSeenAc() == 0);
  assert(!memcmp(cached.data(), g_heap->ac, cached.size()));
}
static const std::string local = R"json({"lat":39.8,"lon":-105.1,"flight":"LOCAL   ","alt_baro":10000})json";
static const std::string near = R"json({"lat":39.7001,"lon":-105.1,"flight":"NEAREST ","alt_baro":10000})json";
static std::string feed(const char* key, const std::string& entries) { return std::string("{\"") + key + "\":[" + entries + "]}"; }
int main() {
  Settings s; radarInit(s);
  std::ifstream sample("tests/radar/fixtures/aircraft.json");
  assert(sample.good());
  std::string sampleBytes((std::istreambuf_iterator<char>(sample)), std::istreambuf_iterator<char>());
  poll(s, sampleBytes);
  assert(!radarError() && radarSeenAc() == 2 && radarCount() == 1);
  assert(std::string(aircraftAt(0).callsign) == "LOCAL01");
  poll(s, feed("aircraft", local));
  assert(!HTTPClient::http10 && radarCount() == 1 && radarSeenAc() == 1 && radarStage() == RADAR_OK);
  assert(std::string(aircraftAt(0).callsign) == "LOCAL");
  // Parser allocation must stay bounded as API/receiver lists grow past 24.
  for (const char* key : {"ac", "aircraft"}) {
    for (unsigned size : {100U, 500U, 5000U}) {
      std::string entries;
      for (unsigned i = 0; i < size; ++i) {
        char entry[200];
        snprintf(entry, sizeof(entry), "{\"lat\":40.0,\"lon\":-105.1,\"track\":120,\"gs\":250,\"flight\":\"TEST%04u\",\"hex\":\"%06x\",\"alt_baro\":10000}", i, i);
        if (i) entries += ',';
        entries += entry;
      }
      entries += ',' + near;
      poll(s, feed(key, entries));
      assert(!radarError() && radarSeenAc() == size + 1 && radarCount() == MAX_AIRCRAFT);
      assert(std::string(aircraftAt(0).callsign) == "NEAREST");
    }
  }
  // Header/trailer metadata exercises scalar-number lookahead and root framing.
  const std::string envelope = "{\"now\":123.5,\"ok\":true,\"none\":null,\"note\":\"text \\\"ac\\\":[ decoy\",\"nested\":{\"ac\":[]},\"aircraft\":[" + local + "],\"last\":42}";
  poll(s, envelope);
  assert(!radarError() && radarSeenAc() == 1 && radarCount() == 1);
  poll(s, "{\"aircraft\":[" + near + "],\"ac\":[" + local + "]}");
  assert(std::string(aircraftAt(0).callsign) == "LOCAL");
  poll(s, "{\"ac\":[" + local + "],\"aircraft\":[" + near + "]}");
  assert(std::string(aircraftAt(0).callsign) == "LOCAL");
  poll(s, "{\"ac\":null,\"aircraft\":[" + local + "]}");
  assert(!radarError() && radarSeenAc() == 1);
  poll(s, "{\"\\u0061c\":[" + local + "]}");
  assert(!radarError() && radarSeenAc() == 1);
  poll(s, "{\"ac\\u0000evil\":[" + near + "],\"aircraft\":[" + local + "]}");
  assert(!radarError() && std::string(aircraftAt(0).callsign) == "LOCAL");
  // Large ignored values do not consume the bounded per-object document.
  poll(s, "{\"ignored\":\"" + std::string(100000, 'X') + "\",\"aircraft\":[" + local + "]}");
  assert(!radarError() && radarCount() == 1);
  poll(s, "{\"aircraft\":[{\"ignored\":\"" + std::string(100000, 'X') + "\",\"lat\":39.8,\"lon\":-105.1}]}");
  assert(!radarError() && radarCount() == 1);
  poll(s, feed("aircraft", local));
  failedPoll(s, "{\"aircraft\":[{\"flight\":\"" + std::string(100000, 'X') + "\",\"lat\":39.8,\"lon\":-105.1}]}", "NoMemory");
  failedPoll(s, "{\"" + std::string(100000, 'X') + "\":0}", "NoMemory");
  // Every incomplete prefix must leave the entire previous display unchanged.
  for (size_t size = 0; size < envelope.size(); ++size) failedPoll(s, envelope.substr(0, size));
  for (const std::string& malformed : std::initializer_list<std::string>{
      "{\"ac\":[" + local + ",]}", "{\"ac\":[," + local + "]}",
      "{\"ac\":[" + local + ",," + local + "]}", "{\"ac\":[1]}",
      "{\"ac\":[null]}", "{\"ac\":[\"object\"]}",
      "{\"ac\":[" + local + "],}", "{\"ac\":[" + local + "] \"missing\":1}",
      "{\"ac\":[" + local + "],\"bad\":]}", "{\"ac\":[" + local + "],\"ac\":[]}"}) {
    failedPoll(s, malformed, "InvalidInput");
  }
  std::string nested(12, '['); nested += '0'; nested += std::string(12, ']');
  failedPoll(s, "{\"ac\":[" + local + "],\"nested\":" + nested + "}", "TooDeep");
  poll(s, "{\"nested\":{\"ac\":[" + near + "]},\"note\":\"ac\"}");
  assert(radarError() && radarStage() == RADAR_NO_AC && radarCount() == 1);
  // Total heap reserve and contiguous allocation fit are checked independently.
  ESP.heap = 4096; failedPoll(s, feed("aircraft", local), "NoMemory"); ESP.heap = 100000;
  ESP.maxBlock = 1; failedPoll(s, feed("aircraft", local), "NoMemory");
  ESP.maxBlock = 4500; poll(s, feed("aircraft", local)); assert(!radarError()); ESP.maxBlock = 100000;
  HTTPClient::status = 503; poll(s, "");
  assert(radarStage() == RADAR_HTTP_ERROR && radarLastHttp() == 503 && !radarParseError()[0]);
  HTTPClient::status = -1; s.radar.source = RADAR_SRC_ADSBFI; poll(s, "");
  assert(radarStage() == RADAR_CONNECT_FAIL && !radarParseError()[0]);
  s.radar.source = RADAR_SRC_WEBHOOK; HTTPClient::status = 200;
  // The envelope parser must work through real chunk decoding and packet gaps.
  for (unsigned source : {0U, 1U, 2U}) {
    s.radar.source = source; poll(s, chunks(envelope, 1), true, 3);
    assert(!HTTPClient::http10 && !radarError() && radarSeenAc() == 1);
  }
  // HTTPClient reads through its clone of the client. An HTTPS feed must still
  // read through the TLS client, not a plain WiFiClient copy of the wrapper.
  unsigned tls = secureReads();
  s.radar.source = RADAR_SRC_ADSBFI; poll(s, feed("ac", local));
  assert(!radarError() && secureReads() > tls);
  s.radar.source = RADAR_SRC_WEBHOOK;
  failedPoll(s, "40\r\n{\"ac\":[{\"lat\":39.8", "IncompleteInput", true);
  // Preserve the prior complete-JSON contract; terminal chunk framing is not drained.
  poll(s, "9\r\n{\"ac\":[]}", true);
  assert(!radarError() && radarStage() == RADAR_NO_AC && radarCount() == 0);
  // Both SDK HTTP clients reject an unsupported coding before exposing a body.
  HTTPClient::status = HTTPC_ERROR_ENCODING; poll(s, "");
  assert(radarStage() == RADAR_ENCODING_ERROR && radarLastHttp() == HTTPC_ERROR_ENCODING);
  assert(std::string(radarStageName()) == "encoding error");
  HTTPClient::status = 200;
  // Header/redirect time consumes the same budget, rather than granting the
  // parser a fresh timeout. Cached results survive a timed-out poll.
  poll(s, feed("ac", local));
  unsigned cachedCount = radarCount(); uint32_t cachedOk = radarLastOkMs();
  HTTPClient::headerDelay = s.httpTimeout;
  poll(s, feed("ac", near));
  assert(radarError() && radarStage() == RADAR_TIMEOUT);
  assert(radarCount() == cachedCount && radarLastOkMs() == cachedOk);
  HTTPClient::headerDelay = s.httpTimeout - 1;
  poll(s, chunks(envelope, 1), true, 1);
  assert(radarError() && radarStage() == RADAR_TIMEOUT);
  assert(radarCount() == cachedCount && radarLastOkMs() == cachedOk);
  HTTPClient::headerDelay = 0;
  s.radar.minAltFt = 20000; poll(s, feed("ac", local));
  assert(!radarError() && radarStage() == RADAR_FILTERED_ALL && radarSeenAc() == 1 && radarCount() == 0);
  s.radar.minAltFt = 0; poll(s, feed("ac", ""));
  assert(!radarError() && radarStage() == RADAR_NO_AC && radarSeenAc() == 0);
  ESP.maxBlock = 4096; s.radar.source = RADAR_SRC_ADSBFI; poll(s, "");
  assert(radarStage() == RADAR_LOW_HEAP && !radarLastHttp() && !radarParseError()[0]);
  // Preserve upstream #141: disabling radar releases cached aircraft/session;
  // enabling it again (including the carousel) allocates only when wanted.
  assert(g_heap);
  s.mode = MODE_STOCKS; radarInit(s);
  assert(!g_heap && !radarCount());
  s.mode = MODE_CAROUSEL; s.carouselRadar = false; radarInit(s);
  assert(!g_heap);
  s.carouselRadar = true; radarInit(s);
  assert(g_heap);
  ESP.maxBlock = 100000; s.radar.source = RADAR_SRC_WEBHOOK;
  poll(s, feed("aircraft", local));
  assert(!radarError() && radarCount() == 1);
  s.mode = MODE_STOCKS; radarInit(s);
  assert(!g_heap && !radarCount());
  // Preserve upstream #148: no direct GET without MFLN, bounded re-probes,
  // and a working local receiver even while the direct host is blocked.
  probeSupportsMfln() = false;
  s.mode = MODE_RADAR; s.radar.source = RADAR_SRC_ADSBLOL; radarInit(s);
  unsigned probes = probeCalls(), requests = HTTPClient::requests;
  poll(s, feed("ac", local));
  assert(radarError() && radarStage() == RADAR_NO_MFLN && radarLastHttp() == 0);
  assert(std::string(radarStageName()) == "server needs 16 KB TLS records");
  assert(probeCalls() == probes + 2 && HTTPClient::requests == requests);
  probes = probeCalls();
  poll(s, feed("ac", local));
  assert(radarStage() == RADAR_NO_MFLN && probeCalls() == probes && HTTPClient::requests == requests);
  ticks += RADAR_REPROBE_MS;
  poll(s, feed("ac", local));
  assert(probeCalls() == probes + 2 && HTTPClient::requests == requests);
  probes = probeCalls();
  radarInit(s); poll(s, feed("ac", local));
  assert(probeCalls() == probes + 2 && HTTPClient::requests == requests);
  probes = probeCalls();
  s.radar.source = RADAR_SRC_ADSBFI; poll(s, feed("ac", local));
  assert(probeCalls() == probes + 2 && radarStage() == RADAR_NO_MFLN);
  probes = probeCalls();
  s.radar.source = RADAR_SRC_WEBHOOK; poll(s, feed("aircraft", local));
  assert(!radarError() && radarCount() == 1 && probeCalls() == probes);
  assert(HTTPClient::requests == requests + 1);
  probeSupportsMfln() = true;
  s.radar.source = RADAR_SRC_ADSBLOL; radarInit(s); poll(s, feed("ac", local));
  assert(!radarError() && radarCount() == 1 && probeCalls() == probes + 1);
  s.mode = MODE_STOCKS; radarInit(s);
  puts("radar streaming client: 100/500/5000-aircraft, nearest-last, metadata, framing, truncation, cache and heap tests passed");
}
