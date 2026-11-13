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

class RainMode : public DisplayMode {
 public:
  const char* id() const override { return "rain"; }
  uint8_t     modeConst() const override { return MODE_RAIN; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }  // repaint only
  // Les grosses requêtes web (GET /api/config ~4 Ko, save, export) exigent le
  // heap : la fenêtre LZ du fetch (8 Ko) est rendue, le service la réalloue à
  // l'étape suivante. Sans cela, la page web pouvait échouer en plein fetch.
  void freeForRequest() override { rainFreeForRequest(); }

 private:
  void render(const Settings& s);

  // Empreinte des réglages qui façonnent les frames (home + portée) : un
  // invalidate() ne réinitialise le fetch que s'ils ont changé. Un save de
  // config qui ne touche pas le radar (luminosité, tickers, carrousel, uiScale…)
  // ne doit pas annuler un fetch en cours : il coûte ~5-6 min, re-télécharge
  // tout, et l'appareil cesse de répondre en HTTP pendant ce temps (vécu).
  float   fetchLat_ = 0.0f;
  float   fetchLon_ = 0.0f;
  int16_t fetchRange_ = -1;     // -1 = jamais initialisé (10..100 sinon)

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
