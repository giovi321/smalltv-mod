// The memory margin after a theme load (issue #132).
//
// After the engine takes a theme (and the first frame has allocated the
// layers' texts), the device checks its free heap and its largest free block.
// If either is under the margin, the theme is dropped, the setting goes back
// to the previous mode, and the web UI and /api/status say so. The Wi-Fi code
// of the ESP8266 SDK writes through a null pointer when it runs out of memory
// (a crash with 1,160 B free and a 200 B block was recorded), and nothing in
// the firmware can catch that.
//
// Three parts:
//   1. ThemeMargin.h: the pure check and the watch that decides when to refuse.
//   2. ThemeRefusal.h: what the settings look like after a refusal.
//   3. The device-only code (ThemeMode.cpp, ThemeWeb.cpp, main.cpp,
//      WebPortal.cpp) cannot run here, so its wiring is checked in the source.
//
// The margin numbers come from the device measurement (reports in the pull
// request). These tests pin the constants by name and by their relations, not
// by value.
// c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme tests/theme/test_margin.cpp -o /tmp/smalltv-margin-tests
#include "ThemeMargin.h"
#include "ThemeRefusal.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace smalltv;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::string slurp(const char* path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
static int count(const std::string& hay, const std::string& needle) {
  int n = 0;
  for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) ++n;
  return n;
}
// The text of the function that starts with `head`, up to its closing brace in column 0.
static std::string body(const std::string& src, const std::string& head) {
  const size_t at = src.find(head);
  if (at == std::string::npos) return "";
  const size_t end = src.find("\n}", at);
  return src.substr(at, end == std::string::npos ? std::string::npos : end - at);
}
static bool before(const std::string& s, const std::string& a, const std::string& b) {
  const size_t pa = s.find(a), pb = s.find(b);
  return pa != std::string::npos && pb != std::string::npos && pa < pb;
}
// True when `needle` sits inside an `#if defined(SMALLTV_FAMILY_ESP8266)` block (no nesting in these files).
static bool onlyOnEsp8266(const std::string& s, const std::string& needle) {
  const size_t at = s.find(needle);
  if (at == std::string::npos) return false;
  const std::string guard = "defined(SMALLTV_FAMILY_ESP8266)";   // also inside "#if WITH_THEME && defined(...)"
  const size_t open = s.rfind(guard, at);
  if (open == std::string::npos) return false;
  const size_t between = s.find("#endif", open);
  return between != std::string::npos && between > at && s.find("#else", open) > at;
}
static unsigned long number(const std::string& s, const std::string& name) {
  const size_t at = s.find(name + " = ");
  if (at == std::string::npos) return 0;
  return std::strtoul(s.c_str() + at + name.size() + 3, nullptr, 10);
}

// ---- 1. The check ----------------------------------------------------------

static void testMemoryShort() {
  const MarginLimits l{4000, 1000};
  CHECK(!memoryShort(4000, 1000, l));    // exactly at the margin is enough
  CHECK(memoryShort(3999, 1000, l));     // free heap one under
  CHECK(memoryShort(4000, 999, l));      // block one under
  CHECK(!memoryShort(4001, 1001, l));    // one over, both
  CHECK(memoryShort(3999, 999, l));      // both short
  CHECK(memoryShort(100000, 999, l));    // a big total does not cover a small block
  CHECK(memoryShort(3999, 100000, l));   // and a big block does not cover a small total
  CHECK(!memoryShort(100000, 100000, l));
}

static const MarginLimits kLimits{4000, 1000};
static bool checkAt(MarginWatch& w, uint32_t now, uint32_t freeHeap, uint32_t block) {
  return w.check(now, freeHeap, block, kLimits);
}

static void testWatchIdleUntilStarted() {
  MarginWatch w;
  CHECK(!w.active());
  for (uint32_t t = 0; t < 2000; t += MarginWatch::GapMs) CHECK(!checkAt(w, t, 10, 10));   // never restarted: never refuses
}

