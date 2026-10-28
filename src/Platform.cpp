// Platform.cpp — the few things that need a real definition (not header-inline)
// but are still core-specific. Keeps the per-chip SNTP callback wiring out of the
// feature code, same as the inline shims in Platform.h.
#include "Platform.h"
#include "config.h"

#if defined(SMALLTV_FAMILY_ESP32)
#include <esp_sntp.h>

static void (*s_syncCb)() = nullptr;
static void sntpSyncNotify(struct timeval*) { if (s_syncCb) s_syncCb(); }

void platformOnTimeSync(void (*cb)()) {
  s_syncCb = cb;
  sntp_set_time_sync_notification_cb(sntpSyncNotify);
}

// No crash record on the ESP32 targets (the core exposes no exception PC).
void platformCrashRecordBegin() {}
bool platformCrashRecordGet(PlatformCrashRecord&) { return false; }
void platformCrashRecordClear() {}

#else  // ESP8266
#include <coredecls.h>

static void (*s_syncCb)() = nullptr;
// settimeofday_cb fires whenever the clock is set; from_sntp distinguishes an
// SNTP update (what we care about) from a manual settimeofday (which we never do).
static void sntpSyncNotify(bool from_sntp) { if (from_sntp && s_syncCb) s_syncCb(); }

void platformOnTimeSync(void (*cb)()) {
  s_syncCb = cb;
  settimeofday_cb(sntpSyncNotify);
}

// ---- crash record in RTC user memory ---------------------------------------
// The RTC block has 128 user words (512 B). Nothing else in the firmware or its
// libraries touches it (the core keeps its own state in the system words ahead
// of the user area), so the record sits at word 96, clear of the low offsets
// that other code tends to pick first. It is 11 words, ending at word 107.
//
// Written only on an abnormal reset: custom_crash_callback() covers exceptions
// and the software watchdog and is the only place the uptime is still known;
// the hardware watchdog never reaches it, so platformCrashRecordBegin() fills
// the record from rst_info at the next boot, with the uptime marked unknown.
// A normal restart leaves the record alone.
static const uint32_t CRASH_RTC_WORD  = 96;
static const uint32_t CRASH_MAGIC     = 0x43525348;   // "CRSH"
static const uint32_t CRASH_PENDING   = 1;            // written by the callback, not yet adopted

struct CrashRtc {
  uint32_t magic;
  uint32_t check;       // FNV-1a over every byte after this field
  uint32_t flags;       // CRASH_PENDING
  uint32_t exccause, epc1, excvaddr;
  uint32_t uptimeS;
  uint8_t  rstReason;   // REASON_* of the crash
  uint8_t  uptimeKnown;
  uint16_t pad;
  char     fw[16];
};
static_assert(sizeof(CrashRtc) % 4 == 0 && sizeof(CrashRtc) / 4 + CRASH_RTC_WORD <= 128,
              "crash record must fit the RTC user area");

static uint32_t crashCheck(const CrashRtc& r) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&r) + 8;
  uint32_t h = 2166136261u;
  for (size_t i = 8; i < sizeof(r); i++) { h ^= *p++; h *= 16777619u; }
  return h;
}

static bool crashLoad(CrashRtc& r) {
  if (!ESP.rtcUserMemoryRead(CRASH_RTC_WORD, reinterpret_cast<uint32_t*>(&r), sizeof(r))) return false;
  return r.magic == CRASH_MAGIC && r.check == crashCheck(r);
}

static void crashStore(CrashRtc& r) {
  r.magic = CRASH_MAGIC;
  r.check = crashCheck(r);
  ESP.rtcUserMemoryWrite(CRASH_RTC_WORD, reinterpret_cast<uint32_t*>(&r), sizeof(r));
}

// The core calls this from its exception and software-watchdog handlers, with
// the heap and the flash cache in whatever state the fault left them. So no
// allocation and no Arduino String in here: static buffers and plain stores.
extern "C" void custom_crash_callback(struct rst_info* ri, uint32_t, uint32_t) {
  CrashRtc r;
  memset(&r, 0, sizeof(r));
  r.flags       = CRASH_PENDING;
  r.exccause    = ri->exccause;
  r.epc1        = ri->epc1;
  r.excvaddr    = ri->excvaddr;
  r.uptimeS     = millis() / 1000;
  r.rstReason   = ri->reason;
  r.uptimeKnown = 1;
  strlcpy(r.fw, FW_VERSION, sizeof(r.fw));
  crashStore(r);
}

void platformCrashRecordBegin() {
  struct rst_info* ri = ESP.getResetInfoPtr();
  if (!ri) return;
  bool abnormal = ri->reason == REASON_WDT_RST || ri->reason == REASON_EXCEPTION_RST ||
                  ri->reason == REASON_SOFT_WDT_RST;
  CrashRtc r;
  bool have = crashLoad(r);
  if (!abnormal) {
    // Keep a valid record. A leftover "pending" flag can't be told apart from a
    // fresh crash after a normal reset, so drop only the flag.
    if (have && (r.flags & CRASH_PENDING)) { r.flags = 0; crashStore(r); }
    return;
  }
  if (have && (r.flags & CRASH_PENDING)) {   // custom_crash_callback() wrote this one
    r.flags = 0;
    crashStore(r);
    return;
  }
  // Hardware watchdog, or a crash the callback didn't see: the ROM still left
  // the registers in rst_info, only the uptime is gone.
  memset(&r, 0, sizeof(r));
  r.exccause  = ri->exccause;
  r.epc1      = ri->epc1;
  r.excvaddr  = ri->excvaddr;
  r.rstReason = ri->reason;
  strlcpy(r.fw, FW_VERSION, sizeof(r.fw));   // the booted image, not necessarily the crashed one
  crashStore(r);
}

bool platformCrashRecordGet(PlatformCrashRecord& out) {
  CrashRtc r;
  if (!crashLoad(r)) return false;
  memset(&out, 0, sizeof(out));
  const char* why = r.rstReason == REASON_EXCEPTION_RST ? "Exception"
                  : r.rstReason == REASON_SOFT_WDT_RST  ? "Software watchdog"
                                                        : "Hardware watchdog";
  strlcpy(out.reason, why, sizeof(out.reason));
  strlcpy(out.fw, r.fw, sizeof(out.fw));
  out.exccause    = r.exccause;
  out.epc1        = r.epc1;
  out.excvaddr    = r.excvaddr;
  out.uptimeS     = r.uptimeS;
  out.uptimeKnown = r.uptimeKnown != 0;
  return true;
}

void platformCrashRecordClear() {
  uint32_t zero[sizeof(CrashRtc) / 4] = {0};
  ESP.rtcUserMemoryWrite(CRASH_RTC_WORD, zero, sizeof(zero));
}
#endif
