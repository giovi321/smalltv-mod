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
  // The record is 12 words and must end inside the 128 user words from word 96.
  assert(sizeof(CrashRtc) == 48);
  assert(96 + sizeof(CrashRtc) / 4 <= 128);

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

  std::cout << "crash record tests passed\n";
  return 0;
}
