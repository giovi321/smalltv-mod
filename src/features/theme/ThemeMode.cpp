#include "ThemeMode.h"
#if WITH_THEME
#include "Clock.h"
#include "Gfx.h"
#include "Platform.h"
#include "ThemeDataClient.h"
#include <Arduino_GFX_Library.h>
#include <font/glcdfont.h>
#include <algorithm>

ThemeMode g_themeMode;
namespace {
class Panel : public smalltv::Display {
 public:
  uint8_t glyphColumn(uint8_t c,uint8_t col) const override {
    return pgm_read_byte(font+unsigned(c)*5+col);
  }
  void row(int x,int y,const uint16_t* pixels,int count) override {
    uint16_t tinted[240];
    for(int i=0;i<count;++i) tinted[i]=gfxTint(pixels[i]);
    gfxDev()->draw16bitRGBBitmap(x,y,tinted,count,1);
    if((y&15)==15) yield();
  }
};
}
void ThemeMode::begin(const Settings&) {
  LittleFS.mkdir("/themes");
  LittleFS.remove("/themes/.upload");
}
void ThemeMode::unload() {
  dataRequests_.cancel();dataRequests_.take();
  package_.reset();file_.reset();dataNextMs_.clear();dataScheduled_.clear();dataValues_.clear();dataCursor_=0;engine_.setValues({});engine_.setTheme(smalltv::Theme{},millis());
  loadedId_="";attempted_=messageDrawn_=false;error_="";
}
void ThemeMode::refreshData() {
  const auto& sources=engine_.theme().data;
  if(auto result=dataRequests_.take()) {
    if(result->success) {
      for(auto& value:result->values) {
        auto existing=std::find_if(dataValues_.begin(),dataValues_.end(),[&](const smalltv::ThemeValue& old) {return old.key==value.key;});
        if(existing==dataValues_.end()) dataValues_.push_back(std::move(value));
        else existing->value=std::move(value.value);
      }
      engine_.setValues(dataValues_);
    }
    // Schedule from completion, preserving the interval even for slow sources.
    dataNextMs_[result->index]=millis()+sources[result->index].interval*1000UL;
  }
  if(sources.empty() || dataRequests_.busy() || WiFi.status()!=WL_CONNECTED) return;
  for(size_t offset=0;offset<sources.size();++offset) {
    size_t i=(dataCursor_+offset)%sources.size();
    if(dataScheduled_[i] && static_cast<int32_t>(millis()-dataNextMs_[i])<0) continue;
    auto request=dataRequests_.start(sources[i],i);
    if(!startThemeDataFetch(request)) request->finish(false);
    dataScheduled_[i]=true;dataCursor_=(i+1)%sources.size();
    break;
  }
}
void ThemeMode::invalidate(const Settings& s) {
  if(loadedId_!=s.themeId || s.mode!=MODE_THEME) unload();
  // A failed load may be retried after installation or settings changes.
  if(!package_) attempted_=false;
  engine_.invalidate();messageDrawn_=false;
}
void ThemeMode::service(const Settings& s) {
  if(loadedId_!=s.themeId) unload();
  if(!attempted_) {
    attempted_=true;loadedId_=s.themeId;
    std::string error;smalltv::Theme theme;
    if(!smalltv::validId(s.themeId.c_str())) error="Select an installed theme";
    else {
      file_.reset(new ThemeFile);
      if(!file_->open("/themes/"+s.themeId+".stheme")) error="Theme file not found";
      else {
        package_.reset(new smalltv::Package(*file_));
        if(!package_->load(theme,error)) package_.reset();
        else if(theme.id!=s.themeId.c_str()) {error="Theme ID mismatch";package_.reset();}
      }
    }
    if(!error.empty()) {error_=error.c_str();package_.reset();file_.reset();}
    else {dataNextMs_.assign(theme.data.size(),0);dataScheduled_.assign(theme.data.size(),false);dataValues_.clear();dataCursor_=0;engine_.setTheme(std::move(theme),millis());engine_.setValues(dataValues_);error_="";}
  }
  if(!package_) {
    if(!messageDrawn_) {gfxMessage("THEMES",error_.c_str(),C_WHITE);messageDrawn_=true;}
    return;
  }
  refreshData();
  tm t{};bool valid=clockNow(t);Panel panel;
  auto dirty=engine_.update(millis(),valid?&t:nullptr);
  if(!smalltv::render(engine_,dirty,*package_,panel)) {
    dataRequests_.cancel();dataRequests_.take();
    error_="Could not read theme asset";package_.reset();file_.reset();messageDrawn_=false;
  }
}
#endif
