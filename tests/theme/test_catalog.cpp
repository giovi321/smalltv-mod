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
  source.data.resize(10);
  auto truncated=inspectInstalledTheme(source,"pixel-room");assert(!truncated.valid&&!truncated.error.empty());
  std::cout<<"theme catalog tests passed\n";
}
