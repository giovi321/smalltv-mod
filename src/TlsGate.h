// TlsGate.h — one heap check for every TLS handshake the ticker, the usage
// client and the update check start (#132, #134).
//
// The closed Wi-Fi SDK allocates while it handles beacons and does not check
// the result: once a handshake leaves it about 1 KB free (200 B largest block)
// it writes through NULL (exc 29, memcpy at 0x4000df64). The old gates looked
// at the total free heap only and left no room for the SDK. A handshake now
// starts only when the total covers its cost plus WIFI_SDK_RESERVE and the
// largest block can hold its biggest single allocation; otherwise the fetch
// is skipped and retried on its normal backoff.
#pragma once
#include <Arduino.h>

// Heap the Wi-Fi SDK must still have once a handshake is up. An estimate, not
// a measured floor: the SDK crashed at ~1.1 KB free on a SmallTV-ultra (#134),
// and 4 KB leaves margin. The measurement on #132 replaces it here.
#define WIFI_SDK_RESERVE    4000u
// A static-RSA BearSSL handshake beyond its receive buffer: the 6.2 KB stack
// thunk, the client context (~3.7 KB), the 512 B send buffer with the I/O
// overheads, and HTTPClient plus the reply parse. Also an estimate.
#define TLS_HANDSHAKE_COST 12500u
// The stack thunk is one 6.2 KB allocation; the largest free block must hold it.
#define TLS_LARGEST_BLOCK   8000u

// True when a handshake with an `rxBuf`-byte receive buffer fits and still
// leaves WIFI_SDK_RESERVE. A theme kept loaded beside the caller (the carousel
// keeps it across pages) is released first when the heap is short; it reloads
// when its page is shown again. Records what it saw for /api/status.
bool tlsHeapReady(uint32_t rxBuf);

// What the last check saw, and how many handshakes it skipped since boot.
// `free` is 0 until the first check.
struct TlsGateStats {
  uint32_t free = 0, block = 0;
  uint16_t skipped = 0;
};
const TlsGateStats& tlsGateStats();
