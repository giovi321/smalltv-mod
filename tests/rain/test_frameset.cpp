// Host tests of the rain frame set (RainClient.cpp, compiled as it is):
// frame files named by their radar time, incremental polls, and a set that
// stays consistent through failures, reboots and location changes.
//
// Fakes: an in-memory LittleFS, a scripted RainViewer (frame list + tiles),
// and a stand-in decoder that writes the frame's own time into its file, so
// a frame shown under the wrong time is caught byte for byte.
#include <ArduinoJson.h>
#include <cstdio>
#include <set>
#include "LittleFS.h"
#include "ESP8266HTTPClient.h"
#include "Settings.h"
#include "RainClient.h"
#include "RainPng.h"
#include "RainWind.h"

uint32_t g_fakeMillis = 1000;
FakeFs   g_fs;
FSClass  LittleFS;
FakeHttp g_net;
WiFiClass WiFi;
MDNSClass MDNS;
UpdateClass Update;
EspClass ESP;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, msg) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

// ---- the stand-in decoder: tile body "TILE <time>" -> 57,600 B frame ------
static int g_decodeFailTime = -1;
static bool g_decodeWriteFail = false;
// Heap floor (RAIN_DECODE_MIN_HEAP): the next N decode passes stop as the
// real decoder does when free heap falls under it.
static int  g_lowHeapPasses = 0;
static bool g_decodeLowHeap = false;
static int  g_decodeCalls = 0;
// Time budget (RAIN_DECODE_MAX_MS): the decode of this radar time runs past it.
static uint32_t g_slowTileTime = 0;
static bool g_decodeTimedOut = false;
bool rainDecodeToFs(File& src, File& dst, int, int, uint8_t* win, RainHistory*) {
  g_decodeCalls++;
  g_decodeLowHeap = false;
  g_decodeWriteFail = false;
  g_decodeTimedOut = false;
  if (!win) return false;
  if (g_lowHeapPasses > 0) {
    g_lowHeapPasses--;
    g_decodeLowHeap = true;
    dst.write((const uint8_t*)"partial", 7);   // stopped part-way
    return false;
  }
  char buf[32] = {0};
  src.readBytes(buf, sizeof buf - 1);
  unsigned long t = 0;
  if (sscanf(buf, "TILE %lu", &t) != 1) return false;
  if ((long)t == g_decodeFailTime) {
    dst.write((const uint8_t*)"partial", 7);   // a failed decode leaves a short tmp.raw
    return false;
  }
  if (g_slowTileTime && t == g_slowTileTime) {
    g_decodeTimedOut = true;
    dst.write((const uint8_t*)"partial", 7);   // stopped part-way
    return false;
  }
  std::vector<uint8_t> frame(57600, 0);
  memcpy(frame.data(), &t, sizeof(uint32_t));
  // As the real decoder: a short write fails the decode.
  g_decodeWriteFail = dst.write(frame.data(), frame.size()) != frame.size();
  return !g_decodeWriteFail;
}
bool rainDecodeNoMem() { return false; }
bool rainDecodeLowHeap() { return g_decodeLowHeap; }
bool rainDecodeTimedOut() { return g_decodeTimedOut; }
bool rainDecodeWriteFail() { return g_decodeWriteFail; }

// ---- scripted RainViewer --------------------------------------------------
static uint32_t g_newest = 0;         // newest frame time in the list
static int      g_listCode = 200;
static std::string g_listHost = "https://tilecache.rainviewer.com";
static bool g_pathOverride = false;    // when set: g_newestPath is the newest frame's path, as is
static std::string g_newestPath;
static bool g_badPathNowcast = false;  // ... given to a nowcast frame instead
static std::set<uint32_t> g_tileFail;  // tile times answered with 500
static size_t g_tilePad = 0;           // bytes added after "TILE <time>" (a bigger tile)
static const uint32_t kStep = 600;

// ---- scripted Open-Meteo (the wind arrow) ----------------------------------
// An answer as the service sends it (Brest, 2026-11-23): integer and decimal
// numbers, the units object the filter must drop.
static const char kWindAnswer[] =
    "{\"latitude\":48.39,\"longitude\":-4.4900002,\"generationtime_ms\":0.17,\"utc_offset_seconds\":0,"
    "\"timezone\":\"GMT\",\"timezone_abbreviation\":\"GMT\",\"elevation\":47.0,\"current_units\":{\"time\":"
    "\"iso8601\",\"interval\":\"seconds\",\"wind_speed_850hPa\":\"km/h\",\"wind_direction_850hPa\":\"°\","
    "\"wind_speed_10m\":\"km/h\",\"wind_direction_10m\":\"°\"},\"current\":{\"time\":\"2026-11-23T06:15\","
    "\"interval\":900,\"wind_speed_850hPa\":39.1,\"wind_direction_850hPa\":306,\"wind_speed_10m\":1.6,"
    "\"wind_direction_10m\":333}}";
static int g_windCode = 200;
static std::string g_windBody = kWindAnswer;
static size_t windRequests() {
  size_t n = 0;
  for (auto& u : g_net.log) if (u.find("open-meteo") != std::string::npos) n++;
  return n;
}

static int serve(const std::string& url, std::string& body) {
  if (url.find("api.open-meteo.com") != std::string::npos) {
    if (g_windCode != 200) return g_windCode;
    body = g_windBody;
    return 200;
  }
  if (url.find("weather-maps.json") != std::string::npos) {
    if (g_listCode != 200) return g_listCode;
    body = "{\"version\":\"2.0\",\"generated\":" + std::to_string(g_newest + 30) +
           ",\"host\":\"" + g_listHost + "\",\"radar\":{\"past\":[";
    for (int i = 12; i >= 0; i--) {
      uint32_t t = g_newest - (uint32_t)i * kStep;
      std::string path = "/v2/radar/" + std::to_string(t);
      if (i == 0 && g_pathOverride && !g_badPathNowcast) path = g_newestPath;
      body += "{\"time\":" + std::to_string(t) + ",\"path\":\"" + path + "\"}";
      if (i) body += ",";
    }
    body += "],\"nowcast\":[";
    if (g_badPathNowcast)
      body += "{\"time\":" + std::to_string(g_newest + kStep) + ",\"path\":\"" + g_newestPath + "\"}";
    body += "]},\"satellite\":{\"infrared\":[]}}";
    return 200;
  }
  size_t p = url.find("/v2/radar/");
  if (p == std::string::npos) return 404;
  uint32_t t = (uint32_t)strtoul(url.c_str() + p + 10, nullptr, 10);
  if (g_tileFail.count(t)) return 500;
  body = "TILE " + std::to_string(t);
  body.append(g_tilePad, ' ');
  return 200;
}

