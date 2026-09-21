// Desktop adapter for the exact firmware parser, package reader and compositor.
#include "ThemeEngine.h"
#include "ThemePackage.h"
#include <ArduinoJson.h>
#include <font/glcdfont.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
class Disk : public smalltv::Source {
 public:
  explicit Disk(const char* path):file_(path,std::ios::binary) {
    if(!file_)throw std::runtime_error(std::string("Cannot open ")+path);
    file_.seekg(0,std::ios::end);
    auto size=file_.tellg();
    if(size<0||size>smalltv::MaxPackage)throw std::runtime_error("Package exceeds 3 MiB");
    length_=static_cast<uint32_t>(size);
  }
  uint32_t size() const override {return length_;}
  bool read(uint32_t offset,void* dest,size_t count) override {
    if(offset>length_||count>length_-offset)return false;
    file_.clear();file_.seekg(offset);file_.read(static_cast<char*>(dest),count);return bool(file_);
  }
 private:
  std::ifstream file_;uint32_t length_=0;
};
class Screen : public smalltv::Display {
 public:
  uint8_t glyphColumn(uint8_t c,uint8_t col) const override {return font[unsigned(c)*5+col];}
  void row(int x,int y,const uint16_t* pixels,int count) override {
    std::copy(pixels,pixels+count,pixels_+y*240+x);
  }
  void write(std::ostream& out) const {
    char row[240*3];
    for(int y=0;y<240;++y) {
      for(int x=0;x<240;++x) {
        uint16_t c=pixels_[y*240+x];
        row[x*3]=((c>>11)*255)/31;row[x*3+1]=(((c>>5)&63)*255)/63;row[x*3+2]=((c&31)*255)/31;
      }
      out.write(row,sizeof(row));
    }
  }
 private:
  uint16_t pixels_[240*240]{}; // Desktop only; firmware remains scanline based.
};
void metadata(const smalltv::Theme& theme) {
  JsonDocument doc;doc["id"]=theme.id;doc["name"]=theme.name;doc["author"]=theme.author;doc["version"]=theme.version;
  serializeJson(doc,std::cout);std::cout<<'\n';
}
uint64_t integer(const char* text,uint64_t maximum) {
  std::string value(text);
  if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Expected a nonnegative integer");
  uint64_t n=std::stoull(value);if(n>maximum)throw std::runtime_error("Preview argument exceeds limit");return n;
}
void u16(std::ostream& out,unsigned n) {out.put(char(n));out.put(char(n>>8));}
}
int main(int argc,char** argv) {
  try {
    if(argc<3)throw std::runtime_error("Expected validate-manifest, validate, or preview and a file");
    std::string command=argv[1];Disk file(argv[2]);smalltv::Theme theme;std::string error;
    if(command=="validate-manifest"&&argc==3) {
      if(file.size()>smalltv::MaxManifest)throw std::runtime_error("theme.json: manifest exceeds 16 KiB");
      std::string json(file.size(),'\0');
      if(!file.read(0,&json[0],json.size())||!smalltv::parseTheme(json,theme,error))throw std::runtime_error(error.empty()?"Cannot read manifest":error);
      metadata(theme);return 0;
    }
    smalltv::Package package(file);
    if(!package.load(theme,error))throw std::runtime_error(error);
    if(command=="validate"&&argc==3) {metadata(theme);return 0;}
    if(command!="preview"||argc!=7)throw std::runtime_error("Expected preview PACKAGE OUTPUT EPOCH FPS FRAMES");
    uint64_t epoch=integer(argv[4],253402300739ULL);
    unsigned fps=integer(argv[5],15),frames=integer(argv[6],900);
    if(!fps||!frames||frames>60*fps)throw std::runtime_error("Preview is limited to 1..15 FPS and 60 seconds");
    std::ofstream out(argv[3],std::ios::binary);
    if(!out)throw std::runtime_error("Cannot create preview frames");
    out.write("STP1",4);u16(out,240);u16(out,240);u16(out,fps);u16(out,frames);
    smalltv::Engine engine;engine.setTheme(std::move(theme),0);Screen screen;
    for(unsigned i=0;i<frames;++i) {
      // Sample at the first whole millisecond on/after the frame deadline.
      // Flooring 1000/15 would repeatedly sample before a 15 FPS frame is due.
      uint32_t ms=(i*1000U+fps-1)/fps;time_t now=static_cast<time_t>(epoch+ms/1000);
      tm* time=std::gmtime(&now);if(!time)throw std::runtime_error("Preview time is out of range");
      if(!smalltv::render(engine,engine.update(ms,time),package,screen))throw std::runtime_error("Could not read theme asset");
      screen.write(out);
    }
    out.close();if(!out)throw std::runtime_error("Could not write preview frames");
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
  }
}
