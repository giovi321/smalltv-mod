// Stand-ins for the ESP8266 core classes that Platform.h names. Nothing here
// is ever called by the harness; the definitions only have to compile.
#pragma once
#include "Arduino.h"
#include <memory>
struct WiFiClient { virtual ~WiFiClient() {} };
struct IPAddress {};
struct rst_info { int reason; unsigned epc1, excvaddr; };
#define REASON_EXCEPTION_RST 2
namespace BearSSL {
struct Session {};
struct WiFiClientSecure : WiFiClient {
  void setInsecure() {}
  void setBufferSizes(int, int) {}
  void setSession(Session*) {}
  void setCiphersLessSecure() {}
};
}
struct WiFiClass { void hostname(const char*) {} int encryptionType(int) { return 0; } };
extern WiFiClass WiFi;
struct ESP8266WebServer {};
#define ENC_TYPE_NONE 7
struct MDNSClass { void update() {} };
extern MDNSClass MDNS;
struct UpdateClass { String getErrorString() { return String(); } };
extern UpdateClass Update;
struct EspClass {
  unsigned getChipId() { return 0; }
  String getResetReason() { return String(); }
  rst_info* getResetInfoPtr() { return nullptr; }
  unsigned getMaxFreeBlockSize() { return 0; }
  unsigned getFreeContStack() { return 0; }
  unsigned freeHeap = 0;               // what getFreeHeap() reports (a test may set it)
  unsigned getFreeHeap() { return freeHeap; }
  void wdtFeed() {}
};
extern EspClass ESP;
inline void configTime(const char*, const char*, const char*) {}
inline void analogWriteRange(int) {}
