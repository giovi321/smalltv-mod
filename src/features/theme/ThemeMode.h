#pragma once
#include "config.h"
#if WITH_THEME
#include "Mode.h"
#include "ThemePackage.h"
#include "ThemeData.h"
#include "ThemeMargin.h"
#include "ThemeRefusal.h"
#include "ThemeSelection.h"
#include <LittleFS.h>
#include <memory>
#include <vector>

class ThemeFile : public smalltv::Source {
 public:
  bool open(const String& path) {
    file_ = LittleFS.open(path, "r");
    return bool(file_);
  }
  uint32_t size() const override { return file_ ? file_.size() : 0; }
  bool read(uint32_t offset, void* dest, size_t count) override {
    return file_ && file_.seek(offset) && file_.read(static_cast<uint8_t*>(dest), count) == count;
  }
  void pause() override { yield(); }
 private:
  File file_;
};
class ThemeMode : public DisplayMode {
 public:
  const char* id() const override { return "theme"; }
  uint8_t modeConst() const override { return MODE_THEME; }
  void begin(const Settings&) override;
  void service(const Settings&) override;
  void invalidate(const Settings&) override;
  void wake(const Settings&) override {
    engine_.invalidate();
    messageDrawn_ = false;
  }
  // The load error, or when there is none the text of a refusal for lack of
  // memory (GET /api/themes shows it).
  const String& error() const { return error_.length() ? error_ : refusal_; }
  // What parsing a package needs: its peak heap (tests/theme/test_memory.cpp
  // holds the examples to 5 KB) plus about 1 KB that the network code may
  // claim meanwhile, and blocks of up to 1 KB (the same test's
  // largest-request limit) with some slack.
  static constexpr uint32_t ParseFreeHeap = 6144;
  static constexpr uint32_t ParseLargestBlock = 1536;
  // What a running theme must leave free once it has loaded and drawn its
  // first frame (ThemeMargin.h): the free heap of the shared Wi-Fi reserve
  // (WifiReserve.h) and ParseLargestBlock as the largest block. The #132
  // measurement showed that the free heap separates clean runs from crashes
  // and that the largest block does not (a clean run went down to 1,568 B,
  // a crash had 1,880 B). The block limit is the parser's own, agreed on
  // #132; it covers heap that is free but split in small pieces.
  // Frees the loaded theme when the heap is short of what a web request needs,
  // so the request does not run out of memory next to it; on the ESP8266 a
  // theme and a second package parse do not fit together (issue #30). With
  // enough heap it does nothing. The selection, the fetched values and the
  // fetch schedule stay: the next service() call loads the theme again and
  // repaints it with the same values, and no source is fetched early.
  void release(uint32_t needFree = ParseFreeHeap, uint32_t needLargestBlock = ParseLargestBlock);
  // DisplayMode::freeForRequest(), before a settings read, save or import.
  // A save needs about 7 KB of heap (#35's measurement on a SmallTV-Ultra)
  // plus the same 1 KB of network slack as ParseFreeHeap, and builds a
  // 2.4 KB settings document. On the ESP8266 with a
  // theme running that is not free, so the theme is released; on the ESP32
  // boards it always is, and the theme stays.
  static constexpr uint32_t RequestFreeHeap = 8192;
  static constexpr uint32_t RequestLargestBlock = 3072;
  void freeForRequest() override { release(RequestFreeHeap, RequestLargestBlock); }
  // The refusal for lack of memory (ThemeMargin.h), "" when there is none. It
  // lives in RAM only: not in the settings, and it survives unload() because
  // the settings fix that follows a refusal changes the selection.
  const String& refusal() const { return refusal_; }
  // The mode and theme the select request replaced, kept until the theme is
  // loaded: a refusal puts them back (ThemeRefusal.h).
  // While an earlier pick still waits to be saved, the previous theme stays
  // the last one that passed: a refusal of this pick must not save the earlier one.
  void rememberPrevious(uint8_t mode, const String& id) {
    if (hasPrev_ && pending_.pending()) return;
    hasPrev_ = true;
    prevMode_ = mode;
    prevId_ = id;
  }
  // A new select, install or delete of the refused theme lets it try again.
  void forgetRefusal() {
    refusal_ = "";
    refusedId_ = "";
    passedTheme_.forget();
  }
  // The select request picked this theme but did not save the setting: it is
  // saved once the theme has loaded and passed the memory check, so a crash
  // during the first load does not bring the theme back at the next boot
  // (ThemeSelection.h).
  void saveAfterLoad(const String& id) { pending_.pick(id.c_str()); }
 private:
  void unload();
  void refreshData();
  void acceptSelection(const Settings& s);
#if defined(SMALLTV_FAMILY_ESP8266)
  bool refusedBeforeBuild(smalltv::Theme& theme);   // true: the theme was refused and dropped
  bool checkMargin();   // true: the theme was refused and dropped
  void refuseForMemory();
  smalltv::MarginWatch marginWatch_;
  bool fetchWatched_ = false;   // the first fetched values of this load restarted the watch
#endif
  smalltv::PendingSelection pending_;
  // Which theme passed its memory check since the boot: a refusal of its
  // reload leaves the settings alone (ThemeRefusal.h). Survives unload().
  smalltv::PassedTheme passedTheme_;
  bool hasPrev_ = false;
  uint8_t prevMode_ = 0;
  String prevId_;
  String refusal_, refusedId_;
  smalltv::Engine engine_;
  std::unique_ptr<ThemeFile> file_;
  std::unique_ptr<smalltv::Package> package_;
  String loadedId_, error_;
  std::vector<uint32_t> dataNextMs_;
  // Per source: 0 = never started, else 1 + failures in a row (fetchWaitMs).
  std::vector<uint8_t> dataTries_;
  std::vector<smalltv::ThemeValue> dataValues_;
  smalltv::DataRequests dataRequests_;
  smalltv::DataAges dataAges_;
  size_t dataCursor_ = 0;
  // Set by release(): the next load of the same theme keeps the data state.
  bool keepData_ = false;
  bool attempted_ = false, messageDrawn_ = false;
};
extern ThemeMode g_themeMode;
#endif
