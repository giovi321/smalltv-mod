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
#include "ThemeSelection.h"
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

// ---- 1b. The check before the engine builds the theme ----------------------------
//
// tooBigToBuild(free, block, added, limits): the heap after the build would be
// under the reserve, or the block already is. `added` comes from engineBytes()
// (ThemeFootprint.h, tested against the engine in test_memory.cpp).

static void testTooBigToBuild() {
  const MarginLimits l{4000, 1000};
  // Free heap minus what the build adds must stay at the reserve or above.
  CHECK(!tooBigToBuild(7000, 5000, 3000, l));   // exactly at the reserve is enough
  CHECK(tooBigToBuild(6999, 5000, 3000, l));    // one byte under
  CHECK(!tooBigToBuild(7001, 5000, 3000, l));
  // More to add than there is free must not wrap round to "plenty".
  CHECK(tooBigToBuild(100, 5000, 200, l));
  CHECK(tooBigToBuild(0, 5000, 1, l));
  CHECK(tooBigToBuild(4294967295u, 5000, 4294967295u, l));
  // The block is judged as it is now: it only shrinks while the theme builds.
  CHECK(!tooBigToBuild(100000, 1000, 0, l));
  CHECK(tooBigToBuild(100000, 999, 0, l));
  CHECK(tooBigToBuild(100000, 999, 10, l));
  // The bigger the theme, the less it takes to refuse.
  for (uint32_t add = 0; add < 12000; add += 250) {
    const bool short1 = tooBigToBuild(9000, 5000, add, l);
    const bool short2 = tooBigToBuild(9000, 5000, add + 250, l);
    CHECK(!short1 || short2);
  }
  // With nothing to add it is the check of memoryShort().
  const uint32_t frees[] = {0, 3999, 4000, 4001, 100000};
  const uint32_t blocks[] = {0, 999, 1000, 1001, 100000};
  for (uint32_t f : frees)
    for (uint32_t b : blocks) CHECK(tooBigToBuild(f, b, 0, l) == memoryShort(f, b, l));
}

// ---- 1c. The selection is saved after the theme passed ----------------------------
//
// A theme picked in the web UI is the live selection at once, and the stored
// one only when it has loaded and passed the memory check (ThemeSelection.h).
// So a crash during the first load leaves the old setting in flash, and the
// next boot does not load the theme that just crashed the device.

static void testPendingSelectionBasics() {
  PendingSelection p;
  CHECK(!p.pending());
  CHECK(!p.accept("a", true));                  // nothing picked: nothing to save
  p.pick("a");
  CHECK(p.pending());
  CHECK(!p.accept("a", false));                 // not through the check yet
  CHECK(p.pending());                           // still waiting
  CHECK(p.accept("a", true));                   // passed: save, once
  CHECK(!p.pending());
  CHECK(!p.accept("a", true));
}

static void testPendingSelectionDropAndReplace() {
  PendingSelection p;
  p.pick("a");
  p.drop();                                     // refused for memory
  CHECK(!p.pending() && !p.accept("a", true));
  p.pick("a");
  CHECK(!p.accept("b", true));                  // the user picked something else on the way
  CHECK(!p.pending());
  CHECK(!p.accept("a", true));                  // and the old pick is gone for good
  p.pick("a");
  p.pick("b");                                  // a second pick replaces the first
  CHECK(!p.accept("a", true));
  p.pick("a");
  p.pick("b");
  CHECK(p.accept("b", true));
  p.pick("");                                   // an empty id is no pick
  CHECK(!p.pending());
}

// The device in miniature: the live selection (RAM), the stored one (flash).
struct MiniDevice {
  std::string live, stored;
  PendingSelection pending;
  int saves = 0;
  explicit MiniDevice(const char* id) : live(id), stored(id) {}
  void select(const char* id) { live = id; pending.pick(id); }    // the web request: no save
  void frame(bool passed) { if (pending.accept(live.c_str(), passed)) { stored = live; ++saves; } }
  void refuse() { pending.drop(); live = stored; }                // the refusal puts the old setting back
  void reboot() { live = stored; pending.drop(); }                // a crash or a restart
};

