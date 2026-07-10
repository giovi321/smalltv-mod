#include "Gfx.h"
#include "Platform.h"
#include <Arduino_GFX_Library.h>
#include <SPI.h>

// The SmallTV's ST7789 has its CS line tied to GND and only latches SPI in
// **mode 3**. Arduino_GFX's stock Arduino_ST7789 forces SPI_MODE2 on the ESP8266
// (wrong clock edge for this panel), so the controller never initializes and the
// screen stays black even with the backlight on. Subclass begin() to force mode 3
// — matching the known-good GeekMagic community firmwares. (On ESP32 the base
// class already selects mode 3, so the override is harmless there.)
class Arduino_ST7789_SmallTV : public Arduino_ST7789 {
 public:
  using Arduino_ST7789::Arduino_ST7789;   // inherit constructors
  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    _override_datamode = SPI_MODE3;
    return Arduino_TFT::begin(speed);
  }

#if TFT_BGR
  // This board's panel is wired B-G-R. Arduino_ST7789 hardcodes the MADCTL RGB
  // order, so re-issue MADCTL with the BGR bit (0x08) set on every rotation
  // change. Only rotations 0-3 are used by the SmallTV (setRotation(r & 3)).
  void setRotation(uint8_t r) override {
    Arduino_TFT::setRotation(r);           // updates _rotation + width/height
    uint8_t madctl;
    switch (_rotation) {
      case 1:  madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MV; break;
      case 2:  madctl = ST7789_MADCTL_MX | ST7789_MADCTL_MY; break;
      case 3:  madctl = ST7789_MADCTL_MY | ST7789_MADCTL_MV; break;
      default: madctl = 0; break;          // case 0
    }
    madctl |= 0x08;                         // BGR
    _bus->beginWrite();
    _bus->writeC8D8(ST7789_MADCTL, madctl);
    _bus->endWrite();
  }
#endif
};

static Arduino_DataBus* bus = nullptr;
static Arduino_GFX*     gfx = nullptr;

Arduino_GFX* gfxDev() { return gfx; }

// ---------------------------------------------------------------------------
void gfxBegin(const Settings& s) {
#ifdef TFT_PWR_PIN
  // Boards with a switched panel power rail (NM-TV-154): energize the display
  // before anything else or the panel never comes up.
  pinMode(TFT_PWR_PIN, OUTPUT);
  digitalWrite(TFT_PWR_PIN, TFT_PWR_ON);
#endif
  // Backlight FIRST: do it before the panel/SPI init so the screen lights up even
  // if panel init has trouble. A dark backlight then means the sketch didn't get
  // this far (early crash / bad flash) — a useful boot indicator.
  pinMode(TFT_BL, OUTPUT);
  platformAnalogWriteInit(TFT_BL);
  gfxSetBrightness(s.brightness, s.backlightInverted);

#if defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32)
  // Hardware SPI via the Arduino SPI library (IDF spi_master driver) on explicit
  // GPIOs. The register-level Arduino_ESP32SPI hangs in begin() on the C2, and
  // Arduino_SWSPI's fast-IO path doesn't cover the C2 — Arduino_HWSPI uses the
  // stock driver (what the working ESPHome config used) and honors SPI mode 3
  // (see the subclass). Pins come from the board header; a TFT_CS of -1 means
  // the panel's CS is tied to GND and is never toggled.
  bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED, &SPI);
#else
  bus = new Arduino_HWSPI(TFT_DC, TFT_CS);   // ESP8266 HW-SPI (fixed SCLK/MOSI)
#endif
  // IPS=true so the panel colors are not inverted; full 240x240, no offsets.
  // Use the SmallTV variant so the SPI bus comes up in mode 3 (see class above).
  gfx = new Arduino_ST7789_SmallTV(bus, TFT_RST, 0 /*rotation*/, true /*IPS*/,
                                   TFT_WIDTH, TFT_HEIGHT, 0, 0, 0, 0);
  gfx->begin();
  gfx->setRotation(s.rotation & 3);
  // Nothing in this UI ever wants wrapped text: overflowing labels used to
  // wrap around to x=0 on the next line (stray characters at the left edge).
  gfx->setTextWrap(false);
  gfx->fillScreen(C_BLACK);
}

