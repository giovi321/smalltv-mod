#pragma once
#include <memory>
#include "Arduino.h"
#include "Client.h"
struct EspClass { size_t heap = 100000, maxBlock = 100000; size_t getFreeHeap() { return heap; } };
extern EspClass ESP;
struct TlsSession {};
inline Stream*& networkBody() { static Stream* body = nullptr; return body; }
class WiFiClient : public Client {
 public:
  virtual std::unique_ptr<WiFiClient> clone() const { return std::unique_ptr<WiFiClient>(new WiFiClient(*this)); }
  uint8_t connected() override { return networkBody() && networkBody()->available() > 0; }
  int read() override { return networkBody() ? networkBody()->read() : -1; }
  int read(uint8_t* out, size_t n) override { return networkBody() ? static_cast<int>(networkBody()->readBytes(reinterpret_cast<char*>(out), n)) : -1; }
  int peek() override { return networkBody() ? networkBody()->peek() : -1; }
  int available() override { return networkBody() ? networkBody()->available() : 0; }
};
using NetClient = WiFiClient;
inline unsigned& secureReads() { static unsigned reads = 0; return reads; }
class SecureClient : public WiFiClient {
 public:
  std::unique_ptr<WiFiClient> clone() const override { return std::unique_ptr<WiFiClient>(new SecureClient(*this)); }
  int read() override { ++secureReads(); return WiFiClient::read(); }
};
inline bool& probeSupportsMfln() { static bool supported = true; return supported; }
inline unsigned& probeCalls() { static unsigned calls = 0; return calls; }
namespace BearSSL {
class WiFiClientSecure {
 public:
  static bool probeMaxFragmentLength(const char*, unsigned, unsigned) { ++probeCalls(); return probeSupportsMfln(); }
};
}
inline SecureClient* platformMakeSecureClient(unsigned, TlsSession*) { return new SecureClient; }
inline unsigned platformMaxFreeBlock() { return ESP.maxBlock; }
#define HTTP_CODE_OK 200
#define HTTPC_ERROR_ENCODING -9
#define HTTPC_STRICT_FOLLOW_REDIRECTS 1
class HTTPClient {
 public:
  static Stream* body;
  static int status;
  static unsigned requests;
  static bool chunked;
  static bool http10;
  static unsigned headerDelay;
  static const char* transferEncoding;
  void useHTTP10(bool v) { http10 = v; }
  void collectHeaders(const char**, size_t) {}
  void setTimeout(unsigned) {}
  void setReuse(bool) {}
  // Like the ESP8266 core 3.1: begin() keeps a clone of the client, not the client.
  bool begin(NetClient& client, const String&) { owned_ = client.clone(); client_ = owned_.get(); networkBody() = body; return true; }
  void addHeader(const char*, const char*) {}
  void setUserAgent(const char*) {}
  void setFollowRedirects(unsigned) {}
  int GET() { ++requests; delay(headerDelay); return status; }
  Client& getStream() { return *client_; }
  String header(const char*) { return chunked ? transferEncoding : ""; }
  void end() {}
 private:
  std::unique_ptr<WiFiClient> owned_;
  NetClient* client_ = nullptr;
};
