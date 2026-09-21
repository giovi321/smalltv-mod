#include "ThemeEngine.h"
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
using namespace smalltv;
const char* base=R"({"spec":1,"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"layers":[{"id":"time","type":"text","x":120,"y":100,"anchor":"center","value":"{HH}:{MM}","size":48,"color":"#ffffff"}]})";
const char* dataBase=R"({"spec":1,"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"data":[{"id":"weather","url":"https://example.test/weather.json","interval":60,"fields":[{"id":"temp","path":"main.temp"}]}],"layers":[{"id":"value","type":"text","x":0,"y":0,"value":"{weather.temp}","size":16,"color":"#ffffff"}]})";
void rejected(JsonDocument& doc) {
  std::string json,error;serializeJson(doc,json);Theme t;t.id="untouched";
  assert(!parseTheme(json,t,error));assert(!error.empty());assert(t.id=="untouched");
}
void dataDocument(JsonDocument& doc) {
  deserializeJson(doc,dataBase);
  doc["data"][0]["insecureTls"]=true;
}
void validScroll(JsonObject layer) {
  layer["scroll"]["width"]=120;
  layer["scroll"]["mode"]="loop";
  layer["scroll"]["speed"]=24;
}
void numericBinding(JsonObject layer,const char* property,const char* source="weather.temp") {
  JsonObject bind=layer["bind"].as<JsonObject>();
  if(bind.isNull()) bind=layer["bind"].to<JsonObject>();
  JsonObject entry=bind[property].to<JsonObject>();
  entry["source"]=source;
  entry["input"].add(0.0);entry["input"].add(40.0);
  entry["output"].add(0);entry["output"].add(200);
}
void colorBinding(JsonObject layer,const char* property,const char* source="weather.temp") {
  JsonObject bind=layer["bind"].as<JsonObject>();
  if(bind.isNull()) bind=layer["bind"].to<JsonObject>();
  JsonObject entry=bind[property].to<JsonObject>();
  entry["source"]=source;
  JsonObject cold=entry["stops"].add<JsonObject>();
  cold["at"]=0;cold["value"]="#00ff00";
  JsonObject hot=entry["stops"].add<JsonObject>();
  hot["at"]=30;hot["value"]="#ff0000";
}
void rectangle(JsonObject layer) {
  layer.clear();
  layer["id"]="rectangle";layer["type"]="shape";layer["shape"]="rectangle";
  layer["x"]=0;layer["y"]=0;layer["width"]=100;layer["height"]=20;layer["fill"]="#ffffff";
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
  deserializeJson(d,dataBase);serializeJson(d,json);assert(!parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["data"][0]["insecureTls"]=true;serializeJson(d,json);assert(parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["data"][0]["insecureTls"]="yes";serializeJson(d,json);assert(!parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["data"][0]["url"]="http://example.test/weather.json";serializeJson(d,json);assert(parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["layers"][0]["value"]="{unknown.temp}";serializeJson(d,json);assert(!parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["data"][0]["url"]="https://";serializeJson(d,json);assert(!parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["data"][0]["fields"][0]["path"]="main..temp";serializeJson(d,json);assert(!parseTheme(json,theme,error));
  deserializeJson(d,dataBase);d["data"]=42;serializeJson(d,json);assert(!parseTheme(json,theme,error));
  json.clear();deserializeJson(d,base);d["display"]["width"]=320;serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("display.width:")==0);

  // Scroll and binding declarations remain static model data for later resolution.
  dataDocument(d);JsonObject text=d["layers"][0];
  text["scroll"]["width"]=120;text["scroll"]["mode"]="bounce";
  text["scroll"]["speed"]=24;text["scroll"]["pause"]=750;
  numericBinding(text,"x");colorBinding(text,"color");
  serializeJson(d,json);assert(parseTheme(json,theme,error));
  assert(theme.layers[0].scroll.enabled);
  assert(theme.layers[0].scroll.width==120);
  assert(theme.layers[0].scroll.mode==ScrollMode::Bounce);
  assert(theme.layers[0].scroll.speed==24&&theme.layers[0].scroll.pauseMs==750);
  assert(theme.layers[0].bindings.size()==2);
  assert(theme.layers[0].bindings[0].property==BoundProperty::X);
  assert(theme.layers[0].bindings[0].source=="weather.temp");
  assert(theme.layers[0].bindings[0].numeric.input0==0&&theme.layers[0].bindings[0].numeric.input1==40);
  assert(theme.layers[0].bindings[1].property==BoundProperty::Color);
  assert(theme.layers[0].bindings[1].color&&theme.layers[0].bindings[1].colors.stops.size()==2);
  assert(theme.layers[0].bindings[1].colors.stops[0].value==0x07e0);
  dataDocument(d);rectangle(d["layers"][0]);d["layers"][0]["cornerRadius"]=8;
  text=d["layers"][0];numericBinding(text,"cornerRadius");text["bind"]["cornerRadius"]["output"][1]=120;serializeJson(d,json);
  assert(parseTheme(json,theme,error));
  assert(theme.layers[0].cornerRadius==8&&theme.layers[0].bindings.size()==1);
  assert(theme.layers[0].bindings[0].property==BoundProperty::CornerRadius);

  // Scroll is text-only and its closed schema requires all applicable fields.
  deserializeJson(d,base);d["layers"][0].clear();text=d["layers"][0];
  text["id"]="image";text["type"]="image";text["x"]=0;text["y"]=0;text["source"]="image.sti";validScroll(text);rejected(d);
  deserializeJson(d,base);text=d["layers"][0];text["scroll"]["width"]=120;rejected(d);
  deserializeJson(d,base);text=d["layers"][0];validScroll(text);text["scroll"]["width"]="120";rejected(d);
  deserializeJson(d,base);text=d["layers"][0];validScroll(text);text["scroll"]["speed"]=241;rejected(d);
  deserializeJson(d,base);text=d["layers"][0];validScroll(text);text["scroll"]["pause"]=-1;rejected(d);
  deserializeJson(d,base);text=d["layers"][0];validScroll(text);text["scroll"]["gap"]=241;rejected(d);
  deserializeJson(d,base);text=d["layers"][0];validScroll(text);text["scroll"]["mode"]="sideways";serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].scroll.mode:")==0);
  deserializeJson(d,base);text=d["layers"][0];validScroll(text);text["scroll"]["mode"]="bounce";text["scroll"]["gap"]=0;rejected(d);

  // Binding containers have a bounded, closed schema and declared data sources.
  deserializeJson(d,base);d["layers"][0]["bind"]=42;rejected(d);
  dataDocument(d);text=d["layers"][0];for(int i=0;i<9;++i) numericBinding(text,("unknown"+std::to_string(i)).c_str());rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"unknown");rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x","missing.temp");rejected(d);

  // Numeric bindings require exactly the declared mapping fields and endpoints.
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["extra"]=1;rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["input"].remove(1);rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["input"].add(60);rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["input"][0]="bad";rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["input"][1]=0;serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].bind.x.input:")==0);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["output"][0]=0.5;rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["output"][1]=480;rejected(d);

  // Color bindings require ordered finite stops with no numeric-mapping fields.
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["input"].add(0);rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"].clear();rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");for(int i=0;i<7;++i) {JsonObject stop=text["bind"]["color"]["stops"].add<JsonObject>();stop["at"]=31+i;stop["value"]="#ffffff";}rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][0]["at"]="bad";rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][1]["at"]=0;serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].bind.color.stops[1].at:")==0);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][0]["value"]="red";rejected(d);

  // Bindings cannot enable properties that a layer did not declare or support.
  dataDocument(d);text=d["layers"][0];text.clear();text["id"]="image";text["type"]="image";text["x"]=0;text["y"]=0;text["source"]="image.sti";numericBinding(text,"width");rejected(d);
  dataDocument(d);rectangle(d["layers"][0]);text=d["layers"][0];colorBinding(text,"stroke");rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"scroll.width");rejected(d);

  // Rounded corners are rectangle-only, integral, and bounded.
  deserializeJson(d,base);d["layers"][0]["cornerRadius"]=8;rejected(d);
  dataDocument(d);rectangle(d["layers"][0]);d["layers"][0]["cornerRadius"]="8";rejected(d);
  dataDocument(d);rectangle(d["layers"][0]);d["layers"][0]["cornerRadius"]=121;rejected(d);
  tm t{};t.tm_hour=12;t.tm_year=100;t.tm_mon=0;t.tm_mday=1;t.tm_wday=6;
  assert(expandText("{hh} {HH} {DD} {MONTH} {WEEKDAY} {YYYY}",&t)=="12 12 01 January Saturday 2000");
  t.tm_hour=23;assert(expandText("{hh}",&t)=="11");assert(expandText("{HH}:{MM}",nullptr)=="--:--");
  std::cout<<"theme validation tests passed\n";
}
