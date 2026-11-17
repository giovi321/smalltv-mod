// RainMode.h — live rain radar feature (RainViewer, past 2h).
//
// Draws a RainToday-style radar centred on the configured home lat/lon:
// range rings + crosshair + north, the rain field in a blue -> purple ramp,
// nearby cities as reference dots, the home position as a green dot, and a
// timeline at the bottom spanning the frame set (T-2h -> now). Owns its
// fetch (RainClient) and its render/animation state.
#pragma once
#include "Mode.h"
#include "config.h"
#include "RainClient.h"

void rainModeFree();   // release the render-side heap (city cache)

class RainMode : public DisplayMode {
 public:
  const char* id() const override { return "rain"; }
  uint8_t     modeConst() const override { return MODE_RAIN; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;   // repaint only
  // Mode switched away, or a big web request (GET /api/config ~4 KB, save,
  // export) needs the heap: give back the city cache, rebuilt on the next
  // render.
  void freeForRequest() override { rainModeFree(); rainFreeForRequest(); }
  // No data members: the mode's state (animation, fingerprint, render
  // timings) is allocated while rain is in use (RainMode.cpp, RainView), so
  // with rain off the object is its vtable pointer.
};

extern RainMode g_rainMode;

// Render cost of the last full draw, surfaced in /api/status (rain.drawMs,
// blitMs, citiesMs) to drive the transition-optimisation work.
uint32_t rainDrawMs();
uint32_t rainBlitMs();
uint32_t rainCitiesMs();
