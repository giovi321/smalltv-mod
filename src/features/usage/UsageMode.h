// UsageMode.h — Claude usage meter feature.
//
// Shows 5h/7d usage bars + a small mascot when data is flowing, and an animated
// pixel-art mascot when the daemon goes quiet. Owns its fetch (UsageClient), its
// mascot animation (Mascot) and its render/dirty state.
#pragma once
#include "Mode.h"
#include "config.h"
#include "UsageData.h"

class UsageMode : public DisplayMode {
 public:
  const char* id() const override { return "usage"; }
  uint8_t     modeConst() const override { return MODE_USAGE; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  // repaint only (no refetch) — another mode may have drawn over the screen since,
  // so force the full layout back in, not just a value diff. showingMascot_ is
  // cleared too: without it a wake mid-animation re-enters the idle mascot on the
  // incremental (diff) path, which skips the full-screen clear and leaves the old
  // page bleeding through between cells. Clearing it forces a clean restart draw.
  void wake(const Settings& s) override {
    needRender_ = true;
    layoutPrimed_ = false;
    showingMascot_ = false;
  }

 private:
  bool contentChanged(const UsageData& u) const;
  void rememberContent(const UsageData& u);
  bool serviceFlash();

  // Prompt activity: the last state seen, so the switch to DONE flashes the
  // panel once (and a wake or mode re-entry does not flash it again).
  uint8_t  lastAct_ = 0;
  bool     flashing_ = false;
  uint32_t flashStartMs_ = 0;
  int8_t   flashPhase_ = -1;

  uint32_t usageSampled_ = 0;              // lastOkMs already fed to the mascot tracker
  uint32_t usageRenderedOk_ = 0xFFFFFFFF;
  bool     showingMascot_ = false;
  bool     needRender_ = true;

  // Last content actually painted — compared in contentChanged() instead of
  // lastOkMs, since the daemon re-POSTs unchanged data too.
  bool     contentPrimed_ = false;

  // Whether the static layout (black bg + header) is currently painted, so a
  // routine update can skip the full-screen clear.
  bool     layoutPrimed_ = false;
  float    lastSessionPct_ = -1;
  float    lastWeeklyPct_ = -1;
  int      lastSessionResetMin_ = -1;
  int      lastWeeklyResetMin_ = -1;
  char     lastStatus_[16] = {0};
  bool     lastValid_ = false;
  bool     lastError_ = false;
};

extern UsageMode g_usageMode;
