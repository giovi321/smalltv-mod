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
File upload;
String uploadError;
bool uploadAllowed=false, uploadEnded=false;
unsigned uploadParts=0;
uint32_t uploaded=0, uploadBudget=0;
constexpr unsigned MaxThemes=16;
const char* staging="/themes/.upload";
void reply(int code,const String& message) {
  JsonDocument doc;doc[code<300?"id":"error"]=message;
  String json;serializeJson(doc,json);server->send(code,"application/json",json);
}
unsigned installedCount() {
  unsigned count=0;File dir=LittleFS.open("/themes");
  for(File f=dir.openNextFile();f;f=dir.openNextFile()) if(!f.isDirectory()&&String(f.name()).endsWith(".stheme")) ++count;
  return count;
}
void resetUpload() {
  upload.close();LittleFS.remove(staging);uploadParts=0;uploadAllowed=false;uploadEnded=false;uploaded=0;
}
void receiveUpload() {
  HTTPUpload& up=server->upload();
  if(up.status==UPLOAD_FILE_START) {
    ++uploadParts;
    if(uploadParts>1) {uploadError="Upload exactly one package";upload.close();return;}
    uploadError="";uploaded=0;uploadEnded=false;
    uploadAllowed=!settings->auth.enabled||!settings->auth.pass.length()||server->authenticate(settings->auth.user.c_str(),settings->auth.pass.c_str());
    if(!uploadAllowed) return;
    if(!up.filename.endsWith(".stheme")) {uploadError="Expected a .stheme file";return;}
    if(installedCount()>=MaxThemes) {uploadError="Remove a theme first (limit 16)";return;}
    LittleFS.remove(staging);
    // Preserve room for an atomic settings replacement and filesystem metadata.
    JsonDocument config;settingsToJson(*settings,config.to<JsonObject>(),true);
    size_t reserve=measureJson(config)+4096;
    if(reserve<16384) reserve=16384;
    size_t free=LittleFS.totalBytes()-LittleFS.usedBytes();
    if(free<=reserve) {uploadError="Not enough storage; remove a theme first";return;}
    uploadBudget=std::min<size_t>(smalltv::MaxPackage,free-reserve);
    upload=LittleFS.open(staging,"w");
    if(!upload) uploadError="Cannot create staging file";
  } else if(up.status==UPLOAD_FILE_WRITE) {
    if(!uploadAllowed||uploadError.length()) return;
    if(!upload||up.currentSize>uploadBudget-uploaded) {uploadError="Package exceeds available storage or 3 MiB";upload.close();return;}
    if(upload.write(up.buf,up.currentSize)!=up.currentSize) {uploadError="Not enough storage";upload.close();return;}
    uploaded+=up.currentSize;
  } else if(up.status==UPLOAD_FILE_END) {
    upload.close();uploadEnded=uploadAllowed&&!uploadError.length()&&uploaded==up.totalSize;
    if(!uploadEnded&&!uploadError.length()) uploadError="Incomplete upload";
  } else if(up.status==UPLOAD_FILE_ABORTED) {
    resetUpload();uploadError="Upload aborted";
  }
}
void finishUpload() {
  if(!auth()) {resetUpload();return;}
  upload.close();
  if(!uploadAllowed||!uploadEnded||uploadError.length()) {
    reply(400,uploadError.length()?uploadError:String("No complete package uploaded"));resetUpload();return;
  }
  smalltv::Theme theme;std::string error;
  bool valid=false;
  {ThemeFile file;
    if(file.open(staging)) {smalltv::Package package(file);valid=package.load(theme,error);}
    else error="Cannot read uploaded package";
  }
  if(!valid) {reply(400,error.c_str());resetUpload();return;}
  String target="/themes/"+String(theme.id.c_str())+".stheme";
  if(LittleFS.exists(target)) {reply(409,"Theme ID already installed; remove it or use a new ID");resetUpload();return;}
  if(!LittleFS.rename(staging,target)) {reply(500,"Cannot publish package");resetUpload();return;}
  reply(201,theme.id.c_str());resetUpload();
}
bool requestId(String& id) {
  if(!server->hasArg("plain")||server->arg("plain").length()>128) {reply(400,"Expected JSON with id");return false;}
  JsonDocument doc;
  if(deserializeJson(doc,server->arg("plain"))||!doc["id"].is<const char*>()) {reply(400,"Expected JSON with id");return false;}
  id=doc["id"].as<String>();
  if(!smalltv::validId(id.c_str())||id.length()!=strlen(id.c_str())) {reply(400,"Invalid theme ID");return false;}
  return true;
}
void listThemes() {
  if(!auth()) return;
  JsonDocument doc;doc["selected"]=settings->themeId;doc["error"]=g_themeMode.error();
  doc["freeBytes"]=LittleFS.totalBytes()-LittleFS.usedBytes();
  JsonArray list=doc["themes"].to<JsonArray>();
  File dir=LittleFS.open("/themes");unsigned count=0;
  for(File entry=dir.openNextFile();entry&&count<MaxThemes;entry=dir.openNextFile()) {
    String name=entry.name();
    if(entry.isDirectory()||!name.endsWith(".stheme")) continue;
    int slash=name.lastIndexOf('/');if(slash>=0) name=name.substring(slash+1);
    String id=name.substring(0,name.length()-7);
    if(!smalltv::validId(id.c_str())) continue;
    ThemeFile file;file.open("/themes/"+name);
    auto info=smalltv::inspectInstalledTheme(file,id.c_str());
    JsonObject o=list.add<JsonObject>();
    o["id"]=info.id.c_str();o["name"]=info.name.c_str();
    o["author"]=info.author.c_str();o["version"]=info.version.c_str();
    o["bytes"]=entry.size();o["valid"]=info.valid;o["error"]=info.error.c_str();++count;
    yield();
  }
  String json;serializeJson(doc,json);server->send(200,"application/json",json);
}
void selectTheme() {
  if(!auth()) return;
  String id;if(!requestId(id)) return;
  {ThemeFile file;smalltv::Theme theme;std::string error;
    if(!file.open("/themes/"+id+".stheme")) {reply(404,"Theme not installed");return;}
    smalltv::Package package(file);
    if(!package.load(theme,error)||theme.id!=id.c_str()) {reply(400,"Invalid installed theme");return;}
  }
  String oldId=settings->themeId;uint8_t oldMode=settings->mode;
  settings->themeId=id;settings->mode=MODE_THEME;
  if(!saveSettings(*settings)) {settings->themeId=oldId;settings->mode=oldMode;reply(500,"Could not save selection");return;}
  clockReapply(*settings);appInvalidate();reply(200,id);
}
void deleteTheme() {
  if(!auth()) return;
  String id;if(!requestId(id)) return;
  String path="/themes/"+id+".stheme";
  if(!LittleFS.exists(path)) {reply(404,"Theme not found");return;}
  if(settings->themeId==id && settings->mode==MODE_THEME) {
    ThemeFile file;file.open(path);
    auto info=smalltv::inspectInstalledTheme(file,id.c_str());
    if(info.valid) {reply(409,"Switch away from theme mode before removing this theme");return;}
  }
  if(settings->themeId==id) {
    settings->themeId="";
    if(!saveSettings(*settings)) {settings->themeId=id;reply(500,"Could not clear saved selection");return;}
    appInvalidate();
  }
  if(!LittleFS.remove(path)) {reply(404,"Theme not found or could not be removed");return;}
  reply(200,id);
}
}
void themeWebBegin(WebServerClass& web,Settings& s,bool (*requireAuth)()) {
  server=&web;settings=&s;auth=requireAuth;
  web.on("/api/themes",HTTP_GET,listThemes);
  web.on("/api/themes/install",HTTP_POST,finishUpload,receiveUpload);
  web.on("/api/themes/select",HTTP_POST,selectTheme);
  web.on("/api/themes/delete",HTTP_POST,deleteTheme);
}
#endif
