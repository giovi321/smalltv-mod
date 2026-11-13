// loader.cpp — a minimal OTA "trampoline" for devices whose stock firmware
// reserves so much flash that its updater rejects a full smalltv-mod image
// ("Not Enough Space"), e.g. the GeekMagic SmallTV-ultra with its large
// image/GIF store.
//
// It is deliberately tiny: WiFi + a bare web OTA endpoint + a fixed status
// screen, no features. Small enough to fit the stock firmware's slot, it is
// flashed over the stock OTA. Once running it uses smalltv-mod's normal 4m1m
// flash layout, whose sketch region is large, so ITS /update then accepts the
// full firmware the stock updater refused. Two hops, no UART needed.
//
// The screen says where to go (network and address), shows a progress bar
// during the upload and the result, so a dark panel is no longer mistaken for
// a bricked device. It is drawn with a few raw ST7789 commands and a 5x7 font
// instead of a graphics library; together with the small /update handler below,
// which replaces ESP8266HTTPUpdateServer, the image ends up smaller than the
// earlier one without a screen.
//
// WiFi creds are compile-time only (-DLOADER_SSID / -DLOADER_PASS), never in
// the repo. Without them it falls back to an open "SmallTV-Loader" AP at
// 192.168.4.1 so the full image can still be uploaded by hand.
#ifdef LOADER_BUILD
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <Updater.h>
#include "board_esp8266.h"
#include "loader_font.h"

#ifndef LOADER_SSID
#define LOADER_SSID ""
#endif
#ifndef LOADER_PASS
#define LOADER_PASS ""
#endif

static ESP8266WebServer server(80);

// ---- Status screen (ST7789 240x240, SPI mode 3) ----------------------------
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GRAY   0x8410
#define C_YELLOW 0xFFE0
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define SCR_W    240
#define BL_ON    (TFT_BL_DEFAULT_INVERTED ? LOW : HIGH)
#define BL_OFF   (TFT_BL_DEFAULT_INVERTED ? HIGH : LOW)

static void tftCmd(uint8_t c) {
  digitalWrite(TFT_DC, LOW);
  SPI.write(c);
  digitalWrite(TFT_DC, HIGH);
}

static void tftWindow(int x, int y, int w, int h) {
  tftCmd(0x2A); SPI.write16(x); SPI.write16(x + w - 1);   // CASET
  tftCmd(0x2B); SPI.write16(y); SPI.write16(y + h - 1);   // RASET
  tftCmd(0x2C);                                           // RAMWR
}

static void scrFill(int x, int y, int w, int h, uint16_t c) {
  const uint8_t px[2] = { uint8_t(c >> 8), uint8_t(c) };
  tftWindow(x, y, w, h);
  SPI.writePattern(px, 2, (uint32_t)w * h);
}

static void scrChar(int x, int y, char ch, uint8_t s, uint16_t fg) {
  if (ch < 32 || ch > 126) ch = '?';
  const uint8_t* g = kFont5x7 + (ch - 32) * 5;
  tftWindow(x, y, 6 * s, 8 * s);
  for (uint8_t row = 0; row < 8; row++)
    for (uint8_t sy = 0; sy < s; sy++)
      for (uint8_t col = 0; col < 6; col++) {
        const bool on = col < 5 && ((pgm_read_byte(g + col) >> row) & 1);
        for (uint8_t sx = 0; sx < s; sx++) SPI.write16(on ? fg : C_BLACK);
      }
}

// Centred text; `str` may be in flash (PSTR) or RAM.
static void scrText(int y, uint8_t s, uint16_t fg, const char* str) {
  int n = strlen_P(str);
  int x = (SCR_W - (n * 6 * s - s)) / 2;
  if (x < 0) x = 0;
  for (int i = 0; i < n && x + 6 * s <= SCR_W; i++, x += 6 * s)
    scrChar(x, y, pgm_read_byte(str + i), s, fg);
}

static void scrClear() { scrFill(0, 0, SCR_W, SCR_W, C_BLACK); }

