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
