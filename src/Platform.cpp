// Platform.cpp — the few things that need a real definition (not header-inline)
// but are still core-specific. Keeps the per-chip SNTP callback wiring out of the
// feature code, same as the inline shims in Platform.h.
#include "Platform.h"
#include "config.h"
#include "CrashRecord.h"

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
// The RTC block has 128 user words (512 B). The core's OTA hand-off to eboot
// (struct eboot_command at 0x60001200) takes the first 32 of them (128 bytes,
// see the note in the core's Esp.cpp): anything written there between
// Update.end() and the reboot corrupts the command and eboot boots the old
// image. The record sits at word 96, well clear of it. It is 12 words, ending
// at word 107.
//
// Written only on an abnormal reset: custom_crash_callback() covers exceptions,
// the software watchdog, abort()/out of memory and stack smashes, and is the
// only place the uptime is still known; the hardware watchdog never reaches it,
// so platformCrashRecordBegin() fills the record from rst_info at the next boot,
// with the uptime marked unknown.
// A normal restart leaves the record alone (CrashRecord.h has the decisions).
static const uint32_t CRASH_RTC_WORD = 96;
static_assert(CRASH_RST_WDT == REASON_WDT_RST && CRASH_RST_EXCEPTION == REASON_EXCEPTION_RST &&
              CRASH_RST_SOFT_WDT == REASON_SOFT_WDT_RST, "CrashReasonCode out of step with the SDK");
static_assert(sizeof(CrashRtc) % 4 == 0 && sizeof(CrashRtc) / 4 + CRASH_RTC_WORD <= 128,
              "crash record must fit the RTC user area");

static bool crashLoad(CrashRtc& r) {
  if (!ESP.rtcUserMemoryRead(CRASH_RTC_WORD, reinterpret_cast<uint32_t*>(&r), sizeof(r))) return false;
  return r.magic == CRASH_MAGIC && r.check == crashCheck(r);
}

static void crashStore(CrashRtc& r) {
  r.magic = CRASH_MAGIC;
  r.check = crashCheck(r);
  ESP.rtcUserMemoryWrite(CRASH_RTC_WORD, reinterpret_cast<uint32_t*>(&r), sizeof(r));
}

// The core calls this from its exception, software-watchdog, abort and stack-smash handlers, with
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
  CrashRtc r;
  bool have = crashLoad(r);
  switch (crashBootAction(have, have && (r.flags & CRASH_PENDING), ri->reason)) {
    case CRASH_KEEP:
      return;
    case CRASH_ADOPT:   // custom_crash_callback() wrote this one
      r.flags = 0;
      crashStore(r);
      return;
    case CRASH_CAPTURE:
      // Hardware watchdog, or a crash the callback didn't see: the ROM still
      // left the registers in rst_info, only the uptime is gone.
      memset(&r, 0, sizeof(r));
      r.exccause  = ri->exccause;
      r.epc1      = ri->epc1;
      r.excvaddr  = ri->excvaddr;
      r.rstReason = ri->reason;
      strlcpy(r.fw, FW_VERSION, sizeof(r.fw));   // the booted image, not necessarily the crashed one
      crashStore(r);
      return;
  }
}

bool platformCrashRecordGet(PlatformCrashRecord& out) {
  CrashRtc r;
  if (!crashLoad(r)) return false;
  memset(&out, 0, sizeof(out));
  strlcpy(out.reason, crashReasonLabel(r.rstReason), sizeof(out.reason));
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
