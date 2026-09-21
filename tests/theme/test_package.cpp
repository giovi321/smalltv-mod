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
  std::cout<<"theme package tests passed\n";
}
