#include "ThemeWeb.h"
#if WITH_THEME
#include "ThemeMode.h"
#include "ThemeCatalog.h"
#include "Clock.h"
#include <ArduinoJson.h>
#include <algorithm>
extern void appInvalidate();
namespace {
WebServerClass* server;
Settings* settings;
bool (*auth)();
File upload;
String uploadError;
bool uploadAllowed = false, uploadEnded = false;
unsigned uploadParts = 0;
uint32_t uploaded = 0, uploadBudget = 0;
constexpr unsigned MaxThemes = 16;
static_assert(MaxThemes <= 32, "listThemes() marks packages in a 32-bit mask");
const char* staging = "/themes/.upload";
// Every theme API route replies with the same small JSON shape: `{"id":...}`
// on success, `{"error":...}` otherwise, keyed by whether `code` is a success
// status.
void reply(int code, const String& message) {
  JsonDocument doc;
  doc[code < 300 ? "id" : "error"] = message;
  String json;
  serializeJson(doc, json);
  server->send(code, "application/json", json);
}
// ESP32's LittleFS exposes totalBytes()/usedBytes() directly; ESP8266's only
// reports them through info(FSInfo&). Both walk the filesystem's blocks, and
// every list and upload start asks, so the answer is kept for a few seconds
// and dropped after this file's own writes (forgetFreeBytes()).
size_t cachedFree = 0;
uint32_t cachedFreeAt = 0;
bool cachedFreeValid = false;
size_t freeBytes() {
  if (cachedFreeValid && millis() - cachedFreeAt < 5000) return cachedFree;
#if defined(SMALLTV_ESP8266)
  FSInfo info;
  cachedFree = LittleFS.info(info) ? info.totalBytes - info.usedBytes : 0;
#else
  cachedFree = LittleFS.totalBytes() - LittleFS.usedBytes();
#endif
  cachedFreeAt = millis();
  cachedFreeValid = true;
  return cachedFree;
}
void forgetFreeBytes() { cachedFreeValid = false; }
String metaPath(const String& id) { return "/themes/" + id + ".meta"; }
// Writes the listing sidecar for a package that just passed Package::load()
// (see readThemeMeta()). A failed write only costs a full inspection the next
// time the package is listed.
void writeMeta(const String& id, const smalltv::InstalledTheme& entry) {
  std::string json = smalltv::themeMetaJson(entry);
  File meta = LittleFS.open(metaPath(id), "w");
  if (meta && meta.write(reinterpret_cast<const uint8_t*>(json.data()), json.size()) != json.size()) {
    meta.close();
    LittleFS.remove(metaPath(id));
  }
  forgetFreeBytes();
}
unsigned installedCount() {
  unsigned count = 0;
#if defined(SMALLTV_ESP8266)
  // Reads the directory entries only. openNextFile() opens every file in
  // turn, and this runs at the start of an upload, when heap is shortest.
  Dir dir = LittleFS.openDir("/themes");
  while (dir.next()) if (dir.isFile() && dir.fileName().endsWith(".stheme")) ++count;
#else
  File dir = LittleFS.open("/themes", "r");
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) if (!f.isDirectory() && String(f.name()).endsWith(".stheme")) ++count;
#endif
  return count;
}
void resetUpload() {
  upload.close();
  LittleFS.remove(staging);
  forgetFreeBytes();
  uploadParts = 0;
  uploadAllowed = false;
  uploadEnded = false;
  uploaded = 0;
}
// The web server framework streams a multipart upload through repeated calls
// to this handler (START, then one or more WRITE, then END or ABORTED), with
// the request body itself validated and stored in finishUpload() only after
// it is complete. Every check here is about the *stream*, not the package
// format: exactly one file, auth, a plausible filename, the theme count
// limit, and available storage, all determined before a single byte is
// written to the staging file so a doomed upload fails fast.
void receiveUpload() {
  HTTPUpload& up = server->upload();
  if (up.status == UPLOAD_FILE_START) {
    ++uploadParts;
    if (uploadParts > 1) {
      uploadError = "Upload exactly one package";
      upload.close();
      return;
    }
    uploadError = "";
    uploaded = 0;
    uploadEnded = false;
    uploadAllowed = !settings->auth.enabled || !settings->auth.pass.length() || server->authenticate(settings->auth.user.c_str(), settings->auth.pass.c_str());
    if (!uploadAllowed) return;
    if (!up.filename.endsWith(".stheme")) {
      uploadError = "Expected a .stheme file";
      return;
    }
    if (installedCount() >= MaxThemes) {
      uploadError = "Remove a theme first (limit 16)";
      return;
    }
    // Receiving a package needs the receive buffers and the staging file at
    // once, and then a package parse. On the ESP8266 that did not fit beside
    // a running theme (a 16-byte allocation failed here with 160 B free), so
    // an install always frees it; there the hook in themeWebBegin() has freed
    // it already.
    g_themeMode.release(UINT32_MAX);
    LittleFS.remove(staging);
    // Preserve room for a settings rewrite plus filesystem metadata overhead,
    // so a large upload cannot leave too little space for the device to save
    // its own configuration afterward. The size of the last save stands in
    // for the next one: building the settings document here to measure it
    // took the ESP8266's heap down to 896 B at an install's tightest point.
    size_t reserve = settingsStoredSize() + 4096;
    if (reserve < 16384) reserve = 16384;
    size_t free = freeBytes();
    if (free <= reserve) {
      uploadError = "Not enough storage; remove a theme first";
      return;
    }
    uploadBudget = std::min<size_t>(smalltv::MaxPackage, free - reserve);
    upload = LittleFS.open(staging, "w");
    if (!upload) uploadError = "Cannot create staging file";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!uploadAllowed || uploadError.length()) return;
    if (!upload || up.currentSize > uploadBudget - uploaded) {
      uploadError = "Package exceeds available storage or 3 MiB";
      upload.close();
      return;
    }
    if (upload.write(up.buf, up.currentSize) != up.currentSize) {
      uploadError = "Not enough storage";
      upload.close();
      return;
    }
    uploaded += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    upload.close();
    uploadEnded = uploadAllowed && !uploadError.length() && uploaded == up.totalSize;
    if (!uploadEnded && !uploadError.length()) uploadError = "Incomplete upload";
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    resetUpload();
    uploadError = "Upload aborted";
  }
}
// Runs once the multipart body is fully received: validates the staged file
// with the same Package::load() the device uses at runtime (so an accepted
// upload is guaranteed installable), then publishes it under a filename
// derived from the theme's own declared ID.
void finishUpload() {
  if (!auth()) {
    resetUpload();
    return;
  }
  upload.close();
  if (!uploadAllowed || !uploadEnded || uploadError.length()) {
    reply(400, uploadError.length() ? uploadError : String("No complete package uploaded"));
    resetUpload();
    return;
  }
  // Validation parses a second theme; free the running one first if the heap
  // is short (issue #30).
  g_themeMode.release();
  smalltv::Theme theme;
  std::string error;
  bool valid = false;
  {
    ThemeFile file;
    if (file.open(staging)) {
      smalltv::Package package(file);
      valid = package.load(theme, error);
    } else error = "Cannot read uploaded package";
  }
  if (!valid) {
    reply(400, error.c_str());
    resetUpload();
    return;
  }
  String target = "/themes/" + String(theme.id.c_str()) + ".stheme";
  if (LittleFS.exists(target)) {
    reply(409, "Theme ID already installed; remove it or use a new ID");
    resetUpload();
    return;
  }
  // Rename, not copy: the staging file becomes the installed one atomically,
  // so a partially-installed package is never visible to listThemes().
  if (!LittleFS.rename(staging, target)) {
    reply(500, "Cannot publish package");
    resetUpload();
    return;
  }
  smalltv::InstalledTheme entry;
  entry.id = theme.id;
  entry.name = theme.name;
  entry.author = theme.author;
  entry.version = theme.version;
  entry.bytes = uploaded;
  entry.valid = true;
  writeMeta(theme.id.c_str(), entry);
  reply(201, theme.id.c_str());
  resetUpload();
}
// Shared by select/delete: reads a JSON body's `id` and validates it as a
// well-formed theme ID before either route touches the filesystem.
// `id.length() != strlen(id.c_str())` catches an embedded NUL byte ArduinoJson
// would otherwise pass through silently.
bool requestId(String& id) {
  if (!server->hasArg("plain") || server->arg("plain").length() > 128) {
    reply(400, "Expected JSON with id");
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain")) || !doc["id"].is<const char*>()) {
    reply(400, "Expected JSON with id");
    return false;
  }
  id = doc["id"].as<String>();
  if (!smalltv::validId(id.c_str()) || id.length() != strlen(id.c_str())) {
    reply(400, "Invalid theme ID");
    return false;
  }
  return true;
}
// GET /api/themes: every installed package's metadata and validity, without
// requiring any of them to be the currently active theme. A package with a
// matching `.meta` sidecar (written at install, see readThemeMeta()) is listed
// from it; any other one goes through inspectInstalledTheme(), the same
// validator a selection would use, so an invalid or corrupt package still gets
// listed (with its error), just not selectable. A valid one then gets its
// sidecar, so it is only inspected once.
void listThemes() {
  if (!auth()) return;
  JsonDocument doc;
  doc["selected"] = settings->themeId;
  doc["error"] = g_themeMode.error();
  doc["freeBytes"] = freeBytes();
  JsonArray list = doc["themes"].to<JsonArray>();
  // Inspecting a package parses it; before the first one that needs it, free
  // the running theme if the heap is short (issue #30). Its error is already
  // copied into the reply above.
  bool released = false;
  uint32_t needMeta = 0;  // bit i: list[i] was inspected and is valid
  File dir = LittleFS.open("/themes", "r");
  unsigned count = 0;
  for (File entry = dir.openNextFile(); entry && count < MaxThemes; entry = dir.openNextFile()) {
    String name = entry.name();
    if (entry.isDirectory() || !name.endsWith(".stheme")) continue;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    String id = name.substring(0, name.length() - 7);
    if (!smalltv::validId(id.c_str())) continue;
    smalltv::InstalledTheme info;
    bool fromMeta = false;
    {
      ThemeFile meta;
      fromMeta = meta.open(metaPath(id)) && smalltv::readThemeMeta(meta, id.c_str(), entry.size(), info);
    }
    if (!fromMeta) {
      if (!released) {
        g_themeMode.release();
        released = true;
      }
      ThemeFile file;
      file.open("/themes/" + name);
      info = smalltv::inspectInstalledTheme(file, id.c_str());
      if (info.valid) needMeta |= 1u << count;
    }
    JsonObject o = list.add<JsonObject>();
    o["id"] = info.id.c_str();
    o["name"] = info.name.c_str();
    o["author"] = info.author.c_str();
    o["version"] = info.version.c_str();
    o["bytes"] = entry.size();
    o["valid"] = info.valid;
    o["error"] = info.error.c_str();
    ++count;
    yield();
  }
  dir.close();
  // Sidecars are written after the directory walk, not during it, so new files
  // in /themes cannot disturb the iteration. MaxThemes fits in the bit mask.
  for (unsigned i = 0; i < count; ++i) {
    if (!(needMeta & (1u << i))) continue;
    JsonObjectConst o = list[i];
    smalltv::InstalledTheme entry;
    entry.name = o["name"].as<const char*>();
    entry.author = o["author"].as<const char*>();
    entry.version = o["version"].as<const char*>();
    entry.bytes = o["bytes"].as<uint32_t>();
    writeMeta(o["id"].as<const char*>(), entry);
  }
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}
// POST /api/themes/select: re-validates the package before committing to it,
// so a corrupt or invalid installed theme can never become the active
// selection (it can still be listed and removed, just not selected). Settings
// are rolled back in memory if the save itself fails, so a failed selection
// never leaves `settings` disagreeing with what was actually persisted.
void selectTheme() {
  if (!auth()) return;
  String id;
  if (!requestId(id)) return;
  // Validation parses a second theme; free the running one first if the heap
  // is short (issue #30).
  g_themeMode.release();
  {
    ThemeFile file;
    smalltv::Theme theme;
    std::string error;
    if (!file.open("/themes/" + id + ".stheme")) {
      reply(404, "Theme not installed");
      return;
    }
    smalltv::Package package(file);
    if (!package.load(theme, error) || theme.id != id.c_str()) {
      reply(400, "Invalid installed theme");
      return;
    }
  }
  String oldId = settings->themeId;
  uint8_t oldMode = settings->mode;
  settings->themeId = id;
  settings->mode = MODE_THEME;
  if (!saveSettings(*settings)) {
    settings->themeId = oldId;
    settings->mode = oldMode;
    reply(500, "Could not save selection");
    return;
  }
  clockReapply(*settings);
  appInvalidate();
  reply(200, id);
}
// POST /api/themes/delete: refuses to remove a package that is both selected
// and currently valid (it would silently blank the active display); removing
// a selected-but-invalid one is allowed, since keeping it installed serves no
// purpose once it can never render.
void deleteTheme() {
  if (!auth()) return;
  String id;
  if (!requestId(id)) return;
  String path = "/themes/" + id + ".stheme";
  if (!LittleFS.exists(path)) {
    reply(404, "Theme not found");
    return;
  }
  bool onScreen = settings->mode == MODE_THEME || (settings->mode == MODE_CAROUSEL && settings->carouselTheme);
  if (settings->themeId == id && onScreen) {
    // Inspecting parses the package; free the running copy first if the heap
    // is short (issue #30).
    g_themeMode.release();
    ThemeFile file;
    file.open(path);
    auto info = smalltv::inspectInstalledTheme(file, id.c_str());
    if (info.valid) {
      reply(409, "Switch away from theme mode or untick it in the carousel before removing this theme");
      return;
    }
  }
  if (settings->themeId == id) {
    settings->themeId = "";
    if (!saveSettings(*settings)) {
      settings->themeId = id;
      reply(500, "Could not clear saved selection");
      return;
    }
    appInvalidate();
  }
  if (!LittleFS.remove(path)) {
    reply(404, "Theme not found or could not be removed");
    return;
  }
  LittleFS.remove(metaPath(id));
  forgetFreeBytes();
  reply(200, id);
}
}
// Registers the theme API routes on the shared web server; `requireAuth`
// is the same digest-auth check every other route in the device uses, so
// theme install/select/delete get identical protection without duplicating it.
void themeWebBegin(WebServerClass& web, Settings& s, bool (*requireAuth)()) {
  server = &web;
  settings = &s;
  auth = requireAuth;
  web.on("/api/themes", HTTP_GET, listThemes);
  web.on("/api/themes/install", HTTP_POST, finishUpload, receiveUpload);
  web.on("/api/themes/select", HTTP_POST, selectTheme);
  web.on("/api/themes/delete", HTTP_POST, deleteTheme);
#if defined(SMALLTV_ESP8266)
  // The web server reserves its 2 KB upload buffer before the first call to
  // receiveUpload(), so the release there comes too late: beside a running
  // theme the heap fell to 512 B at that point, and the Wi-Fi driver crashed
  // on an empty heap. This hook runs once the request line is read, before
  // the headers and the body, so an install frees the theme first. (The
  // ESP32 WebServer has no hooks, and has the heap.)
  web.addHook([](const String& method, const String& url, WiFiClient*, WebServerClass::ContentTypeFunction) {
    if (method == "POST" && url == "/api/themes/install") g_themeMode.release(UINT32_MAX);
    return WebServerClass::CLIENT_REQUEST_CAN_CONTINUE;
  });
#endif
}
#endif
