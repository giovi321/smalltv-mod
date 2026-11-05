#include "UsageMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "UsageClient.h"
#include "Mascot.h"

UsageMode g_usageMode;

// Claude-usage palette (Anthropic-inspired dark theme, RGB565 of the originals).
// Through gfxTint() like the shared palette, so the Display tab's colour
// correction reaches these too.
#define C_ACCENT  gfxTint(0xDBAA)   // terra-cotta 0xd97757
#define C_UGREEN  gfxTint(0x7C6B)   // green 0x788c5d
#define C_PANEL   gfxTint(0x18E3)   // card fill 0x1f1f1e
#define C_BARBG   gfxTint(0x2945)   // unfilled bar track 0x2a2a28
#define C_DIM     gfxTint(0xB574)   // secondary text 0xb0aea5

// Mascot diff state for the flicker-free full-screen idle animation.
static bool            s_mascotPrimed  = false;
static const uint16_t* s_mascotPalette = nullptr;
static uint8_t         s_prevCells[MASCOT_GRID * MASCOT_GRID];

// Copy a mascot palette into a local RAM array using *byte* reads. pgm_read_byte
// is safe from both RAM and flash; a 16-bit load straight from flash (irom) faults
// on the ESP8266, so this never depends on where the palette actually lives.
// Tinted on the way out so the mascot's own palette follows the Display tab's
// colour correction like everything else.
static void loadPalette(const uint16_t* palette, uint16_t* out) {
  const uint8_t* p = (const uint8_t*)palette;
  for (int k = 0; k < MASCOT_PALETTE_SIZE; k++)
    out[k] = gfxTint((uint16_t)(pgm_read_byte(p + 2 * k) | (pgm_read_byte(p + 2 * k + 1) << 8)));
}

// Draw a 20x20 mascot frame at (x0,y0), cellPx per cell. Reads PROGMEM frame data.
static void blitMascot(Arduino_GFX* gfx, const uint8_t* cells, const uint16_t* palette,
                       int x0, int y0, int cellPx) {
  uint16_t pal[MASCOT_PALETTE_SIZE];
  loadPalette(palette, pal);
  for (int i = 0; i < MASCOT_GRID * MASCOT_GRID; i++) {
    uint8_t code = pgm_read_byte(&cells[i]);
    uint16_t color = (code < MASCOT_PALETTE_SIZE) ? pal[code] : 0;
    int gx = i % MASCOT_GRID, gy = i / MASCOT_GRID;
    gfx->fillRect(x0 + gx * cellPx, y0 + gy * cellPx, cellPx, cellPx, color);
  }
}

static void fmtReset(int mins, char* out, size_t n) {
  if (mins <= 0) { strlcpy(out, "now", n); return; }
  int d = mins / 1440, h = (mins % 1440) / 60, m = mins % 60;
  if (d > 0)      snprintf(out, n, "%dd %dh", d, h);
  else if (h > 0) snprintf(out, n, "%dh %02dm", h, m);
  else            snprintf(out, n, "%dm", m);
}

static uint16_t barColor(float pct) {
  if (pct >= 90) return C_RED;
  if (pct >= 75) return C_ACCENT;
  return C_UGREEN;
}

// One usage card: big %, a 5h/7d label, a fill bar coloured by load, and the
// reset countdown. `top` is the card's top y; the card is 82px tall.
static void drawMeter(Arduino_GFX* gfx, int top, const char* label,
                      float pct, int resetMins) {
  const int x = 8, w = 224, h = 82;
  gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  char pc[8];
  snprintf(pc, sizeof(pc), "%d%%", (int)lroundf(constrain(pct, 0.0f, 100.0f)));
  uint8_t sz = gfxFitSize(pc, 150, 5);
  gfx->setTextSize(sz);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(x + 14, top + 10);
  gfx->print(pc);

  int lw = gfxTextW(label, 2);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(x + w - lw - 14, top + 12);
  gfx->print(label);

  int bx = x + 14, by = top + 52, bw = w - 28, bh = 12;
  gfx->fillRoundRect(bx, by, bw, bh, bh / 2, C_BARBG);
  int fw = (int)(bw * constrain(pct, 0.0f, 100.0f) / 100.0f);
  if (fw >= bh)     gfx->fillRoundRect(bx, by, fw, bh, bh / 2, barColor(pct));
  else if (fw > 0)  gfx->fillRect(bx, by, fw, bh, barColor(pct));

  char rs[16], line[28];
  fmtReset(resetMins, rs, sizeof(rs));
  snprintf(line, sizeof(line), "Resets in %s", rs);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(x + 14, top + 64);
  gfx->print(line);
}

