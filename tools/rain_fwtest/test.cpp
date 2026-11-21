// test.cpp — decodes one tile with the firmware's RainPng.cpp, on the host
#include "stub_Arduino.h"
#include "stub_FS.h"
#include "RainPng.h"
#include "RainHistoryMem.h"
#include <stdio.h>
#include <string.h>

// RAIN_HEAP_LOW=N: the decoder's heap-floor check reports low heap from its
// N-th call on (0 = right after the work area is allocated).
static long g_heapLowAt = -1, g_heapChecks = 0;
bool rainDecodeHeapLow() { return g_heapLowAt >= 0 && g_heapChecks++ >= g_heapLowAt; }

// RAIN_GET_MS=N: each history read of the small-memory mode costs N ms of
// virtual time (a LittleFS reopen on the device), and the decode has the
// device's budget, RAIN_BUDGET_MS (default 60000).
static long g_getMs = 0, g_budgetMs = 60000;
static RainHistoryMem* g_hist = nullptr;
bool rainDecodeOverTime() { return g_getMs > 0 && g_hist && (long)g_hist->gets() * g_getMs > g_budgetMs; }

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s in.png out.raw cropK [ring]\n", argv[0]);
    return 2;
  }
  File src(argv[1], "rb");
  File dst(argv[2], "wb");
  if (!src || !dst) return 2;
  int cropK = atoi(argv[3]);
  // "ring": the small-memory mode (RAIN_RING_PIXELS in RAM + history).
  const bool ring = argc > 4 && strcmp(argv[4], "ring") == 0;
  const size_t winLen = ring ? RAIN_RING_PIXELS : RAIN_WINDOW_PIXELS;   // exact size: ASan sees overruns
  uint8_t* win = new uint8_t[winLen];
  memset(win, 0, winLen);
  RainHistoryMem* hist = ring ? new RainHistoryMem : nullptr;
  // RAIN_WRITE_LIMIT=N: the output file takes N bytes, then writes come up short.
  if (const char* lim = getenv("RAIN_WRITE_LIMIT")) g_stubWriteLimit = atol(lim);
  if (const char* low = getenv("RAIN_HEAP_LOW")) g_heapLowAt = atol(low);
  if (const char* ms = getenv("RAIN_GET_MS")) g_getMs = atol(ms);
  if (const char* ms = getenv("RAIN_BUDGET_MS")) g_budgetMs = atol(ms);
  g_hist = hist;
  bool ok = rainDecodeToFs(src, dst, 256, cropK, win, hist);
  if (!ok && rainDecodeWriteFail()) printf("short write\n");
  if (!ok && rainDecodeLowHeap()) printf("low heap\n");
  if (!ok && rainDecodeTimedOut()) printf("too slow\n");
  if (hist && hist->faulted()) { printf("history contract broken\n"); ok = false; }
  if (hist) printf("history: %u segments stored, %u reads\n", hist->puts(), hist->gets());
  delete hist;
  delete[] win;
  printf("decode: %s, output=%u bytes, %lu yields\n", ok ? "OK" : "FAILED", dst.position(), g_yieldCount);
  return ok ? 0 : 1;
}