static size_t tileRequests() {
  size_t n = 0;
  for (auto& u : g_net.log) if (u.find("/v2/radar/") != std::string::npos) n++;
  return n;
}

// ---- invariants, checked after every service() step ------------------------
static bool g_metaStrict = true;   // off while a test removes a listed file itself

static void checkConsistent(const char* when) {
  char msg[160];
  uint8_t n = rainCount();
  // Every shown frame has its own file, holding its own time; times ascend.
  for (uint8_t i = 0; i < n; i++) {
    char path[24];
    bool ok = rainFramePath(i, path, sizeof path);
    auto it = g_fs.files.find(path);
    uint32_t inFile = 0;
    if (ok && it != g_fs.files.end() && it->second.size() == 57600) memcpy(&inFile, it->second.data(), 4);
    snprintf(msg, sizeof msg, "%s: frame %u shows time %u", when, i, rainFrameTime(i));
    CHECK(ok && inFile == rainFrameTime(i), msg);
    if (i) { snprintf(msg, sizeof msg, "%s: times ascend at %u", when, i); CHECK(rainFrameTime(i) > rainFrameTime(i - 1), msg); }
  }
  // meta.json lists only frames whose file exists with that time.
  // (A meta.json without the location key is from older firmware: never shown.)
  auto m = g_fs.files.find("/rain/meta.json");
  if (g_metaStrict && m != g_fs.files.end()) {
    JsonDocument doc;
    std::string s(m->second.begin(), m->second.end());
    CHECK(!deserializeJson(doc, s), "meta.json parses");
    if (doc["k"].isNull()) return;
    for (JsonVariant t : doc["times"].as<JsonArray>()) {
      char path[24];
      snprintf(path, sizeof path, "/rain/t%lu.raw", (unsigned long)t.as<uint32_t>());
      auto f = g_fs.files.find(path);
      uint32_t inFile = 0;
      if (f != g_fs.files.end() && f->second.size() == 57600) memcpy(&inFile, f->second.data(), 4);
      snprintf(msg, sizeof msg, "%s: meta.json lists %u with its file", when, t.as<uint32_t>());
      CHECK(inFile == t.as<uint32_t>(), msg);
    }
  }
  // Never more frame files than a full set + the decode temp file.
  size_t raws = 0;
  for (auto& f : g_fs.files) if (f.first.size() > 4 && f.first.compare(f.first.size() - 4, 4, ".raw") == 0) raws++;
  snprintf(msg, sizeof msg, "%s: %zu .raw files on disk", when, raws);
  CHECK(raws <= MAX_RAIN_FRAMES + 1, msg);
}

static bool hasFile(const char* p) { return g_fs.files.count(p) > 0; }
static std::string stageName() { char b[32]; return rainStageName(b, sizeof b); }
// The name every stage must have in /api/status (rainStageName reads them
// from one flash string in enum order: a missing or extra name shifts all
// the following ones). Checked after every poll, for each stage reached.
static const char* expectedStageName(RainStage st) {
  switch (st) {
    case RAIN_IDLE: return "idle";              case RAIN_FETCHING: return "fetching";
    case RAIN_NO_HOME: return "no home set";    case RAIN_CONNECT_FAIL: return "connect failed";
    case RAIN_HTTP_ERROR: return "http error";  case RAIN_PARSE_FAIL: return "parse failed";
    case RAIN_NO_FRAMES: return "no frames in feed"; case RAIN_PNG_FAIL: return "png decode failed";
    case RAIN_NO_MEM: return "out of memory";   case RAIN_FS_FAIL: return "filesystem error";
    case RAIN_FS_FULL: return "storage full";   case RAIN_LOW_HEAP: return "low memory, tile skipped";
    case RAIN_SLOW_TILE: return "decode too slow, tile skipped"; case RAIN_OK: return "ok";
  }
  return "?";
}
static std::set<int> g_stagesSeen;

// Run one poll to the end, checking the invariants after every step.
static void poll(const Settings& s, const char* when) {
  rainForceRefresh();
  for (int step = 0; step < 40; step++) {
    rainService(s);
    g_fakeMillis += 25000;
    checkConsistent(when);
    if (rainStage() != RAIN_FETCHING) break;
  }
  CHECK(rainStage() != RAIN_FETCHING, "poll finished");
  CHECK(stageName() == expectedStageName(rainStage()), "stage name matches the stage");
  g_stagesSeen.insert(rainStage());
}


