// Rain radar screen simulator: what the SmallTV shows once plugged in.
//
// Runs the firmware's own rain code (RainMode.cpp, RainClient.cpp,
// RainPng.cpp, Settings.cpp) on the PC, drawing with the real GFX Library
// for Arduino into a 240x240 canvas (sim_gfx.cpp), against an in-memory
// LittleFS and a stand-in network that serves real RainViewer tiles fetched
// beforehand by simulate.py. Time is virtual: each network step takes about
// as long as on the device, so the timeline matches what you would see.
//
// Every time the screen changes, the frame is written to OUT/NNNN.rgb565
// (raw little-endian RGB565, 240x240) and a line to OUT/timeline.csv:
// "index,ms,label". simulate.py turns them into PNGs and an animated GIF.
//
//   sim TILES_DIR OUT_DIR lat lon rangeKm animSec uiScale [outage] [wind=FROM,KMH] [windunit=N]
// wind=FROM,KMH turns the wind arrow on (cloud level) and answers the
// Open-Meteo request with that wind (direction it comes from, in degrees).
#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include "LittleFS.h"
#include "ESP8266HTTPClient.h"
#include "Settings.h"
#include "RainClient.h"
#include "RainMode.h"
#include "Gfx.h"

uint32_t g_fakeMillis = 0;
FakeFs   g_fs;
FSClass  LittleFS;
FakeHttp g_net;
WiFiClass WiFi;
MDNSClass MDNS;
UpdateClass Update;
EspClass ESP;

void simGfxBegin();
uint16_t* simFramebuffer();

static bool g_clockSynced = true;
bool clockSynced() { return g_clockSynced; }

// ---- the network: weather-maps.json and the tiles, from TILES_DIR -----------
static std::string g_tiles;
static std::map<std::string, std::string> g_byPath;   // RainViewer frame path -> file
static bool g_providerDown = false;
// Device timings (ESP8266, measured): the frame list ~1.5 s, a tile step
// (download + decode + write) ~20 s.
static const uint32_t kListMs = 1500, kTileMs = 20000;

static bool readFile(const std::string& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

static std::string g_wind;   // the Open-Meteo "current" members, empty = arrow off

static int serve(const std::string& url, std::string& body) {
  if (g_providerDown) { g_fakeMillis += 5000; return -1; }   // connection refused after a timeout
  if (url.find("open-meteo") != std::string::npos) {
    g_fakeMillis += kListMs;
    body = "{\"current\":{" + g_wind + "}}";
    return 200;
  }
  if (url.find("weather-maps.json") != std::string::npos) {
    g_fakeMillis += kListMs;
    return readFile(g_tiles + "/weather-maps.json", body) ? 200 : 404;
  }
  g_fakeMillis += kTileMs;
  for (auto& kv : g_byPath)
    if (url.find(kv.first + "/") != std::string::npos)
      return readFile(g_tiles + "/" + kv.second, body) ? 200 : 404;
  fprintf(stderr, "sim: no tile for %s\n", url.c_str());
  return 404;
}

// ---- capture ------------------------------------------------------------------
static std::string g_out;
static unsigned g_frameNo = 0;
static uint64_t g_lastHash = 0;
static FILE* g_timeline = nullptr;

static void capture(const char* label) {
  const uint16_t* fb = simFramebuffer();
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < TFT_WIDTH * TFT_HEIGHT; i++) h = (h ^ fb[i]) * 1099511628211ull;
  if (h == g_lastHash) return;
  g_lastHash = h;
  char name[32];
  snprintf(name, sizeof name, "/%04u.rgb565", g_frameNo);
  if (FILE* f = fopen((g_out + name).c_str(), "wb")) {
    fwrite(fb, 2, TFT_WIDTH * TFT_HEIGHT, f);
    fclose(f);
  }
  fprintf(g_timeline, "%u,%u,%s\n", g_frameNo, g_fakeMillis, label);
  g_frameNo++;
}

// Run the main loop's rain part (service + 5 ms delay) for `ms` of virtual time.
static void runFor(Settings& s, uint32_t ms, const char* label) {
  uint32_t end = g_fakeMillis + ms;
  while ((int32_t)(g_fakeMillis - end) < 0) {
    g_rainMode.service(s);
    char lab[64];
    char st[32];
    snprintf(lab, sizeof lab, "%s (%s)", label, rainStageName(st, sizeof st));
    capture(lab);
    g_fakeMillis += 50;
  }
}

