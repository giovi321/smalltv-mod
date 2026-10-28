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
void u16(std::string& s,unsigned n) {s+=char(n);s+=char(n>>8);}
void u32(std::string& s,unsigned n) {u16(s,n);u16(s,n>>16);}
void entry(std::string& s,std::string path,std::string data) {u16(s,path.size());u32(s,data.size());s+=path;s+=data;}
int main() {
  Memory source; Package p(source);Theme t;std::string error;
  source.data="STH1";u16(source.data,2);u16(source.data,0);
  std::string json=R"({"spec":1,"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"layers":[{"id":"bg","type":"image","x":0,"y":0,"source":"images/bg.png"}]})";
  entry(source.data,"theme.json",json);
  std::string json2=json;json2.replace(json2.find("\"spec\":1"),8,"\"spec\":2");   // STI2 needs spec 2 (#31)
  std::string image="STI1";u16(image,2);u16(image,1);image+=char(1);image.append(3,'\0');u16(image,0xf800);image+=char(255);u16(image,0x07e0);image+=char(0);
  entry(source.data,"images/bg.png.sti",image);
  assert(p.load(t,error));assert(t.layers[0].width==2);
  uint16_t colors[2];uint8_t alpha[2];
  assert(p.row("images/bg.png.sti",0,0,2,colors,alpha));
  assert(colors[0]==0xf800&&colors[1]==0x07e0&&alpha[0]==255&&alpha[1]==0);
  assert(!p.row("images/bg.png.sti",1,0,2,colors,alpha));
  auto handle=p.resolve("images/bg.png.sti");assert(handle!=InvalidAsset);
  assert(p.row(handle,0,1,1,colors,alpha) && colors[0]==0x07e0 && alpha[0]==0);
  assert(p.resolve("theme.json")==InvalidAsset && p.resolve("missing.sti")==InvalidAsset);
  assert(!p.row(InvalidAsset,0,0,1,colors,alpha));
  auto valid=source.data;
  source.data.pop_back();assert(!p.load(t,error));
  source.data=valid+"junk";assert(!p.load(t,error));
  source.data=valid;source.data.replace(source.data.find("images/bg.png.sti"),16,"../bad/bg.png.sti");assert(!p.load(t,error));
  // Every truncation point must fail without changing the caller's theme.
  for(size_t n=0;n<valid.size();++n) {source.data=valid.substr(0,n);Theme before;before.id="keep";assert(!p.load(before,error));assert(before.id=="keep");}
  source.data=valid;source.data[6]=1;assert(!p.load(t,error)); // reserved header
  source.data=valid;source.data[4]=0;assert(!p.load(t,error)); // zero entries
  source.data=valid;source.data[10]=char(255);source.data[11]=char(255);source.data[12]=char(255);source.data[13]=char(255);assert(!p.load(t,error)); // oversized payload
  source.data=valid;size_t imageOffset=source.data.find("STI1");source.data[imageOffset+8]=2;assert(!p.load(t,error)); // unsupported alpha
  source.data=valid;source.data[imageOffset+4]=0;assert(!p.load(t,error)); // zero width
  source.data=valid;source.data[4]=3;entry(source.data,"theme.json",json);assert(!p.load(t,error)); // duplicate path
  source.data=valid;size_t ref=source.data.find("images/bg.png");source.data.replace(ref,13,"images/no.png");assert(!p.load(t,error)); // missing asset
  // A storage read that fails part-way through the manifest fails the load
  // with its own message and leaves the caller's theme alone, wherever it
  // happens. Only manifest bytes fail, so the package index still reads and
  // the failure reaches the parser.
  struct Flaky : Memory {
    uint32_t failFrom = 0, failTo = 0;
    bool read(uint32_t offset,void* dest,size_t count) override {
      return (offset+count<=failFrom || offset>=failTo) && Memory::read(offset,dest,count);
    }
  } flaky;
  flaky.data=valid;size_t manifestAt=valid.find("{\"spec\"");flaky.failTo=manifestAt+json.size();
  for(size_t at=manifestAt;at<manifestAt+json.size();at+=7) {
    flaky.failFrom=at;Package fp(flaky);Theme before;before.id="keep";
    assert(!fp.load(before,error) && before.id=="keep" && error=="theme.json: could not be read");
  }

  // STI2: the same header, then one u32 offset per row (into the row data
  // after the table), then each row as runs. A run's first byte holds its
  // kind in the top two bits and its length minus one (1-64) in the rest:
  // 00 transparent, 01 one pixel repeated, 10 literal pixels.
  {
    std::string rows[3];
    rows[0]+=char(0x01);                                                   // 2 transparent
    rows[0]+=char(0x81);u16(rows[0],0xf800);rows[0]+=char(255);u16(rows[0],0x07e0);rows[0]+=char(128);  // 2 literal
    rows[0]+=char(0x40);u16(rows[0],0x001f);rows[0]+=char(255);            // 1 repeated
    rows[1]+=char(0x44);u16(rows[1],0xffff);rows[1]+=char(255);            // 5 repeated
    rows[2]+=char(0x04);                                                   // 5 transparent
    auto sti2=[&](const std::string* r,unsigned height){
      std::string s="STI2";u16(s,5);u16(s,height);s+=char(1);s.append(3,'\0');
      unsigned at=0;
      for(unsigned y=0;y<height;++y){u32(s,at);at+=r[y].size();}
      for(unsigned y=0;y<height;++y) s+=r[y];
      return s;
    };
    auto package=[&](const std::string& image){
      std::string s="STH1";u16(s,2);u16(s,0);entry(s,"theme.json",json2);entry(s,"images/bg.png.sti",image);return s;
    };
    Memory packed;Package rle(packed);Theme theme;
    packed.data=package(sti2(rows,3));
    assert(rle.load(theme,error) && theme.layers[0].width==5 && theme.layers[0].height==3);
    uint16_t c[5];uint8_t a[5];
    assert(rle.row("images/bg.png.sti",0,0,5,c,a));
    assert(a[0]==0&&a[1]==0&&c[2]==0xf800&&a[2]==255&&c[3]==0x07e0&&a[3]==128&&c[4]==0x001f&&a[4]==255);
    assert(rle.row("images/bg.png.sti",0,3,2,c,a) && c[0]==0x07e0&&a[0]==128&&c[1]==0x001f);   // starts inside a run
    c[3]=0x1234;a[3]=7;   // past the 3 pixels asked for: must stay untouched
    assert(rle.row("images/bg.png.sti",1,1,3,c,a) && c[0]==0xffff&&c[2]==0xffff&&a[1]==255);
    assert(c[3]==0x1234&&a[3]==7);
    assert(rle.row("images/bg.png.sti",2,4,1,c,a) && a[0]==0);
    assert(!rle.row("images/bg.png.sti",3,0,1,c,a) && !rle.row("images/bg.png.sti",0,4,2,c,a));
    // Damaged data is refused when the package is loaded (as agreed in #31,
    // so row() can trust it), never read past: a row whose runs go past the
    // width, a row that ends early, a literal run cut off, an offset past
    // the data, and offsets that go backwards.
    std::string bad[3]={rows[0],rows[1],rows[2]};
    bad[2]=std::string(1,char(0x05));                                      // 6 transparent in a 5-wide row
    packed.data=package(sti2(bad,3));assert(!rle.load(theme,error));
    bad[2]=std::string(1,char(0x02));                                      // only 3 of 5 pixels
    packed.data=package(sti2(bad,3));assert(!rle.load(theme,error));
    std::string cut=rows[0].substr(0,5);bad[0]=cut;bad[2]=rows[2];         // literal run without its pixels
    packed.data=package(sti2(bad,3));assert(!rle.load(theme,error));
    bad[0]=std::string(1,char(0x01));bad[1]=std::string("\x02\x01",2);   // row 0 is 2 pixels; read on into row 1
    packed.data=package(sti2(bad,3));assert(!rle.load(theme,error));       // (3 + 2, fine itself) it would make 5
    bad[0]=rows[0];bad[1]=rows[1];bad[2]=std::string(1,char(0xc4));         // kind 11 is reserved
    packed.data=package(sti2(bad,3));assert(!rle.load(theme,error));
    std::string image=sti2(rows,3);image[12+4*2]=char(200);                // row 2 offset past the data
    packed.data=package(image);assert(!rle.load(theme,error));
    image=sti2(rows,3);image[12+4*1]=0;image[12+4*2]=char(rows[0].size());  // rows may share data (1 reads row 0's),
    packed.data=package(image);assert(rle.load(theme,error));
    image=sti2(rows,3);image[12+4*2]=0;image[12+4*1]=char(rows[0].size()+1);// but offsets never go backwards
    packed.data=package(image);assert(!rle.load(theme,error));
    image=sti2(rows,3);image.resize(12+4*2);                               // table cut short,
    packed.data=package(image);assert(!rle.load(theme,error));
    packed.data="STH1";u16(packed.data,3);u16(packed.data,0);               // also with a file after it,
    entry(packed.data,"theme.json",json2);entry(packed.data,"images/bg.png.sti",image);  // so reading on would work
    entry(packed.data,"images/other.png.sti",sti2(rows,3));
    assert(!rle.load(theme,error));
  }

  // STI2 with a palette (byte 9 holds its size, 1-16): the entries (RGB565,
  // plus an alpha byte when the image has alpha) come before the row table.
  // A repeat run holds one index byte; a literal run two indices per byte,
  // high nibble first, an odd run's unused low nibble 0.
  {
    auto sti2=[](unsigned width,unsigned height,bool alpha,unsigned colours,const std::string& palette,
                 const std::string* r){
      std::string s="STI2";u16(s,width);u16(s,height);s+=char(alpha);s+=char(colours);s.append(2,'\0');
      s+=palette;
      unsigned at=0;
      for(unsigned y=0;y<height;++y){u32(s,at);at+=r[y].size();}
      for(unsigned y=0;y<height;++y) s+=r[y];
      return s;
    };
    auto package=[&](const std::string& image){
      std::string s="STH1";u16(s,2);u16(s,0);entry(s,"theme.json",json2);entry(s,"images/bg.png.sti",image);return s;
    };
    std::string palette;                                                   // red, blue at half alpha, white
    u16(palette,0xf800);palette+=char(255);u16(palette,0x001f);palette+=char(128);u16(palette,0xffff);palette+=char(255);
    std::string rows[2];
    rows[0]=std::string("\x00\x82\x01\x20\x40\x01",6);                     // 1 transparent, literal 0 1 2, repeat 1
    rows[1]=std::string("\x44\x02",2);                                     // 5 times colour 2
    Memory packed;Package indexed(packed);Theme theme;uint16_t c[5];uint8_t a[5];
    packed.data=package(sti2(5,2,true,3,palette,rows));
    assert(indexed.load(theme,error));
    assert(indexed.row("images/bg.png.sti",0,0,5,c,a));
    assert(a[0]==0&&c[1]==0xf800&&a[1]==255&&c[2]==0x001f&&a[2]==128&&c[3]==0xffff&&a[3]==255&&c[4]==0x001f&&a[4]==128);
    assert(indexed.row("images/bg.png.sti",0,2,2,c,a) && c[0]==0x001f&&a[0]==128&&c[1]==0xffff);  // from a low nibble
    assert(indexed.row("images/bg.png.sti",1,1,3,c,a) && c[0]==0xffff&&c[2]==0xffff&&a[1]==255);
    std::string opaque;u16(opaque,0xf800);u16(opaque,0x07e0);               // no alpha: 2-byte entries
    std::string one=std::string("\x82\x01\x00",3);
    packed.data=package(sti2(3,1,false,2,opaque,&one));
    assert(indexed.load(theme,error) && indexed.row("images/bg.png.sti",0,0,3,c,a));
    assert(c[0]==0xf800&&c[1]==0x07e0&&c[2]==0xf800&&a[0]==255&&a[2]==255);
    // Refused at load: an index past the palette (in a literal and in a
    // repeat), a non-zero unused nibble, more than 16 colours, a palette
    // cut short, a transparent run in an image without alpha (with and
    // without a palette), and non-zero reserved bytes.
    std::string bad=std::string("\x00\x81\x03\x40\x01\x40\x01",7);         // literal 0 3: 3 is past 3 colours
    packed.data=package(sti2(5,1,true,3,palette,&bad));assert(!indexed.load(theme,error));
    bad=std::string("\x00\x81\x01\x41\x03",5);                             // repeat of colour 3
    packed.data=package(sti2(5,1,true,3,palette,&bad));assert(!indexed.load(theme,error));
    bad=std::string("\x00\x81\x01\x41\x02",5);                             // the same with colour 2 is fine
    packed.data=package(sti2(5,1,true,3,palette,&bad));assert(indexed.load(theme,error));
    bad=std::string("\x82\x01\x0f",3);                                     // unused nibble 15
    packed.data=package(sti2(3,1,false,2,opaque,&bad));assert(!indexed.load(theme,error));
    std::string many;for(int i=0;i<17;++i) u16(many,i);
    packed.data=package(sti2(3,1,false,17,many,&one));assert(!indexed.load(theme,error));
    packed.data=package(sti2(3,1,false,16,many.substr(0,32),&one));assert(indexed.load(theme,error));
    std::string cutPalette=sti2(3,1,false,2,opaque,&one);cutPalette.resize(12+3);
    packed.data=package(cutPalette);assert(!indexed.load(theme,error));
    bad=std::string("\x02",1);                                             // 3 transparent, but no alpha
    packed.data=package(sti2(3,1,false,2,opaque,&bad));assert(!indexed.load(theme,error));
    packed.data=package(sti2(3,1,false,0,"",&bad));assert(!indexed.load(theme,error));
    for(int at : {10,11}){
      std::string image=sti2(3,1,false,2,opaque,&one);image[at]=1;
      packed.data=package(image);assert(!indexed.load(theme,error));
    }
    // An STI2 image needs a spec 2 manifest (#31), also when no layer uses
    // it; the error names the image. Spec 2 still takes STI1 images.
    packed.data="STH1";u16(packed.data,2);u16(packed.data,0);
    entry(packed.data,"theme.json",json);entry(packed.data,"images/bg.png.sti",sti2(3,1,false,2,opaque,&one));
    assert(!indexed.load(theme,error) && error=="images/bg.png.sti: STI2 image needs \"spec\": 2");
    packed.data="STH1";u16(packed.data,3);u16(packed.data,0);entry(packed.data,"theme.json",json);
    std::string still="STI1";u16(still,3);u16(still,1);still+=char(0);still.append(3,'\0');still.append(6,'\0');
    entry(packed.data,"images/bg.png.sti",still);entry(packed.data,"images/unused.png.sti",sti2(3,1,false,2,opaque,&one));
    assert(!indexed.load(theme,error) && error=="images/unused.png.sti: STI2 image needs \"spec\": 2");
    packed.data="STH1";u16(packed.data,2);u16(packed.data,0);entry(packed.data,"theme.json",json2);
    entry(packed.data,"images/bg.png.sti",still);
    assert(indexed.load(theme,error) && theme.spec==2);
    // STI1 keeps byte 9 reserved: no palette there.
    std::string raw="STI1";u16(raw,1);u16(raw,1);raw+=char(0);raw+=char(1);raw.append(2,'\0');u16(raw,0xf800);
    packed.data=package(raw);assert(!indexed.load(theme,error));
    raw[9]=0;packed.data=package(raw);assert(indexed.load(theme,error));
  }
  std::cout<<"theme package tests passed\n";
}
