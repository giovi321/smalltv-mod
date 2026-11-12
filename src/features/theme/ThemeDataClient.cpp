#include "ThemeDataClient.h"
#if WITH_THEME
#include "Platform.h"
#if !defined(SMALLTV_ESP8266)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include <new>

namespace {
struct Deadline {
  smalltv::DataFetch& request;
  uint32_t started = millis();
  explicit Deadline(smalltv::DataFetch& r) : request(r) {}
  bool expired() const {
    return request.cancelled() || uint32_t(millis() - started) >= smalltv::DataTimeoutMs;
  }
};

// HTTPClient's timeout is an idle timeout, including for header parsing. Stop
// its transport at the overall deadline too, so trickling headers cannot keep
// the worker alive indefinitely. No other task touches this socket.
template <class Transport>
class BoundedClient : public Transport {
 public:
  explicit BoundedClient(Deadline& deadline) : deadline_(deadline) {}
  using Transport::connect;
  // ESP8266's Client has no connect(host,port,timeout) overload; allowed()
  // already bounds every socket call regardless, so the missing explicit
  // connect timeout costs nothing there.
#if defined(SMALLTV_ESP8266)
  int connect(const char* host, uint16_t port) override {
    if (!allowed()) return 0;
    int result = Transport::connect(host, port);
    return allowed() ? result : 0;
  }
#else
  int connect(const char* host, uint16_t port, int32_t timeout) override {
    if (!allowed()) return 0;
    int result = Transport::connect(host, port, timeout);
    return allowed() ? result : 0;
  }
#endif
  int available() override { return allowed() ? Transport::available() : 0; }
  uint8_t connected() override { return allowed() ? Transport::connected() : 0; }
  int read() override { return allowed() ? Transport::read() : -1; }
  int read(uint8_t* buffer, size_t count) override { return allowed() ? Transport::read(buffer, count) : -1; }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* buffer, size_t count) override { return allowed() ? Transport::write(buffer, count) : 0; }
 private:
  bool allowed() {
    if (!deadline_.expired()) return true;
    Transport::stop();
    // Also end Stream::readStringUntil's internal timedRead after cancellation.
    this->setTimeout(1);
    return false;
  }
  Deadline& deadline_;
};

// After GET(), read the body through HTTPClient's stream (the same pattern
// StockClient and OtaUpdate use): HTTPClient owns the response from that
// point, and on ESP8266 the raw client behind it never yields body bytes, so
// every theme-data fetch failed at the body-read stage despite HTTP 200.
struct BodyStream {
  Stream& stream;
  NetClient& client;
  Deadline& deadline;
  uint32_t now() const { return millis(); }
  bool cancelled() const { return deadline.expired(); }
  int available() { return stream.available(); }
  bool connected() { return client.connected(); }
  int read(uint8_t* buffer, size_t count) { return (int)stream.readBytes(buffer, count); }
  void idle() { delay(1); }
};

// How a fetch ended, for the status (ThemeData.h, FetchStats).
enum class Outcome { Ok, Refused, Failed };
smalltv::FetchStats g_fetchStats;
void noteFetch(Outcome outcome) { g_fetchStats.note(static_cast<int>(outcome)); }