static void testSelectDoesNotSaveBeforeThePass() {
  MiniDevice d("old");
  d.select("new");
  CHECK(d.live == "new");                       // the screen shows the pick at once
  CHECK(d.stored == "old" && d.saves == 0);     // flash still holds the old one
  d.frame(false);
  d.frame(false);
  CHECK(d.stored == "old" && d.saves == 0);     // while the check runs
  d.frame(true);
  CHECK(d.stored == "new" && d.saves == 1);     // saved once the theme passed
  d.frame(true);
  CHECK(d.saves == 1);                          // and not again
}

static void testACrashDuringTheLoadKeepsTheOldSetting() {
  MiniDevice d("old");
  d.select("crashes");
  d.frame(false);
  d.reboot();                                   // the first load crashed the device
  CHECK(d.live == "old" && d.stored == "old");  // the next boot does not bring the theme back
  d.frame(true);
  CHECK(d.saves == 0);
}

static void testARefusedThemeNeverReachesTheStoredSetting() {
  MiniDevice d("old");
  d.select("big");
  d.refuse();
  d.frame(true);
  CHECK(d.live == "old" && d.stored == "old" && d.saves == 0);
  // A new pick after a refusal works as before.
  d.select("small");
  d.frame(true);
  CHECK(d.stored == "small" && d.saves == 1);
}

