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
bool (*allowed)();   // Host/Origin guard, for the upload callback
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
// The listing fields of every installed theme (see ThemeIndexReader). It is
// rewritten through `.index.new`, which replaces it only once complete.
const char* indexPath = "/themes/.index";
const char* indexNext = "/themes/.index.new";
struct FileSink : smalltv::Sink {
  File& file;
  explicit FileSink(File& f) : file(f) {}
  bool write(const char* data, size_t count) override {
    return file.write(reinterpret_cast<const uint8_t*>(data), count) == count;
  }
};
// Puts `.index.new` in place of the index. The old index is removed first, as
// a rename onto an existing file is not the same on every core. Whatever
// fails, no index is left that could be out of date: a missing one only
// costs a full inspection of each package at the next list, which writes it
// again.
void publishIndex(bool written) {
  LittleFS.remove(indexPath);
  if (!written || !LittleFS.rename(indexNext, indexPath)) LittleFS.remove(indexNext);
  forgetFreeBytes();
}
// After an install or a delete: the index without `id`'s old line, plus
// `added` if given.
void updateIndex(const String& id, const smalltv::InstalledTheme* added) {
  bool written;
  {
    ThemeFile from;
    from.open(indexPath);   // none yet: copies nothing
    File to = LittleFS.open(indexNext, "w");
    FileSink sink(to);
    written = to && smalltv::copyThemeIndex(from, sink, id.c_str(), added);
  }
  publishIndex(written);
}
// Firmware 3.1.0 and 3.1.1 kept one `<id>.meta` file per theme, each in an
// 8 KB block of storage; the index replaces them. Removed at boot, a few
// names per pass, so the directory is never changed while it is read.
void removeSidecars() {
  for (int pass = 0; pass < 64; ++pass) {
    String names[4];
    unsigned found = 0;
#if defined(SMALLTV_ESP8266)
    Dir dir = LittleFS.openDir("/themes");
    while (found < 4 && dir.next()) if (dir.fileName().endsWith(".meta")) names[found++] = dir.fileName();
#else
    File dir = LittleFS.open("/themes", "r");
    for (File f = dir.openNextFile(); found < 4 && f; f = dir.openNextFile()) {
      String name = f.name();
      if (name.endsWith(".meta")) names[found++] = name.substring(name.lastIndexOf('/') + 1);
    }
    dir.close();
#endif
    bool removed = false;
    for (unsigned i = 0; i < found; ++i) removed |= LittleFS.remove("/themes/" + names[i]);
    if (found < 4 || !removed) break;
  }
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
    uploadAllowed = allowed() && (!settings->auth.enabled || !settings->auth.pass.length() || server->authenticate(settings->auth.user.c_str(), settings->auth.pass.c_str()));
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
  updateIndex(theme.id.c_str(), &entry);
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
// The three steps of a list after the directory walk each run in a function
// of their own, called one after another: kept out of listThemes() (no
// inlining), their locals are on the loop stack only while that step runs.
// Inlined, they made listThemes()'s own frame 576 B; on 3.1.0 the sidecar
// code did the same, and the list lost 160 B of stack (#41).

// The index, read once: a usable line fills the listed package with the
// same id and size. Returns those packages as bits (MaxThemes fits) and sets
// `rewrite` when a line matches no package.
using Listing = smalltv::BlockList<smalltv::ListedTheme>;
__attribute__((noinline)) uint32_t fillFromIndex(Listing& list, bool& rewrite) {
  uint32_t listed = 0;
  ThemeFile file;
  if (!file.open(indexPath)) return 0;
  smalltv::ThemeIndexReader index(file);
  smalltv::InstalledTheme line;
  for (smalltv::IndexEntry kind; (kind = index.next(line)) != smalltv::IndexEntry::End;) {
    int match = -1;
    for (unsigned i = 0; kind == smalltv::IndexEntry::Usable && match < 0 && i < list.size(); ++i) {
      if (!(listed & (1u << i)) && line.id == list[i].id() && line.bytes == list[i].bytes) match = i;
    }
    if (match < 0) {
      rewrite = true;
      continue;
    }
    smalltv::ListedTheme& t = list[match];
    t.set(t.id(), line.name.c_str(), line.author.c_str(), line.version.c_str(), t.error());
    t.valid = true;
    listed |= 1u << match;
  }
  return listed;
}
// Fills a listed package from the package itself, the way a selection
// checks it. True when it is valid.
__attribute__((noinline)) bool inspectInto(smalltv::ListedTheme& t) {
  String id = t.id();
  smalltv::InstalledTheme info;
  {
    ThemeFile file;
    file.open("/themes/" + id + ".stheme");
    info = smalltv::inspectInstalledTheme(file, id.c_str());
  }
  t.set(id.c_str(), info.name.c_str(), info.author.c_str(), info.version.c_str(), info.error.c_str());
  t.valid = info.valid;
  return info.valid;
}
// The index from a listing: one line per valid package.
__attribute__((noinline)) void writeIndex(const Listing& list) {
  bool written;
  {
    File to = LittleFS.open(indexNext, "w");
    FileSink sink(to);
    written = bool(to);
    for (const smalltv::ListedTheme& t : list) {
      if (!written) break;
      if (!t.valid) continue;
      smalltv::InstalledTheme entry;
      entry.id = t.id();
      entry.name = t.name();
      entry.author = t.author();
      entry.version = t.version();
      entry.bytes = t.bytes;
      std::string line = smalltv::themeIndexLine(entry);
      written = sink.write(line.data(), line.size());
    }
  }
  publishIndex(written);
}
// The body of the current reply, piece by piece, with a pause after each
// (CONTRIBUTING: a loop over input-sized data yields).
struct ReplySink : smalltv::Sink {
  bool write(const char* data, size_t count) override {
    server->sendContent(data, count);
    yield();
    return true;
  }
};
struct CountSink : smalltv::Sink {
  size_t bytes = 0;
  bool write(const char*, size_t count) override {
    bytes += count;
    return true;
  }
};
// The reply, written twice the same way: once only to count its bytes for
// the Content-Length, then straight to the client in 128-byte pieces, never
// whole in one block of heap (#63). A step of its own like the three above,
// so its buffer is on the loop stack only while it sends.
__attribute__((noinline)) void sendList(const Listing& list, const String& error, size_t storage) {
  CountSink count;
  smalltv::writeThemeList<128>(settings->themeId.c_str(), error.c_str(), storage, list, count);
  server->setContentLength(count.bytes);
  server->send(200, "application/json", "");
  ReplySink sink;
  smalltv::writeThemeList<128>(settings->themeId.c_str(), error.c_str(), storage, list, sink);
}
// GET /api/themes: every installed package's metadata and validity, without
// requiring any of them to be the currently active theme. A package with a
// matching line in the index (written at install, see ThemeIndexReader) is
// listed from it; any other one goes through inspectInstalledTheme(), the
// same validator a selection would use, so an invalid or corrupt package
// still gets listed (with its error), just not selectable. When that finds a
// valid package, or the index has a line that matches no package, the index
// is written again, so a package is only inspected once.
void listThemes() {
  if (!auth()) return;
  // The running theme's error and the free storage as the list starts.
  const String error = g_themeMode.error();
  const size_t storage = freeBytes();
  Listing list;
  list.reserve(MaxThemes);
  // First each package's id and size, from the directory alone; the other
  // fields as inspectInstalledTheme() gives them for a package that does not
  // load, until something better is known.
  File dir = LittleFS.open("/themes", "r");
  for (File entry = dir.openNextFile(); entry && list.size() < MaxThemes; entry = dir.openNextFile()) {
    String name = entry.name();
    if (entry.isDirectory() || !name.endsWith(".stheme")) continue;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    String id = name.substring(0, name.length() - 7);
    if (!smalltv::validId(id.c_str())) continue;
    smalltv::ListedTheme& t = list.emplace_back();
    t.set(id.c_str(), id.c_str(), "", "", "");
    t.bytes = entry.size();
    yield();
  }
  dir.close();
  // Then the index; any package it does not cover is inspected. Inspecting
  // a package parses it; before the first one that needs it, free the
  // running theme if the heap is short (issue #30). Its error is already
  // copied above.
  bool rewrite = false;  // the index needs writing again
  const uint32_t listed = fillFromIndex(list, rewrite);
  bool released = false;
  for (unsigned i = 0; i < list.size(); ++i) {
    if (listed & (1u << i)) continue;
    if (!released) {
      g_themeMode.release();
      released = true;
    }
    if (inspectInto(list[i])) rewrite = true;
    yield();
  }
  if (rewrite) writeIndex(list);
  sendList(list, error, storage);
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
  updateIndex(id, nullptr);
  reply(200, id);
}
}
// Registers the theme API routes on the shared web server; `requireAuth`
// is the same digest-auth check every other route in the device uses, so
// theme install/select/delete get identical protection without duplicating it.
// `requestAllowed` is the CSRF / DNS-rebinding check without the 403 reply: the
// upload callback runs before finishUpload() and must refuse to stage the body.
void themeWebBegin(WebServerClass& web, Settings& s, bool (*requireAuth)(), bool (*requestAllowed)()) {
  server = &web;
  settings = &s;
  auth = requireAuth;
  allowed = requestAllowed;
  web.on("/api/themes", HTTP_GET, listThemes);
  web.on("/api/themes/install", HTTP_POST, finishUpload, receiveUpload);
  web.on("/api/themes/select", HTTP_POST, selectTheme);
  web.on("/api/themes/delete", HTTP_POST, deleteTheme);
  removeSidecars();
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
