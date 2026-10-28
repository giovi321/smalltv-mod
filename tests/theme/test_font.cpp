// Package fonts (STF, spec 2, #31): the checks at load, the glyph rows the
// renderer reads, and a text drawn through the firmware's renderer compared
// pixel by pixel with what the glyph bitmaps say.
#include "ThemePackage.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace smalltv;
struct Memory : Source {
  std::string data;
  uint32_t size() const override {return data.size();}
  bool read(uint32_t offset,void* dest,size_t count) override {
    if(offset>data.size()||count>data.size()-offset) return false;
    memcpy(dest,data.data()+offset,count);return true;
  }
};
struct Pixels : Display {
  uint16_t data[240*240]{};
  uint8_t glyphColumn(uint8_t,uint8_t) const override {return 0;}
  void row(int x,int y,const uint16_t* p,int n) override {assert(x>=0&&y>=0&&x+n<=240&&y<240);for(int i=0;i<n;++i)data[y*240+x+i]=p[i];}
};
static void u16(std::string& s,unsigned n) {s+=char(n);s+=char(n>>8);}
static void u32(std::string& s,unsigned n) {u16(s,n);u16(s,n>>16);}
static void entry(std::string& s,const std::string& path,const std::string& data) {u16(s,path.size());u32(s,data.size());s+=path;s+=data;}
static std::string package(const std::string& json,const std::string& fontPath,const std::string& font) {
  std::string s="STH1";u16(s,font.empty()?1:2);u16(s,0);
  entry(s,"theme.json",json);
  if(!font.empty()) entry(s,fontPath,font);
  return s;
}
static std::string manifest(int spec,const std::string& layers) {
  return "{\"spec\":"+std::to_string(spec)+",\"theme\":{\"id\":\"font\",\"name\":\"Font\",\"author\":\"Me\",\"version\":\"1\"},"
    "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"},\"layers\":["+layers+"]}";
}
static void record(std::string& s,unsigned offset,unsigned advance,unsigned left,unsigned top,unsigned width,unsigned height) {
  u16(s,offset);s+=char(advance);s+=char(left);s+=char(top);s+=char(width);s+=char(height);s+='\0';
}
// Line height 8, baseline 6, code points 'A' to 'C', fallback 'A':
// 'A' has a 3x2 bitmap at (1, 2) in an advance of 4, 'B' has no glyph and
// 'C' is a space 2 pixels wide. The coverage of 'A', 0-15 per pixel:
static const uint8_t inkA[2][3]={{15,8,0},{3,15,15}};
static std::string font() {
  std::string s="STF1";s+=char(8);s+=char(6);s+='A';s+=char(3);s+='A';s.append(3,'\0');
  record(s,0,4,1,2,3,2);
  record(s,0,0,0,0,0,0);
  record(s,0,2,0,0,0,0);
  s+=char(0xf8);s+='\0';s+=char(0x3f);s+=char(0xf0);
  return s;
}
static const std::string textLayer=R"({"id":"t","type":"text","x":10,"y":20,"value":"ABC","font":"test","color":"#ffffff"})";
// The expected result of blending white over `below` with coverage c/15.
static uint16_t over(uint16_t below,unsigned c) {
  const unsigned a=c*17,b=255-a;
  return (((31*a+(below>>11)*b+127)/255)<<11)|(((63*a+((below>>5)&63)*b+127)/255)<<5)|((31*a+(below&31)*b+127)/255);
}
static bool loads(const std::string& stf,std::string& error) {
  Memory source;source.data=package(manifest(2,textLayer),"fonts/test.stf",stf);
  Package p(source);Theme t;
  return p.load(t,error);
}
static void testLoad() {
  Memory source;source.data=package(manifest(2,textLayer),"fonts/test.stf",font());
  Package p(source);Theme t;std::string error;
  assert(p.load(t,error));
  assert(t.fonts.size()==1&&t.fonts[0].name=="test"&&t.fonts[0].first=='A'&&t.fonts[0].fallback=='A');
  assert(t.fonts[0].advances.size()==3&&t.fonts[0].advances[0]==4&&t.fonts[0].advances[1]==0&&t.fonts[0].advances[2]==2);
  assert(t.layers[0].font==1&&t.layers[0].size==8);
  // Missing glyphs and bytes outside the font fall back to 'A'.
  assert(t.fonts[0].glyph('B')=='A'&&t.fonts[0].glyph('z')=='A'&&t.fonts[0].glyph(200)=='A'&&t.fonts[0].glyph('C')=='C');
  assert(t.fonts[0].advance('B')==4&&t.fonts[0].advance('C')==2);
  AssetHandle h=p.resolve("fonts/test.stf");assert(h!=InvalidAsset);
  uint8_t coverage[240];int left=-1,width=-1;
  assert(p.glyphRow(h,'A',2,coverage,left,width)&&left==1&&width==3&&coverage[0]==255&&coverage[1]==8*17&&coverage[2]==0);
  assert(p.glyphRow(h,'A',3,coverage,left,width)&&width==3&&coverage[0]==3*17&&coverage[1]==255&&coverage[2]==255);
  assert(p.glyphRow(h,'A',1,coverage,left,width)&&width==0);
  assert(p.glyphRow(h,'C',4,coverage,left,width)&&width==0);
  assert(!p.glyphRow(h,'D',2,coverage,left,width)&&!p.glyphRow(h,'@',2,coverage,left,width));
  // A font is no image, and an image no font.
  uint16_t colors[1];uint8_t alpha[1];
  assert(!p.row(h,0,0,1,colors,alpha));
  assert(!p.glyphRow(p.resolve("theme.json"),'A',2,coverage,left,width));
  // Two layers, one font: read once, both get its line height.
  source.data=package(manifest(2,textLayer+","+R"({"id":"u","type":"text","x":0,"y":0,"value":"A","font":"test","color":"#ffffff"})"),"fonts/test.stf",font());
  Package two(source);Theme t2;
  assert(two.load(t2,error)&&t2.fonts.size()==1&&t2.layers[1].font==1&&t2.layers[1].size==8);
}
static void testRejected() {
  std::string error;
  assert(loads(font(),error));
  // Every byte of the font that a check covers, changed to a wrong value.
  struct Change {size_t at;char value;} changes[]={
    {0,'X'},{3,'2'},
    {4,5},{4,97},{5,0},{5,9},{6,31},{7,0},{7,char(63)},{8,'@'},{8,'B'},{8,'D'},{9,1},{11,1},
    {12+0,1},{12+2,0},{12+2,3},{12+2,121},{12+3,2},{12+4,7},{12+5,4},{12+6,7},{12+7,1},
    {20+0,1},{20+3,1},{20+5,1},{20+7,1},
    {28+0,1},{28+2,121},{28+3,1},{28+4,1},
    {37,1},{39,1},
  };
  for(const Change& c:changes) {
    std::string bad=font();bad[c.at]=c.value;
    if(loads(bad,error)) {std::cerr<<"accepted a change at byte "<<c.at<<"\n";assert(false);}
  }
  // Truncated anywhere, or with a byte too many.
  for(size_t n=0;n<font().size();++n) assert(!loads(font().substr(0,n),error));
  assert(!loads(font()+'\0',error));
  // Code points stop at 126, even in an otherwise well-formed font.
  std::string past="STF1";past+=char(8);past+=char(6);past+='~';past+=char(2);past+='~';past.append(3,'\0');
  record(past,0,1,0,0,0,0);record(past,0,0,0,0,0,0);
  assert(!loads(past,error));
  past[7]=1;past.resize(20);assert(loads(past,error));
  // A font that falls back to a glyph without ink, a space, is fine.
  std::string spaceFallback=font();spaceFallback[8]='C';assert(loads(spaceFallback,error));
  // A font path must be fonts/<name>.stf.
  Memory source;source.data=package(manifest(2,textLayer),"fonts/.stf",font());
  Package p(source);Theme t;assert(!p.load(t,error));
  source.data=package(manifest(2,textLayer),"other/test.stf",font());assert(!p.load(t,error));
}
// Every single-byte change the checks let through must still draw inside
// the text's own rectangle, and no change may crash a load or a draw.
static void testMutations() {
  const std::string valid=font();
  for(size_t at=0;at<valid.size();++at) {
    for(int value:{0,1,2,7,8,15,16,65,66,67,96,120,127,128,200,255}) {
      std::string changed=valid;changed[at]=char(value);
      Memory source;source.data=package(manifest(2,textLayer),"fonts/test.stf",changed);
      Package p(source);Theme t;std::string error;
      if(!p.load(t,error)) continue;
      Engine e;e.setTheme(t,0);Pixels d;
      assert(render(e,e.update(0,nullptr),p,d));
      const Rect b=e.states()[0].bounds;
      for(int y=0;y<240;++y) for(int x=0;x<240;++x)
        if(d.data[y*240+x]) assert(x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h);
    }
  }
}
static void testRender() {
  // A red rectangle under the text: coverage blends over it.
  const std::string below=R"({"id":"r","type":"shape","shape":"rectangle","x":14,"y":20,"width":20,"height":8,"fill":"#ff0000"})";
  Memory source;source.data=package(manifest(2,below+","+textLayer),"fonts/test.stf",font());
  Package p(source);Theme t;std::string error;
  assert(p.load(t,error));
  Engine e;e.setTheme(t,0);Pixels d;
  assert(render(e,e.update(0,nullptr),p,d));
  // "ABC" is 'A', 'A' (for the missing 'B') and a space: 4 + 4 + 2 pixels.
  assert(e.states()[1].bounds.x==10&&e.states()[1].bounds.y==20&&e.states()[1].bounds.w==10&&e.states()[1].bounds.h==8);
  uint16_t expected[240*240]{};
  for(int y=20;y<28;++y) for(int x=14;x<34;++x) expected[y*240+x]=0xf800;
  for(int pen:{10,14}) for(int row=0;row<2;++row) for(int col=0;col<3;++col) {
    uint16_t& px=expected[(20+2+row)*240+pen+1+col];
    px=over(px,inkA[row][col]);
  }
  for(int i=0;i<240*240;++i) if(d.data[i]!=expected[i]) {
    std::cerr<<"pixel "<<i%240<<","<<i/240<<": "<<d.data[i]<<" expected "<<expected[i]<<"\n";assert(false);
  }
  // A dirty rectangle that cuts through the text redraws the same pixels.
  Pixels part;
  assert(render(e,{Rect(12,22,3,1)},p,part));
  for(int x=12;x<15;++x) assert(part.data[22*240+x]==expected[22*240+x]);
  // Centred, the line height counts like a built-in font's size.
  source.data=package(manifest(2,R"({"id":"t","type":"text","x":100,"y":100,"anchor":"center","value":"AC","font":"test","color":"#ffffff"})"),"fonts/test.stf",font());
  Package centred(source);
  assert(centred.load(t,error));
  Engine c;c.setTheme(t,0);c.update(0,nullptr);
  assert(c.states()[0].bounds.x==97&&c.states()[0].bounds.y==96&&c.states()[0].bounds.w==6&&c.states()[0].bounds.h==8);
  // A scrolling text in a 3-pixel viewport shows its glyphs moving left.
  source.data=package(manifest(2,R"({"id":"t","type":"text","x":0,"y":0,"value":"AA","font":"test","color":"#ffffff","scroll":{"width":3,"speed":10,"pause":0,"mode":"loop","gap":2}})"),"fonts/test.stf",font());
  Package scrolling(source);
  assert(scrolling.load(t,error));
  Engine s;s.setTheme(t,0);Pixels sd;
  assert(render(s,s.update(0,nullptr),scrolling,sd));
  assert(sd.data[2*240+0]==0&&sd.data[2*240+1]==0xffff&&sd.data[2*240+2]==over(0,8));
  assert(render(s,s.update(100,nullptr),scrolling,sd));   // 1 pixel on
  assert(sd.data[2*240+0]==0xffff&&sd.data[2*240+1]==over(0,8)&&sd.data[2*240+2]==0);
  // A viewport wider than one cycle (8 + a gap of 2) shows the next copy.
  source.data=package(manifest(2,R"({"id":"t","type":"text","x":0,"y":0,"value":"AA","font":"test","color":"#ffffff","scroll":{"width":12,"speed":10,"pause":0,"mode":"loop","gap":2}})"),"fonts/test.stf",font());
  Package wide(source);
  assert(wide.load(t,error));
  Engine w;w.setTheme(t,0);Pixels wd;
  assert(render(w,w.update(0,nullptr),wide,wd));
  assert(wd.data[2*240+9]==0&&wd.data[2*240+10]==0&&wd.data[2*240+11]==0xffff);
}
static void testManifest() {
  Theme t;std::string error;
  // Spec 1 has no package fonts, neither in the manifest nor in the package.
  assert(!parseTheme(manifest(1,textLayer),t,error)&&error=="layers[0].font: needs \"spec\": 2");
  Memory source;source.data=package(manifest(1,R"({"id":"t","type":"text","x":0,"y":0,"value":"A","size":8,"color":"#ffffff"})"),"fonts/test.stf",font());
  Package p(source);
  assert(!p.load(t,error)&&error=="fonts/test.stf: font needs \"spec\": 2");
  source.data=package(manifest(2,R"({"id":"t","type":"text","x":0,"y":0,"value":"A","font":"nope","color":"#ffffff"})"),"fonts/test.stf",font());
  assert(!p.load(t,error)&&error=="layers[0].font: missing font: fonts/nope.stf");
  // The font sets the size, so the layer neither declares nor binds one.
  assert(!parseTheme(manifest(2,R"({"id":"t","type":"text","x":0,"y":0,"value":"A","font":"a","size":8,"color":"#ffffff"})"),t,error)&&
         error=="layers[0].size: not allowed with a font, which sets the size");
  assert(!parseTheme(manifest(2,R"({"id":"t","type":"text","x":0,"y":0,"value":"A","font":"a/b","color":"#ffffff"})"),t,error)&&
         error=="layers[0].font: use ASCII letters, digits, '-' or '_'");
  std::string bound=manifest(2,R"({"id":"t","type":"text","x":0,"y":0,"value":"A","font":"a","color":"#ffffff","bind":{"size":{"source":"s.v","input":[0,1],"output":[8,9]}}})");
  bound.insert(bound.size()-1,R"(,"data":[{"id":"s","url":"http://example.com/","interval":60,"fields":[{"id":"v","path":"v"}]}])");
  assert(!parseTheme(bound,t,error));
  // At most four fonts; layers share one by name.
  std::string layers;
  for(int i=0;i<5;++i) layers+=std::string(i?",":"")+R"({"id":"t)"+std::to_string(i)+R"(","type":"text","x":0,"y":0,"value":"A","font":"f)"+std::to_string(i%(i==4?5:4))+R"(","color":"#ffffff"})";
  assert(!parseTheme(manifest(2,layers),t,error)&&error=="layers[4].font: expected at most 4 fonts per theme");
  layers.replace(layers.rfind("f4"),2,"f1");
  assert(parseTheme(manifest(2,layers),t,error)&&t.fonts.size()==4&&t.layers[4].font==2);
}
int main() {
  testLoad();
  testRejected();
  testMutations();
  testRender();
  testManifest();
  std::cout<<"theme font tests passed\n";
}
