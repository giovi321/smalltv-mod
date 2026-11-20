#include "TlsGate.h"
#include "Platform.h"

// main.cpp: frees a theme the carousel keeps loaded, when the heap is short.
extern void appReleaseForTls(uint32_t needFree, uint32_t needBlock);

static TlsGateStats g_stats;

bool tlsHeapReady(uint32_t rxBuf) {
  const uint32_t needFree = rxBuf + TLS_HANDSHAKE_COST + WifiReserveFreeHeap;
  appReleaseForTls(needFree, TLS_LARGEST_BLOCK);
  g_stats.free = ESP.getFreeHeap();
  g_stats.block = platformMaxFreeBlock();
  if (g_stats.free >= needFree && g_stats.block >= TLS_LARGEST_BLOCK) return true;
  if (g_stats.skipped < 0xFFFF) g_stats.skipped++;
  return false;
}

const TlsGateStats& tlsGateStats() { return g_stats; }
