#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

// "CRS2". The 3.5.0 record ("CRSH", 12 words at word 96) is never read as this
// one: another magic, and another place.
static const uint32_t CRASH_MAGIC   = 0x43525332;
static const uint32_t CRASH_PENDING = 1;            // written by the callback, not yet adopted
static const uint32_t CRASH_DETAIL  = 2;            // heap, failed allocation, lows and trace were measured
// Code addresses kept from the stack. The first ones of an abort can be the
// core's own frames (raise_exception, abort, operator new), so there are 8.
static const unsigned CRASH_TRACE_WORDS = 8;
// Where the record sits in the user words as ESP.rtcUserMemoryRead() counts
// them (the SDK adds 64). Taken: the OTA hand-off to eboot 0..31 (anything
// written there between Update.end() and the reboot makes eboot boot the old
// image). The user memory ends at word 127 (512 bytes: the core's
// rtcUserMemoryWrite() refuses more). The record is 25 words: 76..100. Its
// place differs from the 3.5.0 record's (word 96) so that an older record is
// never overwritten or read as this one.
static const uint32_t CRASH_RTC_WORD = 76;
// Samples left out after a reset of the low-water marks (100 ms each), at
// boot or after a crash clear: the first ticks are the start-up's own dip.
static const unsigned CRASH_QUIET_TICKS = 20;
// The heap walk behind the largest block runs with interrupts off, so it is
// taken once in this many 100 ms ticks (once a second). A dip of the largest
// block that lasts less than that can be missed; the free heap is a counter
// read and is taken on every tick.
static const unsigned CRASH_BLOCK_EVERY = 10;

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
  uint32_t heapBlock, failSize, failAddr, low, trace[CRASH_TRACE_WORDS], more;
};
static_assert(sizeof(CrashRtc) == 100, "the record is 25 RTC words");

// FNV-1a over every byte after the two header words, except the last word
// (`more`): the callback raises that count with a single word write, so it
// can't be part of the check.
static inline uint32_t crashCheck(const CrashRtc& r) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&r) + 8;
  uint32_t h = 2166136261u;
  for (size_t i = 8; i < sizeof(r) - 4; i++) { h ^= *p++; h *= 16777619u; }
  return h;
}

// The record as the rest of the firmware reads it (Platform.h calls it
// PlatformCrashRecord). One struct, filled by platformCrashRecordGet() and
// handed to the status writer as it is: a second copy of it in handleStatus
// cost 224 B of the 4 KB loop stack (TRAPS T12). Plain data, no Arduino types.
struct CrashSnapshot {
  char     reason[24];     // see crashReasonLabel()
  char     fw[16];         // FW_VERSION of the image that crashed
  bool     hasRegisters;   // false for abort and stack overflow: the three below are not known
  uint32_t exccause;       // Xtensa EXCCAUSE
  uint32_t epc1;           // faulting PC, for addr2line
  uint32_t excvaddr;       // faulting address
  uint32_t uptimeS;        // seconds up before the crash, valid iff uptimeKnown
  bool     uptimeKnown;    // false for the hardware watchdog (no callback runs)
  // Measured by the callback; hasDetail is false for the hardware watchdog.
  bool     hasDetail;
  uint32_t heap;           // free heap at the crash
  bool     hasBlock;       // false if the heap walk did not finish
  uint32_t maxBlock;       // largest free block at the crash
  uint32_t failSize;       // size of the last failed allocation, 0: none since boot
  uint32_t failAddr;       // its caller
  bool     hasLows;        // false if the sampler took no sample
  uint32_t lowHeap, lowBlk;   // lows since boot or the last crash clear
  uint32_t trace[CRASH_TRACE_WORDS];   // code addresses from the stack, 0: unused
  uint32_t more;           // earlier crashes that were never cleared
};

// Free heap and block sizes are kept in 16 bits: the ESP8266's arena is 80 KB
// and its heap never reaches 64 KB. A larger value stays at the top.
static inline uint32_t crashClamp16(uint32_t v) { return v > 65535u ? 65535u : v; }

// The largest free block, as the upper half of heapBlock. 0xFFFF means "not
// known": the heap walk that measures it had not finished (or never ran).
static const uint32_t CRASH_BLOCK_UNKNOWN = 0xFFFF;
static inline bool crashBlockKnown(uint32_t field) { return field != CRASH_BLOCK_UNKNOWN; }
// A real block that big can't happen; it is kept just under the marker.
static inline uint32_t crashBlockField(uint32_t block) {
  return block >= CRASH_BLOCK_UNKNOWN ? CRASH_BLOCK_UNKNOWN - 1 : block;
}