static void testWatchRefusesAfterShortInARow() {
  MarginWatch w;
  w.restart(1000);
  uint32_t t = 1000;
  for (int i = 1; i < MarginWatch::ShortInARow; ++i) {
    CHECK(!checkAt(w, t, 3000, 1500));     // short, but not often enough yet
    t += MarginWatch::GapMs;
  }
  CHECK(checkAt(w, t, 3000, 1500));        // the last one of the row refuses
  CHECK(!w.active());                      // and the watch stops: one refusal per load
  CHECK(!checkAt(w, t + 1000, 3000, 1500));
}

static void testWatchCountsOnlyReadingsApart() {
  MarginWatch w;
  w.restart(0);
  // Readings closer than GapMs are one reading: a burst of frames must not
  // count as a row of three.
  CHECK(!checkAt(w, 0, 100, 100));
  CHECK(!checkAt(w, 1, 100, 100));
  CHECK(!checkAt(w, 2, 100, 100));
  CHECK(!checkAt(w, MarginWatch::GapMs - 1, 100, 100));
  CHECK(w.active());
}

static void testWatchOneGoodReadingRestartsTheCount() {
  MarginWatch w;
  w.restart(0);
  uint32_t t = 0;
  for (int round = 0; round < 3; ++round) {
    for (int i = 1; i < MarginWatch::ShortInARow; ++i) { CHECK(!checkAt(w, t, 10, 10)); t += MarginWatch::GapMs; }
    CHECK(!checkAt(w, t, 8000, 4000));      // a transient: a good reading between
    t += MarginWatch::GapMs;
  }
  CHECK(w.active());
}

static void testWatchEitherValueCounts() {
  {
    MarginWatch w;
    w.restart(0);
    uint32_t t = 0;
    bool refused = false;
    for (int i = 0; i < MarginWatch::ShortInARow; ++i, t += MarginWatch::GapMs) refused = checkAt(w, t, 3999, 5000);
    CHECK(refused);
  }
  {
    MarginWatch w;
    w.restart(0);
    uint32_t t = 0;
    bool refused = false;
    for (int i = 0; i < MarginWatch::ShortInARow; ++i, t += MarginWatch::GapMs) refused = checkAt(w, t, 9000, 999);
    CHECK(refused);
  }
  {
    MarginWatch w;
    w.restart(0);
    uint32_t t = 0;
    bool refused = false;
    for (int i = 0; i < 20; ++i, t += MarginWatch::GapMs) refused = refused || checkAt(w, t, 4000, 1000);
    CHECK(!refused);                          // at the margin exactly: no refusal
  }
}

static void testWatchSettleTime() {
  MarginWatch w;
  w.restart(5000);
  // Short, but only after the settle time is over: nothing happens, and the watch ends.
  CHECK(!checkAt(w, 5000 + MarginWatch::SettleMs, 10, 10));
  CHECK(!w.active());
  CHECK(!checkAt(w, 5000 + MarginWatch::SettleMs + 1000, 10, 10));
  // The last moment inside the window still counts.
  MarginWatch v;
  v.restart(5000);
  CHECK(v.active());
  uint32_t t = 5000 + MarginWatch::SettleMs - 1 - 2 * MarginWatch::GapMs;
  CHECK(!checkAt(v, t, 10, 10));
  CHECK(!checkAt(v, t + MarginWatch::GapMs, 10, 10));
  CHECK(checkAt(v, t + 2 * MarginWatch::GapMs, 10, 10));
}

static void testWatchRestartsAfterAFetchResult() {
  MarginWatch w;
  w.restart(0);
  CHECK(!checkAt(w, MarginWatch::SettleMs + 5, 100, 100));     // over
  CHECK(!w.active());
  w.restart(20000);                                            // a fetch result changed the texts
  CHECK(w.active());
  uint32_t t = 20000;
  bool refused = false;
  for (int i = 0; i < MarginWatch::ShortInARow; ++i, t += MarginWatch::GapMs) refused = checkAt(w, t, 100, 100);
  CHECK(refused);
}

static void testWatchRestartClearsTheCount() {
  MarginWatch w;
  w.restart(0);
  uint32_t t = 0;
  for (int i = 1; i < MarginWatch::ShortInARow; ++i, t += MarginWatch::GapMs) CHECK(!checkAt(w, t, 10, 10));
  w.restart(t);                              // a new load
  CHECK(!checkAt(w, t, 10, 10));             // the old count is gone: this is the first
}

