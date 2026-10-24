#include "ThemeCatalog.h"
#include <cassert>
#include <fstream>
#include <cstring>
#include <iostream>
using namespace smalltv;
struct Memory : Source {
  std::string data;
  uint32_t size() const override {return data.size();}
  bool read(uint32_t offset,void* out,size_t n) override {
    if(offset>data.size()||n>data.size()-offset)return false;
    memcpy(out,data.data()+offset,n);return true;
  }
};
int main() {
  Memory source;source.data="broken package";
  auto broken=inspectInstalledTheme(source,"broken-clock");
  assert(!broken.valid&&broken.id=="broken-clock"&&broken.name=="broken-clock");
  assert(!broken.error.empty()&&broken.bytes==14);
  std::ifstream in("examples/themes/pixel-room.stheme",std::ios::binary);
  source.data.assign(std::istreambuf_iterator<char>(in),{});
  auto good=inspectInstalledTheme(source,"pixel-room");
  assert(good.valid&&good.id=="pixel-room"&&good.name=="Pixel Room"&&good.error.empty());
  auto mismatch=inspectInstalledTheme(source,"other-name");
  assert(!mismatch.valid&&mismatch.id=="other-name"&&mismatch.error.find("ID")!=std::string::npos);
  // listing sidecar: round trip, and every mismatch falls back to inspecting the package
  Memory meta;meta.data=themeMetaJson(good);InstalledTheme listed;
  assert(readThemeMeta(meta,"pixel-room",good.bytes,listed));
  assert(listed.valid&&listed.id=="pixel-room"&&listed.name=="Pixel Room"&&listed.author==good.author&&listed.version==good.version&&listed.bytes==good.bytes&&listed.error.empty());
  assert(!readThemeMeta(meta,"pixel-room",good.bytes+1,listed));
  assert(!readThemeMeta(meta,"Bad Id",good.bytes,listed));
  std::string json=meta.data;
  meta.data.replace(meta.data.find("\"revision\":1"),12,"\"revision\":2");assert(!readThemeMeta(meta,"pixel-room",good.bytes,listed));
  meta.data=json.substr(0,json.size()-2);assert(!readThemeMeta(meta,"pixel-room",good.bytes,listed));
  meta.data="";assert(!readThemeMeta(meta,"pixel-room",good.bytes,listed));
  meta.data=std::string(MaxMeta+1,' ');assert(!readThemeMeta(meta,"pixel-room",good.bytes,listed));
  meta.data="{\"revision\":1,\"bytes\":"+std::to_string(good.bytes)+",\"name\":\"x\",\"author\":\"y\"}";assert(!readThemeMeta(meta,"pixel-room",good.bytes,listed));
  meta.data="{\"revision\":1,\"bytes\":"+std::to_string(good.bytes)+",\"name\":\""+std::string(97,'n')+"\",\"author\":\"y\",\"version\":\"1\"}";assert(!readThemeMeta(meta,"pixel-room",good.bytes,listed));
  meta.data=json;meta.data.resize(meta.data.size()+40,' ');assert(readThemeMeta(meta,"pixel-room",good.bytes,listed));  // reads past one 32-byte buffer
  source.data.resize(10);
  auto truncated=inspectInstalledTheme(source,"pixel-room");assert(!truncated.valid&&!truncated.error.empty());
  std::cout<<"theme catalog tests passed\n";
}
