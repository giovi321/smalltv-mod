// HaScreens.cpp — see HaScreens.h for the contract this implements.
#include "HaScreens.h"
#if WITH_HA
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <new>
#include <pgmspace.h>

static const char* HA_PATH = "/ha_screens.json";

// Packed [0, g_count), sorted by slot name — iteration order is the carousel
// order (lexicographic), which the docs rely on (a "zenergy" slot sorts last).
// One block of HA_MAX_SCREENS screens (5,792 B on the ESP8266), taken at boot
// by haScreensAlloc() and null until then, or for good when Home Assistant is
// off or the heap had no room. While it is null g_count stays 0, so the
// readers (live count, screen at, dirty flag, clear) need no check; the
// functions that would write into it check for null first.
static HaScreen* g_screens = nullptr;
static uint8_t  g_count = 0;
static bool     g_loaded = false;
static bool     g_dirty = false;        // renderer flag
static bool     g_persistDue = false;   // a change is waiting out the debounce
static uint32_t g_lastChange = 0;

// ---------------------------------------------------------------------------
// UTF-8 -> font byte. The built-in 6x8 font (GFX Library glcdfont) is plain
// ASCII below 0x80 and CP437 above it, so the useful Latin-1 characters land
// on single bytes. Table covers U+00A0..U+00FF (index = codepoint - 0xA0);
// 0 means "no glyph" and the character is dropped, the previous behavior for
// all non-ASCII. 3/4-byte UTF-8 characters never have a glyph.
// ---------------------------------------------------------------------------
static const uint8_t latin1ToFont[96] PROGMEM = {
  0x20, 0xAD, 0x9B, 0x9C, 0x00, 0x9D, 0x00, 0x00,  // A0-A7: NBSP ¡ ¢ £ . ¥ . .
  0x00, 0x00, 0xA6, 0xAE, 0xAA, 0x00, 0x00, 0x00,  // A8-AF: . . ª « ¬ . . .
  0xF8, 0xF1, 0xFD, 0x00, 0x00, 0xE6, 0x00, 0xFA,  // B0-B7: ° ± ² . . µ . ·
  0x00, 0x00, 0xA7, 0xAF, 0xAC, 0xAB, 0x00, 0xA8,  // B8-BF: . . º » ¼ ½ . ¿
  0x00, 0x00, 0x00, 0x00, 0x8E, 0x8F, 0x92, 0x80,  // C0-C7: . . . . Ä Å Æ Ç
  0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // C8-CF: . É . . . . . .
  0x00, 0xA5, 0x00, 0x00, 0x00, 0x00, 0x99, 0x00,  // D0-D7: . Ñ . . . . Ö .
  0x00, 0x00, 0x00, 0x00, 0x9A, 0x00, 0x00, 0xE1,  // D8-DF: . . . . Ü . . ß
  0x85, 0xA0, 0x83, 0x00, 0x84, 0x86, 0x91, 0x87,  // E0-E7: à á â . ä å æ ç
  0x8A, 0x82, 0x88, 0x89, 0x8D, 0xA1, 0x8C, 0x8B,  // E8-EF: è é ê ë ì í î ï
  0x00, 0xA4, 0x95, 0xA2, 0x93, 0x00, 0x94, 0xF6,  // F0-F7: . ñ ò ó ô . ö ÷
  0x00, 0x97, 0xA3, 0x96, 0x81, 0x00, 0x00, 0x98,  // F8-FF: . ù ú û ü . . ÿ
};

// Codepoint -> font byte, 0 if the font has no glyph for it.
static uint8_t utf8FontByte(uint16_t cp) {
  if (cp < 0xA0 || cp > 0xFF) return 0;
  return pgm_read_byte(&latin1ToFont[cp - 0xA0]);
}

// Inverse: font byte -> codepoint, 0 if it came from no mapped character.
// Used to re-encode stored text as UTF-8 when persisting screens.
static uint16_t fontByteCp(uint8_t b) {
  if (b < 0x80) return b;
  for (uint16_t cp = 0xA0; cp <= 0xFF; cp++)
    if (pgm_read_byte(&latin1ToFont[cp - 0xA0]) == b) return cp;
  return 0;
}

