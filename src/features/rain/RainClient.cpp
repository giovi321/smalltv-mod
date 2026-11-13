// RainClient.cpp — fetches RainViewer radar frames (past 2h) for the rain radar.
//
// Data flow, every pollSec (15 min by default):
//   1. GET weather-maps.json -> host + 13 past frames {time, path}
//      (+ nowcast frames, currently an empty array — the code tolerates them)
//   2. Per frame: GET {host}{path}/256/7/{lat}/{lon}/2/1_1.png — a 256px
//      RGBA tile centred on the home position (radar is only served at zoom
//      7; higher zooms return map tiles). The centre crop size follows the
//      configured range: K = round(2 * range / kmPerPx), so the 240x240
//      output covers about twice the range ring.
//   3. The custom decoder (RainPng.cpp) crops, upscales and quantizes each
//      frame into /rain/fNN.raw (240x240, 8-bit indices, 57,600 B).
// On any failure the old frames stay on disk and the error is flagged —
// the animation keeps looping the stale set.
#include "RainClient.h"
#include "Platform.h"          // WiFiClient/HTTPClient per platform
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include "RainPng.h"

// Frame set currently on disk (and shown): oldest first, then nowcast frames.
static RainFrame g_frames[MAX_RAIN_FRAMES];
static uint8_t  g_count = 0;
static uint32_t g_generated = 0;     // unix time the server generated the set
static uint32_t g_lastOkMs = 0;
static bool     g_error = false;
static uint32_t g_nextPollMs = 0;

// Last-poll diagnostics (see RainStage in the header).
static RainStage g_stage = RAIN_IDLE;
static int       g_lastHttp = 0;
static uint32_t  g_lastTryMs = 0;
static String    g_lastUrl;
static uint8_t   g_cropK = 240;

// The frames of the fetch in progress (kept between the JSON pass and the
// download loop).
static struct {
  uint32_t time;
  String   path;
  bool     nowcast;
} g_pending[MAX_RAIN_FRAMES];
static uint8_t g_pendingCount = 0;

// Le fetch est découpé en étapes, une par appel de rainService() : entre
// deux étapes la boucle principale anime l'affichage (une étape = ~2-25 s :
// la liste, puis un téléchargement + décodage par frame). Sans ce découpage,
// l'écran restait figé ~4,5 min au boot (serveur et rendu mono-tâche, vécu).
enum RainStep { STEP_IDLE, STEP_LIST, STEP_FRAME, STEP_COMMIT };
static RainStep g_step = STEP_IDLE;
static uint8_t  g_frameIdx = 0;      // prochaine frame du jeu en cours
static uint8_t* g_win = nullptr;     // fenêtre LZ tenue pendant tout le fetch
static String   g_host;

// Buffer de téléchargement d'une tuile. Statique exprès : sur l'ESP8266 la
// pile partage la région du heap, et ces 512 o sur la pile pendant le
// téléchargement (fenêtre LZ + buffers TCP + WiFi déjà alloués) mangeaient
// exactement la marge du pic — abort « out of memory » vécu à ~62 s de boot.
static uint8_t g_dlBuf[512];

// Idem pour les objets HTTP : ~150 o de pile chacun au moment où le heap est
// au plus serré. La boucle est mono-tâche, aucun risque de réentrance.
static HTTPClient g_http;

uint8_t   rainCount()                { return g_count; }
uint32_t  rainFrameTime(uint8_t i)   { return (i < g_count) ? g_frames[i].time : 0; }
bool      rainFrameNowcast(uint8_t i) { return (i < g_count) && g_frames[i].nowcast; }
uint32_t  rainLastOkMs()             { return g_lastOkMs; }
bool      rainError()                { return g_error; }
uint32_t  rainGenerated()            { return g_generated; }

RainStage rainStage()          { return g_stage; }
int       rainLastHttp()       { return g_lastHttp; }
uint8_t   rainCropK()          { return g_cropK; }
uint32_t  rainLastTryMs()      { return g_lastTryMs; }
const String& rainLastUrl()    { return g_lastUrl; }

