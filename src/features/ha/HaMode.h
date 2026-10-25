// HaMode.h — Home Assistant screens display mode (features/ha).
//
// Renders the screens Home Assistant pushed over MQTT (see HaScreens.h for the
// store and the wire contract). Rotates through the live (non-expired) screens
// in slot order, dwelling s.ha.dwellSec on each; a screen arriving, changing,
// or expiring repaints immediately. With an empty store it shows where to
// publish instead of a black panel.
#pragma once
#include "Mode.h"
#include "config.h"

class HaMode : public DisplayMode {
 public:
  const char* id() const override { return "ha"; }
  uint8_t     modeConst() const override { return MODE_HA; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;   // settings changed: reload + repaint
  void wake(const Settings& s) override {        // carousel switch: repaint only
    needRender_ = true;
    lastRotate_ = millis();
  }
  void nextPage();                               // physical button: show the next live screen

 private:
  void render(const Settings& s, uint8_t live);

  uint8_t page_ = 0;
  uint8_t lastLive_ = 0xFF;    // live-screen count as last rendered
  uint32_t lastRotate_ = 0;
  bool     needRender_ = true;
};

extern HaMode g_haMode;