// Two picks in a row: the first has not passed, the second is refused. What
// comes back is the theme that was stored before both, and the first is never saved.
static void testSecondPickRefusedKeepsTheStoredOne() {
  MiniDevice d("old");
  d.select("first");
  d.select("second");     // ThemeMode keeps "old" as the previous one while a pick waits
  d.refuse();
  d.frame(true);
  CHECK(d.live == "old" && d.stored == "old" && d.saves == 0);
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

// ---- a reload is not a first load -------------------------------------------------
//
// A running theme is dropped (release()) when a web request, a carousel page
// change or a save needs the heap, and loaded again at the next frame through
// the same checks. If that reload is refused, the cause is the heap of this
// moment (a buffer of another page, a request), not the theme: it passed
// before. The settings must stay as they are. Only a first load of a theme
// (a boot, a new pick, a reinstall) may take it out of the settings.

static void testReloadRefusalKeepsTheStoredSelection() {
  PassedTheme h;
  h.loadStarts("big");                       // the first load of this boot
  CHECK(!h.reload());
  h.passed("big");                           // it passed its memory check
  h.loadStarts("big");                       // released for a request, loaded again
  CHECK(h.reload());
  Selection stored = sel("big", kTheme);
  CHECK(!h.refused(stored, false, 0, "", "big", kTheme));
  CHECK(stored.themeId == "big" && stored.mode == kTheme);
  // The same in a carousel: the tick and the id stay.
  Selection car = sel("big", kCarousel, true);
  CHECK(!h.refused(car, false, 0, "", "big", kTheme));
  CHECK(car.themeId == "big" && car.carouselTheme && car.mode == kCarousel);
  // It is still a reload after a refused reload: a later refusal leaves the settings alone too.
  h.loadStarts("big");
  CHECK(h.reload());
}

static void testFirstLoadRefusalStillClearsTheSelection() {
  PassedTheme h;
  h.loadStarts("big");
  Selection s = sel("big", kTheme);
  CHECK(h.refused(s, false, 0, "", "big", kTheme));
  CHECK(s.themeId.empty());                  // the rule of the refusal stays for a first load
  // A refusal with a previous selection goes back to it, as before.
  PassedTheme g;
  g.loadStarts("big");
  Selection t = sel("big", kTheme);
  CHECK(g.refused(t, true, kStocks, "pixel-room", "big", kTheme));
  CHECK(t.mode == kStocks && t.themeId == "pixel-room");
}

static void testTheNetAfterAFirstPassStillClears() {
  // The theme passed, then the first fetched values made the texts longer and
  // the net refused it. That load was a first load: the setting is saved, and
  // would be refused at every boot, so it is cleared.
  PassedTheme h;
  h.loadStarts("big");
  h.passed("big");
  Selection s = sel("big", kTheme);
  CHECK(h.refused(s, false, 0, "", "big", kTheme));
  CHECK(s.themeId.empty());
  h.loadStarts("big");                       // a later try is a first load again
  CHECK(!h.reload());
}

static void testAReloadIsOfTheSameThemeOnly() {
  PassedTheme h;
  h.loadStarts("a");
  h.passed("a");
  h.loadStarts("b");
  CHECK(!h.reload());                        // another theme: a first load
  Selection s = sel("b", kTheme);
  CHECK(h.refused(s, false, 0, "", "b", kTheme));
  CHECK(s.themeId.empty());
  h.loadStarts("a");                         // back to the theme that passed
  CHECK(h.reload());
}

static void testNewPickOrReinstallMakesItAFirstLoad() {
  PassedTheme h;
  h.loadStarts("big");
  h.passed("big");
  h.forget();                                // select, install or delete (forgetRefusal)
  h.loadStarts("big");
  CHECK(!h.reload());
  Selection s = sel("big", kTheme);
  CHECK(h.refused(s, false, 0, "", "big", kTheme));
  CHECK(s.themeId.empty());
}

static void testPassedIsCheapToRepeat() {
  // checkMargin() reports the pass at every frame once the watch is over.
  PassedTheme h;
  for (int i = 0; i < 1000; ++i) h.passed("big");
  h.loadStarts("big");
  CHECK(h.reload());
  h.passed("");                              // no id: nothing passed
  h.loadStarts("");
  CHECK(!h.reload());
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
  CHECK(count(mode, "{WifiReserveFreeHeap, ParseLargestBlock}") == 2);   // the check before the build and the check after the load
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

// The check before the build: after the parse, before the engine takes the theme.
static void testBuildWiring() {
  const std::string mode = slurp("src/features/theme/ThemeMode.cpp");
  const std::string service = body(mode, "void ThemeMode::service(");
  CHECK(!service.empty());
  CHECK(mode.find("#include \"ThemeFootprint.h\"") != std::string::npos || slurp("src/features/theme/ThemeMode.h").find("#include \"ThemeFootprint.h\"") != std::string::npos);
  // In service(): the package is parsed, then the check, then the engine takes the theme.
  CHECK(before(service, "package_->load(theme, error)", "refusedBeforeBuild("));
  CHECK(before(service, "refusedBeforeBuild(", "engine_.setTheme(std::move(theme), millis());"));
  CHECK(onlyOnEsp8266(mode, "refusedBeforeBuild("));
  // The work is in a function of its own (the loop stack, T12/T13).
  const size_t def = mode.find("ThemeMode::refusedBeforeBuild(");
  CHECK(def != std::string::npos);
  CHECK(mode.rfind("__attribute__((noinline))", def) != std::string::npos && def - mode.rfind("__attribute__((noinline))", def) < 60);
  const std::string check = body(mode, "ThemeMode::refusedBeforeBuild(");
  CHECK(!check.empty());
  CHECK(check.find("engineBytes(theme)") != std::string::npos);
  CHECK(check.find("ESP.getFreeHeap()") != std::string::npos);
  CHECK(check.find("platformMaxFreeBlock()") != std::string::npos);
  CHECK(check.find("tooBigToBuild(") != std::string::npos);
  CHECK(check.find("WifiReserveFreeHeap") != std::string::npos && check.find("ParseLargestBlock") != std::string::npos);
  // A refusal drops the parsed theme first (the settings save needs the heap),
  // then goes the way of the check after the load.
  CHECK(before(check, "dropTheme(theme);", "refuseForMemory();"));
  CHECK(check.find("restart") == std::string::npos && check.find("Restart") == std::string::npos);
  // The check after the load stays as the net for what the estimate misses.
  CHECK(before(service, "engine_.update(", "checkMargin("));
}

// The select request does not save; the load that passes does.
static void testSelectWiring() {
  const std::string web = slurp("src/features/theme/ThemeWeb.cpp");
  const std::string select = body(web, "void selectTheme()");
  CHECK(!select.empty());
  CHECK(select.find("saveSettings(") == std::string::npos);            // no early save
  CHECK(select.find("Could not save selection") == std::string::npos);  // so no failed-save answer either
  CHECK(select.find("g_themeMode.saveAfterLoad(id);") != std::string::npos);
  CHECK(before(select, "settings->themeId = id;", "g_themeMode.saveAfterLoad(id);"));
  CHECK(before(select, "g_themeMode.saveAfterLoad(id);", "appInvalidate();"));
  CHECK(before(select, "g_themeMode.saveAfterLoad(id);", "reply(200, id);"));
  // The delete handler still saves its own change.
  CHECK(body(web, "void deleteTheme()").find("saveSettings(") != std::string::npos);
  const std::string head = slurp("src/features/theme/ThemeMode.h");
  CHECK(head.find("void saveAfterLoad(") != std::string::npos);
  CHECK(head.find("smalltv::PendingSelection pending_;") != std::string::npos);
  CHECK(head.find("#include \"ThemeSelection.h\"") != std::string::npos);
  // A second pick while the first still waits must not make the unsaved first
  // pick the "previous" theme: a refusal of the second would then save it.
  // The previous one stays the last theme that passed.
  const std::string remember = body(head, "void rememberPrevious(");
  CHECK(!remember.empty());
  CHECK(before(remember, "if (hasPrev_ && pending_.pending()) return;", "hasPrev_ = true;"));
}

static void testAcceptWiring() {
  const std::string mode = slurp("src/features/theme/ThemeMode.cpp");
  const std::string service = body(mode, "void ThemeMode::service(");
  // After the frame is drawn, and not when the draw failed.
  CHECK(before(service, "smalltv::render(", "acceptSelection("));
  CHECK(before(service, "Could not read theme asset", "acceptSelection("));
  CHECK(service.find("} else acceptSelection(s);") != std::string::npos);
  const std::string accept = body(mode, "ThemeMode::acceptSelection(");
  CHECK(!accept.empty());
  const size_t acceptDef = mode.find("ThemeMode::acceptSelection(");
  CHECK(acceptDef != std::string::npos);
  CHECK(mode.rfind("__attribute__((noinline))", acceptDef) != std::string::npos && acceptDef - mode.rfind("__attribute__((noinline))", acceptDef) < 60);
  CHECK(accept.find("pending_.accept(s.themeId.c_str(),") != std::string::npos);
  // On the ESP8266 the theme has passed when the margin watch is over; the
  // other boards have no watch, a drawn frame is the pass.
  CHECK(accept.find("!marginWatch_.active()") != std::string::npos);
  CHECK(onlyOnEsp8266(mode, "!marginWatch_.active();"));
  // A save needs heap that a theme next to the reserve does not leave: the
  // theme is released first, as a request does, and loads again afterwards.
  CHECK(before(accept, "release(ParseFreeHeap, ParseLargestBlock);", "appThemeAccepted();"));
  CHECK(before(accept, "pending_.accept(", "release("));
  CHECK(accept.find("restart") == std::string::npos && accept.find("Restart") == std::string::npos);
  // A refusal ends the wait: the refused theme is never saved.
  const std::string refuse = body(mode, "ThemeMode::refuseForMemory(");
  CHECK(refuse.find("pending_.drop();") != std::string::npos);
  CHECK(before(refuse, "pending_.drop();", "appThemeRefused("));
  // The app's hook saves the settings, on every board.
  const std::string app = slurp("src/main.cpp");
  const std::string hook = body(app, "void appThemeAccepted(");
  CHECK(!hook.empty());
  CHECK(hook.find("saveSettings(g_settings)") != std::string::npos);
  CHECK(!onlyOnEsp8266(app, "void appThemeAccepted("));
  CHECK(mode.find("extern void appThemeAccepted(") != std::string::npos);
}

// A reload after release() goes through the checks again; a refusal there must
// not reach the settings (testReloadRefusalKeepsTheStoredSelection).
static void testReloadWiring() {
  const std::string mode = slurp("src/features/theme/ThemeMode.cpp");
  const std::string head = slurp("src/features/theme/ThemeMode.h");
  const std::string service = body(mode, "void ThemeMode::service(");
  CHECK(head.find("smalltv::PassedTheme passedTheme_;") != std::string::npos);
  CHECK(head.find("#include \"ThemeRefusal.h\"") != std::string::npos);
  // A load says what it is before the first check can refuse it.
  CHECK(before(service, "passedTheme_.loadStarts(s.themeId.c_str());", "refusedBeforeBuild("));
  CHECK(before(service, "attempted_ = true;", "passedTheme_.loadStarts("));
  // The pass is noted where the watch ends, next to the old selection being forgotten.
  const std::string check = body(mode, "ThemeMode::checkMargin(");
  CHECK(before(check, "if (!marginWatch_.active())", "passedTheme_.passed(loadedId_.c_str());"));
  // The refusal of a reload leaves the settings alone: no hook call, no refusal text.
  const std::string refuse = body(mode, "ThemeMode::refuseForMemory(");
  CHECK(refuse.find("if (passedTheme_.reload()) return;") != std::string::npos);
  CHECK(before(refuse, "if (passedTheme_.reload()) return;", "appThemeRefused("));
  CHECK(before(refuse, "if (passedTheme_.reload()) return;", "refusal_ = "));
  CHECK(before(refuse, "if (passedTheme_.reload()) return;", "refusedId_ = "));
  CHECK(before(refuse, "pending_.drop();", "if (passedTheme_.reload()) return;"));
  // The screen still says why (error_), so the message is drawn.
  CHECK(before(refuse, "error_ = ", "if (passedTheme_.reload()) return;"));
  CHECK(before(refuse, "messageDrawn_ = false;", "if (passedTheme_.reload()) return;"));
  // A first-load refusal forgets the pass: the next try is a first load again.
  CHECK(before(refuse, "if (passedTheme_.reload()) return;", "passedTheme_.forget();"));
  // The pass outlives unload() (carousel and mode switches unload the theme).
  const std::string unload = body(mode, "void ThemeMode::unload()");
  CHECK(!unload.empty() && unload.find("passedTheme_") == std::string::npos);
  // A new pick, an install or a delete is a new try.
  const size_t fAt = head.find("void forgetRefusal(");
  const size_t fEnd = head.find("}", fAt);   // the function is a few lines in the class
  CHECK(fAt != std::string::npos && fEnd != std::string::npos);
  CHECK(head.substr(fAt, fEnd - fAt).find("passedTheme_.forget();") != std::string::npos);
}

static void testWebWiring() {
  const std::string web = slurp("src/features/theme/ThemeWeb.cpp");
  const std::string select = body(web, "void selectTheme()");
  CHECK(!select.empty());
  // The previous mode and theme go to ThemeMode before the settings change.
  CHECK(before(select, "g_themeMode.rememberPrevious(settings->mode, settings->themeId);", "settings->themeId = id;"));
  CHECK(before(select, "g_themeMode.rememberPrevious(settings->mode, settings->themeId);", "settings->mode = MODE_THEME;"));
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
  testTooBigToBuild();
  testPendingSelectionBasics();
  testPendingSelectionDropAndReplace();
  testSelectDoesNotSaveBeforeThePass();
  testACrashDuringTheLoadKeepsTheOldSetting();
  testARefusedThemeNeverReachesTheStoredSetting();
  testSecondPickRefusedKeepsTheStoredOne();
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
  testReloadRefusalKeepsTheStoredSelection();
  testFirstLoadRefusalStillClearsTheSelection();
  testTheNetAfterAFirstPassStillClears();
  testAReloadIsOfTheSameThemeOnly();
  testNewPickOrReinstallMakesItAFirstLoad();
  testPassedIsCheapToRepeat();
  testConstants();
  testModeWiring();
  testBuildWiring();
  testSelectWiring();
  testAcceptWiring();
  testReloadWiring();
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
