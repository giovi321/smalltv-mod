#include "AtomicJson.h"
#include "SettingsTransaction.h"
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <iostream>
struct FakeFS {
  std::map<std::string,std::string> files;
  size_t budget=10000;
  bool renameFails=false;
  struct Handle {
    FakeFS* fs;std::string path;bool failed;
  };
  struct File {
    std::shared_ptr<Handle> h;
    explicit operator bool() const {return bool(h);}
    size_t write(const uint8_t* bytes,size_t n) {
      size_t count=std::min(n,h->fs->budget);
      h->fs->budget-=count;h->fs->files[h->path].append(reinterpret_cast<const char*>(bytes),count);
      if(count!=n) h->failed=true;
      return count;
    }
    size_t write(uint8_t byte) {return write(&byte,1);}
    void flush() {}
    size_t size() const {return h->fs->files[h->path].size();}
    bool getWriteError() const {return h->failed;}
    void close() {}
  };
  File open(const char* path,const char*) {files[path]="";return File{std::make_shared<Handle>(Handle{this,path,false})};}
  bool rename(const char* source,const char* target) {if(renameFails)return false;files[target]=files[source];files.erase(source);return true;}
  bool remove(const char* path) {return files.erase(path);}
};
int main() {
  JsonDocument config;config["wifi"]="working-network";config["themeId"]="pixel-room";
  FakeFS fs;fs.files["/config.json"]="old-settings";fs.budget=5;
  assert(!saveJsonAtomically(fs,config,"/config.json","/config.json.tmp"));
  assert(fs.files["/config.json"]=="old-settings");assert(!fs.files.count("/config.json.tmp"));
  fs.budget=10000;fs.renameFails=true;
  assert(!saveJsonAtomically(fs,config,"/config.json","/config.json.tmp"));assert(fs.files["/config.json"]=="old-settings");
  fs.renameFails=false;
  assert(saveJsonAtomically(fs,config,"/config.json","/config.json.tmp"));
  JsonDocument saved;assert(!deserializeJson(saved,fs.files["/config.json"]));assert(saved["themeId"]=="pixel-room");assert(!fs.files.count("/config.json.tmp"));
  struct State { int value; } state{1};
  bool savedState=false;
  assert(!applyAndSaveSettings(state,[](State& s){s.value=2;},[&](const State&){return savedState;}));
  assert(state.value==1);
  savedState=true;
  assert(applyAndSaveSettings(state,[](State& s){s.value=3;},[&](const State& s){return savedState&&s.value==3;}));
  assert(state.value==3);
  std::cout<<"atomic settings storage tests passed\n";
}
