#include "ThemeMode.h"
#if WITH_THEME
#include "Clock.h"
#include "Gfx.h"
#include "Platform.h"
#include "WifiReserve.h"
#include "ThemeDataClient.h"
#include <Arduino_GFX_Library.h>
#include <font/glcdfont.h>
#include <algorithm>

ThemeMode g_themeMode;
#if defined(SMALLTV_FAMILY_ESP8266)
// main.cpp: puts the settings right after a refusal and saves them.
extern void appThemeRefused(bool hasPrev, uint8_t prevMode, const String& prevId, const String& refusedId);
#endif
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
  dataTries_.clear();
  dataValues_.clear();
  dataAges_.reset(0);
  dataCursor_ = 0;
  engine_.setValues({});
  engine_.setTheme(smalltv::Theme{}, millis());
  loadedId_ = "";
  attempted_ = messageDrawn_ = keepData_ = false;
  error_ = "";
}
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
  // The "<source>.age" values (DataAges): move the ages on, and a fetch that
  // just ended sets or leaves its source's age. millis() only; no wall clock.
  dataAges_.tick(millis());
  bool changed = false;
  if (auto result = dataRequests_.take()) {
    // A success merges its values into the running set and sets the age to 0.
    changed = smalltv::collectFetch(*result, dataAges_, dataValues_);
    // Schedule from completion, preserving the interval even for slow sources.
    // A failure is tried again sooner (1, 2, 4, 8 minutes, never beyond the
    // interval); a success returns to the interval.
    dataNextMs_[result->index] = millis() + smalltv::fetchWaitMs(sources[result->index].interval, result->success, dataTries_[result->index]);
  }
  // The ages change once a minute. The engine gets the values only when they
  // changed: a fetch's new values, or a new age.
  if (dataAges_.publish(sources, dataValues_)) changed = true;
  if (changed) engine_.setValues(dataValues_);
#if defined(SMALLTV_FAMILY_ESP8266)
  // The first values of a load change the texts, and so the heap: watch again.
  if (changed && !fetchWatched_) {
    fetchWatched_ = true;
    marginWatch_.restart(millis());
  }