// The 7-day window length in minutes, for the even-pace marker.
#define WEEK_MIN 10080

// Single-card layout for plans without a 5-hour limit: one tall card holding
// the 7-day figure. The percentage gets the extra room, the bar is thicker and
// carries an even-pace marker (where usage would be if spread evenly over the
// week), and a pace line under the countdown says whether you are ahead of it.
// The card spans y=52..232 and repaints its whole area, like drawMeter().
static void drawWeekly(Arduino_GFX* gfx, float pct, int resetMins) {
  const int x = 8, top = 52, w = 224, h = 180;
  gfx->fillRoundRect(x, top, w, h, 10, C_PANEL);
  pct = constrain(pct, 0.0f, 100.0f);

  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(x + 14, top + 14);
  gfx->print("7-DAY USAGE");

  char pc[8];
  snprintf(pc, sizeof(pc), "%d%%", (int)lroundf(pct));
  uint8_t sz = gfxFitSize(pc, w - 28, 7);
  gfxDrawCentered(pc, top + 44, sz, C_WHITE);

  const int bx = x + 14, by = top + 110, bw = w - 28, bh = 18;
  gfx->fillRoundRect(bx, by, bw, bh, bh / 2, C_BARBG);
  int fw = (int)(bw * pct / 100.0f);
  if (fw >= bh)     gfx->fillRoundRect(bx, by, fw, bh, bh / 2, barColor(pct));
  else if (fw > 0)  gfx->fillRect(bx, by, fw, bh, barColor(pct));

  // Even pace: the share of the week already elapsed. Only meaningful while the
  // countdown is inside one week.
  bool havePace = resetMins > 0 && resetMins <= WEEK_MIN;
  float pace = havePace ? 100.0f * (WEEK_MIN - resetMins) / WEEK_MIN : 0;
  if (havePace) {
    int px = bx + (int)(bw * pace / 100.0f);
    px = constrain(px, bx + 1, bx + bw - 3);
    gfx->fillRect(px, by - 5, 2, bh + 10, C_WHITE);
  }

  char rs[16], line[28];
  fmtReset(resetMins, rs, sizeof(rs));
  snprintf(line, sizeof(line), "Resets in %s", rs);
  gfxDrawCentered(line, top + 140, 2, C_DIM);

  if (havePace) {
    snprintf(line, sizeof(line), "Even pace %d%%", (int)lroundf(pace));
    // Amber once usage runs 5 points or more ahead of an even burn.
    gfxDrawCentered(line, top + 160, 2, pct >= pace + 5 ? C_ACCENT : C_DIM);
  }
}

// Last-drawn state of the accent flag, so a routine update can toggle just the
// dot instead of a full-screen clear.
static bool s_flagShown = false;

// `fullRepaint` clears the static layout only on a real transition — cards and
// the flag dot always repaint their own area, so routine updates skip it.
static void drawUsage(const UsageData& u, bool fullRepaint, bool hide5h) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;

  if (fullRepaint) {
    s_mascotPrimed = false;   // force a full redraw next time the idle animation shows
    gfx->fillScreen(C_BLACK);

    // Header: a small calm mascot pose + title.
    blitMascot(gfx, mascotIdleCells(), mascotIdlePalette(), 6, 4, 2);
    gfx->setTextSize(3);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(56, 12);
    gfx->print("CLAUDE");
    s_flagShown = false;
  }

  if (!u.valid) {
    if (fullRepaint) gfxDrawCentered(u.error ? "daemon error" : "waiting...", 120, 2, C_DIM);
    return;
  }

  // A status that is not calm gets a small accent flag; erasing with black is
  // safe here since nothing else ever draws at x=228. The daemon has shipped two
  // status vocabularies: the old rate-limit headers said "allowed" /
  // "allowed_warning" / "rejected", the usage endpoint says "normal" / "warning"
  // / "rejected". Both calm values clear the flag. Compare exactly, because the
  // old 7-char prefix match on "allowed" also swallowed "allowed_warning".
  bool showFlag = u.status[0]
               && strcmp(u.status, "allowed") != 0
               && strcmp(u.status, "normal")  != 0;
  if (showFlag != s_flagShown || fullRepaint) {
    gfx->fillCircle(228, 18, 5, showFlag ? C_ACCENT : C_BLACK);
    s_flagShown = showFlag;
  }

  // Plans without a 5-hour limit can hide that card; the 7-day figure then gets
  // its own full-height layout.
  if (hide5h) {
    drawWeekly(gfx, u.weeklyPct, u.weeklyResetMin);
  } else {
    drawMeter(gfx, 50,  "5h", u.sessionPct, u.sessionResetMin);
    drawMeter(gfx, 138, "7d", u.weeklyPct,  u.weeklyResetMin);
  }
}