// The request and the parse in one call: the reply goes from the socket
// straight into the filtered parse (readDataValues), so it is never held as a
// string. The network objects stay alive for the parse; they hold the heap
// FetchFreeHeap guesses at, and the parse takes at most DataParseBudget more.
__attribute__((noinline)) Outcome fetchValues(smalltv::DataFetch& request) {
  if (request.cancelled()) return Outcome::Failed;
  Deadline deadline(request);
  std::unique_ptr<NetClient> client;
  if (request.source.url.rfind("https://", 0) == 0) {
    if (ESP.getFreeHeap() < 18000) return Outcome::Refused;
    auto secure = new(std::nothrow) BoundedClient<SecureClient>(deadline);
    if (!secure) return Outcome::Failed;
    // HTTPS reaches this path only after the manifest explicitly opted in.
    secure->setInsecure();
#if !defined(SMALLTV_ESP8266)
    secure->setHandshakeTimeout(smalltv::DataTimeoutMs / 1000);
#endif
    client.reset(secure);
  } else client.reset(new(std::nothrow) BoundedClient<WiFiClient>(deadline));
  if (!client) return Outcome::Failed;
  // ESP8266's WiFiClient/HTTPClient have no separate connect-phase timeout;
  // Deadline (via BoundedClient::allowed()) already bounds every socket call
  // on every platform, so these are ESP32-only belt-and-suspenders.
#if !defined(SMALLTV_ESP8266)
  client->setConnectionTimeout(smalltv::DataTimeoutMs);
#endif
  HTTPClient http;
#if !defined(SMALLTV_ESP8266)
  http.setConnectTimeout(smalltv::DataTimeoutMs);
#endif
  http.setTimeout(smalltv::DataTimeoutMs);
  http.setReuse(false);
  http.useHTTP10(true);
  if (!http.begin(*client, request.source.url.c_str())) return Outcome::Failed;
  http.addHeader("Accept", "application/json");
  if (http.GET() != HTTP_CODE_OK || deadline.expired()) return Outcome::Failed;
#if defined(SMALLTV_ESP8266)
  // The network objects are up now, so the heap that is free is what the parse
  // has. The allocator keeps the parse inside DataParseBudget; refuse when
  // that much is not there rather than let a failed allocation restart the
  // device. The source keeps its old values.
  if (ESP.getFreeHeap() < smalltv::FetchParseHeap) return Outcome::Refused;
#endif
  BodyStream stream{http.getStream(), *client, deadline};
  const bool complete = smalltv::readDataValues(stream, http.getSize(), request.source, request.values);
  http.end();
  return complete && !request.cancelled() ? Outcome::Ok : Outcome::Failed;
}
bool fetchThemeSource(smalltv::DataFetch& request) {
  const Outcome outcome = fetchValues(request);
  if (!request.cancelled()) noteFetch(outcome);   // a fetch dropped by a theme change says nothing
  return outcome == Outcome::Ok;
}
#if !defined(SMALLTV_ESP8266)
void runFetch(void* parameter) {
  {
    std::unique_ptr<std::shared_ptr<smalltv::DataFetch>> context(
        static_cast<std::shared_ptr<smalltv::DataFetch>*>(parameter));
    auto request = std::move(*context);
    context.reset();
    request->finish(fetchThemeSource(*request));
  } // release every C++ object before FreeRTOS deletes the task's stack
  vTaskDelete(nullptr);
}
#endif
}

const smalltv::FetchStats& themeFetchStats() { return g_fetchStats; }

#if defined(SMALLTV_ESP8266)
// No FreeRTOS on this chip: run the bounded fetch synchronously on the
// caller's own stack, the same "one blocking call per main-loop tick"
// pattern the ticker and radar clients already use here. Deadline (inside
// fetchThemeSource) already bounds this to DataTimeoutMs plus body-read
// time, the same order of magnitude a TLS handshake already costs this
// chip elsewhere.
bool startThemeDataFetch(const std::shared_ptr<smalltv::DataFetch>& request) {
  // Short of heap the fetch is refused rather than started: a failed
  // allocation in the HTTP client would restart the device.
  if (ESP.getFreeHeap() < smalltv::FetchFreeHeap || platformMaxFreeBlock() < smalltv::FetchLargestBlock) {
    noteFetch(Outcome::Refused);
    return false;
  }
  request->finish(fetchThemeSource(*request));
  return true;
}
#else
bool startThemeDataFetch(const std::shared_ptr<smalltv::DataFetch>& request) {
  constexpr uint32_t StackBytes = 8192;
  if (ESP.getFreeHeap() < StackBytes + 18000) return false;
  auto context = new(std::nothrow) std::shared_ptr<smalltv::DataFetch>(request);
  if (!context) return false;
  if (xTaskCreate(runFetch, "theme-json", StackBytes, context, 1, nullptr) != pdPASS) {
    delete context;
    return false;
  }
  return true;
}
#endif
#endif
