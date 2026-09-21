#include "ThemePackage.h"
#include <fstream>
#include <cassert>
#include <iostream>
#include <font/glcdfont.h>
using namespace smalltv;
struct Disk : Source {
 std::ifstream file;uint32_t length;
 explicit Disk(const char* path):file(path,std::ios::binary){assert(file);file.seekg(0,std::ios::end);length=file.tellg();}
 uint32_t size() const override {return length;}
 bool read(uint32_t offset,void* dest,size_t n) override {file.clear();file.seekg(offset);file.read(static_cast<char*>(dest),n);return bool(file);}
};
struct Screen : Display {
 uint16_t pixels[240*240]{};
 uint8_t glyphColumn(uint8_t c,uint8_t col) const override {return font[c*5+col];}
 void row(int x,int y,const uint16_t* colors,int n) override {for(int i=0;i<n;++i)pixels[y*240+x+i]=colors[i];}
};
int main(int argc,char** argv) {
 assert(argc==2);Disk disk(argv[1]);Package package(disk);Theme theme;std::string error;
 if(!package.load(theme,error)){std::cerr<<error<<'\n';return 1;}
 Engine engine;engine.setTheme(theme,0);Screen screen;tm t{};t.tm_hour=10;t.tm_min=24;t.tm_mday=17;t.tm_mon=8;t.tm_wday=4;t.tm_year=126;
 assert(render(engine,engine.update(0,&t),package,screen));
 std::ofstream out("/tmp/smalltv-theme-preview.ppm",std::ios::binary);out<<"P6\n240 240\n255\n";
 for(uint16_t c:screen.pixels){out.put((c>>11)*255/31);out.put(((c>>5)&63)*255/63);out.put((c&31)*255/31);}
 for(unsigned ms=125;ms<300000;ms+=125) assert(render(engine,engine.update(ms,&t),package,screen));
 std::cout<<theme.name<<": validated and rendered 5 minutes of animation\n";
}