// Prompt activity indicator, in the free header space right of "CLAUDE": three
// pulsing dots while a prompt runs, an accent DONE badge once it finished. The
// box spans x=166..221; the status flag dot sits just right of it at x=223..233,
// so neither ever paints over the other.
#define ACT_X     166
#define ACT_Y     7
#define ACT_W     56
#define ACT_H     22
#define C_ACCDIM  gfxTint(0x5984)   // dimmed terra-cotta 0x5a3124, resting dots

static UsageActivity s_actShown = ACT_IDLE;
static int8_t        s_dotPhase = -1;    // -1 = dots not drawn yet
static uint32_t      s_dotNextMs = 0;

// `force` repaints after a full-screen clear; otherwise only a state change or
// the next dot frame draws anything.
static void drawActivity(bool force) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  UsageActivity a = usageActivity();
  if (force || a != s_actShown) {
    gfx->fillRect(ACT_X, ACT_Y, ACT_W, ACT_H, C_BLACK);
    s_actShown = a;
    s_dotPhase = -1;
    if (a == ACT_DONE) {
      gfx->fillRoundRect(ACT_X, ACT_Y, ACT_W, ACT_H, 6, C_ACCENT);
      gfx->setTextSize(2);
      gfx->setTextColor(C_BLACK);
      gfx->setCursor(ACT_X + (ACT_W - gfxTextW("DONE", 2)) / 2 + 1, ACT_Y + 4);
      gfx->print("DONE");
    }
  }
  if (a != ACT_RUNNING) return;

  uint32_t now = millis();
  if (s_dotPhase >= 0 && (int32_t)(now - s_dotNextMs) < 0) return;
  s_dotNextMs = now + 220;
  s_dotPhase = (int8_t)((s_dotPhase + 1) % 4);   // 0..2 = that dot is up, 3 = all rest
  for (int i = 0; i < 3; i++) {
    int cx = ACT_X + 10 + i * 18;
    bool up = (i == s_dotPhase);
    gfx->fillRect(cx - 5, ACT_Y, 11, ACT_H, C_BLACK);
    gfx->fillCircle(cx, ACT_Y + (up ? 8 : 12), 4, up ? C_ACCENT : C_ACCDIM);
  }
}

// One frame of the DONE flash: the whole panel in the accent colour with a big
// DONE, or black between the blinks.
#define FLASH_FRAME_MS  250
#define FLASH_FRAMES    6     // on, off, on, off, on, off = 1.5 s
static void drawFlashFrame(bool on) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(on ? C_ACCENT : C_BLACK);
  if (on) gfxDrawCentered("DONE", 98, 6, C_BLACK);
}

// Idle animation: full-screen mascot, diffed cell-by-cell for a flicker-free draw.
static void drawMascot(const uint8_t* cells, const uint16_t* palette, bool restart) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx || !cells || !palette) return;
  uint16_t pal[MASCOT_PALETTE_SIZE];
  loadPalette(palette, pal);
  const int CP = TFT_WIDTH / MASCOT_GRID;                 // 240 / 20 = 12
  const int x0 = (TFT_WIDTH  - MASCOT_GRID * CP) / 2;
  const int y0 = (TFT_HEIGHT - MASCOT_GRID * CP) / 2;

  // Full redraw on (re)entry or whenever the palette changes (animation switch);
  // otherwise only repaint the cells that changed since the last frame.
  bool full = restart || !s_mascotPrimed || palette != s_mascotPalette;
  if (full) gfx->fillScreen(C_BLACK);

  for (int i = 0; i < MASCOT_GRID * MASCOT_GRID; i++) {
    uint8_t code = pgm_read_byte(&cells[i]);
    if (!full && code == s_prevCells[i]) continue;
    s_prevCells[i] = code;
    uint16_t color = (code < MASCOT_PALETTE_SIZE) ? pal[code] : 0;
    int gx = i % MASCOT_GRID, gy = i / MASCOT_GRID;
    gfx->fillRect(x0 + gx * CP, y0 + gy * CP, CP, CP, color);
  }
  s_mascotPrimed  = true;
  s_mascotPalette = palette;
}

// The daemon re-POSTs on a fixed timer even when nothing changed, and
// drawUsage() does a full fillScreen — without this check every push flashes.
bool UsageMode::contentChanged(const UsageData& u) const {
  if (!contentPrimed_) return true;
  return u.valid != lastValid_
      || u.error != lastError_
      || u.sessionPct != lastSessionPct_
      || u.weeklyPct != lastWeeklyPct_
      || u.sessionResetMin != lastSessionResetMin_
      || u.weeklyResetMin != lastWeeklyResetMin_
      || strncmp(u.status, lastStatus_, sizeof(lastStatus_)) != 0;
}

