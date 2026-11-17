// The display layer for the simulator: the same API as src/Gfx.cpp, drawing
// with the real GFX Library for Arduino into an in-memory 240x240 canvas.
// The text helpers and message screens are copied from src/Gfx.cpp; the
// streamed blit writes rows into the canvas the way the panel's address
// window fills (left to right, top to bottom).
#include "Gfx.h"
#include <Arduino_GFX_Library.h>

static Arduino_Canvas* g_canvas = nullptr;
static Arduino_GFX* gfx = nullptr;
static uint32_t g_blitPos = 0;

void simGfxBegin() {
  g_canvas = new Arduino_Canvas(TFT_WIDTH, TFT_HEIGHT, nullptr);
  g_canvas->begin(GFX_SKIP_OUTPUT_BEGIN);
  gfx = g_canvas;
  gfx->fillScreen(0);
}
uint16_t* simFramebuffer() { return g_canvas->getFramebuffer(); }

uint16_t gfxTint(uint16_t c) { return c; }   // Display tab gains at their default (100 %)
Arduino_GFX* gfxDev() { return gfx; }

void gfxBlitBegin() { g_blitPos = 0; }
void gfxBlitRow(uint16_t* row, uint32_t len) {
  uint16_t* fb = simFramebuffer();
  for (uint32_t i = 0; i < len && g_blitPos < (uint32_t)TFT_WIDTH * TFT_HEIGHT; i++) fb[g_blitPos++] = row[i];
}
void gfxBlitEnd() {}

// ---- copied from src/Gfx.cpp ------------------------------------------------
int gfxTextW(const char* s, uint8_t size) { return (int)strlen(s) * GFX_FONT_W * size; }

void gfxDrawCentered(const char* s, int y, uint8_t size, uint16_t color) {
  if (!gfx) return;
  int x = (TFT_WIDTH - gfxTextW(s, size)) / 2;
  if (x < 0) x = 0;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

uint8_t gfxFitSize(const char* s, int maxW, uint8_t maxSize) {
  for (uint8_t sz = maxSize; sz > 1; sz--) {
    if (gfxTextW(s, sz) <= maxW) return sz;
  }
  return 1;
}

void gfxBoot(const char* line1, const char* line2) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(line1, 95, 3, C_WHITE);
  if (line2 && line2[0]) gfxDrawCentered(line2, 130, 2, C_GRAY);
}

void gfxStaInfo(const char* ssid, const char* ip, const char* host) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered("CONNECTED", 18, 3, C_GREEN);
  gfxDrawCentered("Network:", 62, 2, C_GRAY);
  gfxDrawCentered(ssid && ssid[0] ? ssid : "-", 84, gfxFitSize(ssid, 232, 3), C_WHITE);
  gfxDrawCentered("Open in browser:", 126, 2, C_GRAY);
  gfxDrawCentered(ip && ip[0] ? ip : "-", 150, gfxFitSize(ip, 232, 3), C_GREEN);
  if (host && host[0]) {
    String url = String("http://") + host + ".local";
    gfxDrawCentered(url.c_str(), 188, gfxFitSize(url.c_str(), 232, 2), C_GRAY);
  }
}

void gfxMessage(const char* title, const char* msg, uint16_t titleColor) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(title, 90, 3, titleColor);
  if (msg && msg[0]) gfxDrawCentered(msg, 130, 2, C_GRAY);
}
