#pragma once
#include "Arduino.h"
#include "Client.h"
struct EspClass { size_t heap = 100000, maxBlock = 100000; size_t getFreeHeap() { return heap; } };
extern EspClass ESP;
struct TlsSession {};
inline Stream*& networkBody() { static Stream* body = nullptr; return body; }
class WiFiClient : public Client {
 public:
  uint8_t connected() override { return networkBody() && networkBody()->available() > 0; }
  int read() override { return networkBody() ? networkBody()->read() : -1; }
  int read(uint8_t* out, size_t n) override { return networkBody() ? static_cast<int>(networkBody()->readBytes(reinterpret_cast<char*>(out), n)) : -1; }
  int peek() override { return networkBody() ? networkBody()->peek() : -1; }
  int available() override { return networkBody() ? networkBody()->available() : 0; }
};
using NetClient = WiFiClient;
class SecureClient : public WiFiClient {};
namespace BearSSL {
class WiFiClientSecure {
 public:
  static bool probeMaxFragmentLength(const char*, unsigned, unsigned) { return false; }
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
  static bool chunked;
  static bool http10;
  static unsigned headerDelay;
  static const char* transferEncoding;
  void useHTTP10(bool v) { http10 = v; }
  void collectHeaders(const char**, size_t) {}
  void setTimeout(unsigned) {}
  void setReuse(bool) {}
  bool begin(NetClient& client, const String&) { client_ = &client; networkBody() = body; return true; }
  void addHeader(const char*, const char*) {}
  void setUserAgent(const char*) {}
  void setFollowRedirects(unsigned) {}
  int GET() { delay(headerDelay); return status; }
  Client& getStream() { return *client_; }
  String header(const char*) { return chunked ? transferEncoding : ""; }
  void end() {}
 private:
  NetClient* client_ = nullptr;
};
