#include "ThemeMode.h"
#if WITH_THEME
#include "Clock.h"
#include "Gfx.h"
#include "Platform.h"
#include "Net.h"
#include "ThemeDataClient.h"
#include <Arduino_GFX_Library.h>
#include <font/glcdfont.h>
#include <algorithm>
#include <cstdio>
#include <new>
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
#include <pgmspace.h>
#include <recovery_theme_data.h>
#endif

ThemeMode g_themeMode;
namespace {
// Adapts the portable engine's Display interface to this device's actual
// panel: glyphColumn() reads the built-in font from flash (PROGMEM), and
// row() applies the usual panel color tint before handing the row to the
// GFX driver. yield() every 16 rows keeps a large dirty region from starving
// WiFi/other tasks during a long render.
class Panel : public smalltv::Display {
 public:
  uint8_t glyphColumn(uint8_t c, uint8_t col) const override {
    return pgm_read_byte(font + unsigned(c) * 5 + col);
  }
  void row(int x, int y, const uint16_t* pixels, int count) override {
    uint16_t tinted[240];
    for (int i = 0; i < count; ++i) tinted[i] = gfxTint(pixels[i]);
    gfxDev()->draw16bitRGBBitmap(x, y, tinted, count, 1);
    if ((y & 15) == 15) yield();
  }
};
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
class RecoveryThemeSource : public smalltv::Source {
 public:
  uint32_t size() const override { return kRecoveryThemeSize; }
  bool read(uint32_t offset, void* dest, size_t count) override {
    if (offset > kRecoveryThemeSize || count > kRecoveryThemeSize - offset) return false;
    uint8_t* out = static_cast<uint8_t*>(dest);
    for (size_t i = 0; i < count; ++i) out[i] = pgm_read_byte(kRecoveryThemeData + offset + i);
    return true;
  }
};
static constexpr const char* RecoveryThemeId = "office-recovery";
#endif
// What a load needs before it starts: a package parse (ThemeMode.h). A failed
// allocation mid-load restarts the ESP8266, so a load that would not fit
// waits instead.
constexpr uint32_t LoadFreeHeap = ThemeMode::ParseFreeHeap;
constexpr uint32_t LoadLargestBlock = ThemeMode::ParseLargestBlock;
const char* const WaitingForMemory = "Waiting for free memory";
}
void ThemeMode::begin(const Settings&) {
  LittleFS.mkdir("/themes");
  // An interrupted install leaves its staging file behind; boot is the one
  // point it is safe to remove without racing a request in progress.
  LittleFS.remove("/themes/.upload");
}
// Drops every bit of state for the currently loaded theme -- the open
// package/file, any in-flight or fetched data, and the engine itself -- so
// the next service() call starts a clean load from scratch.
void ThemeMode::unload() {
  dataRequests_.cancel();
  dataRequests_.take();
  package_.reset();
  file_.reset();
  dataNextMs_.clear();
  dataScheduled_.clear();
  dataValues_.clear();
  dataCursor_ = 0;
  engine_.setValues({});
  engine_.setTheme(smalltv::Theme{}, millis());
  loadedId_ = "";
  attempted_ = messageDrawn_ = keepData_ = false;
  error_ = "";
}
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
void ThemeMode::useRecoveryTheme(uint8_t crashCount) {
  recoveryMode_ = true;
  recoveryCrashCount_ = crashCount;
  unload();
}
#endif
#if WITH_THEME_PLAYLIST
void ThemeMode::setPlaylistTheme(const char* id) {
  String next = id ? id : "";
  if (next == playlistId_) return;
  playlistId_ = next;
  unload();  // cancels the previous fetch and drops all theme-owned heap state
}
bool ThemeMode::playlistLoadFailed() const {
  return attempted_ && !package_ && error_.length() &&
      error_ != WaitingForMemory;
}
#endif
void ThemeMode::release(uint32_t needFree, uint32_t needLargestBlock) {
  if (!package_ || (ESP.getFreeHeap() >= needFree && platformMaxFreeBlock() >= needLargestBlock)) return;
  // A fetch in flight is dropped; its source is still due, so the reload
  // fetches it again.
  dataRequests_.cancel();
  dataRequests_.take();
  package_.reset();
  file_.reset();
  engine_.setValues({});
  engine_.setTheme(smalltv::Theme{}, millis());
  attempted_ = messageDrawn_ = false;
  keepData_ = true;
}
// Collects a finished fetch (if any) and, with at most one request in flight
// at a time, starts the next source whose interval has elapsed, round-robin
// over sources so no single slow/failing one starves the others.
void ThemeMode::refreshData() {
  const auto& sources = engine_.theme().data;
  if (auto result = dataRequests_.take()) {
    if (result->success) {
      // Merge into the running set by key rather than replace it wholesale:
      // a source's fields persist across refreshes of the *other* sources, so
      // a slow source does not blank fields a fast one already delivered.
      for (auto& value : result->values) {
        auto existing = std::find_if(dataValues_.begin(), dataValues_.end(), [&](const smalltv::ThemeValue& old) { return old.key == value.key; });
        if (existing == dataValues_.end()) dataValues_.push_back(std::move(value));
        else existing->value = std::move(value.value);
      }
      engine_.setValues(dataValues_);
    }
    // Schedule from completion, preserving the interval even for slow sources.
    dataNextMs_[result->index] = millis() + sources[result->index].interval * 1000UL;
  }
  if (sources.empty() || dataRequests_.busy() || WiFi.status() != WL_CONNECTED) return;
  // Scan forward from `dataCursor_` for the first source that is due (an
  // unscheduled one always is), start it, and advance the cursor past it --
  // so a source that just fired goes to the back of the round-robin queue.
  for (size_t offset = 0; offset < sources.size(); ++offset) {
    size_t i = (dataCursor_ + offset) % sources.size();
    if (dataScheduled_[i] && static_cast<int32_t>(millis() - dataNextMs_[i]) < 0) continue;
    auto request = dataRequests_.start(sources[i], i);
    if (!startThemeDataFetch(request)) request->finish(false);
    dataScheduled_[i] = true;
    dataCursor_ = (i + 1) % sources.size();
    break;
  }
}
// Called when the theme should be redrawn from scratch, e.g. when a
// notification overlay just finished. Switches to a different theme by
// unloading first; otherwise leaves the loaded package alone and only marks
// the engine's next update() as a full repaint.
void ThemeMode::invalidate(const Settings& s) {
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
  if (recoveryMode_) {
    engine_.invalidate();
    messageDrawn_ = false;
    return;
  }
#endif
  const bool onScreen = s.mode == MODE_THEME || (s.mode == MODE_CAROUSEL && s.carouselTheme);
  String desiredId = s.themeId;
#if WITH_THEME_PLAYLIST
  if (playlistId_.length()) desiredId = playlistId_;
#endif
  if (loadedId_ != desiredId || !onScreen) unload();
  // A failed load may be retried after installation or settings changes.
  if (!package_) attempted_ = false;
  engine_.invalidate();
  messageDrawn_ = false;
}
// Per-frame entry point: loads the selected theme at most once per selection
// (via `attempted_`, so a failed load is not retried every call -- only on
// the next invalidate()/selection change), then renders it. `attempted_` and
// `loadedId_` together are the load state machine; everything past the load
// block runs every call once a package is open.
void ThemeMode::service(const Settings& s) {
  String desiredId = s.themeId;
#if WITH_THEME_PLAYLIST
  if (playlistId_.length()) desiredId = playlistId_;
#endif
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
  if (recoveryMode_) desiredId = RecoveryThemeId;
#endif
  if (loadedId_ != desiredId) {
    unload();
    loadedId_ = desiredId;
  }
  // A web request, or the buffers of its reply, can briefly hold the heap a
  // load needs; the load then waits, checked again every frame.
  const bool loadFits = ESP.getFreeHeap() >= LoadFreeHeap && platformMaxFreeBlock() >= LoadLargestBlock;
  if (!attempted_ && !loadFits && error_ != WaitingForMemory) {
    error_ = WaitingForMemory;
    messageDrawn_ = false;
  }
  if (!attempted_ && loadFits) {
    attempted_ = true;
    std::string error;
    smalltv::Theme theme;
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
    bool sourceReady = false;
    if (recoveryMode_) {
      file_.reset(new(std::nothrow) RecoveryThemeSource);
      sourceReady = bool(file_);
    } else {
      if (!smalltv::validId(desiredId.c_str())) error = "Select an installed theme";
      else {
        ThemeFile* source = new(std::nothrow) ThemeFile;
        if (source && source->open("/themes/" + desiredId + ".stheme")) {
          file_.reset(source);
          sourceReady = true;
        } else {
          delete source;
          error = "Theme file not found";
        }
      }
    }
    if (sourceReady) {
      package_.reset(new(std::nothrow) smalltv::Package(*file_));
      if (!package_) error = "Cannot allocate theme package";
      else if (!package_->load(theme, error)) package_.reset();
      else if (theme.id != desiredId.c_str()) {
        error = "Theme ID mismatch";
        package_.reset();
      }
    }
#else
    if (!smalltv::validId(desiredId.c_str())) error = "Select an installed theme";
    else {
      ThemeFile* source = new(std::nothrow) ThemeFile;
      if (!source || !source->open("/themes/" + desiredId + ".stheme")) {
        delete source;
        error = "Theme file not found";
      } else {
        file_.reset(source);
        package_.reset(new(std::nothrow) smalltv::Package(*file_));
        if (!package_) error = "Cannot allocate theme package";
        else if (!package_->load(theme, error)) package_.reset();
        else if (theme.id != desiredId.c_str()) {
          error = "Theme ID mismatch";
          package_.reset();
        }
      }
    }
#endif
    if (!error.empty()) {
      error_ = error.c_str();
      package_.reset();
      file_.reset();
    } else {
      // After a release the same package loads again, so its sources match.
      if (!keepData_ || dataNextMs_.size() != theme.data.size()) {
        dataNextMs_.assign(theme.data.size(), 0);
        dataScheduled_.assign(theme.data.size(), false);
        dataValues_.clear();
        dataCursor_ = 0;
      }
      engine_.setTheme(std::move(theme), millis());
      engine_.setValues(dataValues_);
      error_ = "";
    }
  }
  if (!package_) {
    if (!messageDrawn_) {
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
      if (recoveryMode_) gfxSafeTheme(netIP().c_str(), recoveryCrashCount_);
      else
#endif
      gfxMessage("THEMES", error_.c_str(), C_WHITE);
      messageDrawn_ = true;
    }
    return;
  }
  refreshData();
  tm t{};
  bool valid = clockNow(t);
  Panel panel;
  auto dirty = engine_.update(millis(), valid ? &t : nullptr);
  // A render failure (a corrupt or truncated asset read mid-frame) is treated
  // like a load failure: drop the package and show an error rather than
  // risk repeatedly failing on every subsequent frame.
  if (!smalltv::render(engine_, dirty, *package_, panel)) {
    dataRequests_.cancel();
    dataRequests_.take();
    error_ = "Could not read theme asset";
    package_.reset();
    file_.reset();
    messageDrawn_ = false;
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
    if (recoveryMode_) gfxSafeTheme(netIP().c_str(), recoveryCrashCount_);
#endif
    return;
  }
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
  if (recoveryMode_) {
    char count[24];
    snprintf(count, sizeof(count), "Crash count: %u", (unsigned)recoveryCrashCount_);
    gfxDrawCentered("Open settings:", 176, 1, C_GRAY);
    gfxDrawCentered(netIP().c_str(), 194, 1, C_GREEN);
    gfxDrawCentered(count, 216, 1, C_GRAY);
  }
#endif
}
#endif
