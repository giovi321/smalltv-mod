#include "ThemeEngine.h"
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
using namespace smalltv;
const char* base=R"({"spec":1,"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"layers":[{"id":"time","type":"text","x":120,"y":100,"anchor":"center","value":"{HH}:{MM}","size":48,"color":"#ffffff"}]})";
void rejected(JsonDocument& doc) {
  std::string json,error;serializeJson(doc,json);Theme t;t.id="untouched";
  assert(!parseTheme(json,t,error));assert(!error.empty());assert(t.id=="untouched");
}
int main() {
  JsonDocument d;
  for(const char* token:{"{foo}","{HH+1}","{HH", "HH}", "{js:alert(1)}", "\n", "\x01"}) {deserializeJson(d,base);d["layers"][0]["value"]=token;rejected(d);}
  for(const char* type:{"video","javascript","lua","group"}) {deserializeJson(d,base);d["layers"][0]["type"]=type;rejected(d);}
  for(const char* path:{"../x","/x","a//b","a/./b","a/../b","a\\b","http://x","a/"}) assert(!validPath(path));
  assert(validPath("animations/cat/000.png.sti"));
  deserializeJson(d,base);d["spec|theme"]=0;rejected(d);
  deserializeJson(d,base);d["spec"]=2;rejected(d);
  deserializeJson(d,base);d["display"]["width"]=320;rejected(d);
  deserializeJson(d,base);d["layers"][0]["x"]="50%";rejected(d);
  deserializeJson(d,base);d["layers"][0]["x"]=100000;rejected(d);
  deserializeJson(d,base);d["layers"][0]["size"]=0;rejected(d);
  deserializeJson(d,base);d["layers"][0]["color"]="#ghijkl";rejected(d);
  deserializeJson(d,base);d["layers"][0]["anchor"]="flex";rejected(d);
  deserializeJson(d,base);d["layers"][0]["script"]="alert(1)";rejected(d);
  deserializeJson(d,base);d["layers"].as<JsonArray>().add(d["layers"][0]);rejected(d);
  deserializeJson(d,base);d["theme"]["id"]="../config";rejected(d);
  deserializeJson(d,base);d["layers"][0]["value"]=std::string(129,'x');rejected(d);
  deserializeJson(d,base);d["layers"][0]["value"]=std::string("abc\0def",7);rejected(d);
  std::string json,error;Theme theme;
  assert(!parseTheme(std::string(base)+"junk",theme,error));
  assert(!parseTheme(std::string(base).insert(1,"\"spec\\u0000extra\":123,"),theme,error));
  assert(!parseTheme(std::string(16385,' '),theme,error));
  // Authoring diagnostics must identify the field rejected by the device.
  deserializeJson(d,base);d["layers"][0]["color"]="red";serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].color:")==0);
  json.clear();deserializeJson(d,base);d["theme"]["id"]="../bad";serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("theme.id:")==0);
  json.clear();deserializeJson(d,base);d["layers"][0]["value"]="{UNKNOWN}";serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].value:")==0);
  const char* dataBase=R"({"spec":1,"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"data":[{"id":"weather","url":"https://example.test/weather.json","interval":60,"fields":[{"id":"temp","path":"main.temp"}]}],"layers":[{"id":"value","type":"text","x":0,"y":0,"value":"{weather.temp}","size":16,"color":"#ffffff"}]})";
  deserializeJson(d,dataBase);serializeJson(d,json);assert(parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["layers"][0]["value"]="{unknown.temp}";serializeJson(d,json);assert(!parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["data"][0]["url"]="https://";serializeJson(d,json);assert(!parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["data"][0]["fields"][0]["path"]="main..temp";serializeJson(d,json);assert(!parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["data"]=42;serializeJson(d,json);assert(!parseTheme(json,theme,error));
  json.clear();deserializeJson(d,base);d["display"]["width"]=320;serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("display.width:")==0);
  tm t{};t.tm_hour=12;t.tm_year=100;t.tm_mon=0;t.tm_mday=1;t.tm_wday=6;
  assert(expandText("{hh} {HH} {DD} {MONTH} {WEEKDAY} {YYYY}",&t)=="12 12 01 January Saturday 2000");
  t.tm_hour=23;assert(expandText("{hh}",&t)=="11");assert(expandText("{HH}:{MM}",nullptr)=="--:--");
  std::cout<<"theme validation tests passed\n";
}
