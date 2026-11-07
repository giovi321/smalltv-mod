// The Home Assistant screen store and the MQTT client are taken from the heap
// once, at boot, and only when Home Assistant is wanted (a broker host is set).
// This runs the real HaBoot.cpp, HaScreens.cpp and MqttClient.cpp on the host
// against stand-in core headers, with a heap that can be made to run out.
//
// What must hold:
//  - Off: nothing is taken from the heap, and every path that touches the
//    store or the client is harmless (no crash, no screens, no connection).
//  - Ready: the store is ONE block of HA_MAX_SCREENS screens, the client is
//    built once, and screens work as before.
//  - Out of memory (at the store, at the client, or in the client's own
//    receive buffer): the state says so, everything taken is given back, and
//    every path is still harmless.
//  - Safe mode takes nothing.
// "Every path" = each public function of the store and the client, as the
// MQTT callback, the web UI (/api/ha/clear), the mode's render, the loop and
// the status code call them.
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include "Settings.h"
#include "Net.h"
#include "MqttClient.h"
#include "HaBoot.h"
#include "HaScreens.h"

// ---- stand-ins the real sources link against --------------------------------
FSClass LittleFS;
EspClass ESP;
bool g_pubsubBufferFails = false;
int  g_pubsubLive = 0;
int  g_pubsubConnects = 0;
int  g_pubsubLoops = 0;
void (*g_pubsubCallback)(char*, uint8_t*, unsigned int) = nullptr;

static uint32_t g_now = 0;
uint32_t millis() { return g_now; }

static bool g_staUp = true;
NetMode netMode() { return NET_STA; }
bool netConnected() { return g_staUp; }
int g_appReleases = 0;
void appApplyBrightness() {}
void appRelease() { g_appReleases++; }

// ---- a heap that can run out ---------------------------------------------------
// Only the nothrow forms are replaced: that is the form the device code must
// use (the ESP8266 core has no exceptions, and a plain `new` that fails
// panics). Every block taken this way is listed, so "given back" can be
// checked, and the Nth one from arming can be made to fail.
struct Block { void* p; size_t n; };
static Block g_blocks[32];
static int    g_failIn = 0;      // 1 = the next one fails, 2 = the one after, ... 0 = never
static int    g_seen = 0;        // nothrow allocations tried since reset
static size_t g_firstSize = 0;   // size of the first one tried

static void* takeBlock(size_t n) {
  g_seen++;
  if (g_seen == 1) g_firstSize = n;
  if (g_failIn && --g_failIn == 0) return nullptr;
  void* p = malloc(n ? n : 1);
  if (!p) return nullptr;
  for (Block& b : g_blocks)
    if (!b.p) { b.p = p; b.n = n; return p; }
  return p;   // table full: not tracked
}
static void giveBlock(void* p) {
  if (!p) return;
  for (Block& b : g_blocks)
    if (b.p == p) { b.p = nullptr; b.n = 0; break; }
  free(p);
}
static size_t liveBytes() { size_t t = 0; for (const Block& b : g_blocks) if (b.p) t += b.n; return t; }
static int    liveBlocks() { int t = 0; for (const Block& b : g_blocks) if (b.p) t++; return t; }

void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return takeBlock(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return takeBlock(n); }
void operator delete(void* p) noexcept { giveBlock(p); }
void operator delete[](void* p) noexcept { giveBlock(p); }
void operator delete(void* p, std::size_t) noexcept { giveBlock(p); }
void operator delete[](void* p, std::size_t) noexcept { giveBlock(p); }

// ---- checks ------------------------------------------------------------------------
static int g_failed = 0, g_checks = 0;
#define CHECK(cond, ...)                                                   \
  do {                                                                     \
    g_checks++;                                                            \
    if (!(cond)) {                                                         \
      g_failed++;                                                          \
      printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);               \
      printf(__VA_ARGS__);                                                 \
      printf("\n");                                                        \
    }                                                                      \
  } while (0)

// A message from the broker, through the callback the client was given.
static void deliver(const char* slot, const uint8_t* payload, size_t len) {
  CHECK(g_pubsubCallback != nullptr, "the client has a message callback");
  if (!g_pubsubCallback) return;
  char topic[64];
  snprintf(topic, sizeof topic, "smalltv/smalltv-test/screen/%s", slot);
  g_pubsubCallback(topic, const_cast<uint8_t*>(payload), (unsigned)len);
}

static const char kScreen[] = "{\"bg\":\"#000000\",\"draw\":[{\"t\":\"text\",\"x\":4,\"y\":4,\"s\":2,\"c\":\"#FFFFFF\",\"v\":\"hello\"}]}";

// One boot, from a clean heap. `host` is the broker host setting.
static HaState boot(Settings& s, const char* host, bool safe, int failIn = 0) {
  s.ha.brokerHost = host;
  g_failIn = failIn;
  g_seen = 0;
  g_firstSize = 0;
  g_pubsubCallback = nullptr;
  HaState st = haBoot(haWanted(s), safe);
  g_failIn = 0;
  return st;
}