// ---------------------------------------------------------------------------
// Colors: "#RRGGBB" -> RGB565. Anything that is not exactly that shape is a
// parse failure (and makes the primitive it belongs to malformed).
// ---------------------------------------------------------------------------
static bool parseColor(const char* s, uint16_t& out) {
  if (!s || s[0] != '#' || strlen(s) != 7) return false;
  char* end = nullptr;
  long v = strtol(s + 1, &end, 16);
  if (!end || *end) return false;             // stopped short: non-hex digit
  uint8_t r = (uint8_t)((v >> 16) & 0xFF);
  uint8_t g = (uint8_t)((v >> 8) & 0xFF);
  uint8_t b = (uint8_t)(v & 0xFF);
  out = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return true;
}

static void colorToStr(uint16_t c, char out[8]) {
  snprintf(out, 8, "#%02X%02X%02X",
           (unsigned)(((c >> 11) & 0x1F) << 3),
           (unsigned)(((c >> 5) & 0x3F) << 2),
           (unsigned)((c & 0x1F) << 3));
}

// Hex digit value, case-insensitive; -1 for anything else. Used by the bitmap
// primitive, which carries its 1-bit pixels as a hex string on the wire.
static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// ---------------------------------------------------------------------------
// Parsing. parseScreen() fully rebuilds `sc` from a valid screen object; it is
// shared by the MQTT path and the persistence loader (same JSON shape).
// Returns false only on a hard failure; individual bad primitives are skipped.
// ---------------------------------------------------------------------------
static bool parsePrim(JsonObjectConst p, HaScreen& sc, HaPrim& out) {
  const char* t = p["t"] | "";
  if (!t[0]) return false;

  memset(&out, 0, sizeof(out));
  // Every primitive carries a colour; a missing or malformed one makes the
  // primitive malformed (skipped), not defaulted to something arbitrary.
  if (!parseColor(p["c"] | "", out.color)) return false;
  out.x  = p["x"] | 0;
  out.y  = p["y"] | 0;
  out.x2 = p["x2"] | 0;
  out.y2 = p["y2"] | 0;

  if (!strcmp(t, "fill")) {
    out.type = HA_P_FILL;
  } else if (!strcmp(t, "rect") || !strcmp(t, "rrect")) {
    int w = p["w"] | 0, h = p["h"] | 0;
    if (w <= 0 || h <= 0) return false;         // nothing to draw
    out.type = (t[1] == 'e') ? HA_P_RECT : HA_P_RRECT;   // "rect" vs "rrect"
    out.x2 = (int16_t)constrain(w, 1, TFT_WIDTH * 2);
    out.y2 = (int16_t)constrain(h, 1, TFT_HEIGHT * 2);
    if (out.type == HA_P_RRECT) out.aux = (uint8_t)constrain((int)(p["r"] | 0), 0, 255);
  } else if (!strcmp(t, "circle")) {
    int r = p["r"] | 0;
    if (r <= 0) return false;
    out.type = HA_P_CIRCLE;
    out.aux = (uint8_t)constrain(r, 1, 255);
  } else if (!strcmp(t, "line")) {
    out.type = HA_P_LINE;
  } else if (!strcmp(t, "text")) {
    const char* v = p["v"];
    if (!v) return false;
    int s = p["s"] | 1;
    if (s < 1) return false;                    // scale is an int >= 1
    // UTF-8 in, font bytes out: printable ASCII passes through, Latin-1 maps
    // to single CP437 bytes via latin1ToFont[], and anything without a glyph
    // is dropped (the old behavior for all non-ASCII). The 64-char cap counts
    // the bytes stored AFTER translation, so "18.4°C" uses 6, not 7.
    char buf[HA_MAX_TEXT];
    size_t n = 0;
    for (size_t i = 0; v[i] && n < HA_MAX_TEXT - 1; ) {
      uint8_t c = (uint8_t)v[i++];
      if (c < 0x80) {
        if (c >= 32 && c <= 126) buf[n++] = (char)c;
      } else if ((c & 0xE0) == 0xC0 && (v[i] & 0xC0) == 0x80) {
        uint16_t cp = ((uint16_t)(c & 0x1F) << 6) | ((uint8_t)v[i++] & 0x3F);
        uint8_t b = utf8FontByte(cp);
        if (b) buf[n++] = (char)b;
      } else if ((c & 0xF0) == 0xE0 && (v[i] & 0xC0) == 0x80 && (v[i + 1] & 0xC0) == 0x80) {
        i += 2;                                 // 3-byte char: no font glyph
      } else if ((c & 0xF8) == 0xF0 && (v[i] & 0xC0) == 0x80 &&
                 (v[i + 1] & 0xC0) == 0x80 && (v[i + 2] & 0xC0) == 0x80) {
        i += 3;                                 // 4-byte char: no font glyph
      }
      // Anything else (lone continuation, truncated sequence) is dropped.
    }
    if (sc.textUsed + n + 1 > sizeof(sc.text)) return false;  // pool full: skip
    out.type  = HA_P_TEXT;
    out.aux   = (uint8_t)constrain(s, 1, 10);
    const char* a = p["a"] | "l";
    out.align = (a[0] == 'c') ? HA_A_CENTER : (a[0] == 'r') ? HA_A_RIGHT : HA_A_LEFT;
    out.voff  = sc.textUsed;
    memcpy(sc.text + sc.textUsed, buf, n);
    sc.textUsed += n;
    sc.text[sc.textUsed++] = 0;
  } else if (!strcmp(t, "icon")) {
    const char* v = p["v"];
    if (!v) return false;
    int s = p["s"] | 1;
    if (s < 1) return false;                    // scale is an int >= 1
    size_t L = strlen(v);
    if (!L || L > 24) return false;             // icon names are <= 24 chars
    if (sc.textUsed + L + 1 > sizeof(sc.text)) return false;  // pool full: skip
    out.type  = HA_P_ICON;
    out.aux   = (uint8_t)constrain(s, 1, 8);    // 24x24 grid scaled 1..8
    const char* a = p["a"] | "l";
    out.align = (a[0] == 'c') ? HA_A_CENTER : (a[0] == 'r') ? HA_A_RIGHT : HA_A_LEFT;
    out.voff  = sc.textUsed;
    // Plain ASCII only (unlike text): an unknown name simply draws nothing.
    for (size_t i = 0; i < L; i++) {
      char c = v[i];
      if (c >= 32 && c <= 126) sc.text[sc.textUsed++] = c;
    }
    sc.text[sc.textUsed++] = 0;
  } else if (!strcmp(t, "bitmap")) {
    // d: hex string (case-insensitive) of 1-bit pixels, row-major, MSB-first
    // within each byte. Decoded straight into the screen's bitmap pool at
    // parse time — no String, no heap (ESP8266 discipline). Any shape error
    // (bad w/h, wrong length, non-hex digit, full pool) skips the primitive.
    const char* d = p["d"];
    if (!d) return false;
    int w = p["w"] | 0, h = p["h"] | 0;
    if (w < 1 || h < 1 || w > HA_BITMAP_MAX_DIM || h > HA_BITMAP_MAX_DIM) return false;
    // s: on-device upscale, each source pixel drawn as an sxs block; the
    // rendered box is w*s x h*s and must still fit the panel.
    int s = constrain(p["s"] | 1, 1, 4);
    if (w * s > TFT_WIDTH || h * s > TFT_HEIGHT) return false;
    size_t need = ((size_t)w * h + 7) / 8;          // decoded bytes
    if (strlen(d) != need * 2) return false;        // exact hex length required
    if (sc.bitmapUsed + need > sizeof(sc.bitmap)) return false;  // pool full: skip
    for (size_t i = 0; i < need * 2; i++)           // validate before touching
      if (hexVal(d[i]) < 0) return false;           //   the pool
    out.type  = HA_P_BITMAP;
    out.x2    = (int16_t)w;
    out.y2    = (int16_t)h;
    out.aux   = (uint8_t)s;
    const char* a = p["a"] | "l";
    out.align = (a[0] == 'c') ? HA_A_CENTER : (a[0] == 'r') ? HA_A_RIGHT : HA_A_LEFT;
    out.voff  = sc.bitmapUsed;
    for (size_t i = 0; i < need; i++)
      sc.bitmap[sc.bitmapUsed + i] = (uint8_t)((hexVal(d[2 * i]) << 4) | hexVal(d[2 * i + 1]));
    sc.bitmapUsed += need;
  } else {
    return false;                               // unknown primitive type
  }
  return true;
}

