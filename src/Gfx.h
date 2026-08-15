// Gfx.h — shared ST7789 device, drawing primitives, and boot/status screens.
//
// This is the core display layer. The three feature modes (ticker / usage / radar)
// render on top of it via gfxDev() and the exposed text helpers; each feature owns
// its own feature-specific rendering. Nothing feature-specific lives here.
#pragma once
#include <Arduino.h>
#include "Settings.h"

class Arduino_GFX;   // fwd-decl: only the drawing .cpp files pull in the full lib

// ---- Shared colors (RGB565) ----------------------------------------------
// The SmallTV variants ship visibly different panels: some run warm, some cold,
// and a few have red and blue swapped in the controller. gfxTint() applies the
// per-channel gain set in the Display tab to every colour on its way to the
// panel, so a unit can be matched to the others without touching each renderer.
// Colours computed inside a feature go through it too (see UsageMode/RadarMode).
uint16_t gfxTint(uint16_t rgb565);

#define C_BLACK  gfxTint(0x0000)
#define C_WHITE  gfxTint(0xFFFF)
#define C_GREEN  gfxTint(0x07E0)
#define C_RED    gfxTint(0xF800)
#define C_GRAY   gfxTint(0x8410)
#define C_DGRAY  gfxTint(0x4208)
#define C_YELLOW gfxTint(0xFFE0)
#define C_BLUE   gfxTint(0x041F)

// ---- Device lifecycle -----------------------------------------------------
void         gfxBegin(const Settings& s);
void         gfxSetBrightness(uint8_t pct, bool inverted);
void         gfxSetRotation(uint8_t r);
// Push the Display tab's colour settings to the panel: MADCTL colour order,
// the inversion bit, and the per-channel gain gfxTint() applies. Callers repaint
// afterwards — already-drawn pixels keep the previous correction.
void         gfxApplyColors(const Settings& s);
Arduino_GFX* gfxDev();                 // shared draw target for feature renderers

// ---- Text helpers (built-in 6x8 font, integer scaled) ---------------------
#define GFX_FONT_W 6
#define GFX_FONT_H 8

int     gfxTextW(const char* s, uint8_t size);
void    gfxDrawCentered(const char* s, int y, uint8_t size, uint16_t color);
uint8_t gfxFitSize(const char* s, int maxW, uint8_t maxSize);

// ---- Shared boot / status / diagnostic screens ----------------------------
void gfxBoot(const char* line1, const char* line2);
void gfxApInfo(const char* ssid, const char* pass, const char* ip);
void gfxStaInfo(const char* ssid, const char* ip, const char* host);
void gfxMessage(const char* title, const char* msg, uint16_t titleColor);
void gfxCrash(const char* epc, const char* addr, const char* ip);  // safe-mode diag