int main(int argc, char** argv) {
  if (argc < 8) {
    fprintf(stderr, "usage: %s TILES_DIR OUT_DIR lat lon rangeKm animSec uiScale [outage] [wind=FROM,KMH] [windunit=N]\n", argv[0]);
    return 2;
  }
  // The heap is not simulated: report a healthy ESP8266 one, above the
  // decoder's floor (RAIN_DECODE_MIN_HEAP) and its yield threshold.
  ESP.freeHeap = 24000;
  g_tiles = argv[1];
  g_out = argv[2];
  bool outage = false;
  float windFrom = -1, windKmh = 0;
  int windUnit = 0;
  for (int i = 8; i < argc; i++) {
    std::string a = argv[i];
    if (a == "outage") outage = true;
    else if (a.rfind("windunit=", 0) == 0) windUnit = atoi(a.c_str() + 9);
    else if (a.rfind("wind=", 0) == 0 && sscanf(a.c_str() + 5, "%f,%f", &windFrom, &windKmh) == 2) {
      char m[200];
      snprintf(m, sizeof m, "\"wind_speed_850hPa\":%.1f,\"wind_direction_850hPa\":%.0f,"
               "\"wind_speed_10m\":%.1f,\"wind_direction_10m\":%.0f", windKmh, windFrom, windKmh, windFrom);
      g_wind = m;
    }
  }
  std::string idx;
  if (!readFile(g_tiles + "/tiles.txt", idx)) { fprintf(stderr, "sim: no tiles.txt in %s\n", argv[1]); return 1; }
  for (size_t p = 0; p < idx.size();) {
    size_t e = idx.find('\n', p);
    std::string line = idx.substr(p, e == std::string::npos ? std::string::npos : e - p);
    size_t sp = line.find(' ');
    if (sp != std::string::npos) g_byPath[line.substr(0, sp)] = line.substr(sp + 1);
    p = e == std::string::npos ? idx.size() : e + 1;
  }
  g_timeline = fopen((g_out + "/timeline.csv").c_str(), "w");
  g_net.handler = serve;

  Settings s;
  s.setDefaults();
  s.mode = MODE_RAIN;
  s.rain.useDevice = false;
  s.rain.lat = (float)atof(argv[3]);
  s.rain.lon = (float)atof(argv[4]);
  s.rain.rangeKm = (uint16_t)atoi(argv[5]);
  s.rain.animSec = (uint16_t)atoi(argv[6]);
  s.rain.uiScale = (uint8_t)atoi(argv[7]);
  if (!g_wind.empty()) s.rain.wind = 1;   // cloud level
  s.rain.windUnit = (uint8_t)windUnit;

  // ---- boot, as main.cpp's setup() draws it (Net.cpp's progress lines) ----
  simGfxBegin();
  gfxBoot("SmallTV", FW_VERSION);              capture("boot");       g_fakeMillis += 1200;
  gfxBoot("SmallTV", "Scanning...");            capture("boot");       g_fakeMillis += 2500;
  gfxBoot("SmallTV", "WiFi: MyWiFi");           capture("boot");       g_fakeMillis += 3000;
  gfxBoot("SmallTV", "192.168.1.50");           capture("boot");       g_fakeMillis += 300;
  g_rainMode.begin(s);
  gfxStaInfo("MyWiFi", "192.168.1.50", "smalltv-1a2b"); capture("connected"); g_fakeMillis += 3500;

  // ---- the rain mode: first fetch, then the animation loop ----
  const uint32_t loopMs = 13u * s.rain.animSec * 1000u;
  runFor(s, 13u * kTileMs + kListMs + 30000u + loopMs, "rain");

  if (outage) {
    // RainViewer stops answering; the clock is not set (the device then
    // falls back on the age of the last good fetch). Skip ahead past the
    // limit: the screen must say the data is unavailable.
    g_providerDown = true;
    g_clockSynced = false;
    runFor(s, 30000u, "outage");                       // the next poll fails: red dot
    g_fakeMillis += (s.rain.pollSec + RAIN_STALE_SEC) * 1000u;   // the loop keeps going meanwhile
    runFor(s, 30000u, "outage, 75 min later");
  }
  fclose(g_timeline);
  char st[32];
  fprintf(stderr, "sim: %u screens, %.1f s of device time, %u frames on disk, stage %s\n",
          g_frameNo, g_fakeMillis / 1000.0, rainCount(), rainStageName(st, sizeof st));
  return 0;
}
