#pragma once
#include "config.h"
#if WITH_THEME
#include "Mode.h"
#include "ThemePackage.h"
#include "ThemeData.h"
#include <LittleFS.h>
#include <memory>
#include <vector>

class ThemeFile : public smalltv::Source {
 public:
  bool open(const String& path) { file_=LittleFS.open(path,"r");return bool(file_); }
  uint32_t size() const override {return file_?file_.size():0;}
  bool read(uint32_t offset,void* dest,size_t count) override {
    return file_ && file_.seek(offset) && file_.read(static_cast<uint8_t*>(dest),count)==count;
  }
 private:
  File file_;
};
class ThemeMode : public DisplayMode {
 public:
  const char* id() const override {return "theme";}
  uint8_t modeConst() const override {return MODE_THEME;}
  void begin(const Settings&) override;
  void service(const Settings&) override;
  void invalidate(const Settings&) override;
  void wake(const Settings&) override {engine_.invalidate(); messageDrawn_=false;}
  const String& error() const {return error_;}
 private:
  void unload();
  void refreshData();
  smalltv::Engine engine_;
  std::unique_ptr<ThemeFile> file_;
  std::unique_ptr<smalltv::Package> package_;
  String loadedId_, error_;
  std::vector<uint32_t> dataNextMs_;
  std::vector<bool> dataScheduled_;
  std::vector<smalltv::ThemeValue> dataValues_;
  smalltv::DataRequests dataRequests_;
  size_t dataCursor_=0;
  bool attempted_=false, messageDrawn_=false;
};
extern ThemeMode g_themeMode;
#endif