// Every public path of the store and the client, once. With no store they
// must do nothing and answer "empty"; with a store they behave as before.
static void touchEveryPath(Settings& s, bool storeUp, const char* label) {
  haScreensBegin();
  haScreensReload();
  haScreensApply("kitchen", (const uint8_t*)kScreen, sizeof(kScreen) - 1);   // a retained screen
  haScreensApply("kitchen", nullptr, 0);                                    // its delete
  haScreensApply("hall", (const uint8_t*)kScreen, sizeof(kScreen) - 1);
  g_now += 5000;
  haScreensService();                                                       // the debounced write
  uint8_t live = haScreensLive();
  const HaScreen* first = haScreenAt(0);
  bool dirty = haScreensTakeDirty();
  CHECK(live == (storeUp ? 1 : 0), "%s: live screens %u", label, live);
  CHECK((first != nullptr) == storeUp, "%s: screen 0 %s", label, first ? "there" : "missing");
  if (storeUp) CHECK(first && !strcmp(first->name, "hall") && dirty, "%s: store works", label);
  else CHECK(!dirty, "%s: nothing changed, nothing dirty", label);

  // The client side: loop, status helpers, the web UI's publish.
  mqttLoop();
  g_now += 70000;
  mqttLoop();
  CHECK(!mqttConnected(), "%s: no broker link", label);
  CHECK(!mqttPublish("smalltv/test/screen/hall", "", true), "%s: nothing published", label);
  char names[HA_MAX_SCREENS][HA_SLOT_LEN];
  uint8_t cleared = haScreensClearAll(names, HA_MAX_SCREENS);                // /api/ha/clear
  CHECK(cleared == (storeUp ? 1 : 0), "%s: cleared %u", label, cleared);
  CHECK(haScreensLive() == 0 && haScreenAt(0) == nullptr, "%s: empty after clear", label);
  (void)s;
}

static void testOff() {
  Settings s; s.setDefaults();
  mqttBegin(s);
  HaState st = boot(s, "", false);
  CHECK(!haWanted(s), "off: a blank host is not wanted");
  CHECK(st == HA_STATE_OFF && haState() == HA_STATE_OFF, "off: state %d", (int)st);
  CHECK(liveBlocks() == 0 && liveBytes() == 0, "off: nothing taken from the heap (%zu B)", liveBytes());
  CHECK(g_seen == 0, "off: no allocation even tried (%d)", g_seen);
  CHECK(g_pubsubLive == 0, "off: no MQTT client built");
  touchEveryPath(s, false, "off");
  CHECK(g_pubsubConnects == 0, "off: never connects");
  CHECK(haProblem(false) == nullptr, "off and not wanted: no problem");
  CHECK(!strcmp(haProblem(true), "reboot"), "off but wanted since the save: restart needed");
}

static void testReady() {
  Settings s; s.setDefaults();
  mqttBegin(s);
  HaState st = boot(s, "broker.example", false);
  CHECK(haWanted(s), "ready: a host is wanted");
  s.ha.brokerHost = "x";
  CHECK(haWanted(s), "ready: even a one-letter host is wanted");
  s.ha.brokerHost = "broker.example";
  CHECK(st == HA_STATE_READY && haState() == HA_STATE_READY, "ready: state %d", (int)st);
  // The store is ONE block of exactly the configured screens.
  CHECK(g_firstSize == sizeof(HaScreen) * HA_MAX_SCREENS, "ready: store block %zu B, expected %zu B",
        g_firstSize, sizeof(HaScreen) * HA_MAX_SCREENS);
  CHECK(liveBlocks() == 2, "ready: two blocks (store, client), %d live", liveBlocks());
  CHECK(g_pubsubLive == 1, "ready: one MQTT client");
  CHECK(g_pubsubCallback != nullptr, "ready: message callback set");
  touchEveryPath(s, true, "ready");
  CHECK(g_pubsubConnects >= 1, "ready: tries the broker");
  CHECK(haProblem(true) == nullptr, "ready and wanted: no problem");
  CHECK(!strcmp(haProblem(false), "reboot"), "ready but host cleared since: restart frees it");
  // A retained screen arriving through the client's callback lands in the store.
  deliver("zone", (const uint8_t*)kScreen, sizeof(kScreen) - 1);
  CHECK(haScreensLive() == 1, "ready: screen from the callback");
  // Taking the host away does not free anything before the next boot.
  s.ha.brokerHost = "";
  mqttLoop();
  CHECK(liveBlocks() == 2 && haState() == HA_STATE_READY, "ready: host cleared mid-run keeps memory until restart");
}

