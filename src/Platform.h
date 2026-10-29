// Platform.h — the single place that knows which chip we build for.
//
// It pulls in the arch-specific SDK headers and exposes a small, uniform surface
// (class aliases + inline shims) so the rest of the firmware stays chip-agnostic.
// Target is chosen by a build-time macro: -D SMALLTV_ESP32C2 (ESP8684 knockoff),
// -D SMALLTV_ESP32 (classic ESP32, NM-TV-154), or -D SMALLTV_ESP8266 (original
// SmallTV). Target.h enforces exactly one and defines the SMALLTV_FAMILY_* macros
// used below. Both ESP32 targets share the Arduino core 3.x branch.
#pragma once
#include "Target.h"
#include <Arduino.h>
#include <time.h>

#if defined(SMALLTV_FAMILY_ESP32)
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
static inline void platformTimeBegin(const char* tz, const char* s1, const char* s2) {
  configTzTime(tz, s1, s2);   // ESP32 core: sets TZ + starts SNTP
}
static inline void platformMdnsUpdate() { /* ESP32 mDNS answers from a background task */ }
static inline void platformAnalogWriteInit(uint8_t pin) { (void)pin; /* analogWrite defaults to 8-bit (0..255) */ }
static inline bool platformScanIsOpen(int i) { return WiFi.encryptionType(i) == WIFI_AUTH_OPEN; }
static inline String platformUpdateError() { return String(Update.errorString()); }
static inline uint32_t platformChipId() { return (uint32_t)ESP.getEfuseMac(); }

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

// ESP32 (mbedTLS) manages its own buffers and negotiates ECDHE natively; the
// extra args exist only so the shared ESP8266 call sites compile here too.
struct DummySession {};
using TlsSession = DummySession;
static inline SecureClient* platformMakeSecureClient(uint16_t rxBuf,
                                                     TlsSession* session = nullptr,
                                                     uint16_t txBuf = 512,
                                                     bool cheapCiphers = false) {
  (void)rxBuf; (void)session; (void)txBuf; (void)cheapCiphers;
  SecureClient* sc = new SecureClient();
  sc->setInsecure();
  return sc;
}
static inline uint32_t platformMaxFreeBlock() { return ESP.getMaxAllocHeap(); }
static inline uint32_t platformFreeContStack() { return 0; }   // N/A on ESP32

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
static inline void platformTimeBegin(const char* tz, const char* s1, const char* s2) {
  configTime(tz, s1, s2);     // ESP8266 core: TZ-string overload, sets TZ + starts SNTP
}
static inline void platformMdnsUpdate() { MDNS.update(); }
static inline void platformAnalogWriteInit(uint8_t pin) { (void)pin; analogWriteRange(255); }
static inline bool platformScanIsOpen(int i) { return WiFi.encryptionType(i) == ENC_TYPE_NONE; }
static inline String platformUpdateError() { return Update.getErrorString(); }
static inline uint32_t platformChipId() { return ESP.getChipId(); }

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

using TlsSession = BearSSL::Session;

// TLS client factory. On the ESP8266 the BearSSL receive buffer is a real heap
// win, so size it for the small JSON payloads we fetch. Options:
//  - session: TLS session resumption. Pass a persistent BearSSL::Session and
//    the first handshake stores its params; later connects to the same server
//    resume without the costly ECDHE/RSA math (cash.ch resumes for ~23 h).
//  - cheapCiphers: offer ONLY the old static-RSA suites. Hosts that accept them
//    (Yahoo, raw.githubusercontent.com) then skip ECDHE entirely, keeping those
//    handshakes as light as the old BASIC build. Only cash.ch, which needs
//    ECDHE, is left on the full (heavy) suite list.
// cash.ch honors MFLN so 512/512 keeps the whole TLS footprint small.
static inline SecureClient* platformMakeSecureClient(uint16_t rxBuf,
                                                     TlsSession* session = nullptr,
                                                     uint16_t txBuf = 512,
                                                     bool cheapCiphers = false) {
  SecureClient* sc = new SecureClient();
  sc->setInsecure();
  sc->setBufferSizes(rxBuf, txBuf);
  if (session) sc->setSession(session);
  if (cheapCiphers) sc->setCiphersLessSecure();   // static-RSA only -> no ECDHE cost
  return sc;
}

// Diagnostics for the TLS memory squeeze (largest contiguous heap block the
// handshake must find, and the separate primary "cont" stack headroom).
static inline uint32_t platformMaxFreeBlock() { return ESP.getMaxFreeBlockSize(); }
static inline uint32_t platformFreeContStack() { return ESP.getFreeContStack(); }

#endif

// ---- common: wall-clock time (SNTP) ---------------------------------------
// True once SNTP has set the clock (epoch past 2021-01-01). Until then the
// caller must treat time as unknown (night mode stays off = fail-safe on).
static inline bool platformTimeValid() { return time(nullptr) > 1609459200; }

// Register a callback fired on every successful SNTP sync (the "NTP was just
// reachable and set the clock" signal used to trust the clock for night mode).
// Defined per core in Platform.cpp. Call once, after platformTimeBegin.
void platformOnTimeSync(void (*cb)());

// ---- common: record of the last crash --------------------------------------
// What the previous abnormal reset (exception or watchdog) left behind. On the
// ESP8266 it lives in RTC user memory, so it survives a normal restart and is
// kept until platformCrashRecordClear() or a power cycle; no flash is written.
// The ESP32 targets keep no record: platformCrashRecordGet() returns false.
struct PlatformCrashRecord {
  char     reason[24];     // see crashReasonLabel() in CrashRecord.h
  char     fw[16];         // FW_VERSION of the image that crashed
  uint32_t exccause;       // Xtensa EXCCAUSE
  uint32_t epc1;           // faulting PC, for addr2line
  uint32_t excvaddr;       // faulting address
  uint32_t uptimeS;        // seconds up before the crash, valid iff uptimeKnown
  bool     uptimeKnown;    // false for the hardware watchdog (no callback runs)
};
// Call once at boot, before anything can reset the chip again.
void platformCrashRecordBegin();
bool platformCrashRecordGet(PlatformCrashRecord& out);
void platformCrashRecordClear();