static bool parseScreen(JsonObjectConst o, const char* name, HaScreen& sc) {
  memset(&sc, 0, sizeof(sc));
  strlcpy(sc.name, name, sizeof(sc.name));

  sc.bg = 0;
  if (o["bg"].is<const char*>()) parseColor(o["bg"].as<const char*>(), sc.bg);

  // ttl: seconds until the screen drops out of the rotation; 0 = sticky.
  uint32_t ttl = o["ttl"] | 0;
  if (ttl > HA_TTL_MAX_SEC) ttl = HA_TTL_MAX_SEC;
  sc.expiresAt = ttl ? millis() + ttl * 1000UL : 0;

  if (o["draw"].is<JsonArrayConst>()) {
    for (JsonObjectConst p : o["draw"].as<JsonArrayConst>()) {
      if (sc.primCount >= HA_MAX_PRIMS) break;  // clamp at the per-chip limit
      HaPrim prim;
      if (parsePrim(p, sc, prim)) sc.prims[sc.primCount++] = prim;
      // a malformed primitive is skipped; the rest of the list still draws
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Store mechanics
// ---------------------------------------------------------------------------
static void markChanged() {
  g_dirty = true;
  g_persistDue = true;
  g_lastChange = millis();
}

// Sorted position for `name` among the packed entries; *found set on a match.
static uint8_t slotPos(const char* name, bool* found) {
  uint8_t i = 0;
  while (i < g_count) {
    int c = strcmp(g_screens[i].name, name);
    if (c == 0) { *found = true; return i; }
    if (c > 0) break;
    i++;
  }
  *found = false;
  return i;
}

static void deleteAt(uint8_t idx) {
  if (idx >= g_count) return;
  memmove(&g_screens[idx], &g_screens[idx + 1], (g_count - idx - 1) * sizeof(HaScreen));
  g_count--;
}

void haScreensApply(const char* slot, const uint8_t* payload, size_t len) {
  if (!g_screens) return;   // no store: drop the message before it is parsed
  if (!slot || !slot[0] || strlen(slot) >= HA_SLOT_LEN) return;

  // Delete first, before any JSON parse: an empty retained payload removes the
  // slot from both the broker and the device (docs contract).
  if (len == 0) {
    bool found;
    uint8_t idx = slotPos(slot, &found);
    if (found) { deleteAt(idx); markChanged(); }
    return;
  }

  // Whole-payload parse failure leaves the slot exactly as it was, so a bad
  // template in Home Assistant cannot blank a working screen.
  JsonDocument doc;   // one reusable parse per message; nothing is retained
  if (deserializeJson(doc, payload, len)) return;
  if (!doc.is<JsonObject>()) return;

  bool found;
  uint8_t idx = slotPos(slot, &found);
  if (!found) {
    if (g_count >= HA_MAX_SCREENS) return;      // full: drop the new screen
    memmove(&g_screens[idx + 1], &g_screens[idx], (g_count - idx) * sizeof(HaScreen));
    g_count++;
  }
  parseScreen(doc.as<JsonObjectConst>(), slot, g_screens[idx]);
  markChanged();
}

// ---------------------------------------------------------------------------
// Renderer view + dirty tracking
// ---------------------------------------------------------------------------
static bool expired(const HaScreen& sc, uint32_t now) {
  return sc.expiresAt && (int32_t)(now - sc.expiresAt) >= 0;
}

uint8_t haScreensLive() {
  uint32_t now = millis();
  uint8_t n = 0;
  for (uint8_t i = 0; i < g_count; i++)
    if (!expired(g_screens[i], now)) n++;
  return n;
}

const HaScreen* haScreenAt(uint8_t liveIndex) {
  uint32_t now = millis();
  for (uint8_t i = 0; i < g_count; i++) {
    if (expired(g_screens[i], now)) continue;
    if (liveIndex-- == 0) return &g_screens[i];
  }
  return nullptr;
}

bool haScreensTakeDirty() {
  bool d = g_dirty;
  g_dirty = false;
  return d;
}

uint8_t haScreensClearAll(char names[][HA_SLOT_LEN], uint8_t maxNames) {
  uint8_t n = g_count;
  for (uint8_t i = 0; i < g_count && i < maxNames; i++)
    strlcpy(names[i], g_screens[i].name, HA_SLOT_LEN);
  g_count = 0;
  // Not markChanged(): instead of a debounced persist of the now-empty store,
  // the file is deleted outright and any pending write of the old data is
  // dropped. g_dirty still goes to the renderer so it repaints next tick.
  g_persistDue = false;
  LittleFS.remove(HA_PATH);
  g_dirty = true;
  return n;
}

// ---------------------------------------------------------------------------
// Persistence: /ha_screens.json, same JSON shape as the wire format, so the
// loader reuses parseScreen(). ttl is written as the remaining seconds;
// already-expired screens are left out (they had dropped out of the rotation
// anyway). Sticky screens persist with ttl 0.
// ---------------------------------------------------------------------------
static void primToJson(const HaScreen& sc, const HaPrim& p, JsonObject o) {
  char col[8];
  colorToStr(p.color, col);
  o["c"] = col;
  switch (p.type) {
    case HA_P_FILL:
      o["t"] = "fill";
      break;
    case HA_P_RECT:
      o["t"] = "rect";
      o["x"] = p.x; o["y"] = p.y; o["w"] = p.x2; o["h"] = p.y2;
      break;
    case HA_P_RRECT:
      o["t"] = "rrect";
      o["x"] = p.x; o["y"] = p.y; o["w"] = p.x2; o["h"] = p.y2; o["r"] = p.aux;
      break;
    case HA_P_CIRCLE:
      o["t"] = "circle";
      o["x"] = p.x; o["y"] = p.y; o["r"] = p.aux;
      break;
    case HA_P_LINE:
      o["t"] = "line";
      o["x"] = p.x; o["y"] = p.y; o["x2"] = p.x2; o["y2"] = p.y2;
      break;
    case HA_P_TEXT: {
      o["t"] = "text";
      o["x"] = p.x; o["y"] = p.y; o["s"] = p.aux;
      o["a"] = (p.align == HA_A_CENTER) ? "c" : (p.align == HA_A_RIGHT) ? "r" : "l";
      // Re-encode the stored font bytes as UTF-8 (inverse of the parser's
      // translation) so the persisted file stays valid JSON text and a reload
      // through parseScreen() reproduces the same pool bytes.
      const uint8_t* t = (const uint8_t*)(sc.text + p.voff);
      char buf[2 * HA_MAX_TEXT - 1];
      size_t n = 0;
      for (size_t i = 0; t[i]; i++) {
        uint16_t cp = fontByteCp(t[i]);
        if (!cp) continue;
        if (cp < 0x80) buf[n++] = (char)cp;
        else { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 0x3F)); }
      }
      buf[n] = 0;
      o["v"] = buf;
      break;
    }
    case HA_P_ICON:
      o["t"] = "icon";
      o["x"] = p.x; o["y"] = p.y; o["s"] = p.aux;
      o["a"] = (p.align == HA_A_CENTER) ? "c" : (p.align == HA_A_RIGHT) ? "r" : "l";
      o["v"] = sc.text + p.voff;
      break;
    case HA_P_BITMAP: {
      o["t"] = "bitmap";
      o["x"] = p.x; o["y"] = p.y; o["w"] = p.x2; o["h"] = p.y2;
      o["s"] = p.aux;   // upscale (text/icon write their s unconditionally too)
      o["a"] = (p.align == HA_A_CENTER) ? "c" : (p.align == HA_A_RIGHT) ? "r" : "l";
      // Re-encode the decoded bytes as the same hex string shape that came in,
      // so a load through parseScreen() reproduces the primitive exactly.
      size_t n = ((size_t)p.x2 * p.y2 + 7) / 8;
      char hex[HA_BITMAP_MAX_DIM * HA_BITMAP_MAX_DIM / 8 * 2 + 1];
      static const char HEXD[] = "0123456789ABCDEF";
      for (size_t i = 0; i < n; i++) {
        uint8_t b = sc.bitmap[p.voff + i];
        hex[2 * i]     = HEXD[b >> 4];
        hex[2 * i + 1] = HEXD[b & 0x0F];
      }
      hex[2 * n] = 0;
      o["d"] = hex;
      break;
    }
  }
}

static void persistNow() {
  JsonDocument doc;
  JsonArray arr = doc["screens"].to<JsonArray>();
  uint32_t now = millis();
  for (uint8_t i = 0; i < g_count; i++) {
    const HaScreen& sc = g_screens[i];
    if (expired(sc, now)) continue;
    JsonObject o = arr.add<JsonObject>();
    o["n"] = sc.name;
    char col[8];
    colorToStr(sc.bg, col);
    o["bg"] = col;
    o["ttl"] = sc.expiresAt ? (uint32_t)((sc.expiresAt - now) / 1000UL) : 0;
    JsonArray draw = o["draw"].to<JsonArray>();
    for (uint8_t k = 0; k < sc.primCount; k++)
      primToJson(sc, sc.prims[k], draw.add<JsonObject>());
  }
  File f = LittleFS.open(HA_PATH, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

void haScreensService() {
  if (!g_persistDue) return;
  if (millis() - g_lastChange < HA_PERSIST_DEBOUNCE_MS) return;
  g_persistDue = false;
  persistNow();
}

void haScreensReload() {
  if (!g_screens) return;   // no store: the saved file is left alone
  if (g_persistDue) { g_persistDue = false; persistNow(); }  // don't lose a pending change
  g_count = 0;
  File f = LittleFS.open(HA_PATH, "r");
  if (!f) { g_dirty = true; return; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { g_dirty = true; return; }
  JsonArrayConst arr = doc["screens"].as<JsonArrayConst>();
  if (arr.isNull()) { g_dirty = true; return; }
  for (JsonObjectConst o : arr) {
    const char* n = o["n"] | "";
    if (!n[0] || strlen(n) >= HA_SLOT_LEN || g_count >= HA_MAX_SCREENS) continue;
    bool found;
    uint8_t idx = slotPos(n, &found);
    if (found) continue;
    memmove(&g_screens[idx + 1], &g_screens[idx], (g_count - idx) * sizeof(HaScreen));
    g_count++;
    parseScreen(o, n, g_screens[idx]);
  }
  g_dirty = true;
}

bool haScreensAlloc() {
  if (g_screens) return true;
  // The nothrow form: this core has no exceptions, and a plain `new` that
  // fails panics. A failure comes back as null instead.
  g_screens = new (std::nothrow) HaScreen[HA_MAX_SCREENS];
  return g_screens != nullptr;
}

void haScreensFree() {
  delete[] g_screens;
  g_screens = nullptr;
  g_count = 0;
  g_loaded = false;
  g_dirty = false;
}

void haScreensBegin() {
  if (g_loaded || !g_screens) return;
  g_loaded = true;
  haScreensReload();
}

#endif  // WITH_HA
