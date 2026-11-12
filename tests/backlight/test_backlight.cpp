// The backlight plan: how a level (steps 0..255) and the polarity become either
// "sigma-delta on with duty D" or "pin held at a fixed level". Pure logic.
// c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc tests/backlight/test_backlight.cpp -o /tmp/smalltv-backlight-tests
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "BacklightPlan.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

using smalltv::BacklightPlan;
using smalltv::backlightPlan;

// Light share in 1/(255*256) units, so the checks are exact integers:
// share = steps/255 is lit*256 compared with duty*255.
// The pin is HIGH for duty/256 of the time while the sigma-delta runs. The
// light is on while the pin is HIGH (active-high) or LOW (inverted).
static long litTimes256(const BacklightPlan& p, bool inverted) {
  if (!p.sigmaDelta) {
    const bool lit = inverted ? !p.level : p.level;
    return lit ? 256 : 0;
  }
  return inverted ? 256 - p.duty : p.duty;
}

static void testEnds() {
  for (int inv = 0; inv < 2; inv++) {
    const bool inverted = inv != 0;
    const BacklightPlan off = backlightPlan(0, inverted);
    CHECK(!off.sigmaDelta);                       // no residual pulses at all
    CHECK(off.level == inverted);                 // dark: HIGH when active-low, LOW when active-high
    CHECK(litTimes256(off, inverted) == 0);
    const BacklightPlan on = backlightPlan(255, inverted);
    CHECK(!on.sigmaDelta);
    CHECK(on.level == !inverted);                 // lit: LOW when active-low, HIGH when active-high
    CHECK(litTimes256(on, inverted) == 256);
  }
}

static void testMiddleIsSigmaDelta() {
  for (int inv = 0; inv < 2; inv++)
    for (int s = 1; s <= 254; s++) {
      const BacklightPlan p = backlightPlan((uint8_t)s, inv != 0);
      CHECK(p.sigmaDelta);
      // Duty 0 and 255 are not constant levels on the chip (255 leaves one
      // pulse in 256): the middle must stay clear of both.
      CHECK(p.duty >= 1 && p.duty <= 255);
    }
}

// The light share is steps/255 to within half a duty step, for both polarities.
static void testShare() {
  for (int inv = 0; inv < 2; inv++)
    for (int s = 0; s <= 255; s++) {
      const BacklightPlan p = backlightPlan((uint8_t)s, inv != 0);
      const long lit = litTimes256(p, inv != 0);              // share * 256
      const long err = std::labs(lit * 255 - (long)s * 256);   // |share - s/255| * 256 * 255
      CHECK(2 * err <= 255);                                   // within 0.5 duty step
      if (2 * err > 255) std::printf("  steps %d inverted %d: lit %ld/256\n", s, inv, lit);
    }
}

// Both polarities add up to the whole period: the lit share of one is the dark
// share of the other.
static void testPolarityMirror() {
  for (int s = 1; s <= 254; s++) {
    const BacklightPlan a = backlightPlan((uint8_t)s, false);
    const BacklightPlan b = backlightPlan((uint8_t)s, true);
    CHECK(a.duty + b.duty == 256);
  }
  for (int s = 0; s <= 255; s++)
    CHECK(litTimes256(backlightPlan((uint8_t)s, false), false) ==
          litTimes256(backlightPlan((uint8_t)s, true), true));
}

// A brighter level never gets a darker output, and every step is a real step
// in the middle (no two levels share a duty).
static void testMonotonic() {
  for (int inv = 0; inv < 2; inv++) {
    long prev = -1;
    for (int s = 0; s <= 255; s++) {
      const long lit = litTimes256(backlightPlan((uint8_t)s, inv != 0), inv != 0);
      CHECK(lit > prev);
      prev = lit;
    }
  }
}

static void testSame() {
  CHECK(smalltv::backlightPlanSame(backlightPlan(0, true), backlightPlan(0, true)));
  CHECK(smalltv::backlightPlanSame(backlightPlan(255, true), backlightPlan(255, true)));
  CHECK(smalltv::backlightPlanSame(backlightPlan(20, true), backlightPlan(20, true)));
  CHECK(!smalltv::backlightPlanSame(backlightPlan(20, true), backlightPlan(21, true)));
  CHECK(!smalltv::backlightPlanSame(backlightPlan(0, true), backlightPlan(0, false)));   // HIGH vs LOW
  CHECK(!smalltv::backlightPlanSame(backlightPlan(0, true), backlightPlan(1, true)));    // fixed vs sigma-delta
  CHECK(!smalltv::backlightPlanSame(backlightPlan(254, false), backlightPlan(255, false)));
  // A fixed plan is never the same as a sigma-delta plan, even when the unused
  // fields happen to match (a fixed LOW has level false, as does a sigma-delta plan).
  CHECK(!smalltv::backlightPlanSame(backlightPlan(0, false), backlightPlan(1, false)));
  CHECK(!smalltv::backlightPlanSame(backlightPlan(1, false), backlightPlan(0, false)));
  CHECK(!smalltv::backlightPlanSame(backlightPlan(255, true), backlightPlan(254, true)));
  // The unused fields of a fixed plan do not make two equal plans differ.
  BacklightPlan a = backlightPlan(0, true), b = backlightPlan(0, true);
  b.duty = 77;
  CHECK(smalltv::backlightPlanSame(a, b));
  a = backlightPlan(20, true); b = a; b.level = !b.level;
  CHECK(smalltv::backlightPlanSame(a, b));
}

// The prescaler: the slowest pulse rate (duty 1 or 255) stays well above the
// 1 kHz of the software PWM, and one pulse lasts at least 1 us.
static void testFrequency() {
  CHECK(smalltv::BL_SD_PRESCALER == 79);
  CHECK(smalltv::backlightClockHz() == 1000000u);
  CHECK(smalltv::backlightPulseHz(1) == 3906u);
  CHECK(smalltv::backlightPulseHz(5) == 19531u);
  CHECK(smalltv::backlightPulseHz(20) == 78125u);
  CHECK(smalltv::backlightPulseHz(128) == 500000u);
  CHECK(smalltv::backlightPulseHz(255) == 3906u);   // mirrored above 128
  CHECK(smalltv::backlightPulseHz(200) == smalltv::backlightPulseHz(56));
  for (int d = 1; d <= 255; d++) CHECK(smalltv::backlightPulseHz((uint8_t)d) >= 3000u);
  CHECK(smalltv::backlightClockHz() <= 1000000u);   // one clock tick >= 1 us
}

int main() {
  testEnds();
  testMiddleIsSigmaDelta();
  testShare();
  testPolarityMirror();
  testMonotonic();
  testSame();
  testFrequency();
  if (failures) { std::printf("%d backlight plan checks failed\n", failures); return 1; }
  std::printf("backlight plan tests passed\n");
  return 0;
}
