#include "ThemeEngine.h"
#include <cassert>
#include <iostream>
using namespace smalltv;
int main() {
  Theme theme; std::string error;
  const char* json = R"({"spec":1,"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"layers":[{"id":"time","type":"text","x":120,"y":100,"anchor":"center","value":"{HH}:{MM}","size":48,"color":"#ffffff"}]})";
  assert(parseTheme(json, theme, error));
  assert(theme.layers.size() == 1);
  tm t{}; t.tm_year=126; t.tm_mon=8; t.tm_mday=17; t.tm_wday=4; t.tm_hour=0; t.tm_min=2; t.tm_sec=3;
  assert(expandText("{HH} {hh} {MM} {SS} {DD} {MON} {MONTH} {WD} {WEEKDAY} {YYYY}", &t)=="00 12 02 03 17 Sep September Thu Thursday 2026");
  Engine engine; engine.setTheme(theme, 0);
  assert(engine.update(0, &t).size()==1);
  t.tm_sec++;
  assert(engine.update(1000, &t).empty());
  t.tm_min++;
  auto dirty=engine.update(2000, &t);
  assert(dirty.size()==1 && dirty[0].w==180 && dirty[0].h==48);
  engine.invalidate(); assert(engine.update(2001, &t)[0].w==240);
  assert(!parseTheme("{}", theme, error));
  const char* dataJson = R"({"spec":1,"theme":{"id":"data","name":"Data","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"data":[{"id":"weather","url":"https://example.test/weather.json","interval":60,"insecureTls":true,"fields":[{"id":"temp","path":"main.temp"}]}],"layers":[{"id":"value","type":"text","x":0,"y":0,"value":"{weather.temp}","size":16,"color":"#ffffff"}]})";
  assert(parseTheme(dataJson,theme,error));assert(theme.data.size()==1&&theme.data[0].fields.size()==1);
  engine.setTheme(theme,0);engine.setValues({{"weather.temp","21.5"}});assert(expandText("{weather.temp}",&t,{{"weather.temp","21.5"}})=="21.5");
  engine.update(0,&t);
  // Data changes must repaint only the old/new text bounds, even within the same second.
  engine.setValues({{"weather.temp","9"}});
  dirty=engine.update(1,&t);
  assert(dirty.size()==1 && dirty[0].x==0 && dirty[0].y==0 && dirty[0].w==48 && dirty[0].h==16);
  assert(engine.states()[0].text=="9");
  engine.setValues({{"weather.temp","9"},{"unused.value","changed"}});
  assert(engine.update(2,&t).empty());
  engine.setValues({});dirty=engine.update(3,&t);
  assert(dirty.size()==1 && dirty[0].w==24 && engine.states()[0].text=="--");
  // Missing clock time must not prevent data updates.
  engine.update(4,nullptr);engine.setValues({{"weather.temp","12"}});
  assert(!engine.update(5,nullptr).empty() && engine.states()[0].text=="12");
  std::cout << "theme engine tests passed\n";
}
