#include "ThemeCatalog.h"
#include "BlockList.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <cstring>
#include <iostream>
#include <vector>
using namespace smalltv;
struct Memory : Source {
  std::string data;
  unsigned pauses=0;
  void pause() override {++pauses;}
  uint32_t size() const override {return data.size();}
  bool read(uint32_t offset,void* out,size_t n) override {
    if(offset>data.size()||n>data.size()-offset)return false;
    memcpy(out,data.data()+offset,n);return true;
  }
};
struct Text : Sink {
  std::string data;
  size_t room=SIZE_MAX;
  bool write(const char* p,size_t n) override {
    if(n>room-data.size())return false;
    data.append(p,n);return true;
  }
};
// The index's entries in order: a usable one as its id, any other as "?".
static std::string walk(const std::string& text) {
  Memory index;index.data=text;ThemeIndexReader reader(index);InstalledTheme e;std::string out;
  for(int i=0;i<40;++i){
    IndexEntry k=reader.next(e);
    if(k==IndexEntry::End)return out;
    out+=k==IndexEntry::Usable?e.id+" ":"? ";
  }
  return out+"(no end)";
}
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
  // listing index: one line per theme, read back entry by entry
  InstalledTheme other=good;other.id="other-theme";other.name="Other \"quoted\" \\ name";other.bytes=1234;
  const std::string a=themeIndexLine(good),b=themeIndexLine(other);
  assert(a.back()=='\n'&&std::count(a.begin(),a.end(),'\n')==1&&std::count(b.begin(),b.end(),'\n')==1);
  {
    Memory index;index.data=a+b;ThemeIndexReader reader(index);InstalledTheme e;
    assert(reader.next(e)==IndexEntry::Usable);
    assert(e.valid&&e.id=="pixel-room"&&e.name=="Pixel Room"&&e.author==good.author&&e.version==good.version&&e.bytes==good.bytes&&e.error.empty());
    assert(reader.next(e)==IndexEntry::Usable&&e.valid&&e.id=="other-theme"&&e.name==other.name&&e.bytes==1234);
    assert(reader.next(e)==IndexEntry::End&&reader.next(e)==IndexEntry::End);
  }
  // A bad line is skipped ("?") and the lines after it are still read.
  std::string line=a.substr(0,a.size()-1);
  auto with=[&](const char* from,const std::string& to){std::string s=line;s.replace(s.find(from),strlen(from),to);return s+"\n";};
  assert(walk("")=="");
  assert(walk("\n\n"+a)=="pixel-room ");
  assert(walk(a+with("\"revision\":1","\"revision\":2")+b)=="pixel-room ? other-theme ");
  assert(walk(with("\"pixel-room\"","\"Bad Id\"")+b)=="? other-theme ");
  assert(walk(with("\"version\":","\"versio\":")+b)=="? other-theme ");
  assert(walk(with("\"Pixel Room\"","\""+std::string(97,'n')+"\"")+b)=="? other-theme ");
  {InstalledTheme v=good;v.version=std::string(32,'v');assert(walk(themeIndexLine(v))=="pixel-room ");   // 32 bytes is the limit
   v.version+='v';assert(walk(themeIndexLine(v)+b)=="? other-theme ");}
  assert(walk(with("\"bytes\":","\"bytes\":-")+b)=="? other-theme ");
  assert(walk(with("\"bytes\":","\"bytes\":1.5,\"x\":")+b)=="? other-theme ");
  assert(walk(a+"not json\n"+b)=="pixel-room ? other-theme ");
  assert(walk(line+" junk\n"+b)=="? other-theme ");
  assert(walk(line+" \r\n"+b)=="pixel-room other-theme ");
  assert(walk(a+b.substr(0,b.size()-9))=="pixel-room ? ");                    // cut off
  assert(walk(with("\"Pixel Room\"","\""+std::string(5000,'n')+"\"")+b)=="? other-theme ");  // over MaxIndexLine
  assert(walk(a+std::string(MaxIndexLine+10,'1')+"\n"+b)=="pixel-room ? other-theme ");
  std::string keys;                                     // well-formed, but over MaxIndexLine
  for(int i=0;keys.size()<MaxIndexLine;++i) keys+=",\"k"+std::to_string(i)+"\":0";
  assert(walk(with("\"revision\":1","\"revision\":1"+keys)+b)=="? other-theme ");
  assert(walk(a+"   \n"+b)=="pixel-room other-theme ");
  // CONTRIBUTING (ESP8266 memory and untrusted input): reading pauses after
  // every line (a yield() on the device), and an index over MaxIndexBytes,
  // more than 16 themes' worth of the longest lines, is not read at all; the
  // list then inspects every package and writes the index again.
  {
    Memory index;index.data=a+b+a;ThemeIndexReader reader(index);InstalledTheme e;
    while(reader.next(e)!=IndexEntry::End){}
    assert(index.pauses>=3);
  }
  assert(MaxIndexBytes==16*MaxIndexLine);
  assert(walk(std::string(MaxIndexBytes-a.size(),'\n')+a)=="pixel-room ");   // exactly at the limit: read
  assert(walk(std::string(MaxIndexBytes-a.size()+1,'\n')+a)=="");            // one byte over: not read
  // The longest entry the manifest limits allow (no control characters, so
  // at most 2 bytes per escaped character) fits a line and reads back.
  InstalledTheme longest;longest.id=std::string(48,'x');longest.name=std::string(96,'"');longest.author=std::string(96,'\\');
  longest.version=std::string(32,'"');longest.bytes=4294967295u;longest.valid=true;
  const std::string l=themeIndexLine(longest);
  assert(l.size()<=MaxIndexLine);
  {
    Memory index;index.data=l;ThemeIndexReader reader(index);InstalledTheme e;
    assert(reader.next(e)==IndexEntry::Usable&&e.id==longest.id&&e.name==longest.name&&e.author==longest.author&&e.version==longest.version&&e.bytes==longest.bytes);
  }
  // Install and delete copy the index, leaving out the theme's old line and
  // any line that is not usable; an install adds the new line at the end.
  {
    InstalledTheme newer=other;newer.version="2";
    Memory from;from.data=a+with("\"revision\":1","\"revision\":2")+b;
    Text out;assert(copyThemeIndex(from,out,"other-theme",&newer)&&out.data==a+themeIndexLine(newer));
    Text removed;assert(copyThemeIndex(from,removed,"pixel-room",nullptr)&&removed.data==b);
    Memory none;Text first;assert(copyThemeIndex(none,first,"pixel-room",&good)&&first.data==a);
    Text full;full.room=a.size()+5;assert(!copyThemeIndex(from,full,"other-theme",&newer));
    Text tiny;tiny.room=5;assert(!copyThemeIndex(from,tiny,"nothing",nullptr));
  }
  // The list reply (issue #63): every package held as a ListedTheme, the
  // reply written from them in pieces of the writer's size, every piece but
  // the last one full. Together the pieces are exactly what 3.2.0 sent, the
  // same fields as one document through serializeJson(), escapes included.
  struct Pieces : Sink {
    std::vector<std::string> got;
    bool write(const char* p,size_t n) override {got.emplace_back(p,n);return true;}
    std::string joined() const {std::string s;for(const std::string& g:got)s+=g;return s;}
  };
  {
    std::string odd;for(int c=1;c<128;++c)odd+=char(c);odd+="\xc3\xa9\xe2\x82\xac";
    BlockList<ListedTheme> themes;themes.reserve(16);
    JsonDocument doc;doc["selected"]="theme-3";doc["error"]=odd;doc["freeBytes"]=786432;
    JsonArray list=doc["themes"].to<JsonArray>();
    for(int i=0;i<16;++i){
      std::string id="theme-"+std::to_string(i),name=i==2?odd:"Name \"q\" \\ "+std::to_string(i),author=i%3?"":"a\tb";
      std::string version="1."+std::to_string(i),error=i%2?"Invalid package":"";
      ListedTheme& t=themes.emplace_back();t.set(id.c_str(),name.c_str(),author.c_str(),version.c_str(),error.c_str());
      t.bytes=i==5?4294967295u:40000+i;t.valid=i%2==0;
      JsonObject o=list.add<JsonObject>();o["id"]=id;o["name"]=name;o["author"]=author;o["version"]=version;
      o["bytes"]=t.bytes;o["valid"]=t.valid;o["error"]=error;
    }
    std::string whole;serializeJson(doc,whole);
    Pieces p16,p128;writeThemeList<16>("theme-3",odd.c_str(),786432,themes,p16);writeThemeList<128>("theme-3",odd.c_str(),786432,themes,p128);
    const std::pair<Pieces*,size_t> runs[]={{&p16,16},{&p128,128}};
    for(const auto& t:runs){
      assert(t.first->joined()==whole);
      assert(t.first->got.size()==(whole.size()+t.second-1)/t.second);
      for(size_t i=0;i+1<t.first->got.size();++i)assert(t.first->got[i].size()==t.second);
    }
    BlockList<ListedTheme> none;Pieces empty;writeThemeList<16>("","",0,none,empty);
    assert(empty.joined()=="{\"selected\":\"\",\"error\":\"\",\"freeBytes\":0,\"themes\":[]}");
  }
  // A ListedTheme keeps its five fields apart, also when set from its own
  // fields; a field ends at a 0 byte, as the document's copy of c_str() did.
  {
    ListedTheme t;t.set("a","","c","d","");
    assert(t.id()==std::string("a")&&!*t.name()&&t.author()==std::string("c")&&t.version()==std::string("d")&&!*t.error());
    t.set(t.id(),"Name",t.author(),t.version(),"e");
    assert(t.id()==std::string("a")&&t.name()==std::string("Name")&&t.author()==std::string("c")&&t.version()==std::string("d")&&t.error()==std::string("e"));
    const std::string cut("x\0y",3);t.set(cut.c_str(),"n","","","");
    assert(t.id()==std::string("x")&&t.name()==std::string("n"));
  }
  source.data.resize(10);
  auto truncated=inspectInstalledTheme(source,"pixel-room");assert(!truncated.valid&&!truncated.error.empty());
  std::cout<<"theme catalog tests passed\n";
}