void UsageMode::rememberContent(const UsageData& u) {
  contentPrimed_ = true;
  lastValid_ = u.valid;
  lastError_ = u.error;
  lastSessionPct_ = u.sessionPct;
  lastWeeklyPct_ = u.weeklyPct;
  lastSessionResetMin_ = u.sessionResetMin;
  lastWeeklyResetMin_ = u.weeklyResetMin;
  strlcpy(lastStatus_, u.status, sizeof(lastStatus_));
}

// ---- DisplayMode ----------------------------------------------------------
void UsageMode::begin(const Settings& s) {
  usageInit(s);
  mascotInit();
  usageSampled_ = 0;
  usageRenderedOk_ = 0xFFFFFFFF;
  showingMascot_ = false;
  needRender_ = true;
  contentPrimed_ = false;
  layoutPrimed_ = false;
  lastAct_ = usageActivity();   // a DONE from before entering the mode does not flash
  flashing_ = false;
}

void UsageMode::invalidate(const Settings& s) {
  needRender_ = true;
  showingMascot_ = false;
  usageRenderedOk_ = 0xFFFFFFFF;
  contentPrimed_ = false;
  layoutPrimed_ = false;
  usageInit(s);
  usageForceRefresh();
}

// Blinks the panel when a prompt finishes, so it is noticed as a notification.
// Non-blocking: one frame per call, then the normal screen is repainted with
// the DONE badge. Returns true while the flash owns the panel.
bool UsageMode::serviceFlash() {
  uint8_t act = usageActivity();
  if (act == ACT_DONE && lastAct_ != ACT_DONE) {
    flashing_ = true;
    flashStartMs_ = millis();
    flashPhase_ = -1;
  }
  lastAct_ = act;
  if (!flashing_) return false;

  int phase = (int)((millis() - flashStartMs_) / FLASH_FRAME_MS);
  if (phase >= FLASH_FRAMES) {
    flashing_ = false;
    // Same as wake(): bring the whole layout back, the badge with it.
    needRender_ = true;
    layoutPrimed_ = false;
    showingMascot_ = false;
    return false;
  }
  if (phase != flashPhase_) {
    flashPhase_ = (int8_t)phase;
    drawFlashFrame(phase % 2 == 0);
  }
  return true;
}

void UsageMode::service(const Settings& s) {
  if (serviceFlash()) return;

  // Pull mode: poll the daemon when a Usage URL is set. Push mode: leave it blank
  // and the daemon POSTs to /api/usage (for networks where the device can't reach
  // the PC). Either way usageGet() drives the render below.
  if (s.usage.usageUrl.length() >= 8) usageService(s);

  const UsageData& u = usageGet();

  // Feed the burn-rate tracker once per fresh reading (drives the mascot's mood).
  if (u.valid && u.lastOkMs != usageSampled_) {
    usageSampled_ = u.lastOkMs;
    // With the 5h card hidden the session window may no longer exist, so the
    // mood follows the 7-day figure instead.
    mascotSample(s.usage.hide5h ? u.weeklyPct : u.sessionPct);
  }

  // Considered stale after ~2 missed polls (plus a grace) — then show the animation.
  uint32_t staleMs = (uint32_t)s.usage.pollSec * 1000UL * 2UL + USAGE_STALE_GRACE_MS;

  if (usageFresh(staleMs)) {
    bool fullRepaint = !layoutPrimed_;
    if (showingMascot_) { showingMascot_ = false; needRender_ = true; fullRepaint = true; }
    if (u.lastOkMs != usageRenderedOk_) {
      usageRenderedOk_ = u.lastOkMs;
      if (contentChanged(u)) {
        if (u.valid != lastValid_) fullRepaint = true;   // layout itself changes shape
        rememberContent(u);
        needRender_ = true;
      }
    }
    bool cleared = false;
    if (needRender_) {
      drawUsage(u, fullRepaint, s.usage.hide5h);
      cleared = fullRepaint;
      layoutPrimed_ = true;
      needRender_ = false;
    }
    // Stats screen only: the idle mascot covers the whole panel, and while a
    // prompt runs the daemon is up anyway, so the stats are what is showing.
    drawActivity(cleared);
  } else {
    if (!showingMascot_) {
      showingMascot_ = true;
      usageRenderedOk_ = 0xFFFFFFFF;
      mascotReset();
      drawMascot(mascotCells(), mascotPalette(), /*restart=*/true);
    } else if (mascotTick()) {
      drawMascot(mascotCells(), mascotPalette(), /*restart=*/false);
    }
  }
}
