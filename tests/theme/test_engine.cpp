#include "ThemeEngine.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace smalltv;
int main() {
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
