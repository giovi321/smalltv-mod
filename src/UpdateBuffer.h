// UpdateBuffer.h -- can a firmware upload start on this heap? (ESP8266)
//
// The ESP8266 core's Updater (cores/esp8266/Updater.cpp, Updater::begin,
// core 3.1.2 lines 172-181) picks its write buffer from the TOTAL free heap:
//
//   if (ESP.getFreeHeap() > 2 * FLASH_SECTOR_SIZE)   // more than 8192 B
//     _bufferSize = FLASH_SECTOR_SIZE;               // 4096 B
//   else
//     _bufferSize = 256;
//   _buffer = new (std::nothrow) uint8_t[_bufferSize];
//   if (!_buffer) { _setError(UPDATE_ERROR_OOM); ... return false; }
//
// It never falls back to the small size when the big one does not fit. On a
// fragmented heap the free total can be large and the largest free block
// small (19.6 KB free, largest block 3,888 B), and the upload is refused.
//
// The allocator (cores/esp8266/umm_malloc/umm_malloc.cpp, umm_blocks()) takes
// 2 + (n - 1) / 8 blocks of 8 B for a request of n bytes, and
// ESP.getMaxFreeBlockSize() reports whole blocks. So a 4096 B request needs
// a free run of 513 blocks = 4104 B.
//
// Pure functions, no Arduino types: tests/update/test_update_buffer.cpp builds
// them on the host. WebPortal.cpp calls them for the ESP8266 only; the ESP32
// updater has no such rule.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// On the device the format text stays in flash (about 150 B of static RAM
// saved); on the host it is a plain snprintf.
#if defined(ARDUINO)
#define UPDATEBUF_SNPRINTF(buf, cap, fmt, ...) snprintf_P(buf, cap, PSTR(fmt), __VA_ARGS__)
#else
#define UPDATEBUF_SNPRINTF(buf, cap, fmt, ...) snprintf(buf, cap, fmt, __VA_ARGS__)
#endif

namespace updatebuf {

constexpr uint32_t kSector = 4096;      // FLASH_SECTOR_SIZE
constexpr uint32_t kSmallBuffer = 256;
constexpr uint32_t kBlock = 8;          // sizeof(umm_block)

// The buffer size the core will ask for.
static inline uint32_t bufferWanted(uint32_t freeHeap) {
  return freeHeap > 2 * kSector ? kSector : kSmallBuffer;
}

// The free run (as getMaxFreeBlockSize() reports it) that a request of
// `bytes` needs.
static inline uint32_t blockNeeded(uint32_t bytes) {
  return (2 + (bytes - 1) / kBlock) * kBlock;
}

// True when the buffer the core will ask for fits into the largest block.
static inline bool uploadCanStart(uint32_t freeHeap, uint32_t maxBlock) {
  return maxBlock >= blockNeeded(bufferWanted(freeHeap));
}

// The text the page shows when the upload is refused. Returns its length.
static inline size_t refusalText(char* buf, size_t cap, uint32_t freeHeap, uint32_t maxBlock) {
  if (!buf || !cap) return 0;
  int n = UPDATEBUF_SNPRINTF(buf, cap,
      "Upload not started: not enough memory in one piece (largest free block %lu B, "
      "needed %lu B, free memory %lu B). Restart the device, then upload again.",
      (unsigned long)maxBlock, (unsigned long)blockNeeded(bufferWanted(freeHeap)),
      (unsigned long)freeHeap);
  if (n < 0) { buf[0] = 0; return 0; }
  return (size_t)n < cap ? (size_t)n : cap - 1;
}

}  // namespace updatebuf