// The first write of the callback: everything that needs no heap walk, with
// the block "unknown". The caller has zeroed `r`, collects the trace and the
// firmware string, and seals (r.check = crashCheck(r)). `more` is how many
// earlier crashes were never cleared (crashMoreAfter()).
static inline void crashFillHead(CrashRtc& r, uint32_t reason, uint32_t exccause, uint32_t epc1, uint32_t excvaddr,
                                 uint32_t uptimeS, uint32_t heap, uint32_t failSize, uint32_t failAddr,
                                 uint32_t lowHeap, uint32_t lowBlk, uint32_t more) {
  r.magic       = CRASH_MAGIC;
  r.flags       = CRASH_PENDING | CRASH_DETAIL;
  r.exccause    = exccause;
  r.epc1        = epc1;
  r.excvaddr    = excvaddr;
  r.uptimeS     = uptimeS;
  r.rstReason   = uint8_t(reason);
  r.uptimeKnown = 1;
  r.heapBlock   = crashClamp16(heap) | CRASH_BLOCK_UNKNOWN << 16;
  r.failSize    = failSize;
  r.failAddr    = failAddr;
  r.low         = crashClamp16(lowHeap) | crashClamp16(lowBlk) << 16;
  r.more        = more;
}
// The second write: the block, measured after the first write was made (the
// walk can hang on a corrupted heap), goes in and the check follows. If the
// walk never returns, the first write stands without a block.
static inline void crashSealBlock(CrashRtc& r, uint32_t block) {
  r.heapBlock = (r.heapBlock & 0xFFFFu) | crashBlockField(block) << 16;
  r.check = crashCheck(r);
}

// The flags of a record the callback wrote, once the boot has taken it over.
static inline uint32_t crashAdoptFlags(uint32_t flags) { return flags & ~CRASH_PENDING; }

// How many earlier crashes sit uncleared behind the one being written: 0 when
// there was no valid record, else the old count plus one. Saturates at 65535.
static inline uint32_t crashMoreAfter(bool haveRecord, uint32_t oldMore) {
  if (!haveRecord) return 0;
  return oldMore >= 65535u ? 65535u : oldMore + 1;
}

// Words on the stack that look like code (flash 0x40200000..0x402FFFFF or
// IRAM 0x40100000..0x4010FFFF).
static inline bool crashCodeAddress(uint32_t v) {
  return (v >= 0x40200000u && v < 0x40300000u) || (v >= 0x40100000u && v < 0x40110000u);
}
// The first CRASH_TRACE_WORDS of them, in stack order; unused slots are set to
// zero. Returns how many were found.
static inline unsigned crashCollectTrace(const uint32_t* words, unsigned count, uint32_t* out) {
  unsigned n = 0;
  for (unsigned i = 0; i < CRASH_TRACE_WORDS; i++) out[i] = 0;
  for (unsigned i = 0; i < count && n < CRASH_TRACE_WORDS; i++)
    if (crashCodeAddress(words[i])) out[n++] = words[i];
  return n;
}

// The lowest free heap and largest block since boot or the last crash clear, fed from a
// 100 ms timer. The first CRASH_QUIET_TICKS after a reset are left out.
class CrashLowWater {
 public:
  void reset() { heap_ = block_ = 0xFFFF; quiet_ = CRASH_QUIET_TICKS; sinceBlock_ = CRASH_BLOCK_EVERY; }
  // True when this tick should walk the heap for the largest block: never in
  // the quiet ticks, then on the first tick and on every CRASH_BLOCK_EVERY-th.
  bool blockDue() const { return !quiet_ && sinceBlock_ >= CRASH_BLOCK_EVERY; }
  // A tick that walked the heap: both values.
  void sample(uint32_t heap, uint32_t block) {
    if (quiet_) { --quiet_; return; }
    sinceBlock_ = 1;
    const uint32_t h = crashClamp16(heap), b = crashClamp16(block);
    if (h < heap_) heap_ = uint16_t(h);
    if (b < block_) block_ = uint16_t(b);
  }
  // A tick that did not: the free heap only (a counter read).
  void sampleHeap(uint32_t heap) {
    if (quiet_) { --quiet_; return; }
    if (sinceBlock_ < CRASH_BLOCK_EVERY) ++sinceBlock_;
    const uint32_t h = crashClamp16(heap);
    if (h < heap_) heap_ = uint16_t(h);
  }
  // False until a sample was taken since the last reset.
  bool any() const { return heap_ != 0xFFFF || block_ != 0xFFFF; }
  uint32_t heap() const { return heap_; }
  uint32_t block() const { return block_; }
 private:
  uint16_t heap_ = 0xFFFF, block_ = 0xFFFF;
  uint8_t quiet_ = 0;
  uint8_t sinceBlock_ = CRASH_BLOCK_EVERY;
};

// Copy a firmware string out of RTC memory. The record passed its checksum but
// the bytes are still untrusted: the field is cut at its end whether or not a
// terminator is there, and the copy always ends in one.
static inline void crashCopyFw(char* dst, size_t dstSize, const CrashRtc& r) {
  size_t n = 0;
  while (n < sizeof r.fw && r.fw[n]) n++;
  if (n > dstSize - 1) n = dstSize - 1;
  memcpy(dst, r.fw, n);
  dst[n] = 0;
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

// The core zeroes rst_info before it calls custom_crash_callback() for an
// abort and for a stack smash, so there is no exccause, epc1 or excvaddr to
// report for them: showing 0x0 would read as a real fault at address 0.
static inline bool crashHasRegisters(uint8_t reason) {
  return reason != CRASH_RST_SW_EXC && reason != CRASH_RST_STACK_SMASH;
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