static void scrBegin() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, BL_OFF);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, LOW);         // selected for good; LOW is also GPIO15's boot level
  pinMode(TFT_DC, OUTPUT);
  digitalWrite(TFT_DC, HIGH);
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(150);
  SPI.begin();
  SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE3));  // the only SPI user
  tftCmd(0x01); delay(150);          // SWRESET
  tftCmd(0x11); delay(120);          // SLPOUT
  tftCmd(0x3A); SPI.write(0x55);     // COLMOD: 16 bit
  tftCmd(0x36); SPI.write(0x00);     // MADCTL: rotation 0, RGB
  tftCmd(0x21);                      // INVON (IPS panel)
  tftCmd(0x13);                      // NORON
  scrClear();
  tftCmd(0x29); delay(10);           // DISPON
  digitalWrite(TFT_BL, BL_ON);
}

static void scrHome(const char* net, const char* url) {
  scrClear();
  scrText(14, 2, C_WHITE, PSTR("SmallTV-Loader"));
  scrText(60, 2, C_GRAY, PSTR("1. Join Wi-Fi"));
  scrText(86, strlen_P(net) > 19 ? 1 : 2, C_YELLOW, net);
  scrText(132, 2, C_GRAY, PSTR("2. Open"));
  scrText(158, strlen_P(url) > 19 ? 1 : 2, C_YELLOW, url);
  scrText(204, 1, C_GRAY, PSTR("and upload the firmware"));
}

static void scrFail(const char* why) {
  scrClear();
  scrText(70, 2, C_RED, PSTR("Update failed"));
  scrText(110, 1, C_WHITE, why);
  scrText(150, 1, C_GRAY, PSTR("Nothing was changed. Try again."));
}

static void scrProgress(uint8_t pct) {
  char buf[5];
  snprintf_P(buf, sizeof(buf), PSTR("%u%%"), pct);
  scrFill(21, 101, (198 * pct) / 100, 18, C_GREEN);
  scrFill(84, 134, 72, 16, C_BLACK);
  scrText(134, 2, C_WHITE, buf);
}

static void scrBusy() {
  scrClear();
  scrText(40, 2, C_WHITE, PSTR("Updating"));
  scrFill(20, 100, 200, 1, C_WHITE);
  scrFill(20, 119, 200, 1, C_WHITE);
  scrFill(20, 100, 1, 20, C_WHITE);
  scrFill(219, 100, 1, 20, C_WHITE);
  scrText(190, 2, C_YELLOW, PSTR("Do not unplug"));
  scrProgress(0);
}

// ---- /update ---------------------------------------------------------------
// Firmware only. The same Updater calls as ESP8266HTTPUpdateServer (begin with
// the free sketch space, write, end(true), restart), without its filesystem
// form: a filesystem image has no use on a one-way loader, and the form
// confused people. CORS headers kept as the library sends them. The form is
// served at / and /update and always posts to /update.
static const char kPage[] PROGMEM =
  "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'></head><body>"
  "<form method='POST' action='/update' enctype='multipart/form-data'>Firmware:<br>"
  "<input type='file' accept='.bin' name='firmware'> "
  "<input type='submit' value='Update Firmware'></form></body></html>";
static const char kSuccess[] PROGMEM =
  "<META http-equiv=\"refresh\" content=\"15;URL=/\">Update Success! Rebooting...";

static bool        g_ok    = false;     // last upload was written and verified by Update.end()
static bool        g_first = false;     // the next chunk is the file's first
static const char* g_err   = nullptr;   // why the file was refused before any write (PSTR)
static uint8_t     g_pct   = 0;

static void sendPage() { server.send_P(200, PSTR("text/html"), kPage); }