static void testWatchGapAcrossTheClockWrap() {
  // Readings 4 ms apart just before the wrap are one reading, not four: the
  // gap is a difference, never "now < last + gap" (which wraps to a small
  // number and lets every reading count).
  MarginWatch w;
  const uint32_t start = 0xFFFFFFF0u;
  w.restart(start);
  bool refused = false;
  for (uint32_t i = 0; i < 4; ++i) refused = checkAt(w, start + 4 * i, 10, 10) || refused;   // 0xFFFFFFF0 .. 0xFFFFFFFC
  CHECK(!refused);
  CHECK(w.active());
  // And across the wrap itself: 0xFFFFFFFC then 0x00000008 are 12 ms apart.
  CHECK(!checkAt(w, 0x00000008u, 10, 10));
  CHECK(w.active());
}

static void testWatchSurvivesClockWrap() {
  MarginWatch w;
  const uint32_t start = 0xFFFFFFFFu - 150;   // millis() wraps within the window
  w.restart(start);
  uint32_t t = start;
  bool refused = false;
  for (int i = 0; i < MarginWatch::ShortInARow; ++i, t += MarginWatch::GapMs) refused = checkAt(w, t, 10, 10);
  CHECK(refused);
}

// ---- 2. The settings after a refusal ----------------------------------------

static const uint8_t kTheme = 6, kStocks = 0, kCarousel = 3;

static Selection sel(const char* id, uint8_t mode, bool carousel = false) { return Selection{id, mode, carousel}; }

static void testRefusalRestoresAPreviousMode() {
  Selection s = sel("big", kTheme);
  CHECK(afterRefusal(s, true, kStocks, "pixel-room", "big", kTheme));
  CHECK(s.mode == kStocks);
  CHECK(s.themeId == "pixel-room");           // the old choice comes back too
}

static void testRefusalRestoresAPreviousTheme() {
  Selection s = sel("big", kTheme);
  CHECK(afterRefusal(s, true, kTheme, "pixel-room", "big", kTheme));
  CHECK(s.mode == kTheme && s.themeId == "pixel-room");
}

static void testRefusalFromTheSameThemeClearsTheId() {
  // The previous choice is the refused theme itself (selected again): never keep it.
  Selection s = sel("big", kTheme);
  CHECK(afterRefusal(s, true, kTheme, "big", "big", kTheme));
  CHECK(s.themeId.empty());
  Selection t = sel("big", kTheme);
  CHECK(afterRefusal(t, true, kStocks, "big", "big", kTheme));
  CHECK(t.themeId.empty() && t.mode == kStocks);
}

static void testRefusalWithNoPreviousClearsTheId() {
  Selection s = sel("big", kTheme);
  CHECK(afterRefusal(s, false, 0, "", "big", kTheme));
  CHECK(s.themeId.empty());
  CHECK(s.mode == kTheme);                    // no mode is known to go back to; the mode stays
}

static void testRefusalWithAnEmptyPreviousIdClearsTheId() {
  Selection s = sel("big", kTheme);
  CHECK(afterRefusal(s, true, kStocks, "", "big", kTheme));
  CHECK(s.mode == kStocks && s.themeId.empty());
}

static void testRefusalInACarouselClearsItsThemeTick() {
  Selection s = sel("big", kCarousel, true);
  CHECK(afterRefusal(s, false, 0, "", "big", kTheme));
  CHECK(s.themeId.empty());
  CHECK(!s.carouselTheme);                    // else the carousel would show "Select an installed theme"
  CHECK(s.mode == kCarousel);
}

static void testRefusalKeepsTheCarouselTickWhenAThemeIsRestored() {
  Selection s = sel("big", kTheme, true);
  CHECK(afterRefusal(s, true, kCarousel, "pixel-room", "big", kTheme));
  CHECK(s.mode == kCarousel && s.themeId == "pixel-room" && s.carouselTheme);
}

static void testRefusalLeavesAnotherSelectionAlone() {
  // The user picked something else while the refusal was on its way.
  Selection s = sel("other", kTheme);
  CHECK(!afterRefusal(s, true, kStocks, "pixel-room", "big", kTheme));
  CHECK(s.themeId == "other" && s.mode == kTheme);
}

