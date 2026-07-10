// Platform.h — the single place that knows which chip we build for.
//
// It pulls in the arch-specific SDK headers and exposes a small, uniform surface
// (class aliases + inline shims) so the rest of the firmware stays chip-agnostic.
// Target is chosen by a build-time macro: -D SMALLTV_ESP32C2 (ESP8684 knockoff),
// -D SMALLTV_ESP32 (classic ESP32, NM-TV-154), or -D SMALLTV_ESP8266 (original
// SmallTV, the default). Both ESP32 targets share the Arduino core 3.x branch.
#pragma once
#include <Arduino.h>

#if defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32)
// ================== ESP32 family (C2/ESP8684 + classic ESP32) ==================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_system.h>

using WebServerClass = WebServer;
using SecureClient   = WiFiClientSecure;
// Common base for a plain-or-TLS client held in a unique_ptr. On ESP32 both
// WiFiClient and WiFiClientSecure derive from NetworkClient (they are siblings,
// NOT parent/child), so NetworkClient is the base that can hold either.
using NetClient      = NetworkClient;

static inline void platformSetHostname(const char* h) { WiFi.setHostname(h); }
static inline void platformMdnsUpdate() { /* ESP32 mDNS answers from a background task */ }
static inline void platformAnalogWriteInit(uint8_t pin) { (void)pin; /* analogWrite defaults to 8-bit (0..255) */ }
static inline bool platformScanIsOpen(int i) { return WiFi.encryptionType(i) == WIFI_AUTH_OPEN; }
static inline String platformUpdateError() { return String(Update.errorString()); }

// Reset / crash info. The ESP32 Arduino API exposes no exception PC (on the C2
// or the classic ESP32), so epc/addr stay empty; panics/WDTs still flag crashes.
struct PlatformReset { String reason; bool wasCrash; char epc[16]; char addr[16]; };
static inline PlatformReset platformResetInfo() {
  PlatformReset r; r.wasCrash = false; r.epc[0] = 0; r.addr[0] = 0;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   r.reason = "Power on"; break;
    case ESP_RST_EXT:       r.reason = "External reset"; break;
    case ESP_RST_SW:        r.reason = "Software restart"; break;
    case ESP_RST_PANIC:     r.reason = "Exception/panic"; r.wasCrash = true; break;
    case ESP_RST_INT_WDT:   r.reason = "Interrupt watchdog"; r.wasCrash = true; break;
    case ESP_RST_TASK_WDT:  r.reason = "Task watchdog"; r.wasCrash = true; break;
    case ESP_RST_WDT:       r.reason = "Watchdog"; r.wasCrash = true; break;
    case ESP_RST_BROWNOUT:  r.reason = "Brownout"; break;
    case ESP_RST_DEEPSLEEP: r.reason = "Deep-sleep wake"; break;
    default:                r.reason = "Unknown"; break;
  }
  return r;
}

// TLS client factory. On ESP32 mbedTLS manages its own buffers, so rxBuf is unused.
static inline SecureClient* platformMakeSecureClient(uint16_t rxBuf) {
  (void)rxBuf;
  SecureClient* sc = new SecureClient();
  sc->setInsecure();
  return sc;
}

#else
// ================================= ESP8266 =================================
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266httpUpdate.h>
#include <WiFiUdp.h>
extern "C" {
#include <user_interface.h>   // struct rst_info + REASON_* (reset cause / crash PC)
}

using WebServerClass = ESP8266WebServer;
using SecureClient   = BearSSL::WiFiClientSecure;
// On the ESP8266, BearSSL::WiFiClientSecure derives from WiFiClient, so WiFiClient
// is the base that can hold either a plain or a TLS client.
using NetClient      = WiFiClient;

static inline void platformSetHostname(const char* h) { WiFi.hostname(h); }
static inline void platformMdnsUpdate() { MDNS.update(); }
static inline void platformAnalogWriteInit(uint8_t pin) { (void)pin; analogWriteRange(255); }
static inline bool platformScanIsOpen(int i) { return WiFi.encryptionType(i) == ENC_TYPE_NONE; }
static inline String platformUpdateError() { return Update.getErrorString(); }

// Reset / crash info from the ESP8266 boot ROM (Xtensa exception PC + fault addr).
struct PlatformReset { String reason; bool wasCrash; char epc[16]; char addr[16]; };
static inline PlatformReset platformResetInfo() {
  PlatformReset r; r.wasCrash = false; r.epc[0] = 0; r.addr[0] = 0;
  r.reason = ESP.getResetReason();
  struct rst_info* ri = ESP.getResetInfoPtr();
  if (ri && ri->reason == REASON_EXCEPTION_RST) {
    r.wasCrash = true;
    snprintf(r.epc,  sizeof(r.epc),  "0x%08x", (unsigned)ri->epc1);
    snprintf(r.addr, sizeof(r.addr), "0x%08x", (unsigned)ri->excvaddr);
  }
  return r;
}

// TLS client factory. On the ESP8266 the BearSSL receive buffer is a real heap
// win, so size it for the small JSON payloads we fetch.
static inline SecureClient* platformMakeSecureClient(uint16_t rxBuf) {
  SecureClient* sc = new SecureClient();
  sc->setInsecure();
  sc->setBufferSizes(rxBuf, 512);
  return sc;
}

#endif