const char* rainStageName() {
  switch (g_stage) {
    case RAIN_NO_HOME:      return "no home set";
    case RAIN_FETCHING:     return "fetching";
    case RAIN_CONNECT_FAIL: return "connect failed";
    case RAIN_HTTP_ERROR:   return "http error";
    case RAIN_PARSE_FAIL:   return "parse failed";
    case RAIN_NO_FRAMES:    return "no frames in feed";
    case RAIN_PNG_FAIL:     return "png decode failed";
    case RAIN_NO_MEM:       return "out of memory";
    case RAIN_FS_FAIL:      return "filesystem error";
    case RAIN_OK:           return "ok";
    default:                return "idle";
  }
}

static void framePath(char* out, size_t n, uint8_t i) {
  snprintf(out, n, RAIN_DIR "/f%02u.raw", (unsigned)i);
}

// F() exige un littéral : le schéma dépend d'une macro de build.
static const char* scheme() { return RAIN_USE_HTTPS ? "https://" : "http://"; }

// Rebuild g_frames from /rain/meta.json so a reboot keeps showing the last
// set without waiting for the next poll.
static void loadMeta() {
  g_count = 0;
  g_generated = 0;
  File f = LittleFS.open(RAIN_DIR "/meta.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  g_generated = doc["generated"].as<uint32_t>();
  uint8_t nc = doc["nowcast"].as<uint8_t>();
  JsonArrayConst times = doc["times"].as<JsonArrayConst>();
  for (JsonVariantConst t : times) {
    if (g_count >= MAX_RAIN_FRAMES) break;
    g_frames[g_count].time = t.as<uint32_t>();
    // nowcast frames come last
    g_frames[g_count].nowcast = (g_count + nc >= (uint8_t)times.size());
    g_count++;
  }
}

void rainInit(const Settings& s) {
  (void)s;
  // Abort d'un fetch en cours (changement de mode ou de config) : la fenêtre
  // LZ est rendue, l'état de la machine à étapes repart à zéro.
  if (g_win) { free(g_win); g_win = nullptr; }
  g_step = STEP_IDLE;
  g_frameIdx = 0;
  loadMeta();
  g_error = false;
  g_lastOkMs = 0;
  g_nextPollMs = millis();   // poll as soon as the mode becomes active
  g_stage = RAIN_IDLE;
  g_lastHttp = 0;
  g_lastTryMs = 0;
  g_lastUrl = "";
  if (!LittleFS.exists(RAIN_DIR)) LittleFS.mkdir(RAIN_DIR);
}

// Un fetch est déjà en cours ? Ses frames sont fraîches : ne pas armer un
// 2e poll qui démarrerait dès la fin de celui-ci (double fetch complet vécu
// quand /api/refresh tombait pendant un fetch).
void rainForceRefresh() {
  if (g_step != STEP_IDLE) return;
  g_nextPollMs = millis();
}

// Crop size for the configured range at this latitude: the 240px output
// must span about twice the range ring.
static uint8_t cropFor(float lat, uint16_t rangeKm) {
  float kmPerPx = 40075.0f * cosf(lat * (float)PI / 180.0f) / 32768.0f;  // z7, 256 px
  float k = 2.0f * (float)rangeKm / kmPerPx;
  int ki = (int)lroundf(k);
  if (ki < 8) ki = 8;
  if (ki > TFT_WIDTH) ki = TFT_WIDTH;
  return (uint8_t)ki;
}

// ---- one HTTP GET + parse of the frame list --------------------------------
static bool fetchFrameList(const Settings& s) {
  String url = String(scheme()) + F(RAIN_API_HOST) + F(RAIN_API_PATH);
  g_lastUrl = url;

  WiFiClient client;
  HTTPClient& http = g_http;
  // HTTP/1.0: the answer cannot be chunked, and the JSON is tiny.
  http.useHTTP10(true);
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(client, url)) { g_stage = RAIN_CONNECT_FAIL; return false; }
  http.addHeader("Accept", "application/json");
  http.setUserAgent(F(RAIN_USER_AGENT));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int code = http.GET();
  g_lastHttp = code;
  if (code != HTTP_CODE_OK) {
    http.end();
    g_stage = (code < 0) ? RAIN_CONNECT_FAIL : RAIN_HTTP_ERROR;
    return false;
  }

  // Filter so only the fields we need materialise: the doc is parsed while
  // the 8 KB LZ window is already allocated and the heap margin at that
  // point is a few KB, so every unexpected field is a crash risk.
  JsonDocument filter;
  filter["host"] = true;
  filter["generated"] = true;
  {
    JsonObject f = filter["radar"]["past"][0].to<JsonObject>();
    f["time"] = true;
    f["path"] = true;
  }
  {
    JsonObject f = filter["radar"]["nowcast"][0].to<JsonObject>();
    f["time"] = true;
    f["path"] = true;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { g_stage = RAIN_PARSE_FAIL; return false; }

  const char* host = doc["host"] | "";
  if (!host[0]) { g_stage = RAIN_PARSE_FAIL; return false; }
  g_host = host;
  if (g_host.startsWith("https://")) g_host.remove(0, 8);
  else if (g_host.startsWith("http://")) g_host.remove(0, 7);

  g_generated = doc["generated"].as<uint32_t>();
  g_pendingCount = 0;
  JsonArrayConst past = doc["radar"]["past"].as<JsonArrayConst>();
  for (JsonObjectConst f : past) {
    if (g_pendingCount >= MAX_RAIN_FRAMES) break;
    g_pending[g_pendingCount].time = f["time"].as<uint32_t>();
    g_pending[g_pendingCount].path = f["path"].as<String>();
    g_pending[g_pendingCount].nowcast = false;
    g_pendingCount++;
  }
  // The nowcast array is empty since 2026-01-01; keep the code path so the
  // future frames appear automatically if RainViewer ever restores them.
  JsonArrayConst nowcast = doc["radar"]["nowcast"].as<JsonArrayConst>();
  for (JsonObjectConst f : nowcast) {
    if (g_pendingCount >= MAX_RAIN_FRAMES) break;
    g_pending[g_pendingCount].time = f["time"].as<uint32_t>();
    g_pending[g_pendingCount].path = f["path"].as<String>();
    g_pending[g_pendingCount].nowcast = true;
    g_pendingCount++;
  }

  if (g_pendingCount == 0) { g_stage = RAIN_NO_FRAMES; return false; }
  return true;
}

// ---- one frame: download to /rain/tmp.png, decode to /rain/fNN.raw --------
static bool fetchFrame(const Settings& s, const String& host, uint8_t i, uint8_t* win) {
  String url = String(scheme()) + host;
  url += g_pending[i].path;
  url += '/';
  url += RAIN_TILE_SIZE;
  url += '/';
  url += RAIN_ZOOM;
  url += '/';
  url += String(rainLat(s), 6);
  url += '/';
  url += String(rainLon(s), 6);
  url += F("/2/1_1.png");
  g_lastUrl = url;
  g_pending[i].path = String();   // le chemin vit maintenant dans l'URL :
                                  // libère son buffer avant le décodage (heap serré)

  WiFiClient client;
  HTTPClient& http = g_http;
  http.useHTTP10(true);
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(client, url)) { g_stage = RAIN_CONNECT_FAIL; return false; }
  http.setUserAgent(F(RAIN_USER_AGENT));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int code = http.GET();
  g_lastHttp = code;
  if (code != HTTP_CODE_OK) {
    http.end();
    g_stage = (code < 0) ? RAIN_CONNECT_FAIL : RAIN_HTTP_ERROR;
    return false;
  }

  // Stream to /rain/tmp.png (sequential writes; ~10-30 KB).
  File tmp = LittleFS.open(RAIN_DIR "/tmp.png", "w");
  if (!tmp) { http.end(); g_stage = RAIN_FS_FAIL; return false; }
  Stream& in = http.getStream();
  uint32_t total = 0;
  // readBytes: same signature on the ESP8266 and ESP32 cores (Stream::read
  // differs between them).
  int n;
  while ((n = (int)in.readBytes(g_dlBuf, sizeof g_dlBuf)) > 0) {
    tmp.write(g_dlBuf, (size_t)n);
    total += (uint32_t)n;
    if (total > 262144) break;   // sanity cap 256 KB
  }
  http.end();
  tmp.close();
  if (total == 0) { g_stage = RAIN_HTTP_ERROR; return false; }

  url = String();   // libère le buffer de l'URL avant le décodage

  // Decode: centre crop + bilinear upscale + quantization into the frame.
  // Décodé dans un fichier temporaire : la frame en place n'est remplacée
  // (rename atomique) qu'au succès — un échec ici ne doit jamais détruire
  // une bonne frame (frame noire vécue : le .raw ouvert en « w » restait
  // vide après un échec du décodage et l'animation affichait noir).
  char path[24];
  framePath(path, sizeof path, i);
  File src = LittleFS.open(RAIN_DIR "/tmp.png", "r");
  File dst = LittleFS.open(RAIN_DIR "/tmp.raw", "w");
  if (!src || !dst) {
    if (src) src.close();
    if (dst) dst.close();
    g_stage = RAIN_FS_FAIL;
    return false;
  }
  bool ok = rainDecodeToFs(src, dst, RAIN_TILE_SIZE, g_cropK, win);
  src.close();
  dst.close();
  if (!ok) {
    LittleFS.remove(RAIN_DIR "/tmp.raw");   // ne pas laisser un .raw tronqué derrière soi
    g_stage = RAIN_PNG_FAIL;
    return false;
  }
  if (!LittleFS.rename(RAIN_DIR "/tmp.raw", path)) {
    LittleFS.remove(RAIN_DIR "/tmp.raw");
    g_stage = RAIN_FS_FAIL;
    return false;
  }
  return true;
}