void gfxSetBrightness(uint8_t pct, bool inverted) {
  if (pct > 100) pct = 100;
  int duty = (int)pct * 255 / 100;
  if (inverted) duty = 255 - duty;
  analogWrite(TFT_BL, duty);
}

void gfxSetRotation(uint8_t r) {
  if (gfx) gfx->setRotation(r & 3);
}

// ---- text helpers (built-in 6x8 font, integer scaled) ---------------------
int gfxTextW(const char* s, uint8_t size) { return (int)strlen(s) * 6 * size; }

void gfxDrawCentered(const char* s, int y, uint8_t size, uint16_t color) {
  if (!gfx) return;
  int x = (TFT_WIDTH - gfxTextW(s, size)) / 2;
  if (x < 0) x = 0;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(s);
}

// Largest size (<= maxSize) whose rendered width fits within maxW.
uint8_t gfxFitSize(const char* s, int maxW, uint8_t maxSize) {
  for (uint8_t sz = maxSize; sz > 1; sz--) {
    if (gfxTextW(s, sz) <= maxW) return sz;
  }
  return 1;
}

// ---------------------------------------------------------------------------
void gfxBoot(const char* line1, const char* line2) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered(line1, 95, 3, C_WHITE);
  if (line2 && line2[0]) gfxDrawCentered(line2, 130, 2, C_GRAY);
}

void gfxApInfo(const char* ssid, const char* pass, const char* ip) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered("SETUP MODE", 18, 3, C_YELLOW);
  gfxDrawCentered("Join WiFi:", 64, 2, C_GRAY);
  gfxDrawCentered(ssid, 88, gfxFitSize(ssid, 232, 3), C_WHITE);
  if (pass && pass[0]) {
    gfxDrawCentered("Password:", 124, 2, C_GRAY);
    gfxDrawCentered(pass, 146, gfxFitSize(pass, 232, 2), C_WHITE);
  } else {
    gfxDrawCentered("(open network)", 124, 2, C_GRAY);
  }
  gfxDrawCentered("Then open:", 182, 2, C_GRAY);
  String url = String("http://") + ip;
  gfxDrawCentered(url.c_str(), 206, gfxFitSize(url.c_str(), 232, 2), C_GREEN);
}

void gfxStaInfo(const char* ssid, const char* ip, const char* host) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered("CONNECTED", 18, 3, C_GREEN);
  gfxDrawCentered("Network:", 62, 2, C_GRAY);
  gfxDrawCentered(ssid && ssid[0] ? ssid : "-", 84, gfxFitSize(ssid, 232, 3), C_WHITE);
  gfxDrawCentered("Open in browser:", 126, 2, C_GRAY);
  // IP shown big (always fits at size 2); mDNS name below as a friendlier option.
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

// Persistent crash screen shown in safe mode (after an exception reset). Holds the
// crash PC + fault address still so they can be read, and the IP for OTA recovery.
void gfxCrash(const char* epc, const char* addr, const char* ip) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  gfxDrawCentered("CRASH", 12, 4, C_RED);
  gfxDrawCentered("epc", 60, 2, C_GRAY);
  gfxDrawCentered(epc && epc[0] ? epc : "-", 80, 3, C_WHITE);
  gfxDrawCentered("addr", 124, 2, C_GRAY);
  gfxDrawCentered(addr && addr[0] ? addr : "-", 146, 2, C_WHITE);
  gfxDrawCentered("OTA flash to fix:", 182, 2, C_GRAY);
  gfxDrawCentered(ip && ip[0] ? ip : "-", 204, 2, C_GREEN);
}
