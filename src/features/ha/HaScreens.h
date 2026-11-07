// HaScreens.h — the Home Assistant screen store (features/ha).
//
// Home Assistant (or any MQTT client) pushes one full 240x240 screen per slot
// as a retained JSON draw list on smalltv/<hostname>/screen/<slot>. This store
// keeps the parsed result as fixed-size structs — no JsonDocument is retained —
// packed and sorted by slot name, so iterating it IS the carousel order
// (lexicographic). Limits come from config.h (HA_MAX_SCREENS / HA_MAX_PRIMS /
// HA_TEXT_POOL / HA_BITMAP_POOL), per chip family.
//
// Screens persist to LittleFS /ha_screens.json, written with a debounce after
// the last change, so they survive a reboot; the broker's retained copies
// re-arrive on the next connect anyway and simply overwrite them.
#pragma once
#include <Arduino.h>
#include "config.h"

// Draw primitive kinds (the "t" field in the screen JSON).
enum HaPrimType : uint8_t {
  HA_P_FILL,    // c
  HA_P_RECT,    // x y w h c     (w/h stored in x2/y2)
  HA_P_RRECT,   // x y w h r c   (r in aux, w/h in x2/y2)
  HA_P_CIRCLE,  // x y r c       (centre x/y, r in aux)
  HA_P_LINE,    // x y x2 y2 c
  HA_P_TEXT,    // x y s c a v   (s in aux, a in align, v in the text pool)
  HA_P_ICON,    // x y s c a v   (like text, but v is an icon name from HaIcons)
  HA_P_BITMAP,  // x y w h c a s d (w/h in x2/y2, s in aux, d in the bitmap pool at voff)
};

enum HaAlign : uint8_t { HA_A_LEFT, HA_A_CENTER, HA_A_RIGHT };

// One draw primitive, 16 bytes. Text payloads live in the owning screen's
// text pool and decoded bitmap data in its bitmap pool (voff indexes the
// matching pool); a screen's pools are bounded by HA_TEXT_POOL /
// HA_BITMAP_POOL and in practice by the MQTT payload cap, which is smaller
// than an all-text screen at the 64-byte string limit (bytes stored, counted
// after the text primitive's UTF-8 translation).
struct HaPrim {
  uint8_t  type;        // HaPrimType
  uint8_t  aux;         // text: font scale (>=1); circle/rrect: radius; bitmap: upscale s (1..4)
  uint8_t  align;       // text/icon/bitmap only: HaAlign
  uint8_t  _pad;
  int16_t  x, y;        // rect/rrect: top-left; circle: centre; text: anchor
  int16_t  x2, y2;      // line: end point; rect/rrect/bitmap: w / h
  uint16_t color;       // RGB565
  uint16_t voff;        // text/icon: offset into text[]; bitmap: into bitmap[]
};
static_assert(sizeof(HaPrim) == 16, "HaPrim must stay compact");

struct HaScreen {
  char     name[HA_SLOT_LEN];   // slot = topic suffix, used as-is
  uint16_t bg;                  // RGB565 background (default black)
  uint32_t expiresAt;           // millis() deadline; 0 = sticky (ttl 0)
  uint16_t textUsed;            // bytes used in text[]
  uint16_t bitmapUsed;          // bytes used in bitmap[]
  uint8_t  primCount;
  HaPrim   prims[HA_MAX_PRIMS];
  char     text[HA_TEXT_POOL];  // NUL-terminated strings referenced by voff
  // Decoded 1-bit bitmap data (row-major, MSB-first) referenced by voff of
  // HA_P_BITMAP prims. Kept per screen, like text[], so the packed-store
  // memmove mechanics stay correct without any pool bookkeeping.
  uint8_t  bitmap[HA_BITMAP_POOL];
};

// The store itself is taken from the heap once, at boot, and only when Home
// Assistant is wanted (HaBoot.h). Until then, and after a failed allocation, it
// does not exist: every function below then does nothing and answers "empty"
// (no screens, nothing to clear, nothing dirty), so no caller needs a check.
bool haScreensAlloc();     // take the store (HA_MAX_SCREENS screens, ONE block); false: no heap
void haScreensFree();      // give it back (also: forget what it held)

void haScreensBegin();     // load /ha_screens.json (idempotent)
void haScreensService();   // debounced persist; call every loop tick
void haScreensReload();    // re-read the persisted file (settings re-applied)

// MQTT entry point. `slot` is the topic suffix after the last '/';
// `payload` is NOT NUL-terminated. A zero-length payload deletes the slot —
// checked before any JSON parsing, per the docs contract. A payload that is
// not valid JSON leaves the slot exactly as it was; inside a valid payload,
// unknown fields are ignored and malformed primitives are skipped
// individually.
void haScreensApply(const char* slot, const uint8_t* payload, size_t len);

// Renderer view: the live (non-expired) screens in lexicographic slot order.
uint8_t haScreensLive();
const HaScreen* haScreenAt(uint8_t liveIndex);

// Set on any store change (set/delete/parse); the renderer takes it once per
// repaint so a screen pushed over MQTT shows up without waiting for rotation.
bool haScreensTakeDirty();

// Web UI "clear screens" (/api/ha/clear). Copies the current slot names (up to
// maxNames) into `names` so the caller can publish an empty retained payload
// per slot, then empties the store and deletes /ha_screens.json immediately.
// Returns the number of slots that were in the store. Slots whose retained
// messages exist only on the broker (the device was offline when the screen
// was deleted) are unknowable from here and are NOT covered by this call.
uint8_t haScreensClearAll(char names[][HA_SLOT_LEN], uint8_t maxNames);