static void testRefusalNeverLeavesTheRefusedTheme() {
  // The property that matters: after any refusal the setting never names the
  // refused theme, so no boot loads it again.
  const char* ids[] = {"", "big", "pixel-room"};
  const uint8_t modes[] = {kStocks, kTheme, kCarousel};
  for (const char* prevId : ids)
    for (uint8_t prevMode : modes)
      for (int hasPrev = 0; hasPrev < 2; ++hasPrev)
        for (int carousel = 0; carousel < 2; ++carousel) {
          Selection s = sel("big", kTheme, carousel);
          afterRefusal(s, hasPrev, prevMode, prevId, "big", kTheme);
          CHECK(s.themeId != "big");
          if (s.themeId.empty()) CHECK(!s.carouselTheme);
        }
}

// ---- constants ---------------------------------------------------------------

static void testConstants() {
  // ONE reserve for the Wi-Fi SDK (issues #132 and #134), in a header of its
  // own that every heavy step can include. The theme load uses it; the TLS
  // handshakes of the ticker, theme data and the update are #134's to change.
  const std::string h = slurp("src/features/theme/ThemeMode.h");
  const std::string w = slurp("src/WifiReserve.h");
  CHECK(!h.empty() && !w.empty());
  const unsigned long freeHeap = number(w, "WifiReserveFreeHeap");
  const unsigned long block = number(h, "ParseLargestBlock");
  CHECK(freeHeap > 0 && block > 0);
  // The measured numbers (decided 2026-11-20 after the #132 series): 5 KiB
  // free heap (the lowest clean run held 3,976 B, crashes at 3,152 B and
  // below, 1 KiB of room on top), in the shared header of #135. The block
  // limit is the parser's own, 1,536 B (just under the lowest clean block of
  // 1,568 B): the measurement did not show that the block separates clean
  // runs from crashes, so the shared header has no block figure.
  CHECK(freeHeap == 5120);
  CHECK(block == 1536);
  CHECK(w.find("FILL_IN") == std::string::npos);   // no placeholder left
  // The reserve is what a running theme must leave; a load needs more than that.
  CHECK(freeHeap <= number(h, "ParseFreeHeap"));
  // No second constant: ThemeMode.h holds no margin number of its own.
  CHECK(h.find("LoadMargin") == std::string::npos);
  // The header needs no Arduino code, so any file can include it. It has no
  // platform guard either: the TLS gate of #135 includes the same file on
  // every board. Only the value is checked here, not the comment text.
  CHECK(w.find("#include <Arduino.h>") == std::string::npos && w.find("#include \"config.h\"") == std::string::npos);
  // The theme code includes it, takes the free heap from it and the block
  // from the parser's constant. It names no block constant of the shared
  // header: main's file has none, and a second 1,536 would drift.
  const std::string mode = slurp("src/features/theme/ThemeMode.cpp");
  CHECK(mode.find("#include \"WifiReserve.h\"") != std::string::npos);
  CHECK(mode.find("{WifiReserveFreeHeap, ParseLargestBlock}") != std::string::npos);
  CHECK(count(mode, "{WifiReserveFreeHeap, ParseLargestBlock}") >= 1);   // the check after the load
  CHECK(mode.find("WifiReserveLargestBlock") == std::string::npos);
  CHECK(h.find("WifiReserveLargestBlock") == std::string::npos);
  CHECK(slurp("src/features/theme/ThemeMargin.h").find("WifiReserveLargestBlock") == std::string::npos);
}

// ---- 3. The wiring --------------------------------------------------------------