static void corsHeaders() {
  server.sendHeader(F("Access-Control-Allow-Headers"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
}

// The first chunk decides, before Update.begin() and any write to flash.
// A gzip image (.bin.gz) is refused: Updater::end() does not verify one, and
// eboot erases and rewrites the app while it inflates, so a truncated .gz
// leaves a unit that resets in a loop. ESP32 and C2 images also start with
// 0xE9, so the entry point (bytes 4..7, little-endian) must be where an
// ESP8266 image starts, 0x40100000..0x4010FFFF.
static const char* refuseImage(const uint8_t* b, size_t n) {
  if (n && b[0] == 0x1F) return PSTR(".bin.gz not accepted: upload the .bin");
  if (n < 8 || b[0] != 0xE9) return PSTR("not an ESP8266 firmware image");
  const uint32_t entry = b[4] | b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
  if ((entry & 0xFFFF0000) != 0x40100000) return PSTR("not an ESP8266 firmware image");
  return nullptr;
}

static void handleUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_ok    = false;
    g_first = true;
    g_err   = nullptr;
    g_pct   = 0;
    if (Update.isRunning()) Update.end();   // drop anything a previous attempt left open
    Serial.printf_P(PSTR("Update: %s\n"), up.filename.c_str());
    scrBusy();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (g_first) {
      g_first = false;
      g_err = refuseImage(up.buf, up.currentSize);
      if (g_err) {
        scrFail(g_err);
      } else {
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        Update.begin(maxSketchSpace, U_FLASH);   // a failure stays in Update.getError()
      }
    }
    if (g_err) return;   // refused: let the rest of the request drain, write nothing
    if (!Update.hasError()) Update.write(up.buf, up.currentSize);   // a short write sets the error
    if (up.contentLength && !Update.hasError()) {   // whole request, so the bar ends a few hundred bytes short of 100
      uint32_t pct = (uint32_t)up.totalSize * 100u / up.contentLength;
      if (pct > 100) pct = 100;
      if (pct != g_pct) { g_pct = pct; scrProgress(pct); }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    // true: size = bytes received; on an error it only resets, so a retry works
    if (!g_err && !g_first) g_ok = Update.end(true);
    Serial.printf_P(PSTR("Update %s: %u\n"), g_ok ? "ok" : "failed", up.totalSize);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    scrFail(g_err ? g_err : PSTR("upload aborted"));
  }
}

static void handleUpdateDone() {
  corsHeaders();
  if (!g_ok) {
    String err = g_err ? String(FPSTR(g_err))
               : Update.hasError() ? Update.getErrorString() : String(F("no firmware received"));
    g_err = nullptr;
    scrFail(err.c_str());
    server.send(200, F("text/html"), String(F("Update error: ")) + err);
    return;
  }
  // Leave "Do not unplug" up: after the restart the boot loader still copies
  // the new image into place, and the panel may go dark while it does.
  scrFill(84, 134, 72, 16, C_BLACK);
  scrText(150, 2, C_GREEN, PSTR("Done, restarting"));
  scrText(214, 1, C_GRAY, PSTR("until the new firmware starts"));
  server.client().setNoDelay(true);
  server.send_P(200, PSTR("text/html"), kSuccess);
  delay(100);
  server.client().stop();
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("\nsmalltv-mod loader"));
  WiFi.persistent(false);
  scrBegin();

  const char* ssid = LOADER_SSID;
  bool joined = false;
  if (ssid[0]) {
    WiFi.mode(WIFI_STA);
    WiFi.hostname("smalltv-loader");
    WiFi.begin(LOADER_SSID, LOADER_PASS);
    for (int i = 0; i < 120 && WiFi.status() != WL_CONNECTED; i++) { delay(250); yield(); }
    joined = (WiFi.status() == WL_CONNECTED);
  }
  if (!joined) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SmallTV-Loader");        // open AP -> browse to 192.168.4.1
  }

  String ip = joined ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  Serial.println(ip);
  scrHome(joined ? ssid : "SmallTV-Loader", ip.c_str());

  // The bare address opens the upload form; /update keeps working for the
  // existing instructions and links.
  server.on(F("/"), HTTP_GET, sendPage);
  server.on(F("/update"), HTTP_GET, sendPage);
  server.on(F("/update"), HTTP_OPTIONS, [] {
    corsHeaders();
    server.send(200, F("text/html"), F("y"));
  });
  server.on(F("/update"), HTTP_POST, handleUpdateDone, handleUpload);
  server.begin();
}

void loop() {
  server.handleClient();
  yield();
}
#endif  // LOADER_BUILD
