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
// The screen says where to go (network and address), so a dark panel is no
// longer mistaken for a bricked device. It is drawn with a few raw ST7789
// commands and a 5x7 font instead of a graphics library, to keep the image
// small.
//
// WiFi creds are compile-time only (-DLOADER_SSID / -DLOADER_PASS), never in
// the repo. Without them it falls back to an open "SmallTV-Loader" AP at
// 192.168.4.1 so the full image can still be uploaded by hand.
#ifdef LOADER_BUILD
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <SPI.h>
#include "board_esp8266.h"
#include "loader_font.h"

#ifndef LOADER_SSID
#define LOADER_SSID ""
#endif
#ifndef LOADER_PASS
#define LOADER_PASS ""
#endif

static ESP8266WebServer       server(80);
static ESP8266HTTPUpdateServer updater;

// ---- Status screen (ST7789 240x240, SPI mode 3) ----------------------------
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GRAY   0x8410
#define C_YELLOW 0xFFE0
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
    WiFi.softAP("SmallTV-Loader");        // open AP -> browse to 192.168.4.1/update
  }

  String ip = joined ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  Serial.println(ip);
  scrHome(joined ? ssid : "SmallTV-Loader", (ip + F("/update")).c_str());
  updater.setup(&server);                 // serves /update (GET form + POST handler)
  server.begin();
}

void loop() {
  server.handleClient();
  yield();
}
#endif  // LOADER_BUILD