static void testModeWiring() {
  const std::string mode = slurp("src/features/theme/ThemeMode.cpp");
  const std::string head = slurp("src/features/theme/ThemeMode.h");
  CHECK(!mode.empty() && !head.empty());
  const std::string service = body(mode, "void ThemeMode::service(");
  CHECK(!service.empty());
  // The check runs after the first update (the layers' texts are allocated
  // there) and before the frame is drawn.
  CHECK(before(service, "engine_.update(", "checkMargin("));
  CHECK(before(service, "checkMargin(", "smalltv::render("));
  CHECK(onlyOnEsp8266(mode, "checkMargin("));
  // The work is in a function of its own, not in service() (the loop stack, T12/T13).
  const size_t def = mode.find("ThemeMode::checkMargin(");
  CHECK(def != std::string::npos);
  CHECK(mode.rfind("__attribute__((noinline))", def) != std::string::npos && def - mode.rfind("__attribute__((noinline))", def) < 60);
  CHECK(count(service, "memoryShort(") == 0);   // the numbers are compared in the sibling
  // The watch starts when a load ends, and again when fetched values changed the texts.
  CHECK(before(service, "engine_.setTheme(std::move(theme), millis());", "marginWatch_.restart("));
  const std::string refresh = body(mode, "void ThemeMode::refreshData()");
  CHECK(before(refresh, "if (changed) engine_.setValues(dataValues_);", "marginWatch_.restart("));
  // Only the first applied result of a load restarts the watch: a value that
  // changes at every fetch must not keep the theme under watch for good.
  CHECK(before(refresh, "!fetchWatched_", "marginWatch_.restart("));
  CHECK(before(refresh, "fetchWatched_ = true;", "marginWatch_.restart(") || before(refresh, "marginWatch_.restart(", "fetchWatched_ = true;"));
  CHECK(service.find("fetchWatched_ = false;") != std::string::npos);   // a new load starts over
  // The check reads both numbers and the limits by name.
  const std::string check = body(mode, "ThemeMode::checkMargin(");
  CHECK(check.find("ESP.getFreeHeap()") != std::string::npos);
  CHECK(check.find("platformMaxFreeBlock()") != std::string::npos);
  CHECK(check.find("WifiReserveFreeHeap") != std::string::npos && check.find("ParseLargestBlock") != std::string::npos);
  CHECK(check.find("marginWatch_.check(") != std::string::npos);
  // The refusal: drop the theme like release() does, tell the app, no restart.
  const std::string refuse = body(mode, "ThemeMode::refuseForMemory(");
  CHECK(!refuse.empty());
  CHECK(refuse.find("dataRequests_.cancel();") != std::string::npos);
  CHECK(refuse.find("dataRequests_.take();") != std::string::npos);
  CHECK(refuse.find("package_.reset();") != std::string::npos);
  CHECK(refuse.find("file_.reset();") != std::string::npos);
  CHECK(refuse.find("engine_.setValues({});") != std::string::npos);
  CHECK(refuse.find("engine_.setTheme(smalltv::Theme{}, millis());") != std::string::npos);
  CHECK(refuse.find("refusal_ = ") != std::string::npos);
  CHECK(refuse.find("appThemeRefused(") != std::string::npos);
  CHECK(before(refuse, "engine_.setTheme(smalltv::Theme{}, millis());", "appThemeRefused("));   // the heap is free again for the save
  CHECK(refuse.find("restart") == std::string::npos && refuse.find("Restart") == std::string::npos);
  CHECK(check.find("restart") == std::string::npos && check.find("Restart") == std::string::npos);
  CHECK(mode.find("ESP.restart") == std::string::npos);
  // The message survives unload(): the settings fix changes the selection, and
  // that is what unload() answers to.
  const std::string unload = body(mode, "void ThemeMode::unload()");
  CHECK(!unload.empty());
  CHECK(unload.find("refusal_") == std::string::npos);
  CHECK(head.find("String refusal_") != std::string::npos || head.find("refusal_;") != std::string::npos);
  // With no id left, the screen shows the refusal, not "Select an installed theme".
  CHECK(service.find("refusal_.length()") != std::string::npos);
  // A new selection or an install or delete of that theme lets it try again.
  CHECK(head.find("void forgetRefusal(") != std::string::npos);
  CHECK(head.find("void rememberPrevious(") != std::string::npos);
  CHECK(head.find("refusal() const") != std::string::npos);
  // GET /api/themes shows error(): a refusal shows there when no load error is on.
  CHECK(head.find("error_.length() ? error_ : refusal_") != std::string::npos);
}

static void testWebWiring() {
  const std::string web = slurp("src/features/theme/ThemeWeb.cpp");
  const std::string select = body(web, "void selectTheme()");
  CHECK(!select.empty());
  // The previous mode and theme go to ThemeMode before the settings change.
  CHECK(before(select, "g_themeMode.rememberPrevious(oldMode, oldId);", "settings->themeId = id;"));
  CHECK(before(select, "uint8_t oldMode = settings->mode;", "g_themeMode.rememberPrevious("));
  CHECK(before(select, "g_themeMode.forgetRefusal(", "appInvalidate();"));
  // Installing or deleting a theme lets the refused one try again.
  CHECK(count(web, "forgetRefusal(") >= 3);
}

