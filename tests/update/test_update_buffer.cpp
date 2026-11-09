// c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc tests/update/test_update_buffer.cpp -o /tmp/smalltv-update-buffer-tests
// && /tmp/smalltv-update-buffer-tests
//
// The rule that decides whether a firmware upload can start (src/UpdateBuffer.h):
// the ESP8266 core's Updater takes one buffer in one piece, and the size of
// that buffer depends on the free heap. All numbers below follow the core's
// code (cores/esp8266/Updater.cpp, Updater::begin, and umm_malloc.cpp).
#include "UpdateBuffer.h"
#include <cstdio>
#include <cstring>

using namespace updatebuf;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// The core: getFreeHeap() > 2 * 4096 gives a 4096 B buffer, else 256 B.
static void testBufferWanted() {
  CHECK(bufferWanted(0) == 256);
  CHECK(bufferWanted(8191) == 256);
  CHECK(bufferWanted(8192) == 256);    // not "more than": exactly 8 KB is still small
  CHECK(bufferWanted(8193) == 4096);
  CHECK(bufferWanted(19600) == 4096);
  CHECK(bufferWanted(45000) == 4096);
}

// umm_malloc: a request of n bytes takes 2 + (n - 1) / 8 blocks of 8 B, and
// getMaxFreeBlockSize() reports whole blocks (8 B each).
static void testBlockNeeded() {
  CHECK(blockNeeded(4096) == 4104);   // 513 blocks
  CHECK(blockNeeded(256) == 264);     // 33 blocks
  CHECK(blockNeeded(1) == 16);        // 2 blocks
  CHECK(blockNeeded(8) == 16);
  CHECK(blockNeeded(9) == 24);
}

static void testCanStart() {
  // The case from the device: 19.6 KB free, largest block 3,888 B. Refused.
  CHECK(!uploadCanStart(19600, 3888));
  // Big heap, block just under / at / over the 4,104 B the buffer takes.
  CHECK(!uploadCanStart(20000, 4096));
  CHECK(!uploadCanStart(20000, 4103));
  CHECK(uploadCanStart(20000, 4104));
  CHECK(uploadCanStart(20000, 4105));
  CHECK(uploadCanStart(20000, 19000));
  // Free heap at the threshold: 8193 asks for 4 KB, 8192 only for 256 B.
  CHECK(!uploadCanStart(8193, 4000));
  CHECK(uploadCanStart(8193, 4104));
  CHECK(uploadCanStart(8192, 264));     // small buffer: block at the limit
  CHECK(!uploadCanStart(8192, 263));    // ... and one under it
  CHECK(uploadCanStart(8192, 1000));    // a 4 KB block is not needed here
  CHECK(uploadCanStart(5000, 300));
  CHECK(!uploadCanStart(5000, 200));
  CHECK(!uploadCanStart(0, 0));
}

static void testMessage() {
  char buf[200];
  size_t n = refusalText(buf, sizeof(buf), 19600, 3888);
  CHECK(n > 0 && n == std::strlen(buf));
  CHECK(std::strstr(buf, "not enough memory in one piece") != nullptr);
  CHECK(std::strstr(buf, "3888") != nullptr);     // the largest block
  CHECK(std::strstr(buf, "4104") != nullptr);     // what the upload needs
  CHECK(std::strstr(buf, "19600") != nullptr);    // free heap
  CHECK(std::strstr(buf, "Restart the device") != nullptr);
  CHECK(std::strstr(buf, "upload again") != nullptr);
  // The small-buffer case names the small need.
  refusalText(buf, sizeof(buf), 5000, 200);
  CHECK(std::strstr(buf, "264") != nullptr);
  CHECK(std::strstr(buf, "4104") == nullptr);
  // A buffer that is too small never overruns and stays terminated.
  char tiny[16];
  std::memset(tiny, 'x', sizeof(tiny));
  n = refusalText(tiny, sizeof(tiny), 19600, 3888);
  CHECK(std::strlen(tiny) < sizeof(tiny));
  CHECK(n == std::strlen(tiny));          // the length returned is what was written
  CHECK(refusalText(nullptr, 10, 1, 1) == 0);
  CHECK(refusalText(tiny, 0, 1, 1) == 0);
  // The longest text fits the 200 B the handler uses.
  n = refusalText(buf, sizeof(buf), 4294967295u, 4294967295u);
  CHECK(n < 190);
}

int main() {
  testBufferWanted();
  testBlockNeeded();
  testCanStart();
  testMessage();
  if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
  std::printf("update buffer tests: all passed\n");
  return 0;
}
