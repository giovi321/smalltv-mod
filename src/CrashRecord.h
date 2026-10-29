#pragma once
#include <stddef.h>
#include <stdint.h>

// Layout and decisions of the ESP8266 crash record, kept free of Arduino and
// SDK headers so tests/test_crash_record.cpp can build them on the host.

// rst_info.reason values, as in the SDK's user_interface.h plus the two the
// core's postmortem handler passes to custom_crash_callback() on its own.
enum CrashReasonCode : uint8_t {
  CRASH_RST_WDT         = 1,    // hardware watchdog
  CRASH_RST_EXCEPTION   = 2,
  CRASH_RST_SOFT_WDT    = 3,
  CRASH_RST_STACK_SMASH = 253,  // core: stack smashing detected
  CRASH_RST_SW_EXC      = 254,  // core: abort(), panic(), out of memory
};

static const uint32_t CRASH_MAGIC   = 0x43525348;   // "CRSH"
static const uint32_t CRASH_PENDING = 1;            // written by the callback, not yet adopted

struct CrashRtc {
  uint32_t magic;
  uint32_t check;       // FNV-1a over every byte after this field
  uint32_t flags;       // CRASH_PENDING
  uint32_t exccause, epc1, excvaddr;
  uint32_t uptimeS;
  uint8_t  rstReason;   // CrashReasonCode
  uint8_t  uptimeKnown;
  uint16_t pad;
  char     fw[16];
};

static inline uint32_t crashCheck(const CrashRtc& r) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&r) + 8;
  uint32_t h = 2166136261u;
  for (size_t i = 8; i < sizeof(r); i++) { h ^= *p++; h *= 16777619u; }
  return h;
}

static inline const char* crashReasonLabel(uint8_t reason) {
  switch (reason) {
    case CRASH_RST_EXCEPTION:   return "Exception";
    case CRASH_RST_SOFT_WDT:    return "Software watchdog";
    case CRASH_RST_WDT:         return "Hardware watchdog";
    case CRASH_RST_SW_EXC:      return "Abort/out of memory";
    case CRASH_RST_STACK_SMASH: return "Stack overflow";
    default:                    return "Unknown";
  }
}

// What platformCrashRecordBegin() does with the record at boot.
enum CrashBootAction { CRASH_KEEP, CRASH_ADOPT, CRASH_CAPTURE };
// A record the callback wrote is adopted whatever the boot reason says: after
// an abort() or a stack smash the chip restarts through a plain software
// restart, so the reset reason alone can't tell it from a normal one. Without
// one, only the three reasons the ROM reports for a crash build a record from
// rst_info (the hardware watchdog never reaches the callback). Anything else is
// a normal restart and leaves the stored record alone.
static inline CrashBootAction crashBootAction(bool haveRecord, bool pending, uint32_t bootReason) {
  if (haveRecord && pending) return CRASH_ADOPT;
  if (bootReason == CRASH_RST_WDT || bootReason == CRASH_RST_EXCEPTION ||
      bootReason == CRASH_RST_SOFT_WDT) return CRASH_CAPTURE;
  return CRASH_KEEP;
}