static void testAppWiring() {
  const std::string app = slurp("src/main.cpp");
  const std::string hook = body(app, "void appThemeRefused(");
  CHECK(!hook.empty());
  CHECK(hook.find("afterRefusal(") != std::string::npos);
  CHECK(before(hook, "afterRefusal(", "saveSettings(g_settings)"));
  // What the check decided goes back into the settings, all three fields, and
  // only then is it saved (a hook that saves the old values changes nothing).
  CHECK(before(hook, "afterRefusal(", "g_settings.themeId = sel.themeId.c_str();"));
  CHECK(before(hook, "g_settings.themeId = sel.themeId.c_str();", "saveSettings(g_settings)"));
  CHECK(before(hook, "afterRefusal(", "g_settings.mode = sel.mode;"));
  CHECK(before(hook, "g_settings.mode = sel.mode;", "saveSettings(g_settings)"));
  CHECK(before(hook, "afterRefusal(", "g_settings.carouselTheme = sel.carouselTheme;"));
  CHECK(before(hook, "g_settings.carouselTheme = sel.carouselTheme;", "saveSettings(g_settings)"));
  CHECK(before(hook, "saveSettings(g_settings)", "appInvalidate();"));
  CHECK(hook.find("restart") == std::string::npos && hook.find("Restart") == std::string::npos);
  CHECK(onlyOnEsp8266(app, "void appThemeRefused("));
  CHECK(app.find("#include \"features/theme/ThemeRefusal.h\"") != std::string::npos || app.find("ThemeRefusal.h") != std::string::npos);
  const std::string mode = slurp("src/features/theme/ThemeMode.cpp");
  CHECK(mode.find("extern void appThemeRefused(") != std::string::npos);
}

static void testStatusWiring() {
  const std::string portal = slurp("src/WebPortal.cpp");
  const std::string status = body(portal, "void handleStatus()");
  CHECK(!status.empty());
  CHECK(status.find("d.themeError = ") != std::string::npos);
  CHECK(status.find("g_themeMode.refusal()") != std::string::npos);
  // The field is filled from the refusal text, not from a literal.
  CHECK(status.find("const String& themeRefusal = g_themeMode.refusal();") != std::string::npos);
  CHECK(status.find("d.themeError = themeRefusal.c_str();") != std::string::npos);
  CHECK(before(status, "const String& themeRefusal = g_themeMode.refusal();", "d.themeError = themeRefusal.c_str();"));
  const std::string json = slurp("src/StatusJson.h");
  CHECK(json.find("themeError") != std::string::npos);
  // The text the maintainer asked for.
  const std::string mode = slurp("src/features/theme/ThemeMode.cpp");
  CHECK(mode.find("not enough memory to run this theme") != std::string::npos);
}

int main() {
  testMemoryShort();
  testWatchIdleUntilStarted();
  testWatchRefusesAfterShortInARow();
  testWatchCountsOnlyReadingsApart();
  testWatchOneGoodReadingRestartsTheCount();
  testWatchEitherValueCounts();
  testWatchSettleTime();
  testWatchRestartsAfterAFetchResult();
  testWatchRestartClearsTheCount();
  testWatchSurvivesClockWrap();
  testWatchGapAcrossTheClockWrap();
  testRefusalRestoresAPreviousMode();
  testRefusalRestoresAPreviousTheme();
  testRefusalFromTheSameThemeClearsTheId();
  testRefusalWithNoPreviousClearsTheId();
  testRefusalWithAnEmptyPreviousIdClearsTheId();
  testRefusalInACarouselClearsItsThemeTick();
  testRefusalKeepsTheCarouselTickWhenAThemeIsRestored();
  testRefusalLeavesAnotherSelectionAlone();
  testRefusalNeverLeavesTheRefusedTheme();
  testConstants();
  testModeWiring();
  testWebWiring();
  testAppWiring();
  testStatusWiring();
  if (failures) {
    std::printf("margin tests: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("margin tests passed\n");
  return 0;
}
