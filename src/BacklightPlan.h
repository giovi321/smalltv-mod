// BacklightPlan.h - how a backlight level is driven on the ESP8266.
//
// Pure logic: no Arduino headers, so the native tests can run it. Gfx.cpp
// applies the plan with the chip's hardware sigma-delta generator (no timer
// interrupt, so Wi-Fi cannot delay an edge, unlike the core's software PWM).
//
// The generator cannot make a clean constant level. Its duty is target/256: a
// target of 255 still drops the output once in 256 clocks, and a target of 0 is
// not documented as a constant level either (core sigma_delta.h, "duty cycle =
// dc/256"). So the two ends are not sigma-delta at all: at steps 0 and 255 the
// pin is detached from the generator and held at a fixed level with
// digitalWrite. Only 1..254 use the generator, with a duty of 1..255.
#pragma once
#include <stdint.h>

namespace smalltv {

// The sigma-delta clock is 80 MHz / (prescaler + 1) (core_esp8266_sigma_delta.cpp,
// sigmaDeltaSetup: 10 MHz / ((p + 1) * 32)). 79 gives a 1 MHz clock: one pulse
// lasts 1 us, so the backlight driver sees pulses it can follow, and the
// slowest pulse rate (duty 1 or 255) is 1 MHz / 256 = 3.9 kHz, almost four
// times the 1 kHz of the software PWM.
static const uint8_t BL_SD_PRESCALER = 79;

struct BacklightPlan {
  bool sigmaDelta;   // true: sigma-delta on with `duty`; false: pin fixed at `level`
  bool level;        // fixed plans: true = pin HIGH, false = pin LOW
  uint8_t duty;      // sigma-delta plans: share of time the pin is HIGH, duty / 256, 1..255
};

// steps 0..255 is the light share (steps / 255). `inverted` is active-low: the
// light is on while the pin is LOW, so the pin's HIGH share is the dark share.
inline BacklightPlan backlightPlan(uint8_t steps, bool inverted) {
  BacklightPlan p;
  p.sigmaDelta = false;
  p.level = false;
  p.duty = 0;
  if (steps == 0) { p.level = inverted; return p; }          // dark: HIGH if active-low
  if (steps == 255) { p.level = !inverted; return p; }       // lit: LOW if active-low
  const uint32_t high = inverted ? 255u - steps : steps;     // HIGH share, in 1/255
  p.sigmaDelta = true;
  p.duty = (uint8_t)((high * 256u + 127u) / 255u);           // round(high * 256 / 255), 1..255
  return p;
}

// The same output? A fixed plan is the same whatever its unused duty is, and a
// sigma-delta plan whatever its unused level is.
inline bool backlightPlanSame(const BacklightPlan& a, const BacklightPlan& b) {
  if (a.sigmaDelta != b.sigmaDelta) return false;
  return a.sigmaDelta ? a.duty == b.duty : a.level == b.level;
}

// The sigma-delta clock in Hz for BL_SD_PRESCALER.
inline uint32_t backlightClockHz() { return 80000000u / (BL_SD_PRESCALER + 1u); }

// Pulses per second at a duty (core sigma_delta.h: clock * target / 256 below
// 128, clock * (256 - target) / 256 above). Computed, not measured.
inline uint32_t backlightPulseHz(uint8_t duty) {
  const uint32_t d = duty <= 128 ? duty : 256u - duty;
  return (uint32_t)(((uint64_t)backlightClockHz() * d) / 256u);
}

}  // namespace smalltv
