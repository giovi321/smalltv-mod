// Scripted HTTP for the rain host tests: the test installs a handler that
// answers each GET from the URL; every URL is logged.
#pragma once
#include "rain_host.h"
#include "ESP8266WiFi.h"
#include <functional>
#include <vector>

#define HTTP_CODE_OK 200
#define HTTPC_DISABLE_FOLLOW_REDIRECTS 0
#define HTTPC_STRICT_FOLLOW_REDIRECTS 1
#define HTTPC_FORCE_FOLLOW_REDIRECTS 2

struct FakeHttp {
  std::function<int(const std::string& url, std::string& body)> handler;
  std::vector<std::string> log;
  bool noLength = false;        // the server sends no Content-Length (getSize() = -1)
  size_t redirectGets = 0;      // GETs sent with redirects followed (must stay 0)
  long sizeOverride = -2;       // >= -1: the Content-Length announced, whatever the body
};
extern FakeHttp g_net;

class BodyStream : public Stream {
 public:
  std::string body;
  size_t pos = 0;
  int read() override { return pos < body.size() ? (uint8_t)body[pos++] : -1; }
};

class HTTPClient {
 public:
  void useHTTP10(bool) {}
  void setTimeout(int) {}
  void setReuse(bool) {}
  bool begin(WiFiClient&, const String& url) { url_ = url.c_str(); return true; }
  void addHeader(const char*, const char*) {}
  void setUserAgent(const char*) {}
  void setFollowRedirects(int f) { follow_ = f; }
  int GET() {
    g_net.log.push_back(url_);
    if (follow_ != HTTPC_DISABLE_FOLLOW_REDIRECTS) g_net.redirectGets++;
    body_.body.clear();
    body_.pos = 0;
    return g_net.handler ? g_net.handler(url_, body_.body) : -1;
  }
  Stream& getStream() { return body_; }
  int getSize() {   // Content-Length
    if (g_net.sizeOverride >= -1) return (int)g_net.sizeOverride;
    return g_net.noLength ? -1 : (int)body_.body.size();
  }
  void end() {}
 private:
  std::string url_;
  int follow_ = HTTPC_STRICT_FOLLOW_REDIRECTS;   // not the core's default: the code must set it
  BodyStream body_;
};
