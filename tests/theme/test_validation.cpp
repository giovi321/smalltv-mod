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
  // Spec 2 (#31) is accepted and kept in the theme; it allows STI2 images
  // (see Package::load()). Other versions are refused.
  {
    std::string json,error;Theme t;
    assert(parseTheme(base,t,error) && t.spec==1);
    deserializeJson(d,base);d["spec"]=2;serializeJson(d,json);
    assert(parseTheme(json,t,error) && t.spec==2);
  }
  for(int spec:{0,3,-1}) {deserializeJson(d,base);d["spec"]=spec;rejected(d);}
  deserializeJson(d,base);d["spec"]=1.5;rejected(d);
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
  assert(theme.layers[0].bindings[0].numeric.clamp);
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
  deserializeJson(d,base);text=d["layers"][0];validScroll(text);text["scroll"]["unknown"]=1;rejected(d);
  deserializeJson(d,base);text=d["layers"][0];validScroll(text);text["scroll"]["mode"]="sideways";serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].scroll.mode:")==0);
  deserializeJson(d,base);text=d["layers"][0];validScroll(text);text["scroll"]["mode"]="bounce";text["scroll"]["gap"]=0;rejected(d);

  // Binding containers have a bounded, closed schema and declared data sources.
  deserializeJson(d,base);d["layers"][0]["bind"]=42;rejected(d);
  dataDocument(d);text=d["layers"][0];for(int i=0;i<9;++i) numericBinding(text,("unknown"+std::to_string(i)).c_str());rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"unknown");rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x","missing.temp");rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"].remove("source");rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["source"]=42;rejected(d);

  // Numeric bindings require exactly the declared mapping fields and endpoints.
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["extra"]=1;rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["input"].remove(1);rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["input"].add(60);rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["input"][0]="bad";rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["input"][1]=0;serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].bind.x.input:")==0);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["output"][0]=0.5;rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["output"][1]=480;rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"x");text["bind"]["x"]["clamp"]="yes";rejected(d);

  // Color bindings require ordered finite stops with no numeric-mapping fields.
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["input"].add(0);rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"].clear();rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");for(int i=0;i<7;++i) {JsonObject stop=text["bind"]["color"]["stops"].add<JsonObject>();stop["at"]=31+i;stop["value"]="#ffffff";}rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][0]["at"]="bad";rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][0]=42;rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][0]["unknown"]=1;rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][0].remove("at");rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][0].remove("value");rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"]=42;rejected(d);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][1]["at"]=0;serializeJson(d,json);
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].bind.color.stops[1].at:")==0);
  dataDocument(d);text=d["layers"][0];colorBinding(text,"color");text["bind"]["color"]["stops"][0]["value"]="red";rejected(d);

  // Bindings cannot enable properties that a layer did not declare or support.
  dataDocument(d);text=d["layers"][0];text.clear();text["id"]="image";text["type"]="image";text["x"]=0;text["y"]=0;text["source"]="image.sti";numericBinding(text,"width");rejected(d);
  dataDocument(d);rectangle(d["layers"][0]);text=d["layers"][0];colorBinding(text,"stroke");rejected(d);
  dataDocument(d);rectangle(d["layers"][0]);text=d["layers"][0];text.remove("fill");text["stroke"]="#ffffff";colorBinding(text,"fill");rejected(d);
  dataDocument(d);text=d["layers"][0];numericBinding(text,"scroll.width");rejected(d);

  // Object-member uniqueness must be enforced before ArduinoJson coalesces duplicate keys.
  const char* duplicateBinding=R"({"spec":1,"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"data":[{"id":"weather","url":"https://example.test/weather.json","interval":60,"insecureTls":true,"fields":[{"id":"temp","path":"main.temp"}]}],"layers":[{"id":"value","type":"text","x":0,"y":0,"value":"{weather.temp}","size":16,"color":"#ffffff","bind":{"x":{"source":"weather.temp","input":[0,40],"output":[0,200]},"x":{"source":"weather.temp","input":[0,40],"output":[20,220]}}}]})";
  assert(!parseTheme(duplicateBinding,theme,error));assert(error.find("layers[0].bind.x:")==0);
  json=duplicateBinding;json.replace(json.rfind("\"x\""),3,"\"\\u0078\"");
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].bind.x:")==0);
  json.replace(json.find("\"bind\""),6,"\"b\\u0069nd\"");
  json.replace(json.find("\"layers\""),8,"\"lay\\u0065rs\"");
  assert(!parseTheme(json,theme,error));assert(error.find("layers[0].bind.x:")==0);
  // ArduinoJson accepts single quotes; an unsupported scanner spelling must not bypass validation.
  json=duplicateBinding;json.replace(json.find("\"name\""),6,"'name'");
  assert(!parseTheme(json,theme,error));
  json=std::string("{\"ignored\":")+std::string(4000,'[')+"0"+std::string(4000,']')+"}";
  assert(!parseTheme(json,theme,error));
  // A regular escaped key remains accepted when it is unique.
  dataDocument(d);numericBinding(d["layers"][0],"x");serializeJson(d,json);
  json.replace(json.rfind("\"x\""),3,"\"\\u0078\"");
  assert(parseTheme(json,theme,error));assert(theme.layers[0].bindings.size()==1);

  // Behaviour that must survive reading layers one at a time (issue #30).
  // `data` may follow the layers that use it.
  json=R"({"spec":1,"layers":[{"id":"value","type":"text","x":0,"y":0,"value":"{weather.temp}","size":16,"color":"#ffffff"}],"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"data":[{"id":"weather","url":"http://example.test/w.json","interval":60,"fields":[{"id":"temp","path":"main.temp"}]}]})";
  assert(parseTheme(json,theme,error));assert(theme.layers.size()==1 && theme.data.size()==1);
  // A repeated `layers` key keeps the last list, as ArduinoJson does for any repeated key.
  json=std::string(base);json.insert(json.find("\"layers\""),R"("layers":[{"id":"a","type":"text","x":0,"y":0,"value":"A","size":8,"color":"#ffffff"},{"id":"b","type":"text","x":0,"y":0,"value":"B","size":8,"color":"#ffffff"}],)");
  assert(parseTheme(json,theme,error));assert(theme.layers.size()==1 && theme.layers[0].value=="{HH}:{MM}");
  // Syntax errors inside a layer name the absolute byte.
  json=base;json.insert(json.find("\"size\":48")+9,",}");
  assert(!parseTheme(json,theme,error));
  assert(error==std::string("theme.json: InvalidInput at byte ")+std::to_string(json.find(",}")+2));
  // Unknown top-level fields are still named.
  json=base;json.insert(1,R"("extra":1,)");
  assert(!parseTheme(json,theme,error));assert(error=="extra: unknown field");
  // Non-object layers and too many layers are still rejected with the same messages.
  json=base;json.insert(json.find("[{")+1,"7,");
  assert(!parseTheme(json,theme,error));assert(error=="layers[0]: expected an object");
  {deserializeJson(d,base);JsonObject first=d["layers"][0];JsonDocument copy;copy.set(first);
   for(int i=1;i<33;++i){JsonObject l=d["layers"].add<JsonObject>();l.set(copy.as<JsonObjectConst>());l["id"]=std::string("t")+std::to_string(i);}
   serializeJson(d,json);assert(!parseTheme(json,theme,error));assert(error=="layers: expected an array of at most 32 layers");}
  json=base;json.replace(json.find("[{"),1,"{\"x\":");json.replace(json.rfind("]"),1,"}");
  assert(!parseTheme(json,theme,error));assert(error=="layers: expected an array of at most 32 layers");
  json=base;json+=" x";assert(!parseTheme(json,theme,error));assert(error=="theme.json: trailing content after manifest");
  // Nesting still stops at eight levels in all, counting the root object and the layers array.
  json=base;json.insert(json.find("\"size\":48"),"\"deep\":"+std::string(6,'[')+std::string(6,']')+",");
  assert(!parseTheme(json,theme,error));assert(error.find("theme.json: TooDeep at byte ")==0);
  json=base;json.insert(json.find("\"size\":48"),"\"deep\":"+std::string(5,'[')+std::string(5,']')+",");
  assert(!parseTheme(json,theme,error));assert(error=="layers[0].deep: unknown field");

  // Rounded corners are rectangle-only, integral, and bounded.
  deserializeJson(d,base);d["layers"][0]["cornerRadius"]=8;rejected(d);
  dataDocument(d);rectangle(d["layers"][0]);d["layers"][0]["cornerRadius"]="8";rejected(d);
  dataDocument(d);rectangle(d["layers"][0]);d["layers"][0]["cornerRadius"]=121;rejected(d);
  tm t{};t.tm_hour=12;t.tm_year=100;t.tm_mon=0;t.tm_mday=1;t.tm_wday=6;
  assert(expandText("{hh} {HH} {DD} {MONTH} {WEEKDAY} {YYYY}",&t)=="12 12 01 January Saturday 2000");
  t.tm_hour=23;assert(expandText("{hh}",&t)=="11");assert(expandText("{HH}:{MM}",nullptr)=="--:--");
  std::cout<<"theme validation tests passed\n";
}
