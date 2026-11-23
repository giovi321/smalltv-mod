// c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc tests/test_crash_record.cpp -o /tmp/smalltv-crash-tests && /tmp/smalltv-crash-tests
#include "CrashRecord.h"
#include <cassert>
#include <cstring>
#include <iostream>

static CrashRtc sample() {
  CrashRtc r;
  memset(&r, 0, sizeof(r));
  r.magic = CRASH_MAGIC;
  r.exccause = 28; r.epc1 = 0x40201234; r.excvaddr = 0; r.uptimeS = 77;
  r.rstReason = CRASH_RST_EXCEPTION; r.uptimeKnown = 1;
  strcpy(r.fw, "3.2.0");
  r.check = crashCheck(r);
  return r;
}

int main() {
  // The record is 25 words. It must stay clear of the OTA hand-off (words
  // 0..31) and end inside the user memory: the core's rtcUserMemoryWrite()
  // refuses anything past 512 bytes, that is 128 words (core Esp.cpp,
  // `offset * 4 + size > 512`).
  assert(sizeof(CrashRtc) == 100);
  assert(CRASH_RTC_WORD >= 32 && CRASH_RTC_WORD + sizeof(CrashRtc) / 4 <= 128);
  // A record of an older layout (#56's "CRSH" at word 96) is never read as this one.
  assert(CRASH_MAGIC != 0x43525348);

  // Labels: 253/254 get their own, the hardware watchdog is only REASON_WDT_RST.
  assert(!strcmp(crashReasonLabel(2), "Exception"));
  assert(!strcmp(crashReasonLabel(3), "Software watchdog"));
  assert(!strcmp(crashReasonLabel(1), "Hardware watchdog"));
  assert(!strcmp(crashReasonLabel(254), "Abort/out of memory"));
  assert(!strcmp(crashReasonLabel(253), "Stack overflow"));
  assert(!strcmp(crashReasonLabel(0), "Unknown"));
  assert(!strcmp(crashReasonLabel(4), "Unknown"));
  assert(!strcmp(crashReasonLabel(255), "Unknown"));
  assert(strlen(crashReasonLabel(254)) < 24);   // PlatformCrashRecord::reason[24]

  // The checksum covers every byte after the two header words, and not the checksum itself.
  CrashRtc a = sample();
  assert(a.check == crashCheck(a));
  CrashRtc b = a; b.epc1++;
  assert(b.check != crashCheck(b));
  b = a; b.fw[0] = 'x';
  assert(b.check != crashCheck(b));
  b = a; b.flags = CRASH_PENDING;
  assert(b.check != crashCheck(b));

  // A firmware field with no terminator is cut, and the copy always ends in one.
  {
    CrashRtc u; memset(&u, 'x', sizeof(u));
    char out[16]; memset(out, 'y', sizeof(out));
    crashCopyFw(out, sizeof(out), u);
    assert(strlen(out) == 15 && out[15] == 0);
    char small[4];
    crashCopyFw(small, sizeof(small), sample());
    assert(!strcmp(small, "3.2"));
    crashCopyFw(out, sizeof(out), sample());
    assert(!strcmp(out, "3.2.0"));
  }

  // Abort and stack overflow carry no registers; the other reasons do.
  assert(!crashHasRegisters(254) && !crashHasRegisters(253));
  assert(crashHasRegisters(1) && crashHasRegisters(2) && crashHasRegisters(3) && crashHasRegisters(0));

  // Boot decisions.
  assert(crashBootAction(true, true, 4)  == CRASH_ADOPT);    // abort(): comes back as a software restart
  assert(crashBootAction(true, true, 2)  == CRASH_ADOPT);    // exception
  assert(crashBootAction(true, true, 3)  == CRASH_ADOPT);    // software watchdog
  assert(crashBootAction(true, false, 1) == CRASH_CAPTURE);  // hardware watchdog: no callback ran
  assert(crashBootAction(false, false, 1) == CRASH_CAPTURE);
  assert(crashBootAction(true, false, 2) == CRASH_CAPTURE);  // exception whose callback write was lost
  assert(crashBootAction(true, false, 4) == CRASH_KEEP);     // normal software restart keeps the record
  assert(crashBootAction(true, false, 6) == CRASH_KEEP);     // external reset
  assert(crashBootAction(false, false, 0) == CRASH_KEEP);    // power on, nothing stored
  assert(crashBootAction(true, false, 0) == CRASH_KEEP);

  // `more` is outside the checksum: raising it must not invalidate the record.
  {
    CrashRtc m = sample();
    m.more = 5;
    m.check = crashCheck(m);
    CrashRtc n = m; n.more = 6;
    assert(crashCheck(n) == m.check);
    // every other new field is inside it
    n = m; n.failSize++;   assert(crashCheck(n) != m.check);
    n = m; n.failAddr++;   assert(crashCheck(n) != m.check);
    n = m; n.heapBlock++;  assert(crashCheck(n) != m.check);
    n = m; n.low++;        assert(crashCheck(n) != m.check);
    n = m; n.trace[7]++;   assert(crashCheck(n) != m.check);
    n = m; n.trace[0]++;   assert(crashCheck(n) != m.check);
  }

  // 16 bit fields: clamped, and 0xFFFF is "block not known".
  assert(crashClamp16(0) == 0 && crashClamp16(65535) == 65535 && crashClamp16(65536) == 65535 && crashClamp16(1u << 31) == 65535);
  assert(crashBlockField(100) == 100);
  // a real block is never turned into the "unknown" marker
  assert(crashBlockField(0xFFFE) == 0xFFFE && crashBlockField(0xFFFF) == 0xFFFE && crashBlockField(70000) == 0xFFFE);
  assert(crashBlockKnown(crashBlockField(0xFFFF)) && !crashBlockKnown(0xFFFF));

  // The head is written before the heap walk: block unknown, detail flagged.
  {
    CrashRtc h; memset(&h, 0xAA, sizeof(h));
    crashFillHead(h, 254, 0, 0, 0, 91, 3456, 756, 0x40201abc, 3000, 576, 4);
    assert(h.magic == CRASH_MAGIC && h.flags == (CRASH_PENDING | CRASH_DETAIL));
    assert(h.rstReason == 254 && h.uptimeS == 91 && h.uptimeKnown == 1);
    assert((h.heapBlock & 0xFFFF) == 3456 && (h.heapBlock >> 16) == 0xFFFF);
    assert(!crashBlockKnown(h.heapBlock >> 16));
    assert(h.failSize == 756 && h.failAddr == 0x40201abc);
    assert((h.low & 0xFFFF) == 3000 && (h.low >> 16) == 576);
    assert(h.more == 4);   // the count of earlier crashes is kept
    // then the walk finished: the block goes in, the heap half stays
    crashSealBlock(h, 576);
    assert((h.heapBlock & 0xFFFF) == 3456 && (h.heapBlock >> 16) == 576);
    assert(h.check == crashCheck(h));
    assert(crashBlockKnown(h.heapBlock >> 16));
  }

  // Adopting a record drops "pending" and keeps what else the flags say.
  assert(crashAdoptFlags(CRASH_PENDING | CRASH_DETAIL) == CRASH_DETAIL);
  assert(crashAdoptFlags(CRASH_PENDING) == 0);
  assert(crashAdoptFlags(CRASH_DETAIL) == CRASH_DETAIL);

  // Earlier crashes that were never cleared are counted, with the new one kept.
  assert(crashMoreAfter(false, 9) == 0);        // no valid record: nothing earlier
  assert(crashMoreAfter(true, 0) == 1);
  assert(crashMoreAfter(true, 41) == 42);
  assert(crashMoreAfter(true, 65535) == 65535);   // saturates
  assert(crashMoreAfter(true, 0xFFFFFFFFu) == 65535);

  // Trace: the first eight words that look like code, in stack order.
  {
    uint32_t stack[] = {0x3ffe8000, 0x40201000, 5, 0x40100010, 0x40300000, 0x4010ffff, 0x40110000, 0x402fffff,
                        0x40200000, 0x40201001, 0x40201002, 0x40201003, 0x40201004};
    uint32_t out[CRASH_TRACE_WORDS];
    unsigned n = crashCollectTrace(stack, sizeof(stack) / 4, out);
    assert(n == 8);
    assert(out[0] == 0x40201000 && out[1] == 0x40100010 && out[2] == 0x4010ffff && out[3] == 0x402fffff);
    assert(out[4] == 0x40200000 && out[5] == 0x40201001 && out[6] == 0x40201002 && out[7] == 0x40201003);
    uint32_t few[] = {1, 0x40201000};
    n = crashCollectTrace(few, 2, out);
    assert(n == 1 && out[0] == 0x40201000 && out[1] == 0 && out[7] == 0);   // the rest is zero, not stale
    assert(crashCollectTrace(nullptr, 0, out) == 0 && out[0] == 0);
    assert(!crashCodeAddress(0x401fffff) && crashCodeAddress(0x40200000) && !crashCodeAddress(0x40300000));
    assert(crashCodeAddress(0x40100000) && !crashCodeAddress(0x40110000) && !crashCodeAddress(0x400fffff));
  }

  // Low-water: the first 20 ticks after a reset are left out; then the minimum.
  {
    CrashLowWater w;
    w.reset();
    assert(!w.any());
    for (unsigned i = 0; i < CRASH_QUIET_TICKS; i++) w.sample(100, 50);
    assert(!w.any());
    w.sample(5000, 4000); w.sample(3000, 4500); w.sample(4000, 900);
    assert(w.any() && w.heap() == 3000 && w.block() == 900);
    w.sample(70000, 70000);
    assert(w.heap() == 3000 && w.block() == 900);
    w.reset();
    assert(!w.any());
    for (unsigned i = 0; i < CRASH_QUIET_TICKS; i++) w.sample(100, 50);
    w.sample(65536, 1);   // a clamped heap is not a low
    assert(w.heap() == 65535 && w.block() == 1);
  }

  // The heap walk behind the largest block runs with interrupts off, so it is
  // taken once in CRASH_BLOCK_EVERY ticks (1 s); the free heap is a counter
  // read and is taken on every tick (100 ms).
  {
    assert(CRASH_BLOCK_EVERY == 10);
    CrashLowWater w;
    w.reset();
    // the quiet ticks ask for no walk at all
    for (unsigned i = 0; i < CRASH_QUIET_TICKS; i++) { assert(!w.blockDue()); w.sampleHeap(100); }
    assert(!w.any());
    // the first tick after them walks at once, so the block is known as soon as the heap is
    assert(w.blockDue());
    w.sample(5000, 4000);
    assert(w.heap() == 5000 && w.block() == 4000);
    // then heap only, for CRASH_BLOCK_EVERY - 1 ticks; the heap low is seen on each, the block stays
    for (unsigned i = 1; i < CRASH_BLOCK_EVERY; i++) {
      assert(!w.blockDue());
      w.sampleHeap(5000 - 100 * i);
      assert(w.heap() == 5000 - 100 * i && w.block() == 4000);
    }
    assert(w.blockDue());           // the tenth
    w.sample(4500, 900);
    assert(w.heap() == 4100 && w.block() == 900);
    assert(!w.blockDue());
    w.sampleHeap(70000);            // a big heap is not a low, and no walk is due yet
    assert(w.heap() == 4100 && w.block() == 900);
    w.reset();                      // a reset starts the quiet ticks and the schedule again
    assert(!w.any() && !w.blockDue());
    // a heap-only tick below the block value moves the heap low, never the block low
    w.reset();
    for (unsigned i = 0; i < CRASH_QUIET_TICKS; i++) w.sampleHeap(100);
    w.sample(5000, 4000);
    w.sampleHeap(300);
    assert(w.heap() == 300 && w.block() == 4000);
    // how many walks in 1000 ticks: none in the quiet 20, then every 10th
    w.reset();
    unsigned walks = 0;
    for (unsigned i = 0; i < 1000; i++) {
      if (w.blockDue()) { walks++; w.sample(2000, 1000); } else w.sampleHeap(2000);
    }
    assert(walks == (1000 - CRASH_QUIET_TICKS + CRASH_BLOCK_EVERY - 1) / CRASH_BLOCK_EVERY);   // 98
  }

  std::cout << "crash record tests passed\n";
  return 0;
}
