// The bebas-clock example before the clock is set (or a data field has never
// been fetched): the engine expands {HH}:{MM} to "--:--". The fonts keep only
// the digits and ':', so '-' is a character the font lacks, and it is drawn as
// the fallback glyph, which the packer takes to be the lowest kept character,
// '0'. The screen then shows 00:00 instead of a placeholder.
// Run from the repository root.
#include "ThemePackage.h"
#include <fstream>
#include <iostream>
using namespace smalltv;
struct Disk : Source {
  std::ifstream file;uint32_t length;
  explicit Disk(const char* path):file(path,std::ios::binary){file.seekg(0,std::ios::end);length=file.tellg();}
  uint32_t size() const override {return length;}
  bool read(uint32_t offset,void* dest,size_t n) override {file.clear();file.seekg(offset);file.read(static_cast<char*>(dest),n);return bool(file);}
};
int main() {
  Disk disk("examples/themes/bebas-clock.stheme");
  Package p(disk);Theme t;std::string error;
  if(!p.load(t,error)) {std::cerr<<"load: "<<error<<"\n";return 2;}
  int failures=0;
  for(const ThemeFont& f:t.fonts) {
    const unsigned char g=f.glyph('-');
    if(g>='0'&&g<='9') {
      std::cerr<<"FAIL font '"<<f.name<<"': '-' is drawn as '"<<char(g)<<"'\n";
      ++failures;
    }
  }
  std::cout<<(failures?"unsynced clock: FAILED ":"unsynced clock passed ")<<failures<<" fonts\n";
  return failures?1:0;
}
