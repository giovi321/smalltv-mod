// Compile the real firmware client with deterministic network/heap stand-ins.
#include "features/radar/RadarClient.cpp"
#include <cassert>
#include <cstdio>
#include <string>
static_assert(sizeof(void*) == 4, "receiver capacity test requires 32-bit pointers");
unsigned long ticks = 0;
EspClass ESP;
int SecureClient::tlsError = 0;
Stream* HTTPClient::body = nullptr;
int HTTPClient::status = 200;
bool HTTPClient::chunked = false;
bool HTTPClient::http10 = true;
class Body : public Stream {
 public:
  explicit Body(std::string bytes) : bytes_(bytes) {}
  int read() override { return pos_ < bytes_.size() ? (unsigned char)bytes_[pos_++] : -1; }
  int peek() override { return pos_ < bytes_.size() ? (unsigned char)bytes_[pos_] : -1; }
  int available() override { return int(bytes_.size() - pos_); }
 private:
  std::string bytes_; size_t pos_ = 0;
};
static void poll(const Settings& s, std::string bytes) {
  ticks += 10;
  Body body(bytes); HTTPClient::body = &body;
  radarForceRefresh(); radarService(s);
}
int main() {
  Settings s;
  radarInit(s);
  const std::string aircraft = R"json({"lat":39.8,"lon":-105.1,"flight":"LOCAL   ","alt_baro":10000})json";
  poll(s, "{\"aircraft\":[" + aircraft + "]}");
  assert(!HTTPClient::http10);
  assert(radarCount() == 1 && radarSeenAc() == 1 && radarStage() == RADAR_OK);
  assert(std::string(aircraftAt(0).callsign) == "LOCAL");
  const Aircraft cached = aircraftAt(0);
  uint32_t lastOk = radarLastOkMs();
  // A modest receiver fixture fills every display slot within the budget.
  std::string nominal = "{\"aircraft\":[";
  for (int i = 0; i < MAX_AIRCRAFT; ++i) { if (i) nominal += ','; nominal += aircraft; }
  nominal += "]}";
  poll(s, nominal);
  assert(radarCount() == MAX_AIRCRAFT && radarSeenAc() == MAX_AIRCRAFT);
  poll(s, "{\"aircraft\":[" + aircraft + "]}");
  // Synthetic local receiver coverage, including every plotted field and
  // unique callsigns/hex IDs; no device capture or real flight identifiers.
  std::string busy = "{\"aircraft\":[";
  for (unsigned i = 0; i < 60; ++i) {
    char entry[200];
    snprintf(entry, sizeof(entry),
      "{\"lat\":39.8,\"lon\":-105.1,\"track\":120,\"gs\":250,\"flight\":\"TEST%04u\",\"hex\":\"%06x\",\"alt_baro\":10000}", i, i);
    if (i) busy += ',';
    busy += entry;
  }
  busy += "]}";
  poll(s, busy);
  fprintf(stderr, "synthetic 60 aircraft: stage=%s error=%s count=%u seen=%u\n", radarStageName(), radarParseError(), radarCount(), radarSeenAc());
  assert(radarSeenAc() == 60 && radarCount() == MAX_AIRCRAFT);
  poll(s, "{\"aircraft\":[" + aircraft + "]}");
  lastOk = radarLastOkMs();
  // Exhaustion must fail atomically: cached aircraft and last-success age stay.
  std::string large = "{\"aircraft\":[";
  for (int i = 0; i < 5000; ++i) { if (i) large += ','; large += aircraft; }
  large += "]}";
  poll(s, large);
  assert(radarError() && radarStage() == RADAR_PARSE_FAIL);
  assert(std::string(radarParseError()) == "NoMemory");
  assert(radarCount() == 1 && radarSeenAc() == 0 && radarLastOkMs() == lastOk);
  assert(!memcmp(&cached, &aircraftAt(0), sizeof(cached)));
  // A single retained string must also be bounded, independently of count.
  poll(s, "{\"aircraft\":[{\"flight\":\"" + std::string(100000, 'X') + "\",\"lat\":39.8,\"lon\":-105.1}]}");
  assert(std::string(radarParseError()) == "NoMemory" && radarCount() == 1);
  assert(!memcmp(&cached, &aircraftAt(0), sizeof(cached)));
  // Live-heap guard includes filter construction and also preserves cache.
  ESP.heap = 4096;
  poll(s, "{\"aircraft\":[" + aircraft + "]}");
  assert(std::string(radarParseError()) == "NoMemory" && radarCount() == 1);
  ESP.heap = 100000;
  // Parser diagnostics reset on the next poll, including HTTP failures.
  HTTPClient::status = 503;
  poll(s, "");
  assert(radarStage() == RADAR_HTTP_ERROR && radarLastHttp() == 503);
  assert(!radarParseError()[0] && !radarTlsError() && radarCount() == 1);
  HTTPClient::status = -1;
  SecureClient::tlsError = 7;
  s.radar.source = RADAR_SRC_ADSBFI;
  poll(s, "");
  assert(radarStage() == RADAR_CONNECT_FAIL && radarTlsError() == 7);
  assert(!radarParseError()[0] && radarCount() == 1);
  SecureClient::tlsError = 0;
  s.radar.source = RADAR_SRC_WEBHOOK;
  HTTPClient::status = 200;
  poll(s, "{\"ac\":[" + aircraft + "]}");
  assert(radarStage() == RADAR_OK && !radarError() && !radarParseError()[0]);
  // Production parsing must reject a chunk truncated before JSON completes,
  // retaining the prior aircraft. This exercises fetchUrl, not a decoder drain.
  HTTPClient::chunked = true;
  poll(s, "40\r\n{\"ac\":[{\"lat\":39.8");
  assert(radarError() && radarStage() == RADAR_PARSE_FAIL);
  assert(std::string(radarParseError()) == "IncompleteInput");
  assert(radarCount() == 1 && !memcmp(&cached, &aircraftAt(0), sizeof(cached)));
  // Existing streaming contract: ArduinoJson stops at the root closing brace.
  // A complete JSON value without terminal chunk framing is still accepted.
  poll(s, "9\r\n{\"ac\":[]}");
  assert(!radarError() && radarStage() == RADAR_NO_AC && radarCount() == 0);
  assert(!radarParseError()[0]);
  // All source selections request HTTP/1.1 and decode the same chunked body.
  const std::string empty = "9\r\n{\"ac\":[]}\r\n0\r\n\r\n";
  HTTPClient::chunked = true;
  for (unsigned source : {0U, 1U, 2U}) {
    s.radar.source = source;
    poll(s, empty);
    assert(!HTTPClient::http10 && !radarError() && radarCount() == 0);
    assert(!radarParseError()[0]);
  }
  // Low heap before TLS resets stale HTTP and parse/TLS errors.
  ESP.heap = 4096;
  poll(s, "");
  assert(radarStage() == RADAR_LOW_HEAP && !radarLastHttp());
  assert(!radarParseError()[0] && !radarTlsError());
  puts("radar client bounded parse/cache/source tests passed");
}
