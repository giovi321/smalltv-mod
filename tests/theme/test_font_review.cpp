// Review tests for the package fonts (STF, spec 2): what the first font tests
// do not pin down. Each check prints "FAIL <name>" and the run goes on, so one
// run shows every gap; the exit status is the number of failures.
#include "ThemePackage.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
using namespace smalltv;
static int failures = 0;
#define CHECK(name, cond) do { if (!(cond)) { ++failures; std::cerr << "FAIL " << name << " (line " << __LINE__ << ")\n"; } } while (0)
struct Memory : Source {
  std::string data;
  uint32_t size() const override {return data.size();}
  bool read(uint32_t offset,void* dest,size_t count) override {
    if(offset>data.size()||count>data.size()-offset) return false;
    memcpy(dest,data.data()+offset,count);return true;
  }
};
struct Disk : Source {
  std::ifstream file;uint32_t length;
  explicit Disk(const char* path):file(path,std::ios::binary){file.seekg(0,std::ios::end);length=file.tellg();}
  uint32_t size() const override {return length;}
  bool read(uint32_t offset,void* dest,size_t n) override {file.clear();file.seekg(offset);file.read(static_cast<char*>(dest),n);return bool(file);}
};
struct Pixels : Display {
  uint16_t data[240*240]{};
  bool outside = false;
  uint8_t glyphColumn(uint8_t,uint8_t) const override {return 0;}
  void row(int x,int y,const uint16_t* p,int n) override {
    if(x<0||y<0||x+n>240||y>=240) {outside=true;return;}
    for(int i=0;i<n;++i)data[y*240+x+i]=p[i];
  }
};
static void u16(std::string& s,unsigned n) {s+=char(n);s+=char(n>>8);}
static void u32(std::string& s,unsigned n) {u16(s,n);u16(s,n>>16);}
static void entry(std::string& s,const std::string& path,const std::string& data) {u16(s,path.size());u32(s,data.size());s+=path;s+=data;}
static std::string package(const std::string& json,const std::string& fontPath,const std::string& font) {
  std::string s="STH1";u16(s,2);u16(s,0);
  entry(s,"theme.json",json);
  entry(s,fontPath,font);
  return s;
}
static std::string manifest(const std::string& layers,const std::string& extra="") {
  return "{\"spec\":2,\"theme\":{\"id\":\"font\",\"name\":\"Font\",\"author\":\"Me\",\"version\":\"1\"},"
    "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"},\"layers\":["+layers+"]"+extra+"}";
}
static void record(std::string& s,unsigned offset,unsigned advance,unsigned left,unsigned top,unsigned width,unsigned height) {
  u16(s,offset);s+=char(advance);s+=char(left);s+=char(top);s+=char(width);s+=char(height);s+='\0';
}
// The font of test_font.cpp: line 8, 'A' 4 wide with a 3x2 ink at (1, 2),
// 'B' no glyph, 'C' a 2 pixel space, fallback 'A'.
static const uint8_t inkA[2][3]={{15,8,0},{3,15,15}};
static std::string font() {
  std::string s="STF1";s+=char(8);s+=char(6);s+='A';s+=char(3);s+='A';s.append(3,'\0');
  record(s,0,4,1,2,3,2);
  record(s,0,0,0,0,0,0);
  record(s,0,2,0,0,0,0);
  s+=char(0xf8);s+='\0';s+=char(0x3f);s+=char(0xf0);
  return s;
}
static uint16_t over(uint16_t below,unsigned c) {
  const unsigned a=c*17,b=255-a;
  return (((31*a+(below>>11)*b+127)/255)<<11)|(((63*a+((below>>5)&63)*b+127)/255)<<5)|((31*a+(below&31)*b+127)/255);
}
static std::string textLayer(const std::string& extra) {
  return R"({"id":"t","type":"text","value":"ABC","font":"test","color":"#ffffff")"+extra+"}";
}
// A reference of the font above: every byte but 'C' draws 'A' (4 wide); 'C' is a space (2 wide).
static int advanceOf(unsigned char c) {return c=='C'?2:4;}
static void model(uint16_t* screen,const std::string& text,int x,int y) {
  int pen=x;
  for(unsigned char c:text) {
    if(c!='C') for(int row=0;row<2;++row) for(int col=0;col<3;++col) {
      const int px=pen+1+col,py=y+2+row;
      if(px>=0&&px<240&&py>=0&&py<240) screen[py*240+px]=over(screen[py*240+px],inkA[row][col]);
    }
    pen+=advanceOf(c);
  }
}
static int widthOf(const std::string& text) {int w=0;for(unsigned char c:text) w+=advanceOf(c);return w;}
struct Loaded {
  Memory source;Package p;Theme t;Engine e;Pixels d;std::string error;bool ok;
  Loaded(const std::string& layers,const std::string& extra="",const std::string& stf=font()):p(source) {
    source.data=package(manifest(layers,extra),"fonts/test.stf",stf);
    ok=p.load(t,error);
    if(ok) e.setTheme(t,0);
  }
};
static bool same(const Pixels& d,const uint16_t* expected,const char* name) {
  for(int i=0;i<240*240;++i) if(d.data[i]!=expected[i]) {
    std::cerr<<"  "<<name<<": pixel "<<i%240<<","<<i/240<<" is "<<d.data[i]<<", expected "<<expected[i]<<"\n";
    return false;
  }
  return true;
}
// 1. Text that runs off the left and right edge of the screen, and past it.
static void testEdges() {
  for(int x:{-6,-2,-1,0,233,234,236,238,239}) {
    Loaded l(textLayer(",\"x\":"+std::to_string(x)+",\"y\":30"));
    CHECK("edges: loads",l.ok);
    if(!l.ok) continue;
    Loaded& f=l;
    f.e.update(0,nullptr);
    CHECK("edges: render ok x="+std::to_string(x),render(f.e,{Rect(0,0,240,240)},f.p,f.d));
    CHECK("edges: nothing written outside the screen x="+std::to_string(x),!f.d.outside);
    uint16_t expected[240*240]{};
    model(expected,"ABC",x,30);
    CHECK("edges: pixels x="+std::to_string(x),same(f.d,expected,"edges"));
  }
}
// 2. Anchors: the text rectangle moves with the anchor and the pixels follow.
static void testAnchors() {
  const char* anchors[]={"top-left","top-center","top-right","center-left","center","center-right","bottom-left","bottom-center","bottom-right"};
  for(int a=0;a<9;++a) {
    Loaded l(textLayer(std::string(",\"x\":100,\"y\":100,\"anchor\":\"")+anchors[a]+"\""));
    l.e.update(0,nullptr);
    CHECK(std::string("anchor loads ")+anchors[a],l.ok&&render(l.e,{Rect(0,0,240,240)},l.p,l.d));
    const int w=widthOf("ABC"),x=100-w*(a%3)/2,y=100-8*(a/3)/2;
    const Rect b=l.e.states()[0].bounds;
    CHECK(std::string("anchor bounds ")+anchors[a],b.x==x&&b.y==y&&b.w==w&&b.h==8);
    uint16_t expected[240*240]{};
    model(expected,"ABC",x,y);
    CHECK(std::string("anchor pixels ")+anchors[a],same(l.d,expected,anchors[a]));
  }
}
// 3. Fetched values: bytes the font does not have, nothing, and a lot.
static void testValues() {
  const std::string data=R"(,"data":[{"id":"d","url":"http://example.com/","interval":60,"fields":[{"id":"v","path":"v"}]}])";
  const std::string layer=R"({"id":"t","type":"text","x":5,"y":10,"value":"{d.v}","font":"test","color":"#ffffff"})";
  const std::string values[]={std::string("\xc2\xb0""C"),std::string("\x01\x7f\xff""C"),std::string(),std::string(300,'A'),std::string(1,'\0')+"AC"};
  for(size_t n=0;n<5;++n) {
    Loaded l(layer,data);
    CHECK("values: loads",l.ok);
    if(!l.ok) continue;
    Loaded& f=l;
    f.e.setValues({{"d.v",values[n]}});
    f.e.update(0,nullptr);
    CHECK("values: render ok "+std::to_string(n),render(f.e,{Rect(0,0,240,240)},f.p,f.d));
    CHECK("values: inside the screen "+std::to_string(n),!f.d.outside);
    uint16_t expected[240*240]{};
    model(expected,values[n],5,10);
    CHECK("values: pixels "+std::to_string(n),same(f.d,expected,"values"));
  }
}
// 4. A dirty rectangle redraws exactly what a full redraw has there, also
// for a scrolling text (looping and not), at every time step.
static void testDirtyRects() {
  const char* scrolls[]={
    R"(,"scroll":{"width":30,"speed":20,"pause":0,"mode":"loop","gap":5})",
    R"(,"scroll":{"width":30,"speed":20,"pause":0,"mode":"bounce"})",
    R"(,"scroll":{"width":90,"speed":20,"pause":0,"mode":"loop","gap":3})",
    ""};
  std::mt19937 random(1);
  for(const char* scroll:scrolls) {
    const std::string layer=R"({"id":"t","type":"text","x":20,"y":50,"value":"ACAACAAAC","font":"test","color":"#ffffff")"+std::string(scroll)+"}";
    Loaded l(layer);
    CHECK("dirty: loads",l.ok);
    if(!l.ok) continue;
    bool mismatch=false;
    for(unsigned ms=0;ms<4000&&!mismatch;ms+=125) {
      auto dirty=l.e.update(ms,nullptr);
      Pixels full;
      if(!render(l.e,{Rect(0,0,240,240)},l.p,full)) {CHECK("dirty: full render",false);break;}
      for(int k=0;k<8;++k) {
        Pixels part;
        for(auto& px:part.data) px=0x1234;
        const int x=random()%140,y=44+random()%12,w=1+random()%100,h=1+random()%6;
        if(!render(l.e,{Rect(x,y,w,h)},l.p,part)) {CHECK("dirty: render",false);mismatch=true;break;}
        for(int yy=y;yy<y+h&&!mismatch;++yy) for(int xx=x;xx<x+w;++xx)
          if(part.data[yy*240+xx]!=full.data[yy*240+xx]) {
            std::cerr<<"  dirty: scroll '"<<scroll<<"' t="<<ms<<" rect "<<x<<","<<y<<" "<<w<<"x"<<h<<" differs at "<<xx<<","<<yy<<"\n";
            mismatch=true;break;
          }
      }
      (void)dirty;
    }
    CHECK(std::string("dirty: partial == full for scroll '")+scroll+"'",!mismatch);
  }
}
// 5. Which glyph is drawn for a byte, in a font whose first code point is not
// its fallback (test_font.cpp's font has both at 'A').
static void testGlyphLookup() {
  ThemeFont f;f.first='A';f.fallback='C';f.advances={4,0,2};
  CHECK("glyph: the first code point is its own glyph",f.glyph('A')=='A'&&f.advance('A')==4);
  CHECK("glyph: a code point without a glyph falls back",f.glyph('B')=='C'&&f.advance('B')==2);
  CHECK("glyph: bytes outside the font fall back",f.glyph('@')=='C'&&f.glyph('D')=='C'&&f.glyph(0)=='C'&&f.glyph(255)=='C');
}
// 6. Not a pass/fail check: how many storage reads one redraw of the example costs.
struct Counting : Disk {
  unsigned reads=0,bytes=0;
  using Disk::Disk;
  bool read(uint32_t offset,void* dest,size_t n) override {++reads;bytes+=n;return Disk::read(offset,dest,n);}
};
static void measureReads() {
  Counting disk("examples/themes/bebas-clock.stheme");
  Package p(disk);Theme t;std::string error;
  if(!p.load(t,error)) return;
  Engine e;e.setTheme(t,0);Pixels d;tm now{};now.tm_year=126;now.tm_mday=17;now.tm_hour=10;now.tm_min=24;now.tm_wday=4;now.tm_mon=8;
  auto dirty=e.update(0,&now);
  disk.reads=disk.bytes=0;
  render(e,{Rect(0,0,240,240)},p,d);
  std::cout<<"measure: full redraw of bebas-clock: "<<disk.reads<<" storage reads, "<<disk.bytes<<" bytes\n";
  disk.reads=disk.bytes=0;
  now.tm_min=25;dirty=e.update(1000,&now);
  size_t area=0;for(const Rect& r:dirty) area+=r.w*r.h;
  render(e,dirty,p,d);
  std::cout<<"measure: redraw after the minute changed ("<<dirty.size()<<" rects, "<<area<<" px): "<<disk.reads<<" storage reads\n";
}
int main() {
  measureReads();
  testEdges();
  testAnchors();
  testValues();
  testDirtyRects();
  testGlyphLookup();
  std::cout<<(failures?"theme font review tests: FAILED ":"theme font review tests passed ")<<failures<<" failing checks\n";
  return failures?1:0;
}