static void testStoreFails() {
  Settings s; s.setDefaults();
  mqttBegin(s);
  HaState st = boot(s, "broker.example", false, 1);   // the store block is refused
  CHECK(st == HA_STATE_NO_MEMORY && haState() == HA_STATE_NO_MEMORY, "store fails: state %d", (int)st);
  CHECK(liveBlocks() == 0 && liveBytes() == 0, "store fails: nothing left taken (%zu B)", liveBytes());
  CHECK(g_pubsubLive == 0, "store fails: no client built (an earlier test's client is dropped too)");
  touchEveryPath(s, false, "store fails");
  CHECK(!strcmp(haProblem(true), "memory"), "store fails: Status says memory");
  CHECK(haProblem(false) == nullptr, "store fails: host cleared, no problem to show");
}

static void testClientFails() {
  Settings s; s.setDefaults();
  mqttBegin(s);
  HaState st = boot(s, "broker.example", false, 2);   // store fine, client object refused
  CHECK(st == HA_STATE_NO_MEMORY, "client fails: state %d", (int)st);
  CHECK(liveBlocks() == 0 && liveBytes() == 0, "client fails: the store is given back (%zu B)", liveBytes());
  CHECK(g_pubsubLive == 0, "client fails: no client left");
  touchEveryPath(s, false, "client fails");
  CHECK(!strcmp(haProblem(true), "memory"), "client fails: Status says memory");
}

static void testClientBufferFails() {
  Settings s; s.setDefaults();
  mqttBegin(s);
  g_pubsubBufferFails = true;   // the object is built, its receive buffer is not
  HaState st = boot(s, "broker.example", false);
  g_pubsubBufferFails = false;
  CHECK(st == HA_STATE_NO_MEMORY, "buffer fails: state %d", (int)st);
  CHECK(liveBlocks() == 0 && liveBytes() == 0, "buffer fails: the store is given back (%zu B)", liveBytes());
  CHECK(g_pubsubLive == 0, "buffer fails: the half-built client is destroyed");
  touchEveryPath(s, false, "buffer fails");
  CHECK(!strcmp(haProblem(true), "memory"), "buffer fails: Status says memory");
}

static void testSafeMode() {
  Settings s; s.setDefaults();
  mqttBegin(s);
  HaState st = boot(s, "broker.example", true);
  CHECK(st == HA_STATE_SAFE_MODE && haState() == HA_STATE_SAFE_MODE, "safe: state %d", (int)st);
  CHECK(g_seen == 0 && liveBlocks() == 0, "safe: nothing taken");
  touchEveryPath(s, false, "safe");
  CHECK(!strcmp(haProblem(true), "safe"), "safe: Status says so");
}

// A client without a store must not be possible by design; if it ever were,
// a screen from the broker must still not crash (belt and braces).
static void testClientWithoutStore() {
  Settings s; s.setDefaults();
  s.ha.brokerHost = "broker.example";
  mqttBegin(s);
  boot(s, "", false);   // clean state: off
  CHECK(mqttAlloc(), "client alone: built");
  deliver("zone", (const uint8_t*)kScreen, sizeof(kScreen) - 1);
  deliver("zone", nullptr, 0);
  CHECK(haScreensLive() == 0, "client alone: a screen is dropped, no crash");
  mqttFree();
  CHECK(g_pubsubLive == 0 && liveBlocks() == 0, "client alone: freed");
  mqttFree();   // twice is harmless
}

// A second boot in the same run (the tests share globals) starts clean.
static void testRetryAfterFailure() {
  Settings s; s.setDefaults();
  mqttBegin(s);
  CHECK(boot(s, "broker.example", false, 1) == HA_STATE_NO_MEMORY, "retry: first boot fails");
  CHECK(boot(s, "broker.example", false) == HA_STATE_READY, "retry: next boot works");
  CHECK(liveBlocks() == 2, "retry: store and client taken");
  CHECK(haProblem(true) == nullptr, "retry: no problem after a good boot");
  // The saved screens are loaded once per store: the first load marks the
  // renderer dirty, and a store given back and taken again loads again.
  haScreensBegin();
  CHECK(haScreensTakeDirty(), "retry: the new store loads the saved screens");
  boot(s, "", false);   // back to off for the next test
  CHECK(liveBlocks() == 0, "retry: off again gives everything back, %d live", liveBlocks());
}

// The mode starts before or without a store: that must not use up the one load.
static void testBeginBeforeStore() {
  Settings s; s.setDefaults();
  mqttBegin(s);
  boot(s, "", false);                       // nothing held
  haScreensBegin();                         // no store yet
  haScreensTakeDirty();
  CHECK(haScreensAlloc(), "begin first: store taken later");
  haScreensBegin();
  CHECK(haScreensTakeDirty(), "begin first: the load still happens once the store exists");
  haScreensFree();
  CHECK(liveBlocks() == 0, "begin first: store given back");
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not swallow the lines before it
  testOff();
  testReady();
  testStoreFails();
  testClientFails();
  testClientBufferFails();
  testSafeMode();
  testClientWithoutStore();
  testRetryAfterFailure();
  testBeginBeforeStore();
  if (g_failed) {
    printf("ha alloc tests: %d of %d checks FAILED\n", g_failed, g_checks);
    return 1;
  }
  printf("ha alloc tests passed: %d checks\n", g_checks);
  return 0;
}
