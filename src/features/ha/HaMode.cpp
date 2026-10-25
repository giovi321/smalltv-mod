// HaMode.cpp — see HaMode.h. Dirty-flag render pattern follows TickerMode.
#include "HaMode.h"
#if WITH_HA
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "HaScreens.h"
#include "HaIcons.h"

HaMode g_haMode;

// Replay one stored screen onto the panel. Every colour goes through gfxTint()
// so the Display tab's per-channel gain applies to pushed screens too.
static void renderScreen(const HaScreen& sc, uint8_t pageIndex, uint8_t pageCount) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(gfxTint(sc.bg));

  for (uint8_t i = 0; i < sc.primCount; i++) {
    const HaPrim& p = sc.prims[i];
    uint16_t c = gfxTint(p.color);
    switch (p.type) {
      case HA_P_FILL:
        gfx->fillScreen(c);
        break;
      case HA_P_RECT:
        gfx->fillRect(p.x, p.y, p.x2, p.y2, c);
        break;
      case HA_P_RRECT:
        gfx->fillRoundRect(p.x, p.y, p.x2, p.y2, p.aux, c);
        break;
      case HA_P_CIRCLE:
        gfx->fillCircle(p.x, p.y, p.aux, c);
        break;
      case HA_P_LINE:
        gfx->drawLine(p.x, p.y, p.x2, p.y2, c);
        break;
      case HA_P_TEXT: {
        const char* v = sc.text + p.voff;
        uint8_t sz = p.aux ? p.aux : 1;
        int w = gfxTextW(v, sz);
        int x = (p.align == HA_A_CENTER) ? p.x - w / 2
              : (p.align == HA_A_RIGHT)  ? p.x - w : p.x;
        gfx->setTextSize(sz);
        gfx->setTextColor(c);
        gfx->setCursor(x, p.y);
        gfx->print(v);
        break;
      }
      case HA_P_ICON: {
        const char* v = sc.text + p.voff;
        uint8_t sz = p.aux ? p.aux : 1;
        int w = 24 * sz;   // the icon's 24x24 logical box scaled by s
        int x = (p.align == HA_A_CENTER) ? p.x - w / 2
              : (p.align == HA_A_RIGHT)  ? p.x - w : p.x;
        haDrawIcon(v, x, p.y, sz, c);   // unknown names skip silently
        break;
      }
      case HA_P_BITMAP: {
        // Same anchor math as the icon, but on the RENDERED box: s upscales
        // each source pixel to an sxs block, so the box is w*s x h*s; y is
        // always the top edge. Set bits draw in c, clear bits leave the
        // background untouched (transparent). Parse already rejects boxes
        // that would exceed the panel.
        int w = p.x2, h = p.y2;
        int s = p.aux ? p.aux : 1;
        int rw = w * s;
        int x = (p.align == HA_A_CENTER) ? p.x - rw / 2
              : (p.align == HA_A_RIGHT)  ? p.x - rw : p.x;
        const uint8_t* bits = sc.bitmap + p.voff;
        for (int py = 0; py < h; py++)
          for (int px = 0; px < w; px++) {
            int i = py * w + px;
            if (bits[i >> 3] & (0x80 >> (i & 7))) {
              if (s == 1) gfx->drawPixel(x + px, p.y + py, c);
              else        gfx->fillRect(x + px * s, p.y + py * s, s, s, c);
            }
          }
        break;
      }
    }
  }

  // Page dots like TickerMode, only once there is something to rotate through.
  if (pageCount > 1) {
    int total = pageCount * 10 - 4;
    int x0 = (TFT_WIDTH - total) / 2;
    for (uint8_t i = 0; i < pageCount; i++)
      gfx->fillCircle(x0 + i * 10 + 2, 230, 2, i == pageIndex ? C_WHITE : C_DGRAY);
  }
}

// Empty store: say what the device is waiting for and where to publish.
static void renderWaiting(const Settings& s) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered("Waiting for", 66, 2, C_GRAY);
  gfxDrawCentered("Home Assistant", 88, 2, C_WHITE);
  char topic[96];
  snprintf(topic, sizeof(topic), "smalltv/%s/screen/<slot>", s.hostname.c_str());
  gfxDrawCentered("publish retained JSON to", 128, 1, C_GRAY);
  gfxDrawCentered(topic, 142, gfxFitSize(topic, 232, 2), C_YELLOW);
  gfxDrawCentered("broker not set? see the", 176, 1, C_DGRAY);
  gfxDrawCentered("MQTT card in settings", 188, 1, C_DGRAY);
}

void HaMode::begin(const Settings& s) {
  (void)s;
  haScreensBegin();          // load the persisted screens once
  page_ = 0;
  lastLive_ = 0xFF;
  lastRotate_ = millis();
  needRender_ = true;
}

void HaMode::invalidate(const Settings& s) {
  (void)s;
  haScreensReload();         // re-read /ha_screens.json
  page_ = 0;
  lastLive_ = 0xFF;
  lastRotate_ = millis();
  needRender_ = true;
}

void HaMode::nextPage() {
  uint8_t live = haScreensLive();
  if (live > 1) page_ = (page_ + 1) % live;
  lastRotate_ = millis();    // the auto-rotation dwell restarts on the page just chosen
  needRender_ = true;
}

void HaMode::service(const Settings& s) {
  uint8_t live = haScreensLive();

  // A screen arrived / changed / was deleted over MQTT.
  if (haScreensTakeDirty()) needRender_ = true;

  // A ttl expired (or one arrived while we looked elsewhere): the rotation
  // changed shape.
  if (live != lastLive_) {
    lastLive_ = live;
    if (page_ >= live) page_ = 0;
    needRender_ = true;
  }

  // Rotate at the configured dwell.
  if (live > 1 && millis() - lastRotate_ >= (uint32_t)s.ha.dwellSec * 1000UL) {
    page_ = (page_ + 1) % live;
    lastRotate_ = millis();
    needRender_ = true;
  }

  if (needRender_) {
    render(s, live);
    needRender_ = false;
  }
}

void HaMode::render(const Settings& s, uint8_t live) {
  if (!live) { renderWaiting(s); return; }
  const HaScreen* sc = haScreenAt(page_);
  if (!sc) { page_ = 0; sc = haScreenAt(0); }
  if (!sc) { renderWaiting(s); return; }
  renderScreen(*sc, page_, live);
}

#endif  // WITH_HA
