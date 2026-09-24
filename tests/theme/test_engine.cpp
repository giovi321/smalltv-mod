#include "ThemeEngine.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace smalltv;
static Theme scrollingTheme(ScrollMode mode=ScrollMode::Loop) {
  Theme theme;Layer text;text.value="ABCDEFGHIJ";text.size=8;
  text.x=120;text.y=100;text.anchorX=text.anchorY=1;
  text.scroll.enabled=true;text.scroll.width=24;text.scroll.speed=20;
  text.scroll.pauseMs=1000;text.scroll.gap=6;text.scroll.mode=mode;
  theme.layers.push_back(text);return theme;
}
static void testScrollViewportAndDirty() {
  for(uint32_t start:{0u,0xffffffc0u}) {
    Engine engine;engine.setTheme(scrollingTheme(),start);
    engine.update(start,nullptr);
    const Rect viewport=engine.states()[0].bounds;
    assert(viewport.x==108&&viewport.y==96&&viewport.w==24&&viewport.h==8);
    assert(engine.update(start+999,nullptr).empty());
    assert(engine.update(start+1049,nullptr).empty());
    auto dirty=engine.update(start+1050,nullptr);
    assert(dirty.size()==1&&dirty[0].x==108&&dirty[0].y==96&&dirty[0].w==24&&dirty[0].h==8);
    assert(engine.update(start+1051,nullptr).empty());
  }
}
static void testScrollTiming() {
  for(uint32_t start:{0u,0xffffffc0u}) {
    Engine loop;loop.setTheme(scrollingTheme(),start);loop.update(start,nullptr);
    assert(loop.states()[0].scrollOffset==0&&loop.states()[0].scrollDirection==-1);
    loop.update(start+1050,nullptr);assert(loop.states()[0].scrollOffset==1);
    loop.update(start+1500,nullptr);assert(loop.states()[0].scrollOffset==10);
    loop.update(start+4299,nullptr);assert(loop.states()[0].scrollOffset==65);
    loop.update(start+4300,nullptr);assert(loop.states()[0].scrollOffset==0);
    assert(loop.update(start+5300,nullptr).empty());
    loop.update(start+5350,nullptr);assert(loop.states()[0].scrollOffset==1);
    loop.update(start+430001500u,nullptr);assert(loop.states()[0].scrollOffset==10);
    loop.update(start+430005800u,nullptr);assert(loop.states()[0].scrollOffset==10);
    // Whole cycles that return to the same integer offset cause no repaint.
    assert(loop.update(start+430010100u,nullptr).empty());

    Engine bounce;bounce.setTheme(scrollingTheme(ScrollMode::Bounce),start);bounce.update(start,nullptr);
    bounce.update(start+1500,nullptr);assert(bounce.states()[0].scrollOffset==10);
    bounce.update(start+2800,nullptr);
    assert(bounce.states()[0].scrollOffset==36&&bounce.states()[0].scrollDirection==1);
    assert(bounce.states()[0].scrollPauseUntil==uint32_t(start+3800));
    assert(bounce.update(start+3800,nullptr).empty());
    bounce.update(start+3850,nullptr);assert(bounce.states()[0].scrollOffset==35);
    bounce.update(start+5600,nullptr);
    assert(bounce.states()[0].scrollOffset==0&&bounce.states()[0].scrollDirection==-1);
    assert(bounce.update(start+6600,nullptr).empty());
    bounce.update(start+560004350u,nullptr);
    assert(bounce.states()[0].scrollOffset==25&&bounce.states()[0].scrollDirection==1);
    assert(bounce.update(start+560009950u,nullptr).empty());

    // Non-integral travel duration must not acquire rounded-millisecond drift.
    Theme fractional=scrollingTheme();fractional.layers[0].scroll.speed=7;
    fractional.layers[0].scroll.pauseMs=333;
    Engine exact;exact.setTheme(fractional,start);exact.update(start,nullptr);
    exact.update(start+68331,nullptr);assert(exact.states()[0].scrollOffset==0);
    assert(exact.update(start+68806,nullptr).empty());
    exact.update(start+68807,nullptr);assert(exact.states()[0].scrollOffset==1);

    Theme noPause=scrollingTheme(ScrollMode::Bounce);noPause.layers[0].scroll.pauseMs=0;
    Engine continuous;continuous.setTheme(noPause,start);continuous.update(start,nullptr);
    continuous.update(start+1800,nullptr);assert(continuous.states()[0].scrollOffset==36);
    continuous.update(start+1850,nullptr);assert(continuous.states()[0].scrollOffset==35);
    continuous.update(start+3600,nullptr);assert(continuous.states()[0].scrollOffset==0);

    for(const char* value:{"","ABC","ABCD"}) {
      Theme shortText=scrollingTheme();shortText.layers[0].value=value;
      Engine fixed;fixed.setTheme(shortText,start);fixed.update(start,nullptr);
      assert(fixed.states()[0].bounds.w==24);
      assert(fixed.update(start+1000000,nullptr).empty());
      assert(fixed.states()[0].scrollOffset==0);
    }
  }
}
static Binding scrollBinding(BoundProperty property,const char* source,int low,int high) {
  Binding binding;binding.property=property;binding.source=source;
  binding.numeric.output0=low;binding.numeric.output1=high;return binding;
}
static void testScrollReset() {
  for(uint32_t start:{0u,0xffffffc0u}) {
    Theme theme=scrollingTheme(ScrollMode::Bounce);auto& layer=theme.layers[0];
    layer.value="{data.text}";
    layer.bindings={scrollBinding(BoundProperty::Size,"data.size",8,16),
      scrollBinding(BoundProperty::ScrollWidth,"data.width",24,30),
      scrollBinding(BoundProperty::ScrollSpeed,"data.speed",20,40),
      scrollBinding(BoundProperty::X,"data.x",120,130)};
    Engine engine;engine.setValues({{"data.text","ABCDEFGHIJ"}});
    engine.setTheme(theme,start);engine.update(start,nullptr);
    engine.update(start+3875,nullptr);
    assert(engine.states()[0].scrollOffset==35&&engine.states()[0].scrollPhase==500);
    engine.setValues({{"data.text","ABCDEFGHIJ"},{"unused.value","changed"}});
    assert(engine.update(start+3875,nullptr).empty());
    assert(engine.states()[0].scrollOffset==35&&engine.states()[0].scrollPhase==500&&engine.states()[0].scrollDirection==1);
    engine.invalidate();auto dirty=engine.update(start+3875,nullptr);
    assert(dirty.size()==1&&dirty[0].w==240&&dirty[0].h==240);
    assert(engine.states()[0].scrollOffset==35&&engine.states()[0].scrollPhase==500&&engine.states()[0].scrollDirection==1);
    engine.setValues({{"data.text","ABCDEFGHIJ"},{"data.x","1"}});
    engine.update(start+3875,nullptr);
    assert(engine.states()[0].scrollOffset==35&&engine.states()[0].scrollPhase==500);

    std::vector<ThemeValue> values={{"data.text","ABCDEFGHIJK"},{"data.x","1"}};
    uint32_t now=start+4000;
    for(const char* source:{"data.text","data.size","data.width","data.speed"}) {
      if(std::string(source)!="data.text") values.push_back({source,"1"});
      engine.setValues(values);assert(!engine.update(now,nullptr).empty());
      const auto& state=engine.states()[0];
      assert(state.scrollOffset==0&&state.scrollPhase==0&&state.scrollDirection==-1);
      assert(state.scrollLastMs==now&&state.scrollPauseUntil==uint32_t(now+1000));
      // Reinstalling the same values must not restart the pause.
      engine.setValues(values);assert(engine.update(now+999,nullptr).empty());
      engine.update(now+1050,nullptr);assert(engine.states()[0].scrollOffset>0);
      now+=2000;
    }
    engine.setTheme(theme,now);engine.update(now,nullptr);
    assert(engine.states()[0].scrollOffset==0&&engine.states()[0].scrollDirection==-1);
  }
  // Scroll dirt is clipped to the canvas, while bounds retain viewport placement.
  Theme clipped=scrollingTheme();clipped.layers[0].x=0;
  Engine engine;engine.setTheme(clipped,0);engine.update(0,nullptr);
  auto dirty=engine.update(1050,nullptr);
  assert(engine.states()[0].bounds.x==-12);
  assert(dirty.size()==1&&dirty[0].x==0&&dirty[0].w==12);
}
int main() {
  testScrollViewportAndDirty();
  testScrollTiming();
  testScrollReset();
  double number=0;
  assert(parseFiniteNumber(" 20.5 ",number)&&number==20.5);
  assert(parseFiniteNumber("+1.25e2",number)&&number==125.0);
  for(const char* bad:{"","20C","nan","inf","1e999","1e-9999","0x1p2"})
    assert(!parseFiniteNumber(bad,number));

  NumericBinding ascending;
  ascending.input0=0;ascending.input1=40;
  ascending.output0=0;ascending.output1=200;ascending.clamp=true;
  assert(resolveNumericBinding(ascending,20,-240,479)==100);
  assert(resolveNumericBinding(ascending,-5,-240,479)==0);

  NumericBinding reversed=ascending;
  reversed.input0=40;reversed.input1=0;
  assert(resolveNumericBinding(reversed,30,-240,479)==50);

  NumericBinding extrapolated=ascending;extrapolated.clamp=false;
  assert(resolveNumericBinding(extrapolated,100,0,240)==240);

  NumericBinding positiveHalf;
  positiveHalf.input0=0;positiveHalf.input1=2;
  positiveHalf.output0=0;positiveHalf.output1=1;
  assert(resolveNumericBinding(positiveHalf,1,-10,10)==1);
  NumericBinding negativeHalf=positiveHalf;
  negativeHalf.output1=-1;
  assert(resolveNumericBinding(negativeHalf,1,-10,10)==-1);

  NumericBinding exactHalf;
  exactHalf.input0=0;exactHalf.input1=12;
  exactHalf.output0=0;exactHalf.output1=54;
  assert(resolveNumericBinding(exactHalf,7,-100,100)==32);
  exactHalf.output1=-54;
  assert(resolveNumericBinding(exactHalf,7,-100,100)==-32);

  NumericBinding extreme;
  extreme.input0=-std::numeric_limits<double>::max();
  extreme.input1=std::numeric_limits<double>::max();
  extreme.output0=-100;extreme.output1=100;
  assert(resolveNumericBinding(extreme,0,-240,479)==0);
  std::swap(extreme.input0,extreme.input1);
  extreme.output0=0;extreme.output1=200;
  assert(resolveNumericBinding(extreme,0,-240,479)==100);

  NumericBinding disparate;
  disparate.input0=std::numeric_limits<double>::min();
  disparate.input1=2*std::numeric_limits<double>::min();
  disparate.output0=0;disparate.output1=10;disparate.clamp=false;
  assert(resolveNumericBinding(disparate,std::numeric_limits<double>::max(),0,240)==240);

  ColorBinding colors;
  ColorStop green;green.at=0;green.value=0x07e0;colors.stops.push_back(green);
  ColorStop amber;amber.at=25;amber.value=0xfd20;colors.stops.push_back(amber);
  ColorStop red;red.at=35;red.value=0xf800;colors.stops.push_back(red);
  assert(resolveColorBinding(colors,-1)==0x07e0);
  assert(resolveColorBinding(colors,25)==0xfd20);
  assert(resolveColorBinding(colors,100)==0xf800);

  Theme theme; std::string error;
  const char* json = R"({"spec":1,"theme":{"id":"test","name":"Test","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"layers":[{"id":"time","type":"text","x":120,"y":100,"anchor":"center","value":"{HH}:{MM}","size":48,"color":"#ffffff"}]})";
  assert(parseTheme(json, theme, error));
  assert(theme.layers.size() == 1);
  tm t{}; t.tm_year=126; t.tm_mon=8; t.tm_mday=17; t.tm_wday=4; t.tm_hour=0; t.tm_min=2; t.tm_sec=3;
  assert(expandText("{HH} {hh} {MM} {SS} {DD} {MON} {MONTH} {WD} {WEEKDAY} {YYYY}", &t)=="00 12 02 03 17 Sep September Thu Thursday 2026");
  Engine engine; engine.setTheme(theme, 0);
  assert(engine.update(0, &t).size()==1);
  t.tm_sec++;
  assert(engine.update(1000, &t).empty());
  t.tm_min++;
  auto dirty=engine.update(2000, &t);
  assert(dirty.size()==1 && dirty[0].w==180 && dirty[0].h==48);
  engine.invalidate(); assert(engine.update(2001, &t)[0].w==240);
  assert(!parseTheme("{}", theme, error));
  const char* dataJson = R"({"spec":1,"theme":{"id":"data","name":"Data","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"data":[{"id":"weather","url":"https://example.test/weather.json","interval":60,"insecureTls":true,"fields":[{"id":"temp","path":"main.temp"}]}],"layers":[{"id":"value","type":"text","x":0,"y":0,"value":"{weather.temp}","size":16,"color":"#ffffff"}]})";
  assert(parseTheme(dataJson,theme,error));assert(theme.data.size()==1&&theme.data[0].fields.size()==1);
  engine.setTheme(theme,0);engine.setValues({{"weather.temp","21.5"}});assert(expandText("{weather.temp}",&t,{{"weather.temp","21.5"}})=="21.5");
  engine.update(0,&t);
  // Data changes must repaint only the old/new text bounds, even within the same second.
  engine.setValues({{"weather.temp","9"}});
  dirty=engine.update(1,&t);
  assert(dirty.size()==1 && dirty[0].x==0 && dirty[0].y==0 && dirty[0].w==48 && dirty[0].h==16);
  assert(engine.states()[0].text=="9");
  engine.setValues({{"weather.temp","9"},{"unused.value","changed"}});
  assert(engine.update(2,&t).empty());
  engine.setValues({});dirty=engine.update(3,&t);
  assert(dirty.size()==1 && dirty[0].w==24 && engine.states()[0].text=="--");
  // Missing clock time must not prevent data updates.
  engine.update(4,nullptr);engine.setValues({{"weather.temp","12"}});
  assert(!engine.update(5,nullptr).empty() && engine.states()[0].text=="12");

  const char* bindingJson = R"({"spec":1,"theme":{"id":"binding","name":"Binding","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"data":[{"id":"sensor","url":"http://example.test/value.json","interval":60,"fields":[{"id":"value","path":"value"}]}],"layers":[{"id":"bar","type":"shape","shape":"rectangle","x":20,"y":120,"width":10,"height":18,"cornerRadius":8,"fill":"#00ff00","bind":{"width":{"source":"sensor.value","input":[0,50],"output":[0,100]},"fill":{"source":"sensor.value","stops":[{"at":0,"value":"#00ff00"},{"at":50,"value":"#ffff00"}]}}}]})";
  assert(parseTheme(bindingJson,theme,error));
  const uint16_t staticGreen=0x07e0,warningColor=0xffe0;
  Engine boundEngine;
  boundEngine.setTheme(theme,0);
  boundEngine.update(0,&t);
  assert(boundEngine.states()[0].resolved.width==10);
  assert(boundEngine.states()[0].resolved.fill==staticGreen);
  assert(boundEngine.states()[0].resolved.cornerRadius==5);

  boundEngine.setValues({{"sensor.value","50"}});
  dirty=boundEngine.update(1,&t);
  assert(boundEngine.states()[0].resolved.width==100);
  assert(boundEngine.states()[0].resolved.fill==warningColor);
  assert(boundEngine.states()[0].resolved.cornerRadius==8);
  assert(dirty.size()==1);
  assert(dirty[0].x==20 && dirty[0].y==120 &&
         dirty[0].w==100 && dirty[0].h==18);

  boundEngine.setValues({{"sensor.value","50"},{"unused.value","changed"}});
  assert(boundEngine.update(2,&t).empty());

  boundEngine.setValues({{"sensor.value","not-a-number"}});
  dirty=boundEngine.update(3,&t);
  assert(boundEngine.states()[0].resolved.width==10);
  assert(boundEngine.states()[0].resolved.fill==staticGreen);
  assert(boundEngine.states()[0].resolved.cornerRadius==5);
  assert(dirty.size()==1);
  assert(dirty[0].x==20 && dirty[0].y==120 &&
         dirty[0].w==100 && dirty[0].h==18);

  const char* circleJson = R"({"spec":1,"theme":{"id":"circle-binding","name":"Circle Binding","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"data":[{"id":"sensor","url":"http://example.test/value.json","interval":60,"fields":[{"id":"value","path":"value"}]}],"layers":[{"id":"dot","type":"shape","shape":"circle","x":120,"y":120,"radius":10,"fill":"#00ff00","bind":{"radius":{"source":"sensor.value","input":[0,10],"output":[0,10]}}}]})";
  assert(parseTheme(circleJson,theme,error));
  Engine circleEngine;
  circleEngine.setTheme(theme,0);
  circleEngine.update(0,&t);
  circleEngine.setValues({{"sensor.value","0"}});
  dirty=circleEngine.update(1,&t);
  assert(circleEngine.states()[0].resolved.radius==0);
  assert(circleEngine.states()[0].bounds.empty());
  assert(dirty.size()==1);
  assert(dirty[0].x==110 && dirty[0].y==110 &&
         dirty[0].w==21 && dirty[0].h==21);
  std::cout << "theme engine tests passed\n";
}
