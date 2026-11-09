#pragma once
#include "Arduino.h"
#include "Client.h"
struct EspClass { size_t heap = 100000; size_t getFreeHeap() { return heap; } };
extern EspClass ESP;
struct TlsSession {};
class WiFiClient : public Client {
 public:
  int read() override { return -1; }
  int read(uint8_t*, size_t) override { return -1; }
  int peek() override { return -1; }
  int available() override { return 0; }
};
using NetClient = WiFiClient;
class SecureClient : public WiFiClient {
 public:
  static int tlsError;
  int getLastSSLError(char*, size_t) { return tlsError; }
};
namespace BearSSL {
class WiFiClientSecure {
 public:
  static bool probeMaxFragmentLength(const char*, unsigned, unsigned) { return false; }
};
}
inline SecureClient* platformMakeSecureClient(unsigned, TlsSession*) { return new SecureClient; }
inline unsigned platformMaxFreeBlock() { return ESP.getFreeHeap(); }
#define HTTP_CODE_OK 200
#define HTTPC_STRICT_FOLLOW_REDIRECTS 1
class HTTPClient {
 public:
  static Stream* body;
  static int status;
  static bool chunked;
  static bool http10;
  void useHTTP10(bool v) { http10 = v; }
  void collectHeaders(const char**, size_t) {}
  void setTimeout(unsigned) {}
  void setReuse(bool) {}
  bool begin(NetClient&, const String&) { return true; }
  void addHeader(const char*, const char*) {}
  void setUserAgent(const char*) {}
  void setFollowRedirects(unsigned) {}
  int GET() { return status; }
  Stream& getStream() { return *body; }
  String header(const char*) { return chunked ? "chunked" : ""; }
  void end() {}
};
