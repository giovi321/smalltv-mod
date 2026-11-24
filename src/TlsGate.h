// TlsGate.h — one heap check for every TLS handshake the ticker, the usage
// client and the update check start (#132, #134).
//
// The closed Wi-Fi SDK allocates while it handles beacons and does not check
// the result: once a handshake leaves it about 1 KB free (200 B largest block)
// it writes through NULL (exc 29, memcpy at 0x4000df64). The old gates looked
// at the total free heap only and left no room for the SDK. A handshake now
// starts only when the total covers its cost plus WifiReserveFreeHeap and the
// largest block can hold its biggest single allocation; otherwise the fetch
// is skipped and retried on its normal backoff.
#pragma once
#include <Arduino.h>
#include "WifiReserve.h"

// Heap one HTTPS fetch takes beyond its receive buffer: the handshake, the
// request, the reply and its parse, with the SDK's own buffers meanwhile.
// Measured on a SmallTV-ultra as free heap before the fetch minus the lowest
// point during it, Yahoo at a 512 B receive buffer (standard image +
// UMM_STATS_FULL): on 3.10.0 with the theme released, largest 15,504 B over
// 39 fetches (resumption saves almost nothing); on 3.12.0 + #141 with a theme
// in the carousel, largest 16,208 B over 31 fetches of a 1d chart at 15m
// candles. 16,208 B less the buffer, rounded up. #135 has the tables.
#define TLS_HANDSHAKE_COST 15700u
// The largest single allocation of that fetch, measured on the same run: the
// 6,200 B BearSSL stack thunk, in every fetch. Plus the allocator's header,
// rounded up.
#define TLS_LARGEST_BLOCK   6400u

// True when a handshake with an `rxBuf`-byte receive buffer fits and still
// leaves WifiReserveFreeHeap (WifiReserve.h), and the largest block holds
// `needBlock` (a host with a heavier handshake asks for more). A theme kept
// loaded beside the caller (the carousel keeps it across pages) is released
// first when the heap is short; it reloads when its page is shown again.
// Records what it saw for /api/status.
bool tlsHeapReady(uint32_t rxBuf, uint32_t needBlock = TLS_LARGEST_BLOCK);

// What the last check saw, and how many handshakes it skipped since boot.
// `free` is 0 until the first check.
struct TlsGateStats {
  uint32_t free = 0, block = 0;
  uint16_t skipped = 0;
};
const TlsGateStats& tlsGateStats();
