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
void platformCrashLowsReset() {}
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
// image. The record sits at CRASH_RTC_WORD (CrashRecord.h has the map of the
// words). It is 25 words.
//
// Written only on an abnormal reset: custom_crash_callback() covers exceptions,
// the software watchdog, abort()/out of memory and stack smashes, and is the
// only place the uptime is still known; the hardware watchdog never reaches it,
// so platformCrashRecordBegin() fills the record from rst_info at the next boot,
// with the uptime marked unknown.
// A normal restart leaves the record alone (CrashRecord.h has the decisions).
static_assert(sizeof(PlatformCrashRecord::trace) / 4 == CRASH_TRACE_WORDS, "trace size out of step");
static_assert(CRASH_RST_WDT == REASON_WDT_RST && CRASH_RST_EXCEPTION == REASON_EXCEPTION_RST &&
              CRASH_RST_SOFT_WDT == REASON_SOFT_WDT_RST, "CrashReasonCode out of step with the SDK");
// The RTC user memory is 512 bytes, 128 words (core Esp.cpp: rtcUserMemoryRead
// and Write refuse `offset * 4 + size > 512`); words 0..31 are the OTA hand-off.
static_assert(sizeof(CrashRtc) % 4 == 0 && CRASH_RTC_WORD >= 32 && sizeof(CrashRtc) / 4 + CRASH_RTC_WORD <= 128,
              "crash record must fit the RTC user words, clear of the OTA hand-off");

// The core's own record of its last failed allocation (heap.cpp: set on every
// failed malloc, and by operator new right before it aborts with reason 254).
// Plain extern: core 3.1.2 defines both unconditionally.
extern "C" {
extern void* umm_last_fail_alloc_addr;
extern int umm_last_fail_alloc_size;
}

// Lowest free heap and largest block since boot or the last crash clear, from a
// 100 ms timer: the callback writes them into the record, so a crash says how
// low the heap had been, not only what it was at the last moment. The free
// heap is read every tick (a counter read). The largest block needs a heap
// walk with interrupts off, so it is read once a second (CRASH_BLOCK_EVERY).
static CrashLowWater s_lows;
static os_timer_t s_lowsTimer;
static void lowsTick(void*) {
  if (s_lows.blockDue()) s_lows.sample(ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
  else s_lows.sampleHeap(ESP.getFreeHeap());
}
void platformCrashLowsReset() { s_lows.reset(); }

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
//
// Two writes. The heap walk (getMaxFreeBlockSize runs umm_info with interrupts
// off) can hang or fault on a corrupted heap, and that is a crash we most want
// the record of. So everything that needs no walk is written first, with the
// block "unknown"; then the walk; then the block and a new checksum. If the
// walk never returns, the first write stands, and /api/status shows the record
// without a maxBlock key. getFreeHeap() is safe before the first write: it
// reads a counter and does not walk the heap.
// An older record that was never cleared is not lost silently: it is counted
// in `more` (the newest crash is the one shown).
// `stack` is where the core says the trace starts. After a crash on the
// BearSSL stack the core prints that stack on the serial port, then passes the
// main stack's pointer (core_esp8266_postmortem.cpp), so the trace has no
// BearSSL frames.
extern "C" void custom_crash_callback(struct rst_info* ri, uint32_t stack, uint32_t stackEnd) {
  CrashRtc r;
  const bool have = crashLoad(r);
  const uint32_t more = crashMoreAfter(have, r.more);
  memset(&r, 0, sizeof(r));
  crashCollectTrace(reinterpret_cast<const uint32_t*>(stack), stackEnd > stack ? (stackEnd - stack) / 4 : 0, r.trace);
  crashFillHead(r, ri->reason, ri->exccause, ri->epc1, ri->excvaddr, millis() / 1000, ESP.getFreeHeap(),
                uint32_t(umm_last_fail_alloc_size), uint32_t(umm_last_fail_alloc_addr),
                s_lows.any() ? s_lows.heap() : 0, s_lows.any() ? s_lows.block() : 0, more);
  strlcpy(r.fw, FW_VERSION, sizeof(r.fw));
  crashStore(r);
  crashSealBlock(r, ESP.getMaxFreeBlockSize());
  ESP.rtcUserMemoryWrite(CRASH_RTC_WORD, reinterpret_cast<uint32_t*>(&r), sizeof(r));
}

void platformCrashRecordBegin() {
  s_lows.reset();
  os_timer_setfn(&s_lowsTimer, lowsTick, nullptr);
  os_timer_arm(&s_lowsTimer, 100, true);
  struct rst_info* ri = ESP.getResetInfoPtr();
  if (!ri) return;
  CrashRtc r;
  bool have = crashLoad(r);
  switch (crashBootAction(have, have && (r.flags & CRASH_PENDING), ri->reason)) {
    case CRASH_KEEP:
      return;
    case CRASH_ADOPT:   // custom_crash_callback() wrote this one
      r.flags = crashAdoptFlags(r.flags);
      crashStore(r);
      return;
    case CRASH_CAPTURE:
      // Hardware watchdog, or a crash the callback didn't see: the ROM still
      // left the registers in rst_info, only the uptime is gone.
      {
        const uint32_t more = crashMoreAfter(have, r.more);
        memset(&r, 0, sizeof(r));
        r.more = more;
      }
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
  crashCopyFw(out.fw, sizeof(out.fw), r);
  out.hasRegisters = crashHasRegisters(r.rstReason);
  out.exccause    = r.exccause;
  out.epc1        = r.epc1;
  out.excvaddr    = r.excvaddr;
  out.uptimeS     = r.uptimeS;
  out.uptimeKnown = r.uptimeKnown != 0;
  out.hasDetail   = (r.flags & CRASH_DETAIL) != 0;
  if (out.hasDetail) {
    out.heap     = r.heapBlock & 0xFFFF;
    out.hasBlock = crashBlockKnown(r.heapBlock >> 16);
    out.maxBlock = r.heapBlock >> 16;
    out.failSize = r.failSize;
    out.failAddr = r.failAddr;
    out.hasLows  = r.low != 0;
    out.lowHeap  = r.low & 0xFFFF;
    out.lowBlk   = r.low >> 16;
    for (unsigned i = 0; i < CRASH_TRACE_WORDS; i++) out.trace[i] = r.trace[i];
  }
  out.more = r.more;
  return true;
}

void platformCrashRecordClear() {
  uint32_t zero[sizeof(CrashRtc) / 4] = {0};
  ESP.rtcUserMemoryWrite(CRASH_RTC_WORD, zero, sizeof(zero));
}
#endif