#endif
  if (sources.empty() || dataRequests_.busy() || WiFi.status() != WL_CONNECTED) return;
  // Scan forward from `dataCursor_` for the first source that is due (an
  // unscheduled one always is), start it, and advance the cursor past it --
  // so a source that just fired goes to the back of the round-robin queue.
  for (size_t offset = 0; offset < sources.size(); ++offset) {
    size_t i = (dataCursor_ + offset) % sources.size();
    if (!smalltv::fetchDue(dataTries_[i], dataNextMs_[i], millis())) continue;
    auto request = dataRequests_.start(sources[i], i);
    // A fetch refused for lack of memory counts as a failure: it ends here and
    // the take() above collects it on the next call.
    if (!startThemeDataFetch(request)) request->finish(false);
    smalltv::fetchStarted(dataTries_[i]);
    dataCursor_ = (i + 1) % sources.size();
    break;
  }
}
// Called when the theme should be redrawn from scratch, e.g. when a
// notification overlay just finished. Switches to a different theme by
// unloading first; otherwise leaves the loaded package alone and only marks
// the engine's next update() as a full repaint.
void ThemeMode::invalidate(const Settings& s) {
  const bool onScreen = s.mode == MODE_THEME || (s.mode == MODE_CAROUSEL && s.carouselTheme);
  if (loadedId_ != s.themeId || !onScreen) unload();
  // A failed load may be retried after installation or settings changes.
  if (!package_) attempted_ = false;
  engine_.invalidate();
  messageDrawn_ = false;
}
#if defined(SMALLTV_FAMILY_ESP8266)
// Drops the theme for lack of memory, the way release() does, and tells the
// app to put the settings right (main.cpp). The heap is free again before that
// call, which saves the settings. Nothing here ends the program: the device
// goes on with the previous mode. `loadedId_` and `attempted_` stay as they
// are, so the refused theme is not loaded again at the next frame.
__attribute__((noinline)) void ThemeMode::refuseForMemory() {
  dataRequests_.cancel();
  dataRequests_.take();
  package_.reset();
  file_.reset();
  dataNextMs_.clear();
  dataTries_.clear();
  dataValues_.clear();
  dataAges_.reset(0);
  dataCursor_ = 0;
  engine_.setValues({});
  engine_.setTheme(smalltv::Theme{}, millis());
  refusal_ = "not enough memory to run this theme";
  refusedId_ = loadedId_;
  error_ = refusal_;
  messageDrawn_ = false;
  keepData_ = false;
  appThemeRefused(hasPrev_, prevMode_, prevId_, refusedId_);
  hasPrev_ = false;
}
// One reading of the margin per frame while the watch is on (ThemeMargin.h).
// The loop stack is tight, so the numbers are compared here and service()
// only learns whether the theme is gone.
__attribute__((noinline)) bool ThemeMode::checkMargin() {
  const smalltv::MarginLimits limits{WifiReserveFreeHeap, ParseLargestBlock};
  if (!marginWatch_.check(millis(), ESP.getFreeHeap(), platformMaxFreeBlock(), limits)) {
    if (!marginWatch_.active()) hasPrev_ = false;   // the load passed its test: the old selection no longer applies
    return false;
  }
  refuseForMemory();
  return true;
}
#endif
// Per-frame entry point: loads the selected theme at most once per selection
// (via `attempted_`, so a failed load is not retried every call -- only on
// the next invalidate()/selection change), then renders it. `attempted_` and
// `loadedId_` together are the load state machine; everything past the load
// block runs every call once a package is open.
void ThemeMode::service(const Settings& s) {
  if (loadedId_ != s.themeId) {
    unload();
    loadedId_ = s.themeId;
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
    // After a refusal the setting no longer names the theme; say why instead.
    if (!smalltv::validId(s.themeId.c_str())) error = refusal_.length() ? refusal_.c_str() : "Select an installed theme";
    else {
      file_.reset(new ThemeFile);
      if (!file_->open("/themes/" + s.themeId + ".stheme")) error = "Theme file not found";
      else {
        package_.reset(new smalltv::Package(*file_));
        if (!package_->load(theme, error)) package_.reset();
        else if (theme.id != s.themeId.c_str()) {
          error = "Theme ID mismatch";
          package_.reset();
        }
      }
    }
    if (!error.empty()) {
      error_ = error.c_str();
      package_.reset();
      file_.reset();
      hasPrev_ = false;   // no theme, so nothing to refuse later
    } else {
      // After a release the same package loads again, so its sources match.
      if (!keepData_ || dataNextMs_.size() != theme.data.size()) {
        dataNextMs_.assign(theme.data.size(), 0);
        dataTries_.assign(theme.data.size(), 0);
        dataValues_.clear();
        // Only a spec 2 theme can use "<source>.age"; a spec 1 theme gets none.
        dataAges_.reset(theme.data.size(), theme.spec >= 2);
        dataCursor_ = 0;
      }
      engine_.setTheme(std::move(theme), millis());
      engine_.setValues(dataValues_);
      error_ = "";
#if defined(SMALLTV_FAMILY_ESP8266)
      // The texts of the layers are allocated by the first update() below; the
      // margin is checked after that, further down.
      fetchWatched_ = false;
      marginWatch_.restart(millis());
#endif
    }
  }
  if (!package_) {
    if (!messageDrawn_) {
      gfxMessage("THEMES", error_.c_str(), C_WHITE);
      messageDrawn_ = true;
    }
    return;
  }
  refreshData();
  tm t{};
  bool valid = clockNow(t);
  Panel panel;
  engine_.setLanguage(smalltv::languageFromCode(s.clock.lang.c_str()));
  auto dirty = engine_.update(millis(), valid ? &t : nullptr);
#if defined(SMALLTV_FAMILY_ESP8266)
  // After the first update the layers hold their texts: this is the heap the
  // theme leaves for the Wi-Fi code. Too little drops the theme (ThemeMargin.h).
  if (checkMargin()) return;
#endif
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
  }
}
#endif