// ---- commit the new set to g_frames + meta.json, purge leftovers ----------
static bool commitFrames() {
  // meta.json écrit atomiquement (tmp + rename) : une coupure secteur
  // pendant l'écriture ne peut plus laisser un meta.json tronqué (loadMeta
  // échouait alors au boot et l'écran restait sur « Waiting for radar data »
  // alors que les 13 frames étaient sur le disque).
  bool ok = false;
  File m = LittleFS.open(RAIN_DIR "/meta.tmp", "w");
  if (m) {
    JsonDocument doc;
    doc["generated"] = g_generated;
    uint8_t nc = 0;
    for (uint8_t i = 0; i < g_pendingCount; i++)
      if (g_pending[i].nowcast) nc++;
    doc["nowcast"] = nc;
    JsonArray times = doc["times"].to<JsonArray>();
    for (uint8_t i = 0; i < g_pendingCount; i++) times.add(g_pending[i].time);
    ok = serializeJson(doc, m) > 0;
    m.close();
    ok = ok && LittleFS.rename(RAIN_DIR "/meta.tmp", RAIN_DIR "/meta.json");
    if (!ok) LittleFS.remove(RAIN_DIR "/meta.tmp");
  }

  // Les frames sont déjà renommées en place (une par étape du fetch) : dans
  // tous les cas, l'état en mémoire doit décrire les frames du disque, sinon
  // la timeline et l'heure d'en-tête dérivent d'un poll par rapport aux
  // pixels affichés (vécu quand meta.json ne s'ouvrait pas). Si meta n'a pas
  // pu être écrit, le prochain poll réessaiera — l'erreur reste signalée.
  g_count = g_pendingCount;
  for (uint8_t i = 0; i < g_count; i++) {
    g_frames[i].time = g_pending[i].time;
    g_frames[i].nowcast = g_pending[i].nowcast;
  }
  // Purge frame files beyond the new count (aussi en cas d'échec de meta :
  // un FS plein redevient utilisable pour le poll suivant).
  for (uint8_t i = g_count; i < MAX_RAIN_FRAMES; i++) {
    char path[24];
    framePath(path, sizeof path, i);
    if (LittleFS.exists(path)) LittleFS.remove(path);
  }
  if (!ok) { g_stage = RAIN_FS_FAIL; return false; }
  g_lastOkMs = millis();
  g_error = false;
  g_stage = RAIN_OK;
  return true;
}

