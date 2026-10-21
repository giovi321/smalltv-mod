// NotifyMode.h — transient full-screen attention overlay. Armed over HTTP, kept
// out of the main.cpp mode registry, never persisted.
//
// One overlay is on the panel at a time; the rest wait in a small priority queue
// and take over as each expires, so a burst of events is shown in turn instead
// of the last one erasing the others.
#pragma once
#include "Mode.h"
#include "Gfx.h"
#include "config.h"

#define NOTIFY_LABEL_SIZE  2

// What one POST /api/notify asks for. Everything is optional: an unset field
// falls back to the preset named by `type`, and an unset `type` falls back to
// "info". WebPortal fills this straight from the JSON and does no interpreting
// of its own — names, colours and clamping are resolved in NotifyMode.cpp,
// where the preset table lives.
struct NotifyRequest {
  const char* type   = nullptr;   // preset name; also accepts the legacy "state"
  const char* title  = nullptr;   // overrides the preset's word
  const char* label  = nullptr;   // scrolls when wider than the panel
  const char* anim   = nullptr;   // overrides the preset's animation
  const char* color  = nullptr;   // "#rrggbb" or a colour name; overrides the preset
  uint32_t    ttlSec = 0;         // 0 -> the preset's own hold time
  int         prio   = -1;        // <0 -> the preset's own priority
};

// One queued or on-screen overlay, resolved down to what the renderer needs.
struct NotifyItem {
  char     label[NOTIFY_LABEL_MAX + 1] = {0};
  char     title[NOTIFY_TITLE_MAX + 1] = {0};
  uint16_t color  = 0;            // RGB565, untinted
  uint8_t  anim   = 0;
  uint8_t  prio   = NOTIFY_PRIO_LOW;
  uint8_t  type   = 0;            // preset index, kept only to recognise a repeat
  uint16_t ttlSec = NOTIFY_TTL_DEFAULT_SEC;
};

// What the endpoint answers with. A full queue is deliberately not the same
// outcome as a malformed request: one is worth retrying, the other never is.
enum NotifyResult {
  NOTIFY_ACCEPTED,
  NOTIFY_REJECTED,     // unknown type/animation/colour -> 400
  NOTIFY_QUEUE_FULL,   // nothing less important to displace -> 429
};

class NotifyMode : public DisplayMode {
 public:
  const char* id() const override { return "notify"; }
  uint8_t     modeConst() const override { return MODE_NOTIFY; }

  void service(const Settings& s) override;

  // Show it now, restart what is on screen if this repeats it, or queue it.
  NotifyResult request(const NotifyRequest& r);

  // True while the overlay owns the panel. Time-gated on purpose: main.cpp
  // returns before the notify block in safe mode and in NET_AP, so service() —
  // the only place armed_ is cleared — may never run for an armed overlay. If
  // this reported ownership from armed_ alone, such an overlay would never
  // expire and would credit the carousel for the whole time the device sat in
  // that state.
  bool    active() const { return owns(); }
  uint8_t queued() const { return qCount_; }

  // Acknowledge the overlay on screen: expire it now so the next queued item, or
  // the underlying feature, takes over on the next tick exactly as a natural TTL
  // expiry would. A no-op when nothing owns the panel.
  void dismiss() { if (owns()) untilMs_ = millis(); }

  // How long the overlay owned the panel, across the whole run rather than the
  // last request in it, so main.cpp can credit the carousel after active() has
  // gone false. Never exceeds what the run was entitled to, which is what keeps
  // a starved service() from crediting real time it never spent on the panel.
  uint32_t heldMs() const;

 private:
  bool owns() const;
  void retire();
  void draw(bool full);
  void drawLabelBand(bool full);
  void startItem(const NotifyItem& it);
  void advance();
  bool enqueue(const NotifyItem& it);
  void copyText(char* dst, size_t cap, const char* src);
  static bool sameEvent(const NotifyItem& a, const NotifyItem& b);

  NotifyItem cur_;
  NotifyItem queue_[NOTIFY_QUEUE_DEPTH];
  uint8_t    qCount_ = 0;

  uint32_t startedMs_ = 0;        // current item
  uint32_t untilMs_   = 0;
  uint16_t frame_     = 0;
  uint32_t frameStartMs_ = 0;

  int      scrollW_   = 0;        // label pixel width, 0 when it fits (no scroll)
  int      scrollX_   = 0;
  uint32_t scrollStepMs_ = 0;

  uint32_t overlayStartedMs_ = 0; // whole run, for the carousel credit
  uint32_t lastHeldMs_ = 0;

  bool armed_  = false;
  bool primed_ = false;
};

extern NotifyMode g_notifyMode;
