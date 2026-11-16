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

  // `show`: a layer is drawn only while every rule holds: its value lies in
  // one of the rule's ranges (ends included, null open), or is missing and
  // the rule says "missing": true. Hidden, the layer has no bounds, so it is
  // not drawn, and hiding or showing it repaints just its own area.
  // `show` needs spec 2.
  auto spec2 = [](std::string json) {
    json.replace(json.find("\"spec\":1"), 8, "\"spec\":2");
    return json;
  };
  {
    auto withShow = [&](const std::string& rules) {
      std::string json = spec2(dataJson);
      const std::string end = "\"color\":\"#ffffff\"}]}";
      json.replace(json.find(end), end.size(), "\"color\":\"#ffffff\",\"show\":" + rules + "}]}");
      assert(parseTheme(json, theme, error));
      return theme;
    };
    Engine e; e.setTheme(withShow("[{\"source\":\"weather.temp\",\"in\":[[0,9],[20,null]]}]"), 0);
    e.update(0, nullptr);
    auto visible = [&](const char* temp) {
      e.setValues({{"weather.temp", temp}}); e.update(1, nullptr);
      return e.states()[0].resolved.visible;
    };
    assert(!e.states()[0].resolved.visible && e.states()[0].bounds.empty());  // no value yet
    for (const char* in : {"5", "0", "9", "20", "1000", " 7 "}) assert(visible(in));
    for (const char* out : {"9.1", "15", "-1", "abc", "--"}) assert(!visible(out) && e.states()[0].bounds.empty());
    e.setValues({{"weather.temp", "5"}}); e.update(2, nullptr);
    e.setValues({{"weather.temp", "15"}}); auto hidden = e.update(3, nullptr);
    assert(hidden.size() == 1 && hidden[0].x == 0 && hidden[0].y == 0 && hidden[0].w == 12 && hidden[0].h == 16);
    e.setValues({{"weather.temp", "16"}}); assert(e.update(4, nullptr).empty());  // hidden: nothing to repaint
    e.setValues({{"weather.temp", "20"}}); auto shownAgain = e.update(5, nullptr);
    assert(shownAgain.size() == 1 && shownAgain[0].w == 24 && shownAgain[0].h == 16);
    // Two rules must both hold; "missing": true counts a missing or
    // non-numeric value as holding.
    Engine both; both.setTheme(withShow("[{\"source\":\"weather.temp\",\"in\":[[0,9]],\"missing\":true},"
                                        "{\"source\":\"weather.temp\",\"in\":[[5,null]],\"missing\":true}]"), 0);
    both.update(0, nullptr);
    assert(both.states()[0].resolved.visible);
    auto bothVisible = [&](const char* temp) {
      both.setValues({{"weather.temp", temp}}); both.update(1, nullptr);
      return both.states()[0].resolved.visible;
    };
    assert(bothVisible("7") && bothVisible("abc") && !bothVisible("3") && !bothVisible("12"));
    // Rules belong to their own layer. Here only the second layer, a
    // rectangle, has one; hiding it changes nothing but its visibility, and
    // that alone repaints its area.
    std::string twoLayers = spec2(dataJson);
    const std::string end = "\"color\":\"#ffffff\"}]}";
    twoLayers.replace(twoLayers.find(end), end.size(), "\"color\":\"#ffffff\"},"
      "{\"id\":\"box\",\"type\":\"shape\",\"shape\":\"rectangle\",\"x\":100,\"y\":100,\"width\":20,\"height\":10,\"fill\":\"#ff0000\","
      "\"show\":[{\"source\":\"weather.temp\",\"in\":[[0,9]]}]}]}");
    assert(parseTheme(twoLayers, theme, error));
    Engine two; two.setTheme(theme, 0); two.update(0, nullptr);
    two.setValues({{"weather.temp", "5"}}); two.update(1, nullptr);
    assert(two.states()[0].resolved.visible && two.states()[1].resolved.visible);
    two.setValues({{"weather.temp", "50"}}); two.update(2, nullptr);
    assert(two.states()[0].resolved.visible && !two.states()[1].resolved.visible);
    two.setValues({{"weather.temp", "8"}});
    auto boxDirty = two.update(3, nullptr);
    bool boxRepainted = false;
    for (const Rect& r : boxDirty) if (r.x == 100 && r.y == 100 && r.w == 20 && r.h == 10) boxRepainted = true;
    assert(boxRepainted && two.states()[1].resolved.visible);
  }

  // The most a theme can hold: MaxShowRules rules, here layers with 4
  // rules of 4 ranges each. Layer i, rule r, range k covers [40i + 10k + r, 40i + 10k + r + 6].
  // Rules 0 and 2 read w.code, rules 1 and 3 read w.temp. A layer shows while
  // all four rules hold, so a range that ended up with another rule, layer or
  // other ends would change what some value shows. Swept value by value
  // against a plain oracle.
  {
    auto holds = [](int layer, int rule, int value) {
      for (int k = 0; k < 4; ++k) {
        const int low = layer * 40 + k * 10 + rule;
        if (value >= low && value <= low + 6) return true;
      }
      return false;
    };
    // The sweep takes as many layers of 4 rules as the cap holds; the rest of the
    // cap (MaxShowRules % 4 rules) is one more layer, in the cap tests below. The
    // cap may be set by a build flag, so none of this assumes a value.
    // The default is 36 (the docs and the skill say so): the most rules a theme can
    // hold inside the load budget of test_memory.cpp. A build that sets
    // THEME_MAX_SHOW_RULES says what to expect with -DEXPECTED_MAX_SHOW_RULES.
#ifndef EXPECTED_MAX_SHOW_RULES
#define EXPECTED_MAX_SHOW_RULES 36
#endif
    assert(MaxShowRules == size_t(EXPECTED_MAX_SHOW_RULES));
    assert(MaxShowRules / 4 + 2 < MaxLayers);
    const int layerCount = int(MaxShowRules / 4);
    const int restRules = int(MaxShowRules % 4);
    auto restLayer = [](int rules, const char* id) {
      std::string show;
      for (int r = 0; r < rules; ++r) show += std::string(r ? "," : "") + "{\"source\":\"w.code\",\"in\":[[0,1]]}";
      return std::string(",{\"id\":\"") + id + "\",\"type\":\"shape\",\"shape\":\"rectangle\",\"x\":1,\"y\":2,\"width\":5,\"height\":5,\"fill\":\"#ffffff\",\"show\":[" + show + "]}";
    };
    auto overCap = [&](int layers) {
      return "layers[" + std::to_string(layers) + "].show: expected at most " + std::to_string(MaxShowRules) + " rules in a theme";
    };
    auto layersJson = [&](int count) {
    std::string layers;
    for (int i = 0; i < count; ++i) {
      std::string show;
      for (int r = 0; r < 4; ++r) {
        std::string in;
        for (int k = 0; k < 4; ++k) {
          const int low = i * 40 + k * 10 + r;
          in += std::string(k ? "," : "") + "[" + std::to_string(low) + "," + std::to_string(low + 6) + "]";
        }
        show += std::string(r ? "," : "") + "{\"source\":\"w." + (r % 2 ? "temp" : "code") + "\",\"in\":[" + in + "]}";
      }
      layers += std::string(i ? "," : "") + "{\"id\":\"l" + std::to_string(i) + "\",\"type\":\"shape\",\"shape\":\"rectangle\","
        "\"x\":1,\"y\":2,\"width\":5,\"height\":5,\"fill\":\"#ffffff\",\"show\":[" + show + "]}";
    }
    return layers;
    };
    const std::string head = "{\"spec\":2,\"theme\":{\"id\":\"sweep\",\"name\":\"Sweep\",\"author\":\"Me\",\"version\":\"1\"},"
      "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"},\"data\":[{\"id\":\"w\",\"url\":\"http://example.test/w.json\","
      "\"interval\":60,\"fields\":[{\"id\":\"code\",\"path\":\"code\"},{\"id\":\"temp\",\"path\":\"temp\"}]}],\"layers\":[";
    Theme sweep;
    // Exactly the cap is accepted: full layers, and a last one with what is left of it.
    const std::string atCap = layersJson(layerCount) + (restRules ? restLayer(restRules, "rest") : "");
    const int capLayers = layerCount + (restRules ? 1 : 0);
    assert(parseTheme(head + atCap + "]}", sweep, error) && sweep.layers.size() == size_t(capLayers) && sweep.show.size() == MaxShowRules);
    // One rule over is refused, and the error names the layer that crosses the cap.
    assert(!parseTheme(head + atCap + restLayer(1, "more") + "]}", sweep, error) && error == overCap(capLayers));
    // So is one more layer of 4.
    assert(!parseTheme(head + layersJson(layerCount + 1) + "]}", sweep, error) && error == overCap(layerCount));
    // (The refused parse above left `sweep` half filled; parse the sweep's theme.)
    assert(parseTheme(head + layersJson(layerCount) + "]}", sweep, error));
    Engine e;
    e.setTheme(sweep, 0);
    e.update(0, nullptr);
    uint32_t now = 1;
    int shown = 0;
    for (int shift : {0, 2}) {
      for (int v = -3; v < layerCount * 40 + 20; ++v) {
        e.setValues({{"w.code", std::to_string(v)}, {"w.temp", std::to_string(v + shift)}});
        e.update(now++, nullptr);
        for (int i = 0; i < layerCount; ++i) {
          const bool expected = holds(i, 0, v) && holds(i, 1, v + shift) && holds(i, 2, v) && holds(i, 3, v + shift);
          const bool visible = e.states()[i].resolved.visible;
          shown += visible;
          if (visible != expected) std::cerr << "layer " << i << ", code " << v << ", temp " << v + shift << ": shown " << visible << ", expected " << expected << "\n";
          assert(visible == expected);
        }
      }
    }
    // A sweep that never reaches the ranges would pass anything. A layer shows for 4
    // values in each of its 4 ranges, under both shifts: layers x 4 x 4 x 2.
    assert(shown == layerCount * 32);
    // The bounds are floats, as on the device: the value 9.49 holds for the single-value
    // range [9.49, 9.49], and one step either side does not.
    std::string precise = spec2(dataJson);
    const std::string tail = "\"color\":\"#ffffff\"}]}";
    precise.replace(precise.find(tail), tail.size(), "\"color\":\"#ffffff\",\"show\":[{\"source\":\"weather.temp\",\"in\":[[9.49,9.49]]}]}]}");
    assert(parseTheme(precise, sweep, error));
    Engine p;
    p.setTheme(sweep, 0);
    p.update(0, nullptr);
    uint32_t at = 1;
    auto shows = [&](const char* temp) {
      p.setValues({{"weather.temp", temp}});
      p.update(at++, nullptr);
      return p.states()[0].resolved.visible;
    };
    assert(shows("9.49") && !shows("9.48") && !shows("9.5"));
    // A rule has the ranges it names and no more: with one range, 0 is not in it (the
    // slot after the last range holds 0 to 0).
    assert(!shows("0"));
    // A rule reads its own key and no other: not a longer or shorter one, not another source's.
    for (const char* other : {"weather.temperature", "weather.tem", "weathxr.temp", "weather_temp", "weather.age"}) {
      p.setValues({{other, "9.49"}});
      p.update(at++, nullptr);
      assert(!p.states()[0].resolved.visible);
    }
    // `<source>.age` is read through its own index (spec 2): hide the layer once the data is stale.
    std::string aged = spec2(dataJson);
    aged.replace(aged.find(tail), tail.size(), "\"color\":\"#ffffff\",\"show\":[{\"source\":\"weather.age\",\"in\":[[null,30]]}]}]}");
    assert(parseTheme(aged, sweep, error));
    Engine a;
    a.setTheme(sweep, 0);
    a.update(0, nullptr);
    auto freshWhen = [&](const char* age) {
      a.setValues({{"weather.age", age}, {"weather.temp", "1"}});
      a.update(at++, nullptr);
      return a.states()[0].resolved.visible;
    };
    assert(freshWhen("0") && freshWhen("30") && !freshWhen("31") && !freshWhen("500"));
    // Two sources: a rule reads the key of the source and the field it names (r.b), not the
    // same field of the first source (w.temp).
    const std::string twoSources = "{\"spec\":2,\"theme\":{\"id\":\"two\",\"name\":\"Two\",\"author\":\"Me\",\"version\":\"1\"},"
      "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"},\"data\":["
      "{\"id\":\"w\",\"url\":\"http://example.test/w.json\",\"interval\":60,\"fields\":[{\"id\":\"code\",\"path\":\"code\"},{\"id\":\"temp\",\"path\":\"temp\"}]},"
      "{\"id\":\"r\",\"url\":\"http://example.test/r.json\",\"interval\":60,\"fields\":[{\"id\":\"a\",\"path\":\"a\"},{\"id\":\"b\",\"path\":\"b\"}]}],"
      "\"layers\":[{\"id\":\"box\",\"type\":\"shape\",\"shape\":\"rectangle\",\"x\":1,\"y\":2,\"width\":5,\"height\":5,\"fill\":\"#ffffff\","
      "\"show\":[{\"source\":\"r.b\",\"in\":[[5,6]]}]}]}";
    assert(parseTheme(twoSources, sweep, error));
    Engine two;
    two.setTheme(sweep, 0);
    two.update(0, nullptr);
    auto twoShows = [&](const char* rb, const char* wtemp) {
      two.setValues({{"r.b", rb}, {"w.temp", wtemp}});
      two.update(at++, nullptr);
      return two.states()[0].resolved.visible;
    };
    assert(twoShows("5", "99") && !twoShows("9", "5"));
  }

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