int main() {
  g_net.handler = serve;
  Settings s;
  s.setDefaults();
  s.rain.useDevice = false;
  s.rain.lat = 48.39012f;
  s.rain.lon = -4.48765f;
  g_newest = 1763380800;

  // 0. Rain off at boot (P6): rainInit does not create /rain.
  rainInit(s);
  CHECK(!g_fs.dirs.count("/rain"), "rainInit leaves no /rain behind");

  // 1. Upgrade from the fNN.raw firmware: an old meta.json without the key
  //    and thirteen fNN.raw files. Nothing old is shown under new times; the
  //    first poll fetches everything and removes the old files.
  g_fs.dirs["/rain"] = true;
  for (int i = 0; i < 13; i++) {
    char p[24]; snprintf(p, sizeof p, "/rain/f%02d.raw", i);
    g_fs.files[p] = std::vector<uint8_t>(57600, 7);
  }
  { std::string m = "{\"generated\":1,\"nowcast\":0,\"times\":[1,2,3]}"; g_fs.files["/rain/meta.json"] = std::vector<uint8_t>(m.begin(), m.end()); }
  rainInit(s);
  CHECK(rainCount() == 0, "old-format set not shown");
  poll(s, "first poll");
  CHECK(rainStage() == RAIN_OK, "first poll ok");
  CHECK(rainCount() == 13, "13 frames after the first poll");
  CHECK(tileRequests() == 13, "first poll downloads 13 tiles");
  CHECK(!hasFile("/rain/f00.raw") && !hasFile("/rain/f12.raw"), "fNN.raw files removed");
  CHECK(rainFrameTime(12) == g_newest, "newest frame last");

  // Privacy: every tile URL carries the position rounded to 2 decimals.
  {
    size_t tiles = 0, rounded = 0;
    for (auto& u : g_net.log) {
      if (u.find("/v2/radar/") == std::string::npos) continue;
      tiles++;
      if (u.find("/256/7/48.39/-4.49/2/1_1.png") != std::string::npos) rounded++;
      CHECK(u.find("48.390") == std::string::npos && u.find("-4.4876") == std::string::npos, "no stored decimals in the URL");
    }
    CHECK(tiles == 13 && rounded == tiles, "tile URLs carry the 2-decimal position");
  }
  CHECK(std::string(rainLastUrl()).find("/48.39/-4.49/") != std::string::npos, "status URL is the rounded one too");

  // 2. Ten minutes later: one new frame, one download.
  g_net.log.clear();
  g_newest += kStep;
  poll(s, "shift by one");
  CHECK(rainStage() == RAIN_OK && !rainError(), "shift poll ok");
  CHECK(tileRequests() == 1, "a shifted list downloads only the new frame");
  CHECK(rainCount() == 13 && rainFrameTime(12) == g_newest, "set complete, newest last");
  CHECK(!hasFile(("/rain/t" + std::to_string(g_newest - 13 * kStep) + ".raw").c_str()), "the frame out of the window is removed");

  // 3. Nothing new: only the list is fetched.
  g_net.log.clear();
  poll(s, "same list");
  CHECK(tileRequests() == 0 && rainStage() == RAIN_OK, "an unchanged list downloads nothing");

  // 4. Two new frames, the second one fails to download: the set keeps the
  //    frames it has, each under its own time, and flags the error.
  g_net.log.clear();
  g_newest += 2 * kStep;
  g_tileFail = {g_newest};
  poll(s, "tile failure");
  CHECK(rainError() && rainStage() == RAIN_HTTP_ERROR, "failure flagged");
  CHECK(rainCount() == 12, "12 consistent frames after a failed tile");
  CHECK(rainFrameTime(11) == g_newest - kStep, "newest good frame last");

  // 5. The next poll recovers with one download.
  g_net.log.clear();
  g_tileFail.clear();
  poll(s, "recovery");
  CHECK(rainStage() == RAIN_OK && !rainError() && rainCount() == 13, "recovered to 13 frames");
  CHECK(tileRequests() == 1, "recovery downloads only the missing frame");

  // 6. A decode failure leaves no tmp.raw and no frame behind.
  g_net.log.clear();
  g_newest += kStep;
  g_decodeFailTime = (int)g_newest;
  poll(s, "decode failure");
  CHECK(rainStage() == RAIN_PNG_FAIL && rainCount() == 12, "decode failure: 12 frames");
  CHECK(!hasFile("/rain/tmp.raw"), "no tmp.raw left");
  g_decodeFailTime = -1;
  poll(s, "after decode failure");
  CHECK(rainStage() == RAIN_OK && rainCount() == 13, "decode failure recovered");

  // 6b. The heap floor (PR #122): a decode that would take free heap under
  //     RAIN_DECODE_MIN_HEAP stops. The 8 KB window pass retries in the
  //     small-memory mode; under the floor there too, the tile is skipped:
  //     the set is kept whole, nothing temporary stays on disk, and the next
  //     poll fetches the frame.
  {
    g_net.log.clear();
    const uint32_t newestBefore = rainFrameTime(rainCount() - 1);
    g_newest += kStep;
    g_lowHeapPasses = 2;
    g_decodeCalls = 0;
    poll(s, "low heap");
    CHECK(rainStage() == RAIN_LOW_HEAP && rainError(), "low heap: tile skipped, reported");
    CHECK(stageName() == "low memory, tile skipped", "low heap: stage name");
    CHECK(g_decodeCalls == 2, "low heap: full window, then the small-memory mode");
    CHECK(tileRequests() == 1, "low heap: the fetch stops at the skipped tile");
    CHECK(rainCount() == 12 && rainFrameTime(11) == newestBefore, "low heap: the frames kept are whole, newest good one last");
    CHECK(!hasFile("/rain/tmp.raw") && !hasFile("/rain/tmp.png"), "low heap: no temp file left");
    CHECK(!hasFile(("/rain/t" + std::to_string(g_newest) + ".raw").c_str()), "low heap: no frame for the skipped tile");
    poll(s, "after low heap");
    CHECK(rainStage() == RAIN_OK && rainCount() == 13 && rainFrameTime(12) == g_newest, "low heap: next poll fetches it");

    // Under the floor with the 8 KB window only: the small-memory pass decodes it.
    g_newest += kStep;
    g_lowHeapPasses = 1;
    g_decodeCalls = 0;
    poll(s, "low heap, full window only");
    CHECK(rainStage() == RAIN_OK && !rainError() && rainCount() == 13 && rainFrameTime(12) == g_newest,
          "low heap with the full window: decoded in the small-memory mode");
    CHECK(g_decodeCalls == 2 && rainSmallMem(), "low heap with the full window: second pass is the small-memory one");
  }

  // 6c. The time budget (PR #122): a tile whose decode runs past
  //     RAIN_DECODE_MAX_MS is skipped (not retried in the other mode), and
  //     later polls skip it without a request, so the newer frames come.
  {
    g_net.log.clear();
    g_newest += kStep;
    g_slowTileTime = g_newest;
    g_decodeCalls = 0;
    poll(s, "slow tile");
    CHECK(rainStage() == RAIN_SLOW_TILE && rainError(), "slow tile: skipped, reported");
    CHECK(stageName() == "decode too slow, tile skipped", "slow tile: stage name");
    CHECK(g_decodeCalls == 1, "slow tile: no second pass");
    CHECK(rainCount() == 12 && !hasFile("/rain/tmp.raw") && !hasFile("/rain/tmp.png"), "slow tile: set kept, no temp file");
    g_net.log.clear();
    poll(s, "after slow tile");
    CHECK(tileRequests() == 0 && rainStage() == RAIN_OK && rainCount() == 12, "slow tile: not requested again");
    g_net.log.clear();
    g_newest += kStep;
    poll(s, "newer frame after slow tile");
    CHECK(tileRequests() == 1 && rainStage() == RAIN_OK && rainFrameTime(rainCount() - 1) == g_newest,
          "slow tile: the newer frame still comes");
    for (uint8_t i = 0; i < rainCount(); i++) CHECK(rainFrameTime(i) != g_slowTileTime, "slow tile: never in the set");
    // Once it has left the 2-hour window, a full set again.
    g_slowTileTime = 0;
    g_newest += 13 * kStep;
    poll(s, "slow tile out of the window");
    CHECK(rainStage() == RAIN_OK && rainCount() == 13, "slow tile: 13 frames once it is out of the window");
  }

  // 7. The frame list fails: nothing changes.
  uint32_t before = rainFrameTime(0);
  g_listCode = 503;
  g_newest += kStep;
  poll(s, "list failure");
  CHECK(rainError() && rainCount() == 13 && rainFrameTime(0) == before, "list failure keeps the set");
  g_listCode = 200;
  poll(s, "after list failure");
  CHECK(rainStage() == RAIN_OK && rainCount() == 13, "list failure recovered");

  // 7b. A list naming another tile host is refused: no tile is requested
  //     from it and the set is kept.
  for (const char* bad : {"http://evil.example", "https://evil.com/x.rainviewer.com",
                          "evil.com#.rainviewer.com", "tilecache.rainviewer.com:8080",
                          "rainviewer.com", "user@tilecache.rainviewer.com",
                          "tilecache.rainviewer.com.evil.com", ""}) {
    g_listHost = bad;
    g_net.log.clear();
    g_newest += kStep;
    poll(s, "foreign host");
    CHECK(rainError() && rainStage() == RAIN_PARSE_FAIL, "foreign host refused");
    CHECK(tileRequests() == 0 && rainCount() == 13, "no tile from a foreign host");
  }

  // 7c. A frame path that would move the request elsewhere (PR #122 review:
  //     "@evil.example/x" reads as credentials + host, ":8080" as a port), or
  //     carry a query, a fragment or anything outside '/' + [A-Za-z0-9/_-]
  //     (64 characters at most): the whole list is refused, nothing is
  //     requested and the set is kept. Same for a nowcast frame.
  g_listHost = "https://tilecache.rainviewer.com";
  {
    const std::string long65 = "/v2/radar/" + std::string(55, 'a');
    for (int nc = 0; nc < 2; nc++) {
      for (const std::string& bad : {std::string("@evil.example/x"), std::string(":8080/v2/radar/1"),
                                     std::string("/v2/radar/1?x=1"), std::string("/v2/radar/1#f"),
                                     std::string("/v2/../radar/1"), std::string("v2/radar/1"),
                                     std::string("/v2/radar/1@evil.example"), std::string("/v2/radar/1%2f"),
                                     std::string("/v2/radar/1 x"), std::string("/"), std::string(""), long65}) {
        g_newestPath = bad;
        g_pathOverride = true;
        g_badPathNowcast = nc != 0;
        g_net.log.clear();
        g_newest += kStep;
        poll(s, "bad frame path");
        CHECK(rainError() && rainStage() == RAIN_PARSE_FAIL, "bad frame path refused");
        CHECK(tileRequests() == 0 && rainCount() == 13, "no tile for a list with a bad frame path");
        for (auto& u : g_net.log) CHECK(u.find("evil") == std::string::npos && u.find(":8080") == std::string::npos, "no request to another host or port");
      }
    }
    g_badPathNowcast = false;
    // The longest path accepted: 64 characters.
    g_newest += kStep;
    g_newestPath = "/v2/radar/" + std::to_string(g_newest) + "_" + std::string(64 - 11 - std::to_string(g_newest).size(), 'b');
    CHECK(g_newestPath.size() == 64, "64-character path built");
    g_net.log.clear();
    poll(s, "64-character path");
    CHECK(rainStage() == RAIN_OK && rainFrameTime(rainCount() - 1) == g_newest, "a 64-character path is accepted");
    g_newestPath.clear();
    g_pathOverride = false;
  }
  // Redirects are never followed: a 3xx could send the request to any host.
  CHECK(g_net.redirectGets == 0, "every list and tile GET has redirects off");
  g_listHost = "http://tilecache2.rainviewer.com";
  g_net.log.clear();
  poll(s, "other rainviewer host");
  CHECK(rainStage() == RAIN_OK && rainCount() == 13, "another rainviewer.com host accepted");
  for (auto& u : g_net.log)
    if (u.find("/v2/radar/") != std::string::npos)
      CHECK(u.find("tilecache2.rainviewer.com/v2/") != std::string::npos, "tiles from the listed host");
  g_listHost = "https://tilecache.rainviewer.com";
  poll(s, "back to tilecache");
  CHECK(rainStage() == RAIN_OK && rainCount() == 13, "host check recovered");

  // 8. meta.json cannot be renamed after a new frame: the shown set still
  //    follows the disk, and the meta.json left behind only lists frames
  //    whose files exist (the invariant check covers it).
  g_newest += kStep;
  g_fs.failRenameName = "meta.json";
  g_fs.failRenameTo = 1;
  poll(s, "meta failure");
  CHECK(rainStage() == RAIN_FS_FAIL && rainError(), "meta failure flagged");
  g_fs.failRenameTo = 0;

  // 8b. meta.json cannot be written at the list step: nothing is purged, the
  //     old meta.json and all its frames stay valid.
  poll(s, "before list-step meta failure");
  g_newest += kStep;
  {
    uint32_t oldest = rainFrameTime(0);
    g_fs.failRenameName = "meta.json";
    g_fs.failRenameTo = 1;
    g_net.log.clear();
    poll(s, "list-step meta failure");
    CHECK(rainStage() == RAIN_FS_FAIL && tileRequests() == 0, "list-step meta failure stops the poll");
    CHECK(hasFile(("/rain/t" + std::to_string(oldest) + ".raw").c_str()), "old frames kept when meta.json fails");
    g_fs.failRenameTo = 0;
  }

  // 9. Reboot: the set comes back from meta.json alone.
  poll(s, "before reboot");
  CHECK(rainStage() == RAIN_OK, "ok before reboot");
  uint8_t n = rainCount();
  uint32_t first = rainFrameTime(0), last = rainFrameTime(n - 1);
  rainInit(s);
  CHECK(rainCount() == n && rainFrameTime(0) == first && rainFrameTime(n - 1) == last, "reboot restores the set");
  checkConsistent("after reboot");

  // 10. A listed frame whose file vanished is skipped at boot.
  {
    char p[24]; rainFramePath(5, p, sizeof p);
    g_fs.files.erase(p);
    g_metaStrict = false;
    rainInit(s);
    CHECK(rainCount() == n - 1, "missing file skipped at boot");
    checkConsistent("missing file");
    g_net.log.clear();
    poll(s, "refill");
    g_metaStrict = true;
    checkConsistent("after refill");
    CHECK(rainCount() == 13 && tileRequests() == 1, "the missing frame is fetched again");
  }

  // 11. New location: the old set is not shown, and is fully replaced.
  s.rain.lat = 52.367573f;
  s.rain.lon = 4.904139f;
  rainInit(s);
  CHECK(rainCount() == 0, "frames of the old location are not shown");
  g_net.log.clear();
  poll(s, "new location");
  CHECK(rainStage() == RAIN_OK && rainCount() == 13 && tileRequests() == 13, "new location fetched in full");
  bool allNew = true;
  for (auto& u : g_net.log) if (u.find("/v2/radar/") != std::string::npos && u.find("/7/52.37/4.90/") == std::string::npos) allNew = false;
  CHECK(allNew, "tiles requested at the new location");

  // 11b. A move within the rounding (same tile URL) keeps the set: nothing
  //      is refetched.
  s.rain.lat = 52.3712f;
  s.rain.lon = 4.9012f;
  rainInit(s);
  CHECK(rainCount() == 13, "a move within the rounding keeps the frames");
  g_net.log.clear();
  poll(s, "move within rounding");
  CHECK(tileRequests() == 0 && rainStage() == RAIN_OK, "and downloads nothing");

  // 11c. Frames stored with the previous palette (a meta.json with the
  //      right place and crop but no palette version) are not shown: their
  //      bytes index another colour table.
  {
    auto& m = g_fs.files["/rain/meta.json"];
    std::string js(m.begin(), m.end());
    size_t p = js.find(",\"pv\":");
    CHECK(p != std::string::npos, "meta.json records the palette version");
    if (p != std::string::npos) {
      size_t e = js.find_first_of(",}", p + 1);
      js.erase(p, e - p);
      m.assign(js.begin(), js.end());
    }
    rainInit(s);
    CHECK(rainCount() == 0, "frames of the previous palette are not shown");
    g_net.log.clear();
    poll(s, "palette change");
    CHECK(rainCount() == 13 && tileRequests() == 13, "and are fetched again in full");
  }

  // 12. New range (another crop) at the same place: refetched in full too.
  s.rain.rangeKm = 20;
  rainInit(s);
  CHECK(rainCount() == 0, "frames of the old crop are not shown");
  g_net.log.clear();
  poll(s, "new range");
  CHECK(rainCount() == 13 && tileRequests() == 13, "new crop fetched in full");

  // 13. A fetch interrupted by a mode switch (rainFreeForRequest + rainInit)
  //     midway keeps a consistent set and finishes on the next poll.
  g_newest += 3 * kStep;
  rainForceRefresh();
  for (int i = 0; i < 3; i++) { rainService(s); g_fakeMillis += 25000; checkConsistent("interrupted"); }
  rainFreeForRequest();
  rainService(s); checkConsistent("window given back");
  rainInit(s);
  checkConsistent("after interruption");
  poll(s, "after interruption");
  CHECK(rainStage() == RAIN_OK && rainCount() == 13, "interrupted fetch completes later");

  // 14. Stale data: never show frames older than pollSec + RAIN_STALE_SEC.
  {
    const uint32_t ps = s.rain.pollSec, limit = ps + RAIN_STALE_SEC;
    // With the clock: by the time RainViewer generated the last list.
    uint32_t gen = rainGenerated();
    CHECK(gen > rainFrameTime(rainCount() - 1), "the list's generation time is known");
    CHECK(!rainStale(ps, gen + 1200), "a 20-minute-old list is fresh (clock set)");
    CHECK(!rainStale(ps, gen + limit), "fresh up to the limit");
    CHECK(rainStale(ps, gen + limit + 1), "stale past the limit (clock set)");
    // No clock: by the time since the last good fetch.
    CHECK(!rainStale(ps, 0), "just fetched: fresh (no clock)");
    // The provider stops answering: list fails from now on.
    g_listCode = 503;
    g_fakeMillis += (limit - 300) * 1000;   // each poll step also advances the clock 25 s
    poll(s, "provider down 1");
    CHECK(rainError() && !rainStale(ps, 0), "failing, but not old yet");
    g_fakeMillis += 300 * 1000;
    CHECK(rainStale(ps, 0), "stale once the last good fetch is older than the limit (no clock)");
    CHECK(rainCount() == 13, "the frames stay on disk for when the provider is back");
    // A reboot reads the set back: its age is unknown without a clock, so
    // it counts from the boot; with a clock, from the frame times.
    rainInit(s);
    CHECK(!rainStale(ps, 0), "after a reboot without clock: shown until the limit");
    CHECK(rainStale(ps, gen + limit + 1), "after a reboot with clock: old frames stay hidden");
    g_fakeMillis += (limit + 1) * 1000;
    CHECK(rainStale(ps, 0), "after a reboot without clock: stale at the limit");
    // The provider is back: fresh again.
    g_listCode = 200;
    g_newest += 30 * kStep;
    poll(s, "provider back");
    CHECK(rainStage() == RAIN_OK && !rainStale(ps, 0) && !rainStale(ps, rainGenerated() + 1200), "fresh again once the provider answers");

    // A first fetch brings the oldest frames first (two hours old): the set
    // is not stale meanwhile, the list it comes from is fresh (seen in the
    // simulator: "Radar data unavailable" for the first ~100 s after boot).
    s.rain.rangeKm = 70;
    rainInit(s);
    g_newest += kStep;
    rainForceRefresh();
    bool everStale = false;
    for (int i = 0; i < 6; i++) {
      rainService(s);
      g_fakeMillis += 25000;
      if (rainCount() > 0 && rainStale(ps, g_newest + 600)) everStale = true;
    }
    CHECK(rainCount() >= 2 && rainCount() < 13, "first fetch under way");
    CHECK(!everStale, "a first fetch in progress is never called stale");
    poll(s, "first fetch end");
    // No frames: nothing to call stale (the screen shows its own message).
    s.rain.rangeKm = 60;
    rainInit(s);
    CHECK(rainCount() == 0 && !rainStale(ps, 0) && !rainStale(ps, 0xFFFFFFFFu), "empty set is never stale");
  }

  // 15. A full filesystem. Every write is checked: a frame that does not
  //     fit is dropped, never kept truncated, and no temp file stays.
  {
    s.rain.rangeKm = 50;
    auto noTemp = [] { return !hasFile("/rain/tmp.png") && !hasFile("/rain/tmp.raw") && !hasFile("/rain/meta.tmp"); };
    // a) Room for a few frames only: every frame of the list is still
    //    downloaded, oldest first, and the oldest ones are deleted to make
    //    room: the set keeps the NEWEST frames (the screen keeps "now"),
    //    consecutive, and this is not an error.
    rainRemoveAll();
    rainInit(s);
    g_net.log.clear();
    g_fs.capacity = g_fs.used() + 600000;   // ~6 frames with the reserve and the fetch's own room
    poll(s, "fs small");
    uint8_t n = rainCount();
    CHECK(rainStage() == RAIN_OK && !rainError(), "a small filesystem is not an error");
    CHECK(n >= 2 && n < 13, "a shorter set on a small filesystem");
    CHECK(n && rainFrameTime(n - 1) == g_newest, "the newest frame is kept");
    CHECK(n && rainFrameTime(0) == g_newest - (uint32_t)(n - 1) * kStep, "the set is the newest frames, consecutive");
    CHECK(tileRequests() == 13, "every listed frame was fetched, the oldest ones then dropped");
    CHECK(noTemp() && rainFsFree() > 0, "no temp file, free space reported");
    // The next poll: one new frame, one download, the oldest one goes.
    g_net.log.clear();
    g_newest += kStep;
    poll(s, "fs small, shift");
    CHECK(rainStage() == RAIN_OK && rainCount() == n && rainFrameTime(n - 1) == g_newest, "a small set slides with the list");
    CHECK(tileRequests() == 1, "a small set downloads only the new frame");
    // No Content-Length: the PNG size is guessed, same outcome.
    g_net.noLength = true;
    g_newest += kStep;
    poll(s, "fs small, no length");
    g_net.noLength = false;
    CHECK(rainStage() == RAIN_OK && rainCount() >= 1 && rainFrameTime(rainCount() - 1) == g_newest, "no Content-Length: newest kept");
    // b0) No room even with nothing else of rain on disk: storage full, nothing written.
    rainRemoveAll();
    rainInit(s);
    g_fs.capacity = g_fs.used() + 100000;
    poll(s, "fs full");
    CHECK(rainError() && rainStage() == RAIN_FS_FULL && rainCount() == 0, "storage full reported");
    CHECK(noTemp(), "no temp file after storage full");
    g_fs.capacity = 0;
    poll(s, "fs room again");
    CHECK(rainStage() == RAIN_OK && rainCount() == 13, "set completed once there is room");

    // b) The filesystem refuses part of the tile download.
    g_newest += kStep;
    g_fs.shortName = "tmp.png";
    g_fs.shortAfter = 5;
    poll(s, "short png write");
    g_fs.shortAfter = -1;
    CHECK(rainStage() == RAIN_FS_FULL && rainCount() == 12, "short download: frame dropped");
    CHECK(noTemp(), "short download: no temp file");

    // c) The filesystem refuses part of the decoded frame.
    g_fs.shortName = "tmp.raw";
    g_fs.shortAfter = 1000;
    poll(s, "short frame write");
    g_fs.shortAfter = -1;
    CHECK(rainStage() == RAIN_FS_FULL && rainCount() == 12, "short frame: frame dropped");
    CHECK(!hasFile(("/rain/t" + std::to_string(g_newest) + ".raw").c_str()), "short frame: never renamed into the set");
    CHECK(noTemp(), "short frame: no temp file");

    // d) The filesystem refuses part of meta.json: the old one stays.
    std::vector<uint8_t> oldMeta = g_fs.files["/rain/meta.json"];
    g_fs.shortName = "meta.tmp";
    g_fs.shortAfter = 10;
    poll(s, "short meta write");
    g_fs.shortAfter = -1;
    CHECK(rainError() && rainStage() == RAIN_FS_FAIL, "short meta.json: error");
    CHECK(g_fs.files["/rain/meta.json"] == oldMeta && noTemp(), "short meta.json: the old one is kept");
    poll(s, "after short writes");
    CHECK(rainStage() == RAIN_OK && rainCount() == 13, "recovered after the short writes");

    // e) Rain turned off: /rain goes, whatever it holds.
    g_fs.files["/rain/tmp.png"] = std::vector<uint8_t>(10, 1);
    g_fs.files["/rain/h3"] = std::vector<uint8_t>(10, 1);
    rainRemoveAll();
    size_t left = 0;
    for (auto& f : g_fs.files) if (f.first.compare(0, 6, "/rain/") == 0) left++;
    CHECK(left == 0 && !g_fs.dirs.count("/rain"), "rain off: /rain removed");
    CHECK(rainCount() == 0 && !rainFetching(), "rain off: empty set");
    // Rain off holds no state: every reader /api/status uses answers "nothing".
    {
      char st[32], p[24];
      CHECK(rainStage() == RAIN_IDLE && std::string(rainStageName(st, sizeof st)) == "idle" && !rainError(),
            "rain off: idle, no error");
      CHECK(rainLastUrl()[0] == 0 && rainLastHttp() == 0 && rainLastTryMs() == 0 && rainLastOkMs() == 0 &&
            rainGenerated() == 0 && rainFrameTime(0) == 0 && !rainFramePath(0, p, sizeof p) &&
            !rainStale(900, 0) && rainMinHeap() == 0 && rainFsFree() == 0 && rainDecodeMs() == 0,
            "rain off: every status reader answers nothing");
      rainForceRefresh();   // /api/refresh with rain off: nothing to do, no crash
      rainHeapSample();     // and the decoder's hooks
      CHECK(!rainDecodeOverTime(), "rain off: no decode budget running");
    }
    rainInit(s);
    CHECK(rainCount() == 0, "rain off then on: nothing comes back");
    // f) Back on: the first fetch recreates /rain.
    poll(s, "rain back on");
    CHECK(g_fs.dirs.count("/rain") && rainStage() == RAIN_OK && rainCount() == 13, "rain back on: /rain recreated, full set");
  }

  // 16. A theme upload drops the rain frames (PR #122: a full set leaves
  //     only RAIN_FS_KEEP_FREE, too little for most packages). It can come
  //     between two steps of a fetch: the fetch's list is freed with it, and
  //     the steps that follow must not touch it. The next poll refills.
  {
    g_newest += 3 * kStep;
    g_net.log.clear();
    rainForceRefresh();
    rainService(s);                      // arms the list
    rainService(s);                      // the list
    rainService(s);                      // one tile
    CHECK(rainFetching() && tileRequests() == 1, "theme upload: a fetch is running");
    CHECK(stageName() == "fetching", "stage name while fetching");
    rainRemoveAll();                     // what the theme upload calls
    CHECK(!rainFetching() && rainCount() == 0 && !g_fs.dirs.count("/rain"), "theme upload: frames and fetch dropped");
    CHECK(stageName() == "idle", "stage name once dropped");
    for (int i = 0; i < 3; i++) rainService(s);   // the loop goes on: a new fetch, from an empty set
    poll(s, "after theme upload");
    CHECK(rainStage() == RAIN_OK && rainCount() == 13 && rainFrameTime(12) == g_newest, "theme upload: the next poll refills the set");
  }

  // 17. The download never takes more than the room checked for it (PR #122):
  //     without a Content-Length, RAIN_PNG_SIZE_GUESS; with one, the size it
  //     announces; one announcing more than RAIN_PNG_MAX is refused at once.
  {
    auto noTemp = [] { return !hasFile("/rain/tmp.png") && !hasFile("/rain/tmp.raw"); };
    g_newest += kStep;
    g_tilePad = RAIN_PNG_SIZE_GUESS;     // "TILE <time>" + 32 KB: over the guess
    g_net.noLength = true;
    poll(s, "no length, over the guess");
    g_net.noLength = false;
    CHECK(rainStage() == RAIN_HTTP_ERROR && rainCount() == 12 && noTemp(), "no length: a tile over the reserved size is refused");
    g_tilePad = 100;
    g_net.sizeOverride = 50;             // announces less than it sends
    poll(s, "longer than announced");
    CHECK(rainStage() == RAIN_HTTP_ERROR && rainCount() == 12 && noTemp(), "a tile longer than its Content-Length is refused");
    g_net.sizeOverride = RAIN_PNG_MAX + 1;
    poll(s, "announced too big");
    CHECK(rainStage() == RAIN_HTTP_ERROR && rainCount() == 12 && noTemp(), "a tile announced over RAIN_PNG_MAX is refused");
    g_net.sizeOverride = -2;
    g_net.noLength = true;               // under the guess without a length: fine
    poll(s, "no length, under the guess");
    g_net.noLength = false;
    g_tilePad = 0;
    CHECK(rainStage() == RAIN_OK && rainCount() == 13 && rainFrameTime(12) == g_newest, "no length: a tile under the reserved size is kept");
  }

  // 18. The wind arrow: one Open-Meteo request per poll, before the frame
  //     list, only while the arrow is on; its failures never touch the radar;
  //     values out of bounds, oversized or redirected answers are refused,
  //     and an answer ages out like the radar.
  {
    char st[32];
    int16_t dir = -1;
    float kmh = -1;
    CHECK(!rainWindNow(s, dir, kmh) && windRequests() == 0, "wind off: no request, no arrow");
    s.rain.wind = RAIN_WIND_CLOUDS;
    g_net.log.clear();
    poll(s, "wind on");
    CHECK(rainStage() == RAIN_OK && rainCount() == 13, "wind on: the radar poll is unchanged");
    CHECK(windRequests() == 1, "wind on: one request per poll");
    const std::string url = g_net.log.empty() ? "" : g_net.log[0];
    char want[256];
    snprintf(want, sizeof want, "http://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&current="
             "wind_speed_850hPa,wind_direction_850hPa,wind_speed_10m,wind_direction_10m&wind_speed_unit=kmh",
             rainLat(s), rainLon(s));
    CHECK(url == want && rainLat(s) != roundf(rainLat(s) * 100) / 100,
          "wind: plain HTTP, position rounded to 2 decimals, before the frame list");
    CHECK(g_net.redirectGets == 0, "wind: redirects not followed");
    CHECK(rainWindNow(s, dir, kmh) && dir == 306 && fabsf(kmh - 39.1f) < 0.01f, "wind: cloud level read");
    CHECK(std::string(rainWindStageName(st, sizeof st)) == "ok" && rainWindHttp() == 200, "wind: status ok");
    s.rain.wind = RAIN_WIND_GROUND;      // the other level comes with the same answer
    CHECK(rainWindNow(s, dir, kmh) && dir == 333 && fabsf(kmh - 1.6f) < 0.01f, "wind: ground level read");
    s.rain.wind = RAIN_WIND_CLOUDS;

    // A failed request: the radar goes on, the last good wind stays until it ages out.
    for (int code : {500, 301, -1}) {
      g_windCode = code;
      poll(s, "wind request fails");
      CHECK(rainStage() == RAIN_OK && !rainError(), "wind failure: radar unaffected");
      CHECK(rainWindNow(s, dir, kmh) && dir == 306, "wind failure: the last good wind kept");
    }
    CHECK(std::string(rainWindStageName(st, sizeof st)) == "connect failed", "wind failure: reported");
    CHECK(g_net.redirectGets == 0, "wind: a 301 is not followed");
    g_windCode = 200;

    // Ages out like the radar: past the refresh period plus RAIN_STALE_SEC.
    g_fakeMillis += (s.rain.pollSec + RAIN_STALE_SEC) * 1000UL + 1000;
    CHECK(!rainWindNow(s, dir, kmh), "wind: too old, no arrow");

    // Values out of bounds, or missing, drop their level only.
    auto answer = [](const std::string& cur) { return "{\"current\":{" + cur + "}}"; };
    g_windBody = answer("\"wind_speed_850hPa\":-1,\"wind_direction_850hPa\":400,"
                        "\"wind_speed_10m\":12.5,\"wind_direction_10m\":90");
    poll(s, "wind cloud level out of bounds");
    CHECK(!rainWindNow(s, dir, kmh), "wind: cloud level out of bounds, no arrow");
    s.rain.wind = RAIN_WIND_GROUND;
    CHECK(rainWindNow(s, dir, kmh) && dir == 90 && fabsf(kmh - 12.5f) < 0.01f, "wind: the ground level still read");
    s.rain.wind = RAIN_WIND_CLOUDS;
    g_windBody = answer("\"wind_speed_850hPa\":null,\"wind_direction_850hPa\":null,"
                        "\"wind_speed_10m\":9000,\"wind_direction_10m\":\"N\"");
    poll(s, "wind values unusable");
    CHECK(std::string(rainWindStageName(st, sizeof st)) == "parse failed", "wind: no usable level, parse failed");
    g_windBody = answer("\"wind_speed_850hPa\":20,\"wind_direction_850hPa\":360,"
                        "\"wind_speed_10m\":0,\"wind_direction_10m\":0");
    poll(s, "wind at 360");
    CHECK(rainWindNow(s, dir, kmh) && dir == 0 && fabsf(kmh - 20.0f) < 0.01f, "wind: 360 is north");

    // Oversized: announced, or without a length (the body runs to the close).
    g_windBody = kWindAnswer;
    g_windBody.insert(g_windBody.size() - 1, std::string(RAIN_WIND_MAX_BYTES, ' '));
    g_net.noLength = true;
    poll(s, "wind answer too long");
    g_net.noLength = false;
    CHECK(std::string(rainWindStageName(st, sizeof st)) == "parse failed", "wind: an answer past the cap is refused");
    CHECK(rainWindNow(s, dir, kmh) && dir == 0, "wind: the last good wind kept");
    g_windBody = kWindAnswer;
    g_net.sizeOverride = RAIN_WIND_MAX_BYTES + 1;
    poll(s, "wind announced too long");
    g_net.sizeOverride = -2;
    CHECK(std::string(rainWindStageName(st, sizeof st)) == "http error", "wind: an answer announced past the cap is refused");
    CHECK(rainStage() == RAIN_OK && rainCount() == 13, "wind: oversized answers leave the radar alone");
    g_net.noLength = true;               // the real service: no Content-Length
    poll(s, "wind without a length");
    g_net.noLength = false;
    CHECK(rainWindNow(s, dir, kmh) && dir == 306, "wind: an answer without a length is read");

    // Turned off: no more requests, no arrow.
    s.rain.wind = RAIN_WIND_OFF;
    g_net.log.clear();
    poll(s, "wind off again");
    CHECK(windRequests() == 0 && !rainWindNow(s, dir, kmh), "wind off: no request, no arrow");

    // The arrow points where the wind goes (its direction is where it comes from).
    struct { int from, heading, sx, sy; } cases[] = {
      {0, 180, 0, 1}, {90, 270, -1, 0}, {180, 0, 0, -1}, {270, 90, 1, 0}, {306, 126, 1, 1}, {360, 180, 0, 1}, {45, 225, -1, 1},
    };
    for (auto& c : cases) {
      char msg[80];
      snprintf(msg, sizeof msg, "arrow for a wind from %d", c.from);
      const int h = rainWindHeading((float)c.from);
      RainArrow a;
      rainWindArrow(100, 100, 11, h, a);
      auto sgn = [](int v) { return (v > 0) - (v < 0); };
      CHECK(h == c.heading && sgn(a.tipX - 100) == c.sx && sgn(a.tipY - 100) == c.sy, msg);
      // The head's base sits behind the tip, the shaft's tail behind the base.
      const int tx = a.tipX - 100, ty = a.tipY - 100;
      const int bx = (a.b1x + a.b2x) / 2 - 100, by = (a.b1y + a.b2y) / 2 - 100;
      const int ex = (a.s1x + a.s2x) / 2 - 100, ey = (a.s1y + a.s2y) / 2 - 100;
      CHECK(tx * bx + ty * by < tx * tx + ty * ty && tx * ex + ty * ey < 0, msg);
    }
    // The speed in each unit (fetched in km/h), auto following the distances.
    CHECK(rainWindSpeedIn(39.1f, RAIN_WUNIT_AUTO, false) == 39 && rainWindSpeedIn(39.1f, RAIN_WUNIT_AUTO, true) == 24 &&
          rainWindSpeedIn(39.1f, RAIN_WUNIT_KMH, true) == 39 && rainWindSpeedIn(39.1f, RAIN_WUNIT_MPH, false) == 24 &&
          rainWindSpeedIn(39.1f, RAIN_WUNIT_KN, false) == 21 && rainWindSpeedIn(39.1f, RAIN_WUNIT_MS, false) == 11,
          "wind speed units");
    CHECK(rainWindUnitLabel(RAIN_WUNIT_AUTO, false) == 0 && rainWindUnitLabel(RAIN_WUNIT_AUTO, true) == 1 &&
          rainWindUnitLabel(RAIN_WUNIT_KN, false) == 2 && rainWindUnitLabel(RAIN_WUNIT_MS, true) == 3 &&
          rainWindUnitLabel(9, false) == 0, "wind unit labels");
    CHECK(rainWindValueOk(0, 0) && rainWindValueOk(360, 500) && !rainWindValueOk(-1, 5) &&
          !rainWindValueOk(10, -0.1f) && !rainWindValueOk(10, 501) && !rainWindValueOk(NAN, 5), "wind value bounds");
  }

  // The names checked against the stages above: most of them were reached.
  CHECK(g_stagesSeen.size() >= 8, "stage names checked for most stages");
  printf("%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
