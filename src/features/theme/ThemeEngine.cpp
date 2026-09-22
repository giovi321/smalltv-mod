#include "ThemeEngine.h"
#ifdef ARDUINO
#include "config.h"
#endif
#if !defined(ARDUINO) || WITH_THEME
#include <ArduinoJson.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace smalltv {
Rect intersect(Rect a, Rect b) {
  int x=std::max(a.x,b.x), y=std::max(a.y,b.y);
  return Rect(x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),std::max(0,std::min(a.y+a.h,b.y+b.h)-y));
}
Rect unite(Rect a, Rect b) {
  if(a.empty()) return b;
  if(b.empty()) return a;
  int x=std::min(a.x,b.x), y=std::min(a.y,b.y);
  return Rect(x,y,std::max(a.x+a.w,b.x+b.w)-x,std::max(a.y+a.h,b.y+b.h)-y);
}
bool validId(const std::string& s) {
  if(s.empty() || s.size()>48) return false;
  for(char c:s) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_')) return false;
  return true;
}
bool validPath(const std::string& s) {
  if(s.empty() || s.size()>120 || s.front()=='/' || s.back()=='/') return false;
  size_t start=0;
  for(size_t i=0;i<=s.size();++i) {
    if(i==s.size() || s[i]=='/') {
      auto part=s.substr(start,i-start);
      if(part.empty() || part=="." || part=="..") return false;
      start=i+1;
    } else {
      char c=s[i];
      if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')) return false;
    }
  }
  return true;
}
static bool asciiSpace(char c) {
  return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';
}
static bool decimalSyntax(const char* begin,const char* end) {
  const char* p=begin;
  if(p!=end&&(*p=='+'||*p=='-')) ++p;
  bool digits=false;
  while(p!=end&&*p>='0'&&*p<='9') {digits=true;++p;}
  if(p!=end&&*p=='.') {
    ++p;
    while(p!=end&&*p>='0'&&*p<='9') {digits=true;++p;}
  }
  if(!digits) return false;
  if(p!=end&&(*p=='e'||*p=='E')) {
    ++p;
    if(p!=end&&(*p=='+'||*p=='-')) ++p;
    const char* exponent=p;
    while(p!=end&&*p>='0'&&*p<='9') ++p;
    if(p==exponent) return false;
  }
  return p==end;
}
bool parseFiniteNumber(const std::string& value,double& number) {
  const char* begin=value.c_str();
  const char* end=begin+value.size();
  while(begin!=end&&asciiSpace(*begin)) ++begin;
  while(begin!=end&&asciiSpace(end[-1])) --end;
  if(begin==end||!decimalSyntax(begin,end)) return false;
  errno=0;
  char* parsed=nullptr;
  const double result=std::strtod(begin,&parsed);
  if(parsed!=end||errno==ERANGE||!std::isfinite(result)) return false;
  number=result;
  return true;
}
static int clampInteger(int value,int lo,int hi) {
  return std::max(lo,std::min(value,hi));
}
int resolveNumericBinding(const NumericBinding& binding,double value,int lo,int hi) {
  if(lo>hi) std::swap(lo,hi);
  if(!std::isfinite(value)||!std::isfinite(binding.input0)||
     !std::isfinite(binding.input1)||binding.input0==binding.input1)
    return clampInteger(binding.output0,lo,hi);
  if(binding.clamp) {
    const double inputLow=std::min(binding.input0,binding.input1);
    const double inputHigh=std::max(binding.input0,binding.input1);
    value=std::max(inputLow,std::min(value,inputHigh));
  }
  const long double input0=binding.input0,input1=binding.input1,source=value;
  const long double outputDelta=static_cast<long double>(binding.output1)-binding.output0;
  const long double inputDelta=input1-input0,sourceDelta=source-input0;
  const long double numerator=sourceDelta*outputDelta;
  long double mapped=0;
  if(std::isfinite(inputDelta)&&std::isfinite(numerator)) {
    mapped=static_cast<long double>(binding.output0)+numerator/inputDelta;
  } else {
    const long double scale=std::max(std::fabs(input0),std::fabs(input1));
    if(scale==0) return clampInteger(binding.output0,lo,hi);
    const long double denominator=input1/scale-input0/scale;
    if(denominator==0) return clampInteger(binding.output0,lo,hi);
    const long double position=(source/scale-input0/scale)/denominator;
    mapped=static_cast<long double>(binding.output0)+position*outputDelta;
  }
  if(std::isnan(mapped)) return clampInteger(binding.output0,lo,hi);
  if(mapped<=lo) return lo;
  if(mapped>=hi) return hi;
  return static_cast<int>(std::round(mapped));
}
uint16_t resolveColorBinding(const ColorBinding& binding,double value) {
  if(binding.stops.empty()) return 0;
  uint16_t result=binding.stops.front().value;
  for(const auto& stop:binding.stops) {
    if(stop.at>value) break;
    result=stop.value;
  }
  return result;
}
static const char* const tokens[]={"HH","hh","MM","SS","DD","MON","MONTH","WD","WEEKDAY","YYYY"};
static bool validFieldToken(const std::string& token,const Theme& theme) {
  size_t dot=token.find('.');if(dot==std::string::npos||dot==0||dot+1>=token.size())return false;
  std::string source=token.substr(0,dot),field=token.substr(dot+1);
  if(!validId(source)||!validId(field))return false;
  for(const auto& data:theme.data) { if(data.id!=source) continue; for(const auto& f:data.fields) if(f.id==field)return true; }
  return false;
}
static bool validText(const std::string& s,const Theme& theme) {
  if(s.size()>128) return false;
  for(size_t i=0;i<s.size();++i) {
    if(s[i]<32 || s[i]>126 || s[i]=='}') return false;
    if(s[i]!='{') continue;
    size_t end=s.find('}',i);
    if(end==std::string::npos) return false;
    bool found=false;
    for(auto token:tokens) if(s.substr(i+1,end-i-1)==token) found=true;
    if(!found) found=validFieldToken(s.substr(i+1,end-i-1),theme);
    if(!found) return false;
    i=end;
  }
  return true;
}
struct JsonReader {
  const std::string& input;
  size_t position=0;
  explicit JsonReader(const std::string& text) : input(text) {}
  int read() {return position<input.size()?static_cast<unsigned char>(input[position++]):-1;}
  size_t readBytes(char* out,size_t count) {
    count=std::min(count,input.size()-position);
    memcpy(out,input.data()+position,count);position+=count;return count;
  }
};
static bool parseColor(JsonVariantConst v,uint16_t& out) {
  if(!v.is<const char*>()) return false;
  JsonString s=v.as<JsonString>();
  if(s.size()!=7 || s.c_str()[0]!='#') return false;
  uint32_t rgb=0;
  for(int i=1;i<7;++i) {
    char c=s.c_str()[i]; int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
    if(n<0) return false;
    rgb=(rgb<<4)|n;
  }
  out=((rgb>>8)&0xf800)|((rgb>>5)&0x07e0)|((rgb>>3)&0x001f); return true;
}
// The device and desktop tool share both validation rules and field diagnostics.
class Fields {
 public:
  Fields(JsonObjectConst object,std::string path,std::string& error)
    : object_(object),path_(std::move(path)),error_(error) {}
  bool fail(const char* key,const std::string& reason) const {
    std::string path=path_;
    if(key&&*key) {if(!path.empty()) path+='.';path+=key;}
    error_=(path.empty()?"theme.json":path)+": "+reason;return false;
  }
  bool keys(const char* allowed) const {
    if(object_.isNull()) return fail("","expected an object");
    for(JsonPairConst p:object_) {
      bool found=false;
      for(const char* start=allowed+1;*start;) {
        const char* end=strchr(start,'|');if(!end) break;
        if(p.key().size()==size_t(end-start) && !memcmp(start,p.key().c_str(),end-start)) {found=true;break;}
        start=end+1;
      }
      if(!found) {
        std::string key;
        for(size_t i=0;i<std::min<size_t>(p.key().size(),64);++i) {
          unsigned char c=p.key().c_str()[i];
          if(c>=32&&c<=126) key+=char(c);
          else {char hex[7];snprintf(hex,sizeof(hex),"\\u%04x",c);key+=hex;}
        }
        return fail(key.c_str(),"unknown field");
      }
    }
    return true;
  }
  bool number(const char* key,int lo,int hi,int& value,bool required=true) const {
    auto v=object_[key];if(v.isNull()&&!required)return true;
    if(!v.is<int>()||v.as<int>()<lo||v.as<int>()>hi)
      return fail(key,"expected an integer from "+std::to_string(lo)+" to "+std::to_string(hi));
    value=v.as<int>();return true;
  }
  bool text(const char* key,std::string& value,size_t max,bool required=true) const {
    auto v=object_[key];if(v.isNull()&&!required)return true;
    if(!v.is<const char*>())return fail(key,"expected a string");
    JsonString s=v.as<JsonString>();value.assign(s.c_str(),s.size());
    if(value.empty()||value.size()>max)return fail(key,"expected 1 to "+std::to_string(max)+" bytes");
    for(unsigned char c:value)if(c<32||c==127)return fail(key,"control characters are not allowed");
    return true;
  }
  bool boolean(const char* key,bool& value,bool required=true) const {
    auto v=object_[key];if(v.isNull()&&!required)return true;
    if(!v.is<bool>())return fail(key,"expected a boolean");
    value=v.as<bool>();return true;
  }
  bool color(const char* key,uint16_t& value) const {
    return parseColor(object_[key],value)||fail(key,"expected a color in #RRGGBB form");
  }
  bool real(const char* key,double& value) const {
    JsonVariantConst v=object_[key];
    if(!v.is<double>()&&!v.is<long>()&&!v.is<int>())
      return fail(key,"expected a finite number");
    value=v.as<double>();
    return std::isfinite(value)||fail(key,"expected a finite number");
  }
  bool has(const char* key) const {
    for(JsonPairConst pair:object_) if(pair.key().size()==strlen(key)&&!memcmp(pair.key().c_str(),key,pair.key().size())) return true;
    return false;
  }
 private:
  JsonObjectConst object_;
  std::string path_;
  std::string& error_;
};
// ArduinoJson retains only the last duplicate object member. Scan binding keys
// in the raw manifest so that a manifest cannot silently replace a target.
// Run only after JSON validation (including its eight-level nesting limit).
// The manifest byte limit bounds string storage; at most eight keys are kept.
class DuplicateBindingScanner {
 public:
  DuplicateBindingScanner(const std::string& input,std::string& error) : input_(input),error_(error) {}
  bool valid() {
    const bool scanned=scanRoot();
    space();
    if(scanned&&position_==input_.size()) return true;
    if(error_.empty()) error_="theme.json: expected standard JSON object syntax";
    return false;
  }
 private:
  void space() { while(position_<input_.size()&&(input_[position_]==' '||input_[position_]=='\n'||input_[position_]=='\r'||input_[position_]=='\t')) ++position_; }
  bool take(char expected) {
    space();if(position_>=input_.size()||input_[position_]!=expected) return false;
    ++position_;return true;
  }
  bool string(std::string& output) {
    space();if(position_>=input_.size()||input_[position_++]!='\"') return false;
    output.clear();
    while(position_<input_.size()) {
      unsigned char c=input_[position_++];
      if(c=='\"') return true;
      if(c<32) return false;
      if(c!='\\') {output+=char(c);continue;}
      if(position_>=input_.size()) return false;
      c=input_[position_++];
      if(c=='\"'||c=='\\'||c=='/') output+=char(c);
      else if(c=='b'||c=='f'||c=='n'||c=='r'||c=='t') output+='?';
      else if(c=='u') {
        unsigned value=0;
        for(int i=0;i<4;++i) {
          if(position_>=input_.size()) return false;
          unsigned char hex=input_[position_++];
          int digit=hex>='0'&&hex<='9'?hex-'0':hex>='a'&&hex<='f'?hex-'a'+10:hex>='A'&&hex<='F'?hex-'A'+10:-1;
          if(digit<0) return false;
          value=(value<<4)|unsigned(digit);
        }
        output+=value<=127?char(value):'?';
      } else return false;
    }
    return false;
  }
  bool value() {
    space();if(position_>=input_.size()) return false;
    if(input_[position_]=='{'||input_[position_]=='[') {
      if(depth_==8) return false;
      ++depth_;
      const bool valid=input_[position_]=='{'?object():array();
      --depth_;
      return valid;
    }
    if(input_[position_]=='\"') {std::string ignored;return string(ignored);}
    const size_t start=position_;
    while(position_<input_.size()&&input_[position_]!=','&&input_[position_]!=']'&&input_[position_]!='}'&&input_[position_]!=' '&&input_[position_]!='\n'&&input_[position_]!='\r'&&input_[position_]!='\t') ++position_;
    return position_>start;
  }
  bool object() {
    if(!take('{')) return false;
    space();if(position_<input_.size()&&input_[position_]=='}') {++position_;return true;}
    for(;;) {
      std::string key;if(!string(key)||!take(':')||!value()) return false;
      space();if(position_>=input_.size()) return false;
      if(input_[position_]=='}') {++position_;return true;}
      if(input_[position_++]!=',') return false;
    }
  }
  bool array() {
    if(!take('[')) return false;
    space();if(position_<input_.size()&&input_[position_]==']') {++position_;return true;}
    for(;;) {
      if(!value()) return false;
      space();if(position_>=input_.size()) return false;
      if(input_[position_]==']') {++position_;return true;}
      if(input_[position_++]!=',') return false;
    }
  }
  bool bindings(size_t layer) {
    if(!take('{')) return false;
    std::vector<std::string> seen;
    space();if(position_<input_.size()&&input_[position_]=='}') {++position_;return true;}
    for(;;) {
      std::string key;if(!string(key)||!take(':')) return false;
      for(const auto& old:seen) if(old==key) {
        error_="layers["+std::to_string(layer)+"].bind."+key+": duplicate binding target";
        return false;
      }
      if(seen.size()==8) {
        error_="layers["+std::to_string(layer)+"].bind: expected at most 8 bindings";
        return false;
      }
      seen.push_back(key);
      if(!value()) return false;
      space();if(position_>=input_.size()) return false;
      if(input_[position_]=='}') {++position_;return true;}
      if(input_[position_++]!=',') return false;
    }
  }
  bool layer(size_t index) {
    if(!take('{')) return false;
    space();if(position_<input_.size()&&input_[position_]=='}') {++position_;return true;}
    for(;;) {
      std::string key;if(!string(key)||!take(':')) return false;
      if(key=="bind") {
        space();if(position_<input_.size()&&input_[position_]=='{') {if(!bindings(index)) return false;}
        else if(!value()) return false;
      } else if(!value()) return false;
      space();if(position_>=input_.size()) return false;
      if(input_[position_]=='}') {++position_;return true;}
      if(input_[position_++]!=',') return false;
    }
  }
  bool layers() {
    if(!take('[')) return false;
    size_t index=0;space();if(position_<input_.size()&&input_[position_]==']') {++position_;return true;}
    for(;;) {
      space();if(position_<input_.size()&&input_[position_]=='{') {if(!layer(index)) return false;}
      else if(!value()) return false;
      ++index;space();if(position_>=input_.size()) return false;
      if(input_[position_]==']') {++position_;return true;}
      if(input_[position_++]!=',') return false;
    }
  }
  bool scanRoot() {
    if(!take('{')) return false;
    space();if(position_<input_.size()&&input_[position_]=='}') {++position_;return true;}
    for(;;) {
      std::string key;if(!string(key)||!take(':')) return false;
      if(key=="layers") {
        space();if(position_<input_.size()&&input_[position_]=='[') {if(!layers()) return false;}
        else if(!value()) return false;
      } else if(!value()) return false;
      space();if(position_>=input_.size()) return false;
      if(input_[position_]=='}') {++position_;return true;}
      if(input_[position_++]!=',') return false;
    }
  }
  const std::string& input_;
  std::string& error_;
  size_t position_=0;
  unsigned depth_=0;
};
static bool numericProperty(const std::string& name,BoundProperty& property) {
  struct Name { const char* name; BoundProperty property; };
  static const Name names[]={
    {"x",BoundProperty::X},{"y",BoundProperty::Y},{"width",BoundProperty::Width},
    {"height",BoundProperty::Height},{"radius",BoundProperty::Radius},
    {"cornerRadius",BoundProperty::CornerRadius},{"x2",BoundProperty::X2},
    {"y2",BoundProperty::Y2},{"size",BoundProperty::Size},
    {"strokeWidth",BoundProperty::StrokeWidth},{"scroll.width",BoundProperty::ScrollWidth},
    {"scroll.speed",BoundProperty::ScrollSpeed}
  };
  for(const auto& item:names) if(name==item.name) {property=item.property;return true;}
  return false;
}
static bool colorProperty(const std::string& name,BoundProperty& property) {
  if(name=="color") {property=BoundProperty::Color;return true;}
  if(name=="fill") {property=BoundProperty::Fill;return true;}
  if(name=="stroke") {property=BoundProperty::Stroke;return true;}
  return false;
}
static bool propertyRange(BoundProperty property,int& lo,int& hi) {
  switch(property) {
    case BoundProperty::X: case BoundProperty::Y: case BoundProperty::X2: case BoundProperty::Y2:
      lo=-240;hi=479;return true;
    case BoundProperty::Width: case BoundProperty::Height: case BoundProperty::Radius:
      lo=0;hi=240;return true;
    case BoundProperty::CornerRadius:
      lo=0;hi=120;return true;
    case BoundProperty::Size:
      lo=8;hi=96;return true;
    case BoundProperty::StrokeWidth:
      lo=1;hi=32;return true;
    case BoundProperty::ScrollWidth: case BoundProperty::ScrollSpeed:
      lo=1;hi=240;return true;
    default:
      return false;
  }
}
static bool applicable(const Layer& layer,BoundProperty property,bool color) {
  if(color) {
    if(property==BoundProperty::Color) return layer.type==LayerType::Text;
    if(property==BoundProperty::Fill) return layer.type==LayerType::Shape&&layer.hasFill;
    return property==BoundProperty::Stroke&&layer.type==LayerType::Shape&&layer.hasStroke;
  }
  if(property==BoundProperty::X||property==BoundProperty::Y) return true;
  if(property==BoundProperty::ScrollWidth||property==BoundProperty::ScrollSpeed)
    return layer.type==LayerType::Text&&layer.scroll.enabled;
  if(layer.type==LayerType::Text) return property==BoundProperty::Size;
  if(layer.type!=LayerType::Shape) return false;
  if(property==BoundProperty::StrokeWidth) return true;
  if(layer.shape==Shape::Rectangle)
    return property==BoundProperty::Width||property==BoundProperty::Height||property==BoundProperty::CornerRadius;
  if(layer.shape==Shape::Circle) return property==BoundProperty::Radius;
  return property==BoundProperty::X2||property==BoundProperty::Y2;
}
static bool declaredSource(const Theme& theme,const std::string& source) {
  return validFieldToken(source,theme);
}
static bool twoReals(JsonVariantConst value,const Fields& fields,const char* key,double& first,double& second) {
  JsonArrayConst values=value.as<JsonArrayConst>();
  if(values.isNull()||values.size()!=2) return fields.fail(key,"expected exactly two finite numbers");
  JsonVariantConst a=values[0],b=values[1];
  if((!a.is<double>()&&!a.is<long>()&&!a.is<int>())||(!b.is<double>()&&!b.is<long>()&&!b.is<int>()))
    return fields.fail(key,"expected exactly two finite numbers");
  first=a.as<double>();second=b.as<double>();
  return (std::isfinite(first)&&std::isfinite(second))||fields.fail(key,"expected exactly two finite numbers");
}
static bool twoIntegers(JsonVariantConst value,const Fields& fields,const char* key,int lo,int hi,int& first,int& second) {
  JsonArrayConst values=value.as<JsonArrayConst>();
  if(values.isNull()||values.size()!=2||!values[0].is<int>()||!values[1].is<int>())
    return fields.fail(key,"expected exactly two integers from "+std::to_string(lo)+" to "+std::to_string(hi));
  first=values[0].as<int>();second=values[1].as<int>();
  if(first<lo||first>hi||second<lo||second>hi)
    return fields.fail(key,"expected exactly two integers from "+std::to_string(lo)+" to "+std::to_string(hi));
  return true;
}
static bool parseScroll(JsonObjectConst object,const std::string& path,std::string& error,Layer& result) {
  JsonObjectConst scroll=object["scroll"].as<JsonObjectConst>();
  Fields fields(scroll,path+".scroll",error);
  if(!fields.keys("|width|mode|speed|pause|gap|")) return false;
  if(!fields.number("width",1,240,result.scroll.width)) return false;
  std::string mode;
  if(!fields.text("mode",mode,6)||!fields.number("speed",1,240,result.scroll.speed)) return false;
  if(mode=="loop") result.scroll.mode=ScrollMode::Loop;
  else if(mode=="bounce") result.scroll.mode=ScrollMode::Bounce;
  else return fields.fail("mode","expected loop or bounce");
  if(fields.has("pause")&&!fields.number("pause",0,10000,result.scroll.pauseMs)) return false;
  if(fields.has("gap")&&!fields.number("gap",0,240,result.scroll.gap)) return false;
  if(result.scroll.mode==ScrollMode::Bounce&&fields.has("gap")) return fields.fail("gap","not allowed in bounce mode");
  result.scroll.enabled=true;
  return true;
}
static bool parseBindings(JsonObjectConst object,const Theme& theme,const std::string& path,std::string& error,Layer& layer) {
  JsonObjectConst objectBindings=object["bind"].as<JsonObjectConst>();
  Fields bindings(objectBindings,path+".bind",error);
  if(objectBindings.isNull()) return bindings.fail("","expected an object");
  if(objectBindings.size()>8) return bindings.fail("","expected at most 8 bindings");
  for(JsonPairConst pair:objectBindings) {
    std::string name(pair.key().c_str(),pair.key().size());
    Binding binding;
    if(numericProperty(name,binding.property)) binding.color=false;
    else if(colorProperty(name,binding.property)) binding.color=true;
    else return bindings.fail(name.c_str(),"unknown binding target");
    if(!applicable(layer,binding.property,binding.color)) return bindings.fail(name.c_str(),"binding is not applicable to this layer");
    JsonObjectConst declaration=pair.value().as<JsonObjectConst>();
    Fields fields(declaration,path+".bind."+name,error);
    if(declaration.isNull()) return fields.fail("","expected an object");
    if(binding.color) {
      if(!fields.keys("|source|stops|")||!fields.text("source",binding.source,64)) return false;
      if(!declaredSource(theme,binding.source)) return fields.fail("source","expected a declared data field");
      JsonArrayConst stops=declaration["stops"].as<JsonArrayConst>();
      if(stops.isNull()||stops.size()==0||stops.size()>8) return fields.fail("stops","expected 1 to 8 stops");
      double previous=0;
      for(size_t i=0;i<stops.size();++i) {
        JsonObjectConst item=stops[i].as<JsonObjectConst>();
        Fields stop(item,path+".bind."+name+".stops["+std::to_string(i)+"]",error);
        ColorStop color;
        if(!stop.keys("|at|value|")||!stop.real("at",color.at)||!stop.color("value",color.value)) return false;
        if(i&&color.at<=previous) return stop.fail("at","stops must be strictly increasing");
        previous=color.at;binding.colors.stops.push_back(color);
      }
    } else {
      int lo=0,hi=0;
      if(!propertyRange(binding.property,lo,hi)) return fields.fail("","expected a numeric binding target");
      if(!fields.keys("|source|input|output|clamp|")||!fields.text("source",binding.source,64)) return false;
      if(!declaredSource(theme,binding.source)) return fields.fail("source","expected a declared data field");
      if(!twoReals(declaration["input"],fields,"input",binding.numeric.input0,binding.numeric.input1)) return false;
      if(binding.numeric.input0==binding.numeric.input1) return fields.fail("input","endpoints must differ");
      if(!twoIntegers(declaration["output"],fields,"output",lo,hi,binding.numeric.output0,binding.numeric.output1)) return false;
      if(fields.has("clamp")&&!fields.boolean("clamp",binding.numeric.clamp)) return false;
    }
    layer.bindings.push_back(std::move(binding));
  }
  return true;
}
bool parseTheme(const std::string& json, Theme& out, std::string& error) {
  error.clear();
  if(json.empty()||json.size()>MaxManifest) {error="theme.json: expected 1 to 16384 bytes";return false;}
  JsonDocument doc;JsonReader reader(json);
  auto err=deserializeJson(doc,reader,DeserializationOption::NestingLimit(8));
  if(err) {error="theme.json: "+std::string(err.c_str())+" at byte "+std::to_string(reader.position);return false;}
  for(size_t i=reader.position;i<json.size();++i) {
    if(json[i]!=' '&&json[i]!='\n'&&json[i]!='\r'&&json[i]!='\t') {error="theme.json: trailing content after manifest";return false;}
  }
  if(!DuplicateBindingScanner(json,error).valid()) return false;
  auto root=doc.as<JsonObjectConst>();Fields r(root,"",error);int spec=0;
  if(!r.keys("|spec|theme|display|layers|data|")||!r.number("spec",1,1,spec))return false;
  Theme t;
  auto meta=root["theme"].as<JsonObjectConst>();Fields m(meta,"theme",error);
  if(!m.keys("|id|name|author|version|")||!m.text("id",t.id,48))return false;
  if(!validId(t.id))return m.fail("id","use ASCII letters, digits, '-' or '_'");
  if(!m.text("name",t.name,96)||!m.text("author",t.author,96)||!m.text("version",t.version,32))return false;
  auto display=root["display"].as<JsonObjectConst>();Fields d(display,"display",error);int w=0,h=0;
  if(!d.keys("|width|height|background|")||!d.number("width",240,240,w)||!d.number("height",240,240,h)||!d.color("background",t.background))return false;
  bool hasData=false;for(JsonPairConst pair:root)if(pair.key()=="data")hasData=true;
  if(hasData) {
    auto sources=root["data"].as<JsonArrayConst>();
    if(sources.isNull())return r.fail("data","expected an array of sources");
    if(sources.size()>MaxDataSources)return r.fail("data","expected at most 4 sources");
    for(JsonObjectConst source:sources) {
      ThemeDataSource data;Fields sf(source,"data["+std::to_string(t.data.size())+"]",error);int interval=0;
      if(!sf.keys("|id|url|interval|insecureTls|fields|")||!sf.text("id",data.id,32)||!sf.text("url",data.url,200)||!sf.number("interval",10,86400,interval)||!sf.boolean("insecureTls",data.insecureTls,false))return false;
      if(!validId(data.id)||data.id.find('.')!=std::string::npos)return sf.fail("id","use ASCII letters, digits, '-' or '_'");
      size_t hostStart=data.url.rfind("https://",0)==0?8:data.url.rfind("http://",0)==0?7:std::string::npos;
      if(hostStart==std::string::npos)return sf.fail("url","only http:// and https:// URLs are supported");
      if(hostStart==8&&!data.insecureTls)return sf.fail("insecureTls","set true to acknowledge that HTTPS certificate validation is unavailable");
      size_t hostEnd=data.url.find('/',hostStart);
      if(hostStart==data.url.size()||(hostEnd==hostStart)||(data.url[hostStart]=='?'||data.url[hostStart]=='#'))return sf.fail("url","URL must include a host");
      data.interval=static_cast<uint32_t>(interval);
      auto fields=source["fields"].as<JsonArrayConst>();if(fields.isNull()||fields.size()==0||fields.size()>MaxDataFields)return sf.fail("fields","expected 1 to 8 fields");
      for(JsonObjectConst field:fields) {
        ThemeDataField item;Fields ff(field,"data["+std::to_string(t.data.size())+"].fields["+std::to_string(data.fields.size())+"]",error);
        if(!ff.keys("|id|path|")||!ff.text("id",item.id,24)||!ff.text("path",item.path,96))return false;
        if(!validId(item.id)||item.id.find('.')!=std::string::npos)return ff.fail("id","use ASCII letters, digits, '-' or '_'");
        for(const auto& old:data.fields)if(old.id==item.id)return ff.fail("id","duplicate field ID");
        size_t pathPart=0;
        for(size_t p=0;p<=item.path.size();++p) {
          if(p==item.path.size()||item.path[p]=='.') {if(p==pathPart)return ff.fail("path","use non-empty dotted JSON object keys");pathPart=p+1;continue;}
          char c=item.path[p];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return ff.fail("path","use a dotted JSON object path");
        }
        data.fields.push_back(std::move(item));
      }
      for(const auto& old:t.data)if(old.id==data.id)return sf.fail("id","duplicate data source ID");
      t.data.push_back(std::move(data));
    }
  }
  auto layers=root["layers"].as<JsonArrayConst>();
  if(layers.isNull()||layers.size()>MaxLayers)return r.fail("layers","expected an array of at most 32 layers");
  for(JsonObjectConst o:layers) {
    Layer l;std::string type;std::string layerPath="layers["+std::to_string(t.layers.size())+"]";Fields f(o,layerPath,error);
    if(o.isNull())return f.fail("","expected an object");
    if(!f.text("id",l.id,48))return false;
    if(!validId(l.id))return f.fail("id","use ASCII letters, digits, '-' or '_'");
    for(const auto& existing:t.layers)if(existing.id==l.id)return f.fail("id","duplicate layer ID");
    if(!f.text("type",type,16)||!f.number("x",-240,479,l.x)||!f.number("y",-240,479,l.y))return false;
    if(type=="text") {
      l.type=LayerType::Text;
      if(!f.keys("|id|type|x|y|anchor|value|size|color|scroll|bind|")||!f.text("value",l.value,128))return false;
      if(!validText(l.value,t))return f.fail("value","use printable ASCII and declared clock/data variables only");
      if(!f.number("size",8,96,l.size)||!f.color("color",l.color))return false;
      std::string anchor="top-left";if(!f.text("anchor",anchor,20,false))return false;
      const char* anchors[]={"top-left","top-center","top-right","center-left","center","center-right","bottom-left","bottom-center","bottom-right"};
      int a=0;for(;a<9;++a)if(anchor==anchors[a])break;
      if(a==9)return f.fail("anchor","unsupported text anchor");
      l.anchorX=a%3;l.anchorY=a/3;
    } else if(type=="image"||type=="animation") {
      l.type=type=="image"?LayerType::Image:LayerType::Animation;
      if(!f.keys(type=="image"?"|id|type|x|y|source|scroll|bind|":"|id|type|x|y|width|height|source|frames|fps|loop|scroll|bind|")||!f.text("source",l.source,110))return false;
      if(!validPath(l.source))return f.fail("source","expected a safe relative asset path");
      if(type=="animation") {
        int frames=0,fps=0;
        if(!f.number("width",1,240,l.width)||!f.number("height",1,240,l.height)||!f.number("frames",1,240,frames)||!f.number("fps",1,15,fps))return false;
        if(!o["loop"].is<bool>())return f.fail("loop","expected true or false");
        l.frames=frames;l.fps=fps;l.loop=o["loop"].as<bool>();
      }
      if(assetPath(l,l.frames-1).size()>120)return f.fail("source","compiled asset path exceeds 120 bytes");
    } else if(type=="shape") {
      l.type=LayerType::Shape;std::string shape;
      if(!f.text("shape",shape,16))return false;
      if(shape!="rectangle"&&shape!="circle"&&shape!="line")return f.fail("shape","expected rectangle, circle or line");
      const char* allowed=shape=="rectangle"?"|id|type|shape|x|y|width|height|fill|stroke|strokeWidth|cornerRadius|scroll|bind|":
        shape=="circle"?"|id|type|shape|x|y|radius|fill|stroke|strokeWidth|scroll|bind|":"|id|type|shape|x|y|x2|y2|stroke|strokeWidth|scroll|bind|";
      if(!f.keys(allowed))return false;
      l.hasFill=!o["fill"].isNull();l.hasStroke=!o["stroke"].isNull();
      if(!l.hasFill&&!l.hasStroke)return f.fail(shape=="line"?"stroke":"fill","provide a fill or stroke color");
      if((l.hasFill&&!f.color("fill",l.fill))||(l.hasStroke&&!f.color("stroke",l.stroke))||!f.number("strokeWidth",1,32,l.strokeWidth,false))return false;
      if(shape=="rectangle") {
        if(!f.number("width",1,240,l.width)||!f.number("height",1,240,l.height))return false;
        if(f.has("cornerRadius")&&!f.number("cornerRadius",0,120,l.cornerRadius))return false;
      } else if(shape=="circle") {
        l.shape=Shape::Circle;if(!f.number("radius",1,240,l.radius))return false;
      } else {
        l.shape=Shape::Line;
        if(!l.hasStroke)return f.fail("stroke","required for a line");
        if(!f.number("x2",-240,479,l.x2)||!f.number("y2",-240,479,l.y2))return false;
      }
    } else return f.fail("type","expected text, image, animation or shape");
    if(f.has("scroll")) {
      if(l.type!=LayerType::Text) return f.fail("scroll","only allowed on text layers");
      if(!parseScroll(o,layerPath,error,l)) return false;
    }
    if(f.has("bind")&&!parseBindings(o,t,layerPath,error,l)) return false;
    t.layers.push_back(std::move(l));
  }
  out=std::move(t);error.clear();return true;
}
std::string expandText(const std::string& value,const tm* t) {
  static const std::vector<ThemeValue> empty;
  return expandText(value,t,empty);
}
std::string expandText(const std::string& value,const tm* t,const std::vector<ThemeValue>& values) {
  static const char* months[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
  static const char* days[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
  std::string out;
  for(size_t i=0;i<value.size();) {
    if(value[i]!='{') {out+=value[i++];continue;}
    size_t end=value.find('}',i);
    if(end==std::string::npos) {out+=value.substr(i);break;}
    std::string key=value.substr(i+1,end-i-1), v="--";
    if(t) {
      char b[16]; int n=-1;
      if(key=="HH") n=t->tm_hour;
      else if(key=="hh") n=t->tm_hour%12?t->tm_hour%12:12;
      else if(key=="MM") n=t->tm_min;
      else if(key=="SS") n=t->tm_sec;
      else if(key=="DD") n=t->tm_mday;
      else if(key=="YYYY") n=t->tm_year+1900;
      if(n>=0) {snprintf(b,sizeof(b),key=="YYYY"?"%04d":"%02d",n);v=b;}
      else if((key=="MON"||key=="MONTH")&&t->tm_mon>=0&&t->tm_mon<12) {v=months[t->tm_mon];if(key=="MON") v.resize(3);}
      else if((key=="WD"||key=="WEEKDAY")&&t->tm_wday>=0&&t->tm_wday<7) {v=days[t->tm_wday];if(key=="WD") v.resize(3);}
    }
    if(v=="--") for(const auto& item:values) if(item.key==key) {v=item.value;break;}
    out+=v; i=end+1;
  }
  return out;
}
std::string assetPath(const Layer& l,uint16_t frame) {
  if(l.type==LayerType::Image) return l.source+".sti";
  char b[24]; snprintf(b,sizeof(b),"/%03u.png.sti",frame);
  return l.source+b;
}
void Engine::resolveLayer(size_t index) {
  const Layer& layer=theme_.layers[index];
  ResolvedLayer resolved;
  resolved.x=layer.x;resolved.y=layer.y;resolved.width=layer.width;resolved.height=layer.height;
  resolved.x2=layer.x2;resolved.y2=layer.y2;resolved.radius=layer.radius;
  resolved.cornerRadius=layer.cornerRadius;resolved.size=layer.size;
  resolved.strokeWidth=layer.strokeWidth;resolved.scrollWidth=layer.scroll.width;
  resolved.scrollSpeed=layer.scroll.speed;resolved.color=layer.color;
  resolved.fill=layer.fill;resolved.stroke=layer.stroke;
  for(const auto& binding:layer.bindings) {
    const std::string* source=nullptr;
    for(const auto& item:values_) if(item.key==binding.source) {source=&item.value;break;}
    double value=0;
    if(!source||!parseFiniteNumber(*source,value)) continue;
    if(binding.color) {
      const uint16_t color=resolveColorBinding(binding.colors,value);
      if(binding.property==BoundProperty::Color) resolved.color=color;
      else if(binding.property==BoundProperty::Fill) resolved.fill=color;
      else if(binding.property==BoundProperty::Stroke) resolved.stroke=color;
      continue;
    }
    int lo=0,hi=0;
    if(!propertyRange(binding.property,lo,hi)) continue;
    const int number=resolveNumericBinding(binding.numeric,value,lo,hi);
    switch(binding.property) {
      case BoundProperty::X: resolved.x=number;break;
      case BoundProperty::Y: resolved.y=number;break;
      case BoundProperty::Width: resolved.width=number;break;
      case BoundProperty::Height: resolved.height=number;break;
      case BoundProperty::Radius: resolved.radius=number;break;
      case BoundProperty::CornerRadius: resolved.cornerRadius=number;break;
      case BoundProperty::X2: resolved.x2=number;break;
      case BoundProperty::Y2: resolved.y2=number;break;
      case BoundProperty::Size: resolved.size=number;break;
      case BoundProperty::StrokeWidth: resolved.strokeWidth=number;break;
      case BoundProperty::ScrollWidth: resolved.scrollWidth=number;break;
      case BoundProperty::ScrollSpeed: resolved.scrollSpeed=number;break;
      default: break;
    }
  }
  if(layer.type==LayerType::Shape&&layer.shape==Shape::Rectangle)
    resolved.cornerRadius=std::min(resolved.cornerRadius,
      std::max(0,std::min(resolved.width,resolved.height)/2));
  states_[index].resolved=resolved;
}
// True once `now` is strictly past `deadline`; wrap-safe across millis() rollover.
static bool scrollDeadlinePassed(uint32_t now,uint32_t deadline) {
  return int32_t(now-deadline)>0;
}
static void resetScroll(LayerState& s,const Layer& l,uint32_t now) {
  s.scrollOffset=0;s.scrollPhase=0;s.scrollDirection=-1;
  s.scrollLastMs=now;s.scrollPauseUntil=now+l.scroll.pauseMs;
}
// Advances loop/bounce scroll state to `now`, resolving however many pause/leg
// boundaries fall within the elapsed interval without iterating per pixel.
static void advanceScroll(const Layer& l,LayerState& s,uint32_t now,int textWidth) {
  const int viewportWidth=s.resolved.scrollWidth;
  const int speed=s.resolved.scrollSpeed;
  const bool loop=l.scroll.mode==ScrollMode::Loop;
  const int target=loop?(textWidth+l.scroll.gap):(textWidth-viewportWidth);
  if(textWidth<=viewportWidth||target<=0||speed<=0) return;
  for(;;) {
    if(!scrollDeadlinePassed(now,s.scrollPauseUntil)) return;
    const uint32_t base=scrollDeadlinePassed(s.scrollLastMs,s.scrollPauseUntil)?s.scrollLastMs:s.scrollPauseUntil;
    const uint32_t elapsed=now-base;
    const uint64_t ticksAvailable=uint64_t(elapsed)*speed+s.scrollPhase;
    const int legProgress=loop?s.scrollOffset:(s.scrollDirection<0?s.scrollOffset:target-s.scrollOffset);
    const uint64_t neededTicks=uint64_t(target-legProgress)*1000;
    if(ticksAvailable<neededTicks) {
      const uint64_t newProgressTicks=uint64_t(legProgress)*1000+ticksAvailable;
      const int newProgressPixels=int(newProgressTicks/1000);
      s.scrollPhase=uint32_t(newProgressTicks%1000);
      s.scrollOffset=loop?newProgressPixels:(s.scrollDirection<0?newProgressPixels:target-newProgressPixels);
      s.scrollLastMs=now;
      return;
    }
    // The leg completes before `now`; find the exact millisecond it did, so the
    // leftover sub-pixel remainder and the next pause carry no rounding drift.
    const uint64_t remainingTicks=neededTicks-s.scrollPhase;
    const uint64_t msNeeded=(remainingTicks+uint64_t(speed)-1)/speed;
    const uint32_t completion=base+uint32_t(msNeeded);
    const uint64_t leftover=msNeeded*speed+s.scrollPhase-neededTicks;
    s.scrollPhase=uint32_t(leftover);
    s.scrollLastMs=completion;
    s.scrollPauseUntil=completion+l.scroll.pauseMs;
    if(loop) {
      s.scrollOffset=0;
    } else if(s.scrollDirection<0) {
      s.scrollOffset=target;s.scrollDirection=1;
    } else {
      s.scrollOffset=0;s.scrollDirection=-1;
    }
  }
}
static bool sameGeometry(const ResolvedLayer& a,const ResolvedLayer& b) {
  return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height&&
    a.x2==b.x2&&a.y2==b.y2&&a.radius==b.radius&&
    a.cornerRadius==b.cornerRadius&&a.size==b.size&&
    a.strokeWidth==b.strokeWidth&&a.scrollWidth==b.scrollWidth;
}
static bool sameColors(const ResolvedLayer& a,const ResolvedLayer& b) {
  return a.color==b.color&&a.fill==b.fill&&a.stroke==b.stroke;
}
static Rect bounds(const Layer& l,const ResolvedLayer& resolved,const std::string& text) {
  if(l.type==LayerType::Text) {
    int w=l.scroll.enabled?resolved.scrollWidth:static_cast<int>(text.size())*((resolved.size*6+7)/8);
    return Rect(resolved.x-w*l.anchorX/2,resolved.y-resolved.size*l.anchorY/2,w,resolved.size);
  }
  if(l.type==LayerType::Shape && l.shape==Shape::Circle && resolved.radius==0)
    return Rect(resolved.x,resolved.y,0,0);
  if(l.type==LayerType::Shape && l.shape==Shape::Circle)
    return Rect(resolved.x-resolved.radius,resolved.y-resolved.radius,2*resolved.radius+1,2*resolved.radius+1);
  if(l.type==LayerType::Shape && l.shape==Shape::Line) {
    int pad=(resolved.strokeWidth+1)/2;
    return Rect(std::min(resolved.x,resolved.x2)-pad,std::min(resolved.y,resolved.y2)-pad,
      abs(resolved.x-resolved.x2)+2*pad+1,abs(resolved.y-resolved.y2)+2*pad+1);
  }
  return Rect(resolved.x,resolved.y,resolved.width,resolved.height);
}
void Engine::setTheme(Theme theme,uint32_t now) {
  theme_=std::move(theme); states_.assign(theme_.layers.size(),LayerState{});
  for(size_t i=0;i<states_.size();++i) {states_[i].lastMs=now;resolveLayer(i);}
  full_=true;
}
void Engine::setValues(std::vector<ThemeValue> values) {
  if(values_==values) return;
  values_=std::move(values);valuesChanged_=true;
}
static void addDirty(std::vector<Rect>& dirty,Rect r) {
  r=intersect(r,Rect(0,0,240,240)); if(r.empty()) return;
  for(size_t i=0;i<dirty.size();) {
    if(!intersect(r,dirty[i]).empty()) {r=unite(r,dirty[i]);dirty.erase(dirty.begin()+i);i=0;}
    else ++i;
  }
  dirty.push_back(r);
}
std::vector<Rect> Engine::update(uint32_t now,const tm* time) {
  std::vector<Rect> dirty;
  const bool timeChanged=full_ || bool(time)!=hadTime_ || (time &&
    (time->tm_sec!=lastTime_.tm_sec || time->tm_min!=lastTime_.tm_min ||
     time->tm_hour!=lastTime_.tm_hour || time->tm_mday!=lastTime_.tm_mday ||
     time->tm_mon!=lastTime_.tm_mon || time->tm_year!=lastTime_.tm_year || time->tm_wday!=lastTime_.tm_wday));
  hadTime_=bool(time); if(time) lastTime_=*time;
  for(size_t i=0;i<states_.size();++i) {
    auto& s=states_[i]; const auto& l=theme_.layers[i]; bool changed=full_;
    const ResolvedLayer previous=s.resolved;
    if(valuesChanged_) {
      resolveLayer(i);
      changed=changed||!sameGeometry(previous,s.resolved)||!sameColors(previous,s.resolved);
    }
    bool textChanged=false;
    if(l.type==LayerType::Text && (timeChanged || valuesChanged_)) {
      std::string text=expandText(l.value,time,values_);
      if(text!=s.text) {s.text=std::move(text);changed=true;textChanged=true;}
    } else if(l.type==LayerType::Animation && !s.finished) {
      uint64_t ticks=uint64_t(uint32_t(now-s.lastMs))*l.fps+s.phase;
      uint64_t step=ticks/1000; s.phase=ticks%1000; s.lastMs=now;
      uint16_t next=l.loop?(s.frame+step)%l.frames:std::min<uint64_t>(s.frame+step,l.frames-1);
      if(!l.loop && next==l.frames-1) s.finished=true;
      if(next!=s.frame) {s.frame=next;changed=true;}
    }
    if(l.type==LayerType::Text && l.scroll.enabled) {
      const bool scrollResetNeeded=textChanged||previous.size!=s.resolved.size||
        previous.scrollWidth!=s.resolved.scrollWidth||previous.scrollSpeed!=s.resolved.scrollSpeed;
      if(scrollResetNeeded) {
        resetScroll(s,l,now);changed=true;
      } else {
        const int textWidth=static_cast<int>(s.text.size())*((s.resolved.size*6+7)/8);
        const int previousOffset=s.scrollOffset;
        advanceScroll(l,s,now,textWidth);
        if(s.scrollOffset!=previousOffset) changed=true;
      }
    }
    Rect next=bounds(l,s.resolved,s.text);
    if(changed&&!full_) addDirty(dirty,unite(s.bounds,next));
    s.bounds=next;
  }
  if(full_) {dirty.assign(1,Rect(0,0,240,240));full_=false;}
  valuesChanged_=false;
  return dirty;
}
static uint16_t blend(uint16_t bg,uint16_t fg,uint8_t a) {
  if(a==255) return fg;
  if(a==0) return bg;
  unsigned b=255-a;
  return ((((fg>>11)*a+(bg>>11)*b+127)/255)<<11) |
    (((((fg>>5)&63)*a+((bg>>5)&63)*b+127)/255)<<5) |
    (((fg&31)*a+(bg&31)*b+127)/255);
}
static bool linePixel(const Layer& l,int x,int y) {
  int64_t dx=l.x2-l.x,dy=l.y2-l.y,px=x-l.x,py=y-l.y;
  int64_t len=dx*dx+dy*dy,dot=px*dx+py*dy;
  int64_t sw=l.strokeWidth;
  if(dot<=0||len==0) return 4*(px*px+py*py)<=sw*sw;
  if(dot>=len) {px=x-l.x2;py=y-l.y2;return 4*(px*px+py*py)<=sw*sw;}
  int64_t cross=px*dy-py*dx;return 4*cross*cross<=sw*sw*len;
}
bool render(const Engine& e,const std::vector<Rect>& dirty,Assets& assets,Display& display) {
  uint16_t pixels[240], colors[240]; uint8_t alpha[240];
  if(e.theme().layers.size()>MaxLayers || e.states().size()!=e.theme().layers.size()) return false;
  AssetHandle handles[MaxLayers];
  std::fill(handles,handles+MaxLayers,InvalidAsset);
  for(Rect region:dirty) {
    region=intersect(region,Rect(0,0,240,240));
    for(int y=region.y;y<region.y+region.h;++y) {
      std::fill(pixels,pixels+region.w,e.theme().background);
      for(size_t i=0;i<e.theme().layers.size();++i) {
        const auto& l=e.theme().layers[i];const auto& s=e.states()[i];
        Rect r=intersect(s.bounds,Rect(region.x,y,region.w,1));if(r.empty()) continue;
        if(l.type==LayerType::Image||l.type==LayerType::Animation) {
          if(handles[i]==InvalidAsset) handles[i]=assets.resolve(assetPath(l,s.frame));
          if(handles[i]==InvalidAsset || !assets.row(handles[i],y-l.y,r.x-l.x,r.w,colors,alpha)) return false;
          for(int x=0;x<r.w;++x) pixels[r.x-region.x+x]=blend(pixels[r.x-region.x+x],colors[x],alpha[x]);
          continue;
        }
        for(int x=r.x;x<r.x+r.w;++x) {
          uint16_t c=0; bool draw=false;
          if(l.type==LayerType::Text) {
            int cell=(l.size*6+7)/8, local=x-s.bounds.x;
            int col=(local%cell)*6/cell, row=(y-s.bounds.y)*8/l.size;
            draw=col<5 && (display.glyphColumn(s.text[local/cell],col)&(1<<row));c=l.color;
          } else {
            bool inside=false,edge=false;
            if(l.shape==Shape::Rectangle) {
              inside=true;edge=x-l.x<l.strokeWidth || l.x+l.width-x<=l.strokeWidth || y-l.y<l.strokeWidth || l.y+l.height-y<=l.strokeWidth;
            } else if(l.shape==Shape::Circle) {
              int dx=x-l.x,dy=y-l.y,dist=dx*dx+dy*dy,inner=std::max(0,l.radius-l.strokeWidth);
              inside=dist<=l.radius*l.radius;edge=inside && (inner==0||dist>inner*inner);
            } else {inside=edge=linePixel(l,x,y);}
            if(inside&&l.hasFill) {draw=true;c=l.fill;}
            if(edge&&l.hasStroke) {draw=true;c=l.stroke;}
          }
          if(draw) pixels[x-region.x]=c;
        }
      }
      display.row(region.x,y,pixels,region.w);
    }
  }
  return true;
}
}
#endif
