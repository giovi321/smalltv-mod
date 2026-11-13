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

class RainMode : public DisplayMode {
 public:
  const char* id() const override { return "rain"; }
  uint8_t     modeConst() const override { return MODE_RAIN; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }  // repaint only

 private:
  void render(const Settings& s);

  uint32_t renderedOk_ = 0xFFFFFFFF;
  bool     renderedError_ = false;
  bool     needRender_ = true;
  uint8_t  animIdx_ = 0;        // frame shown in the loop
  uint32_t nextAnimMs_ = 0;
};

extern RainMode g_rainMode;

// Render cost of the last full draw, surfaced in /api/status (rain.drawMs,
// blitMs, citiesMs) to drive the transition-optimisation work.
uint32_t rainDrawMs();
uint32_t rainBlitMs();
uint32_t rainCitiesMs();