// Fin du fetch en cours (succès ou échec) : rend la fenêtre LZ, remet
// l'état de la machine à étapes à zéro et arme le prochain poll à partir
// d'ici. Sans ce ré-armement, un pollSec plus court que le fetch (~5-6 min
// sur l'Ultra) enchaînait les fetchs dos à dos : heap sous pression en
// permanence (profil de l'abort OOM, bug 12) et serveur HTTP quasi mort.
// Les frames sur disque restent en place — en cas d'échec, l'animation
// continue de boucler l'ancien jeu.
static void fetchAbort(const Settings& s) {
  if (g_win) { free(g_win); g_win = nullptr; }
  g_step = STEP_IDLE;
  g_frameIdx = 0;
  g_nextPollMs = millis() + (uint32_t)s.rain.pollSec * 1000UL;
}

// ---------------------------------------------------------------------------
void rainService(const Settings& s) {
  // No home set yet -> nothing to fetch (the mode shows a prompt instead).
  if (rainLat(s) == 0.0f && rainLon(s) == 0.0f) { g_stage = RAIN_NO_HOME; return; }

  if (g_step == STEP_IDLE) {
    if ((int32_t)(millis() - g_nextPollMs) < 0) return;
    // Valeur provisoire, ré-armée à la FIN du fetch par fetchAbort() : le
    // prochain poll part pollSec après la fin du fetch, pas du début. Elle
    // ne reste en place que si le malloc de la fenêtre échoue (retry).
    g_nextPollMs = millis() + (uint32_t)s.rain.pollSec * 1000UL;
    g_lastTryMs = millis();
    g_cropK = cropFor(rainLat(s), s.rain.rangeKm);
    g_stage = RAIN_FETCHING;

    // Fenêtre LZ allouée 1x par fetch (13 frames), AVANT le réseau : à cet
    // instant le heap est au plus propre (15 min d'idle). En l'allouant ici,
    // un échec arrive avant qu'aucun fichier ne soit touché, et on évite 13
    // cycles malloc/free de 8 Ko qui fragmentaient le heap (vécu : maxblk
    // < 8192 après ~3 h d'uptime → fetch en échec permanent).
    g_win = (uint8_t*)malloc(RAIN_WINDOW_PIXELS);
    if (!g_win) { g_error = true; g_stage = RAIN_NO_MEM; return; }
    memset(g_win, 0, RAIN_WINDOW_PIXELS);
    g_step = STEP_LIST;
    return;   // la liste partira au prochain appel : l'écran anime d'abord
  }

  // Une étape par appel — la boucle principale anime l'affichage entre deux.
  if (g_step == STEP_LIST) {
    if (!fetchFrameList(s)) { g_error = true; fetchAbort(s); return; }   // keep the old frames
    g_step = STEP_FRAME;
    g_frameIdx = 0;
    return;
  }
  if (g_step == STEP_FRAME) {
    // Fail fast on the first bad frame: the stale set stays on disk and the
    // animation keeps looping it; the next poll (15 min) retries everything.
    if (!fetchFrame(s, g_host, g_frameIdx, g_win)) { g_error = true; fetchAbort(s); return; }
    g_frameIdx++;
    if (g_frameIdx >= g_pendingCount) g_step = STEP_COMMIT;
    return;
  }
  if (g_step == STEP_COMMIT) {
    if (!commitFrames()) g_error = true;
    fetchAbort(s);
  }
}
