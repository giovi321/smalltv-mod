// An animation layer picks its frame from a value (`bind.frame`), spec 2 only.
#include "ThemeEngine.h"
#include <cassert>
#include <iostream>
using namespace smalltv;

static const char* const data =
  "\"data\":[{\"id\":\"w\",\"url\":\"http://example.test/a.json\",\"interval\":60,"
  "\"fields\":[{\"id\":\"moon\",\"path\":\"m\"},{\"id\":\"day\",\"path\":\"d\"}]}],";
static std::string manifest(int spec, const std::string& layers) {
  return "{\"spec\":" + std::to_string(spec) + ",\"theme\":{\"id\":\"t\",\"name\":\"T\",\"author\":\"A\",\"version\":\"1\"},"
    "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"}," + data + "\"layers\":[" + layers + "]}";
}
// An animation at (10,20), 30 x 40, with `frames` frames and the given `bind` object body.
static std::string anim(int frames, const std::string& bind, const std::string& id = "a", bool loop = true) {
  return "{\"id\":\"" + id + "\",\"type\":\"animation\",\"x\":10,\"y\":20,\"width\":30,\"height\":40,\"source\":\"img/" + id +
    "\",\"frames\":" + std::to_string(frames) + ",\"fps\":5,\"loop\":" + (loop ? "true" : "false") + (bind.empty() ? "" : ",\"bind\":{" + bind + "}") + "}";
}
static std::string linear(const std::string& source, const std::string& in, const std::string& out, const std::string& extra = "") {
  return "\"frame\":{\"source\":\"" + source + "\",\"input\":" + in + ",\"output\":" + out + extra + "}";
}
static std::string steps(const std::string& source, const std::string& stops, const std::string& extra = "") {
  return "\"frame\":{\"source\":\"" + source + "\",\"stops\":" + stops + extra + "}";
}
static bool parse(const std::string& json, Theme& theme, std::string& error) { return parseTheme(json, theme, error); }
static bool bad(const std::string& layer, const char* expected, int spec = 2) {
  Theme theme; std::string error;
  if (parseTheme(manifest(spec, layer), theme, error)) { std::cerr << "accepted: " << layer << "\n"; return false; }
  if (error.find(expected) == std::string::npos) { std::cerr << "error '" << error << "' lacks '" << expected << "'\n"; return false; }
  return true;
}
static tm clockAt(int hour) {
  tm t{}; t.tm_year = 126; t.tm_mon = 8; t.tm_mday = 17; t.tm_wday = 4; t.tm_hour = hour; return t;
}
// Runs one frame-bound animation through a list of values and returns the frame for each.
static int frameFor(Engine& e, const std::string& value) {
  e.setValues({{"w.moon", value}});
  e.update(1, nullptr);
  return e.states()[0].frame;
}
static Engine engineFor(const std::string& layers) {
  Theme theme; std::string error;
  if (!parse(manifest(2, layers), theme, error)) { std::cerr << error << "\n"; assert(false); }
  Engine e; e.setTheme(theme, 0); e.update(0, nullptr);
  return e;
}

static void testParsing() {
  Theme theme; std::string error;
  assert(parse(manifest(2, anim(8, linear("w.moon", "[0,29]", "[0,7]"))), theme, error));
  assert(theme.layers[0].frameBound && !theme.layers[0].timeBound && theme.layers[0].bindings.size() == 1);
  assert(parse(manifest(2, anim(3, steps("w.moon", "[{\"at\":0,\"frame\":0},{\"at\":10,\"frame\":2}]"))), theme, error));
  assert(theme.layers[0].frameBound);
  // A built-in source makes the layer time-bound (it re-resolves when the clock moves).
  assert(parse(manifest(2, anim(2, linear("time.hour", "[0,23]", "[0,1]"))), theme, error));
  assert(theme.layers[0].frameBound && theme.layers[0].timeBound);
  // A layer without the binding is not frame-bound; neither is one bound on x.
  assert(parse(manifest(2, anim(2, "")), theme, error) && !theme.layers[0].frameBound);
  assert(parse(manifest(2, anim(2, "\"x\":{\"source\":\"w.moon\",\"input\":[0,1],\"output\":[0,9]}")), theme, error) && !theme.layers[0].frameBound);
  // Frame together with another property; one frame binding only.
  assert(parse(manifest(2, anim(2, linear("w.day", "[0,1]", "[0,1]") + ",\"x\":{\"source\":\"w.moon\",\"input\":[0,1],\"output\":[0,9]}")), theme, error));
  // Frame count 1 and 240; clamp given.
  assert(parse(manifest(2, anim(1, linear("w.moon", "[0,1]", "[0,0]"))), theme, error));
  assert(parse(manifest(2, anim(240, linear("w.moon", "[0,1]", "[0,239]", ",\"clamp\":false"))), theme, error));
  assert(parse(manifest(2, anim(1, steps("w.moon", "[{\"at\":0,\"frame\":0}]"))), theme, error));
  // 16 stops are the limit (a moon cycle of 8 phases needs 9).
  std::string sixteen = "[", seventeen = "[";
  for (int i = 0; i < 17; ++i) {
    const std::string stop = std::string(i ? "," : "") + "{\"at\":" + std::to_string(i) + ",\"frame\":" + std::to_string(i % 4) + "}";
    if (i < 16) sixteen += stop;
    seventeen += stop;
  }
  assert(parse(manifest(2, anim(4, steps("w.moon", sixteen + "]"))), theme, error));
  assert(bad(anim(4, steps("w.moon", seventeen + "]")), "layers[0].bind.frame.stops: expected 1 to 16 stops"));
}

static void testRefusals() {
  // Spec 1 and the older firmwares: refused, with the reason.
  assert(bad(anim(8, linear("w.moon", "[0,29]", "[0,7]")), "layers[0].bind.frame: needs \"spec\": 2", 1));
  assert(bad(anim(8, steps("w.moon", "[{\"at\":0,\"frame\":0}]")), "layers[0].bind.frame: needs \"spec\": 2", 1));
  // Only animations have frames.
  const std::string bind = linear("w.moon", "[0,1]", "[0,1]");
  assert(bad("{\"id\":\"t\",\"type\":\"text\",\"x\":0,\"y\":0,\"value\":\"x\",\"size\":16,\"color\":\"#ffffff\",\"bind\":{" + bind + "}}",
             "layers[0].bind.frame: binding is not applicable to this layer"));
  assert(bad("{\"id\":\"i\",\"type\":\"image\",\"x\":0,\"y\":0,\"source\":\"img/i\",\"bind\":{" + bind + "}}",
             "layers[0].bind.frame: binding is not applicable to this layer"));
  assert(bad("{\"id\":\"s\",\"type\":\"shape\",\"shape\":\"circle\",\"x\":5,\"y\":5,\"radius\":4,\"fill\":\"#ffffff\",\"bind\":{" + bind + "}}",
             "layers[0].bind.frame: binding is not applicable to this layer"));
  // Output frames must exist.
  assert(bad(anim(3, linear("w.moon", "[0,1]", "[0,3]")), "layers[0].bind.frame.output: expected exactly two integers from 0 to 2"));
  assert(bad(anim(3, linear("w.moon", "[0,1]", "[-1,2]")), "layers[0].bind.frame.output"));
  assert(bad(anim(3, linear("w.moon", "[0,1]", "[0,1.5]")), "layers[0].bind.frame.output"));
  assert(bad(anim(3, linear("w.moon", "[0,1]", "[0]")), "layers[0].bind.frame.output"));
  assert(bad(anim(3, linear("w.moon", "[1,1]", "[0,2]")), "layers[0].bind.frame.input: endpoints must differ"));
  assert(bad(anim(3, linear("w.moon", "[0,1]", "[0,2]", ",\"clamp\":1")), "layers[0].bind.frame.clamp"));
  // Stops: every frame must exist, strictly increasing, whole numbers.
  assert(bad(anim(3, steps("w.moon", "[{\"at\":0,\"frame\":3}]")), "layers[0].bind.frame.stops[0].frame"));
  assert(bad(anim(3, steps("w.moon", "[{\"at\":0,\"frame\":-1}]")), "layers[0].bind.frame.stops[0].frame"));
  assert(bad(anim(3, steps("w.moon", "[{\"at\":0,\"frame\":0.5}]")), "layers[0].bind.frame.stops[0].frame"));
  assert(bad(anim(3, steps("w.moon", "[{\"at\":0,\"frame\":\"1\"}]")), "layers[0].bind.frame.stops[0].frame"));
  assert(bad(anim(3, steps("w.moon", "[{\"at\":0}]")), "layers[0].bind.frame.stops[0].frame"));
  assert(bad(anim(3, steps("w.moon", "[{\"at\":0,\"frame\":0},{\"at\":0,\"frame\":1}]")), "layers[0].bind.frame.stops[1].at: stops must be strictly increasing"));
  assert(bad(anim(3, steps("w.moon", "[{\"at\":2,\"frame\":0},{\"at\":1,\"frame\":1}]")), "layers[0].bind.frame.stops[1].at"));
  assert(bad(anim(3, steps("w.moon", "[]")), "layers[0].bind.frame.stops: expected 1 to 16 stops"));
  assert(bad(anim(3, steps("w.moon", "[{\"at\":0,\"frame\":0,\"color\":1}]")), "layers[0].bind.frame.stops[0]"));
  // Stops or a linear map, never both, never neither.
  assert(bad(anim(3, "\"frame\":{\"source\":\"w.moon\",\"input\":[0,1],\"output\":[0,2],\"stops\":[{\"at\":0,\"frame\":0}]}"), "layers[0].bind.frame"));
  assert(bad(anim(3, "\"frame\":{\"source\":\"w.moon\"}"), "layers[0].bind.frame"));
  assert(bad(anim(3, "\"frame\":{\"source\":\"w.moon\",\"input\":[0,1]}"), "layers[0].bind.frame.output"));
  assert(bad(anim(3, "\"frame\":{\"source\":\"w.moon\",\"stops\":[{\"at\":0,\"frame\":0}],\"clamp\":true}"), "layers[0].bind.frame"));
  assert(bad(anim(3, "\"frame\":{\"input\":[0,1],\"output\":[0,2]}"), "layers[0].bind.frame"));
  assert(bad(anim(3, "\"frame\":5"), "layers[0].bind.frame: expected an object"));
  // Sources: declared fields and built-ins as for any binding; nothing else.
  assert(bad(anim(3, linear("w.nothing", "[0,1]", "[0,2]")), "layers[0].bind.frame.source: expected a declared data field"));
  assert(bad(anim(3, linear("time.nothing", "[0,1]", "[0,2]")), "layers[0].bind.frame.source: unknown time.* or device.* name"));
  // A repeated target is a duplicate, as for every binding.
  assert(bad(anim(3, linear("w.moon", "[0,1]", "[0,2]") + "," + linear("w.day", "[0,1]", "[0,2]")), "duplicate binding target"));
}

// The mapping, at its edges.
static void testLinearMapping() {
  Engine e = engineFor(anim(8, linear("w.moon", "[0,29]", "[0,7]")));
  assert(e.states()[0].frame == 0);                  // no value yet
  assert(frameFor(e, "0") == 0);
  assert(frameFor(e, "29") == 7);
  assert(frameFor(e, "-5") == 0);                    // below the range
  assert(frameFor(e, "-0.001") == 0);
  assert(frameFor(e, "30") == 7);                    // above
  assert(frameFor(e, "1e9") == 7);
  assert(frameFor(e, "14.5") == 4);                  // 3.5 rounds up, as for every numeric binding
  assert(frameFor(e, "14.49") == 3);
  assert(frameFor(e, "4") == 1);                     // 0.97
  assert(frameFor(e, "8") == 2);                     // 1.93
  assert(frameFor(e, "7") == 2);                     // 1.69
  assert(frameFor(e, "6") == 1);                     // 1.45
  // Open-Meteo's phase, 0..1.
  Engine p = engineFor(anim(8, linear("w.moon", "[0,1]", "[0,7]")));
  assert(frameFor(p, "0") == 0 && frameFor(p, "0.5") == 4 && frameFor(p, "1") == 7);
  // Descending input and output.
  Engine r = engineFor(anim(4, linear("w.moon", "[10,0]", "[3,0]")));
  assert(frameFor(r, "10") == 3 && frameFor(r, "0") == 0 && frameFor(r, "5") == 2);   // 1.5 rounds away from zero
  // Day and night: two frames, 0 = night, 1 = day.
  Engine d = engineFor(anim(2, linear("w.moon", "[0,1]", "[0,1]")));
  assert(frameFor(d, "0") == 0 && frameFor(d, "1") == 1 && frameFor(d, "0.4") == 0 && frameFor(d, "0.6") == 1);
  // One frame: always frame 0.
  Engine one = engineFor(anim(1, linear("w.moon", "[0,1]", "[0,0]")));
  assert(frameFor(one, "0") == 0 && frameFor(one, "1") == 0 && frameFor(one, "5") == 0);
  // Many frames: the last one is reachable.
  Engine many = engineFor(anim(240, linear("w.moon", "[0,239]", "[0,239]")));
  assert(frameFor(many, "239") == 239 && frameFor(many, "238") == 238 && frameFor(many, "1000") == 239 && frameFor(many, "0") == 0);
  // clamp false: the input may extrapolate, the frame still stays inside 0..frames-1.
  Engine free_ = engineFor(anim(8, linear("w.moon", "[0,10]", "[2,4]", ",\"clamp\":false")));
  assert(frameFor(free_, "10") == 4 && frameFor(free_, "20") == 6 && frameFor(free_, "1000") == 7 && frameFor(free_, "-1000") == 0);
}

static void testStops() {
  // Moonlight: none / half / full, from the moon age 0..29.
  Engine e = engineFor(anim(3, steps("w.moon", "[{\"at\":0,\"frame\":0},{\"at\":4,\"frame\":1},{\"at\":11.5,\"frame\":2},{\"at\":17,\"frame\":1},{\"at\":24,\"frame\":0}]")));
  assert(frameFor(e, "-3") == 0);                    // below the first stop: the first stop's frame
  assert(frameFor(e, "0") == 0);
  assert(frameFor(e, "3.999") == 0);
  assert(frameFor(e, "4") == 1);                     // exactly on a threshold
  assert(frameFor(e, "11.4") == 1);
  assert(frameFor(e, "11.5") == 2);
  assert(frameFor(e, "16.99") == 2);
  assert(frameFor(e, "17") == 1);
  assert(frameFor(e, "23.9") == 1);
  assert(frameFor(e, "24") == 0);
  assert(frameFor(e, "29") == 0);
  assert(frameFor(e, "500") == 0);                   // above the last stop: the last stop's frame
  // The first stop's frame holds below it, even if it is not frame 0.
  Engine f = engineFor(anim(3, steps("w.moon", "[{\"at\":5,\"frame\":2},{\"at\":6,\"frame\":1}]")));
  assert(frameFor(f, "0") == 2 && frameFor(f, "5") == 2 && frameFor(f, "5.5") == 2 && frameFor(f, "6") == 1);
  // A single stop, and one frame.
  Engine one = engineFor(anim(1, steps("w.moon", "[{\"at\":0,\"frame\":0}]")));
  assert(frameFor(one, "-1") == 0 && frameFor(one, "9") == 0);
  // Day and night with thresholds.
  Engine d = engineFor(anim(2, steps("w.day", "[{\"at\":0,\"frame\":0},{\"at\":1,\"frame\":1}]")));
  d.setValues({{"w.day", "0"}}); d.update(1, nullptr); assert(d.states()[0].frame == 0);
  d.setValues({{"w.day", "1"}}); d.update(2, nullptr); assert(d.states()[0].frame == 1);
  // A moon cycle of 8 phases needs 9 stops: new moon at both ends.
  Engine m = engineFor(anim(8, steps("w.moon",
    "[{\"at\":0,\"frame\":0},{\"at\":0.0625,\"frame\":1},{\"at\":0.1875,\"frame\":2},{\"at\":0.3125,\"frame\":3},{\"at\":0.4375,\"frame\":4},"
    "{\"at\":0.5625,\"frame\":5},{\"at\":0.6875,\"frame\":6},{\"at\":0.8125,\"frame\":7},{\"at\":0.9375,\"frame\":0}]")));
  assert(frameFor(m, "0.02") == 0 && frameFor(m, "0.25") == 2 && frameFor(m, "0.5") == 4 && frameFor(m, "0.75") == 6 && frameFor(m, "0.99") == 0);
}

static void testMissingValue() {
  // No value yet: frame 0, and the layer does not play although it loops at 5 fps.
  Engine e = engineFor(anim(4, linear("w.moon", "[0,3]", "[0,3]")));
  for (uint32_t t = 1000; t <= 10000; t += 1000) {
    assert(e.update(t, nullptr).empty());
    assert(e.states()[0].frame == 0);
  }
  assert(frameFor(e, "3") == 3);
  // Time passes: the picked frame stays, nothing is repainted.
  for (uint32_t t = 11000; t <= 15000; t += 1000) { assert(e.update(t, nullptr).empty()); assert(e.states()[0].frame == 3); }
  // The value goes away (not a number, or not delivered): back to frame 0.
  e.setValues({{"w.moon", "n/a"}}); assert(e.update(16000, nullptr).size() == 1 && e.states()[0].frame == 0);
  assert(frameFor(e, "2") == 2);
  e.setValues({}); assert(e.update(17000, nullptr).size() == 1 && e.states()[0].frame == 0);
  // Not finite.
  for (const char* v : {"nan", "inf", "-inf", ""}) { assert(frameFor(e, "1") == 1); assert(frameFor(e, v) == 0); }
  // Same for a loop:false animation.
  Engine once = engineFor(anim(4, linear("w.moon", "[0,3]", "[0,3]"), "a", false));
  once.update(5000, nullptr); assert(once.states()[0].frame == 0 && !once.states()[0].finished);
  assert(frameFor(once, "2") == 2);
  // A clock source: no clock, frame 0; clock set, the frame of the hour.
  Engine t = engineFor(anim(2, linear("time.hour", "[0,23]", "[0,1]")));
  assert(t.update(1000, nullptr).empty() && t.states()[0].frame == 0);
  tm evening = clockAt(20);
  assert(t.update(2000, &evening).size() == 1 && t.states()[0].frame == 1);
  tm early = clockAt(2);
  assert(t.update(3000, &early).size() == 1 && t.states()[0].frame == 0);
  assert(t.update(4000, nullptr).empty() && t.states()[0].frame == 0);   // clock lost: static frame 0
  assert(t.update(5000, &evening).size() == 1 && t.states()[0].frame == 1);
  assert(t.update(6000, nullptr).size() == 1 && t.states()[0].frame == 0);
}

// Repaint only when the picked frame changes, and only the layer's rectangle.
static void testRepaint() {
  Engine e = engineFor(anim(8, linear("w.moon", "[0,29]", "[0,7]")) + "," +
    "{\"id\":\"txt\",\"type\":\"text\",\"x\":0,\"y\":200,\"value\":\"static\",\"size\":16,\"color\":\"#ffffff\"}");
  e.setValues({{"w.moon", "0"}});
  assert(e.update(10, nullptr).empty());                       // frame 0 stays 0
  e.setValues({{"w.moon", "14.5"}});
  auto dirty = e.update(20, nullptr);
  assert(e.states()[0].frame == 4);
  assert(dirty.size() == 1 && dirty[0].x == 10 && dirty[0].y == 20 && dirty[0].w == 30 && dirty[0].h == 40);
  e.setValues({{"w.moon", "14.6"}});                           // a new value, the same frame
  assert(e.update(30, nullptr).empty());
  e.setValues({{"w.moon", "14.6"}, {"w.day", "1"}});           // an unrelated field
  assert(e.update(40, nullptr).empty());
  for (uint32_t t = 1000; t < 4000; t += 100) assert(e.update(t, nullptr).empty());   // time alone
  e.setValues({{"w.moon", "29"}});
  dirty = e.update(5000, nullptr);
  assert(dirty.size() == 1 && e.states()[0].frame == 7);
  e.invalidate();
  dirty = e.update(5001, nullptr);
  assert(dirty.size() == 1 && dirty[0].w == 240 && dirty[0].h == 240 && e.states()[0].frame == 7);
  // A new theme starts from the value in hand.
  Theme theme; std::string error;
  assert(parse(manifest(2, anim(8, linear("w.moon", "[0,29]", "[0,7]"))), theme, error));
  e.setTheme(theme, 6000);
  e.update(6000, nullptr);
  assert(e.states()[0].frame == 7);
}

// A frame-bound layer next to a looping one: the looping one is unchanged.
static void testLoopingUnchanged() {
  Engine e = engineFor(anim(8, linear("w.moon", "[0,29]", "[0,7]"), "bound") + "," + anim(4, "", "loop"));
  e.setValues({{"w.moon", "29"}});
  e.update(1, nullptr);
  assert(e.states()[0].frame == 7 && e.states()[1].frame == 0);
  // 5 fps: one frame every 200 ms.
  auto dirty = e.update(201, nullptr);
  assert(e.states()[1].frame == 1 && e.states()[0].frame == 7);
  assert(dirty.size() == 1 && dirty[0].x == 10 && dirty[0].w == 30);
  e.update(401, nullptr); assert(e.states()[1].frame == 2);
  // A new value moves the bound layer and leaves the looping one where it is.
  e.setValues({{"w.moon", "0"}});
  dirty = e.update(402, nullptr);
  assert(e.states()[0].frame == 0 && e.states()[1].frame == 2 && dirty.size() == 1);
  e.setValues({{"w.moon", "29"}});
  e.update(403, nullptr); assert(e.states()[0].frame == 7 && e.states()[1].frame == 2);
  e.update(801, nullptr); assert(e.states()[1].frame == 0 && e.states()[0].frame == 7);   // wrapped (4 frames), bound layer still 7
  // A bound layer does not disturb a one-shot animation either.
  Engine once = engineFor(anim(3, "", "once", false));
  once.update(2000, nullptr);
  assert(once.states()[0].frame == 2 && once.states()[0].finished);
}

// ---- groups: the value picks a group of frames, the group then plays ----
static const std::string kGroup3 = ",\"group\":3";
// A cat with 6 frames: 0..2 at night, 3..5 by day. `w.day` picks the group. 5 fps: 200 ms a frame.
static std::string cat(const std::string& id = "a", bool loop = true) {
  return anim(6, linear("w.day", "[0,1]", "[0,1]", kGroup3), id, loop);
}
static void setDay(Engine& e, const char* value) { e.setValues({{"w.day", value}}); }

static void testGroupParsing() {
  Theme theme; std::string error;
  assert(parse(manifest(2, cat()), theme, error));
  assert(theme.layers[0].frameBound && theme.layers[0].group == 3);
  // Without `group` the layer is not grouped: today's behaviour.
  assert(parse(manifest(2, anim(6, linear("w.day", "[0,1]", "[0,5]"))), theme, error) && theme.layers[0].group == 0);
  // The output is counted in groups: 6 frames in groups of 2 are 3 groups.
  assert(parse(manifest(2, anim(6, linear("w.day", "[0,1]", "[0,2]", ",\"group\":2"))), theme, error) && theme.layers[0].group == 2);
  // Edges: groups of one frame, and one group of all the frames.
  assert(parse(manifest(2, anim(6, linear("w.day", "[0,1]", "[0,5]", ",\"group\":1"))), theme, error) && theme.layers[0].group == 1);
  assert(parse(manifest(2, anim(6, linear("w.day", "[0,1]", "[0,0]", ",\"group\":6"))), theme, error) && theme.layers[0].group == 6);
  assert(parse(manifest(2, anim(240, linear("w.day", "[0,1]", "[0,1]", ",\"group\":120"))), theme, error) && theme.layers[0].group == 120);
  assert(parse(manifest(2, anim(240, linear("w.day", "[0,1]", "[0,0]", ",\"group\":240"))), theme, error) && theme.layers[0].group == 240);
  // Stops and clamp go with it; a clock source too.
  assert(parse(manifest(2, anim(6, steps("w.day", "[{\"at\":0,\"frame\":0},{\"at\":1,\"frame\":1}]", kGroup3))), theme, error) && theme.layers[0].group == 3);
  assert(parse(manifest(2, anim(6, linear("w.day", "[0,1]", "[0,1]", ",\"clamp\":false,\"group\":3"))), theme, error));
  assert(parse(manifest(2, anim(6, linear("time.hour", "[0,23]", "[0,1]", kGroup3))), theme, error) && theme.layers[0].timeBound);
}

static void testGroupRefusals() {
  const std::string group = "layers[0].bind.frame.group";
  // frames must be a multiple of group.
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,1]", ",\"group\":4")), (group + ": frames must be a multiple of group").c_str()));
  assert(bad(anim(7, linear("w.day", "[0,1]", "[0,1]", ",\"group\":2")), (group + ": frames must be a multiple of group").c_str()));
  assert(bad(anim(5, steps("w.day", "[{\"at\":0,\"frame\":0}]", ",\"group\":3")), (group + ": frames must be a multiple of group").c_str()));
  // The group is a whole number from 1 to frames.
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,1]", ",\"group\":0")), (group + ": expected an integer from 1 to 6").c_str()));
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,1]", ",\"group\":-3")), (group + ": expected an integer from 1 to 6").c_str()));
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,1]", ",\"group\":12")), (group + ": expected an integer from 1 to 6").c_str()));
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,1]", ",\"group\":1.5")), group.c_str()));
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,1]", ",\"group\":\"3\"")), group.c_str()));
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,1]", ",\"group\":true")), group.c_str()));
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,1]", ",\"group\":null")), group.c_str()));
  // The output ends and the stops' frames are group numbers: 0 to frames / group - 1.
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,2]", kGroup3)), "layers[0].bind.frame.output: expected exactly two integers from 0 to 1"));
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,5]", ",\"group\":2")), "layers[0].bind.frame.output: expected exactly two integers from 0 to 2"));
  assert(bad(anim(6, linear("w.day", "[0,1]", "[1,1]", ",\"group\":6")), "layers[0].bind.frame.output: expected exactly two integers from 0 to 0"));
  assert(bad(anim(6, steps("w.day", "[{\"at\":0,\"frame\":2}]", kGroup3)), "layers[0].bind.frame.stops[0].frame: expected an integer from 0 to 1"));
  assert(bad(anim(6, steps("w.day", "[{\"at\":0,\"frame\":0},{\"at\":1,\"frame\":3}]", ",\"group\":2")), "layers[0].bind.frame.stops[1].frame: expected an integer from 0 to 2"));
  // `group` is for the frame binding only, and spec 2 only.
  assert(bad(anim(6, "\"x\":{\"source\":\"w.day\",\"input\":[0,1],\"output\":[0,9],\"group\":3}"), "layers[0].bind.x"));
  assert(bad(anim(6, linear("w.day", "[0,1]", "[0,1]", kGroup3)), "layers[0].bind.frame: needs \"spec\": 2", 1));
  // The layer itself still takes no `group` key.
  assert(bad("{\"id\":\"a\",\"type\":\"animation\",\"x\":0,\"y\":0,\"width\":3,\"height\":3,\"source\":\"img/a\",\"frames\":6,\"fps\":5,\"loop\":true,\"group\":3}", "layers[0]"));
}

// The mapping counts groups; the frame shown is the first of the picked group.
static void testGroupMapping() {
  Engine e = engineFor(anim(8, linear("w.moon", "[0,29]", "[0,3]", ",\"group\":2")));
  assert(e.states()[0].frame == 0);
  assert(frameFor(e, "29") == 6);
  assert(frameFor(e, "0") == 0);
  assert(frameFor(e, "10") == 2);                    // 1.03 rounds to group 1
  assert(frameFor(e, "20") == 4);                    // 2.07 -> group 2
  assert(frameFor(e, "100") == 6);                   // clamped: the last group
  assert(frameFor(e, "-100") == 0);
  Engine r = engineFor(anim(8, linear("w.moon", "[0,29]", "[3,0]", ",\"group\":2")));
  assert(frameFor(r, "0") == 6 && frameFor(r, "29") == 0);
  // Stops pick groups too.
  Engine s = engineFor(anim(6, steps("w.moon", "[{\"at\":0,\"frame\":0},{\"at\":10,\"frame\":2},{\"at\":20,\"frame\":1}]", ",\"group\":2")));
  assert(frameFor(s, "-5") == 0 && frameFor(s, "9.9") == 0 && frameFor(s, "10") == 4 && frameFor(s, "19") == 4 && frameFor(s, "20") == 2);
  // One group: always frame 0.
  Engine one = engineFor(anim(6, linear("w.moon", "[0,1]", "[0,0]", ",\"group\":6")));
  assert(frameFor(one, "0") == 0 && frameFor(one, "1") == 0 && frameFor(one, "9") == 0);
  // Groups of one frame: the frame is the value, as without `group`.
  Engine single = engineFor(anim(6, linear("w.moon", "[0,5]", "[0,5]", ",\"group\":1")));
  assert(frameFor(single, "4") == 4 && frameFor(single, "5") == 5);
}

// The picked group plays at the layer's fps and loops inside the group.
static void testGroupPlays() {
  Engine e = engineFor(cat());
  assert(e.states()[0].frame == 0);                  // no value: group 0
  assert(e.update(100, nullptr).empty());            // half a frame
  auto dirty = e.update(200, nullptr);
  assert(e.states()[0].frame == 1);
  assert(dirty.size() == 1 && dirty[0].x == 10 && dirty[0].y == 20 && dirty[0].w == 30 && dirty[0].h == 40);
  e.update(400, nullptr); assert(e.states()[0].frame == 2);
  e.update(600, nullptr); assert(e.states()[0].frame == 0);   // wraps inside the group, not into 3
  e.update(800, nullptr); assert(e.states()[0].frame == 1);
  assert(e.update(850, nullptr).empty());
  // A late update jumps to the due frame: 7 frames later is 1 + 7 = 8, i.e. 2 in the group.
  e.update(850 + 1400, nullptr); assert(e.states()[0].frame == 2);
  // A value that picks the group already shown repaints nothing and does not restart it.
  setDay(e, "0.2");
  assert(e.update(2251, nullptr).empty() && e.states()[0].frame == 2);
  e.update(2450, nullptr); assert(e.states()[0].frame == 0);  // the group played on: 2 wraps to 0
}

// A group change restarts the new group at its first frame, with a fresh frame clock.
static void testGroupChange() {
  Engine e = engineFor(cat());
  e.update(200, nullptr); e.update(400, nullptr); e.update(600, nullptr); e.update(800, nullptr);
  assert(e.states()[0].frame == 1);
  e.update(900, nullptr);                            // half a frame has gone by: the clock holds 0.5 frame
  setDay(e, "1");
  auto dirty = e.update(1000, nullptr);              // a kept 0.5 frame would make this frame 4
  assert(e.states()[0].frame == 3);
  assert(dirty.size() == 1 && dirty[0].x == 10 && dirty[0].w == 30);
  e.update(1199, nullptr); assert(e.states()[0].frame == 3);
  e.update(1200, nullptr); assert(e.states()[0].frame == 4);
  e.update(1400, nullptr); assert(e.states()[0].frame == 5);
  e.update(1600, nullptr); assert(e.states()[0].frame == 3);  // wraps inside group 1, not to 0
  e.update(1700, nullptr);
  // A long quiet time before the change must not turn into skipped frames after it.
  setDay(e, "0");
  dirty = e.update(6700, nullptr);
  assert(e.states()[0].frame == 0 && dirty.size() == 1);
  e.update(6899, nullptr); assert(e.states()[0].frame == 0);
  e.update(6900, nullptr); assert(e.states()[0].frame == 1);
  // The same group again, with another value: no restart, no repaint.
  setDay(e, "0.3");
  assert(e.update(6950, nullptr).empty() && e.states()[0].frame == 1);
  e.update(7100, nullptr); assert(e.states()[0].frame == 2);
  // The group changes and the animation is on frame 0 of the old group: still repainted.
  Engine f = engineFor(cat());
  setDay(f, "1");
  dirty = f.update(10, nullptr);
  assert(dirty.size() == 1 && f.states()[0].frame == 3);
  // A value that goes away: back to group 0, playing.
  setDay(f, "n/a");
  dirty = f.update(20, nullptr);
  assert(dirty.size() == 1 && f.states()[0].frame == 0);
  f.update(220, nullptr); assert(f.states()[0].frame == 1);
  setDay(f, "1"); f.update(221, nullptr); assert(f.states()[0].frame == 3);
  f.setValues({}); dirty = f.update(222, nullptr);
  assert(dirty.size() == 1 && f.states()[0].frame == 0);
  // A clock source: the group follows the hour, and plays.
  Engine t = engineFor(anim(6, linear("time.hour", "[0,23]", "[0,1]", kGroup3)));
  assert(t.update(1000, nullptr).size() == 1 && t.states()[0].frame == 2);   // no clock yet: group 0 plays, 5 frames on
  tm evening = clockAt(20);
  assert(t.update(1001, &evening).size() == 1 && t.states()[0].frame == 3);
  t.update(1201, &evening); assert(t.states()[0].frame == 4);
}

// loop:false plays the group once; a group change plays the next one from its start.
static void testGroupOnce() {
  Engine e = engineFor(cat("a", false));
  e.update(200, nullptr); assert(e.states()[0].frame == 1 && !e.states()[0].finished);
  e.update(400, nullptr); assert(e.states()[0].frame == 2 && e.states()[0].finished);
  assert(e.update(2000, nullptr).empty() && e.states()[0].frame == 2);       // stays on the group's last frame
  setDay(e, "1");
  assert(e.update(2001, nullptr).size() == 1 && e.states()[0].frame == 3 && !e.states()[0].finished);
  e.update(2201, nullptr); assert(e.states()[0].frame == 4);
  e.update(12000, nullptr); assert(e.states()[0].frame == 5 && e.states()[0].finished);   // a long gap stops at the group's end
  assert(e.update(13000, nullptr).empty() && e.states()[0].frame == 5);
  setDay(e, "0");
  assert(e.update(13001, nullptr).size() == 1 && e.states()[0].frame == 0 && !e.states()[0].finished);
  e.update(13201, nullptr); assert(e.states()[0].frame == 1);
}

// Groups of one frame do not play: like a bound frame without `group`.
static void testGroupOfOne() {
  Engine e = engineFor(anim(4, linear("w.day", "[0,3]", "[0,3]", ",\"group\":1")));
  setDay(e, "2");
  assert(e.update(10, nullptr).size() == 1 && e.states()[0].frame == 2);
  for (uint32_t t = 1000; t < 6000; t += 500) assert(e.update(t, nullptr).empty() && e.states()[0].frame == 2);
}

// A grouped layer beside a looping one and a text: each repaints on its own clock.
static void testGroupNeighbours() {
  Engine e = engineFor(cat("cat") + "," + anim(4, "", "loop") + "," +
    "{\"id\":\"txt\",\"type\":\"text\",\"x\":0,\"y\":200,\"value\":\"static\",\"size\":16,\"color\":\"#ffffff\"}");
  setDay(e, "1");
  e.update(1, nullptr);
  assert(e.states()[0].frame == 3 && e.states()[1].frame == 0);
  auto dirty = e.update(201, nullptr);
  assert(e.states()[0].frame == 4 && e.states()[1].frame == 1);
  assert(!dirty.empty() && dirty[0].x == 10 && dirty[0].w == 30);
  // Four more steps. The looping one has its own cycle of 4 (1 -> 5 % 4 = 1); the cat's cycle is 3 (1 -> 5 % 3 = 2, in group 1).
  for (uint32_t t = 401; t <= 1001; t += 200) e.update(t, nullptr);
  assert(e.states()[0].frame == 3 + 2 && e.states()[1].frame == 1);
}

// ---- render ----------------------------------------------------------
struct Pixels : Display {
  uint16_t data[240 * 240]{}; int writes = 0;
  uint8_t glyphColumn(uint8_t, uint8_t) const override { return 0x7f; }
  void row(int x, int y, const uint16_t* p, int n) override { for (int i = 0; i < n; ++i) data[y * 240 + x + i] = p[i]; ++writes; }
};
// The frame number is the colour: frame k paints 0x0100 * (k + 1) over the layer.
struct Frames : Assets {
  std::vector<std::string> paths;
  AssetHandle resolve(const std::string& path) override {
    paths.push_back(path);
    const size_t dot = path.rfind('/');
    return static_cast<AssetHandle>(std::stoi(path.substr(dot + 1, 3)));
  }
  bool row(AssetHandle handle, int, int, int count, uint16_t* c, uint8_t* a) override {
    for (int i = 0; i < count; ++i) { c[i] = 0x0100 * (handle + 1); a[i] = 255; }
    return true;
  }
};
static void testRender() {
  Engine e = engineFor(anim(8, linear("w.moon", "[0,29]", "[0,7]")));
  Pixels d; Frames f;
  assert(render(e, {Rect(0, 0, 240, 240)}, f, d));
  assert(d.data[25 * 240 + 15] == 0x0100 && f.paths.back() == "img/a/000.png.sti");   // frame 0 before any value
  for (int frame = 0; frame < 8; ++frame) {
    e.setValues({{"w.moon", std::to_string(frame * 29.0 / 7)}});
    auto dirty = e.update(100 + frame, nullptr);
    if (frame) assert(dirty.size() == 1);
    f.paths.clear();
    assert(render(e, dirty.empty() ? std::vector<Rect>{Rect(0, 0, 240, 240)} : dirty, f, d));
    assert(d.data[25 * 240 + 15] == 0x0100 * (frame + 1));
    assert(d.data[59 * 240 + 39] == 0x0100 * (frame + 1));       // the last pixel of the layer (x 10..39, y 20..59)
    assert(d.data[20 * 240 + 9] == 0x0000 && d.data[60 * 240 + 15] == 0x0000);   // outside stays background
    char expected[32]; snprintf(expected, sizeof(expected), "img/a/%03d.png.sti", frame);
    assert(f.paths.size() == 1 && f.paths[0] == expected);
  }
  // Nothing to repaint, nothing written.
  e.setValues({{"w.moon", "29"}});
  auto none = e.update(900, nullptr);
  assert(none.empty());
  const int writes = d.writes;
  assert(render(e, none, f, d) && d.writes == writes);
}

// The picture drawn is the playing frame of the picked group.
static void testGroupRender() {
  Engine e = engineFor(cat());
  Pixels d; Frames f;
  assert(render(e, {Rect(0, 0, 240, 240)}, f, d));
  assert(d.data[25 * 240 + 15] == 0x0100 && f.paths.back() == "img/a/000.png.sti");
  setDay(e, "1");
  auto dirty = e.update(100, nullptr);
  f.paths.clear();
  assert(render(e, dirty, f, d));
  assert(d.data[25 * 240 + 15] == 0x0400 && f.paths.size() == 1 && f.paths[0] == "img/a/003.png.sti");
  dirty = e.update(300, nullptr);
  f.paths.clear();
  assert(render(e, dirty, f, d));
  assert(d.data[59 * 240 + 39] == 0x0500 && f.paths.size() == 1 && f.paths[0] == "img/a/004.png.sti");
  e.update(500, nullptr); assert(e.states()[0].frame == 5);
  e.update(700, nullptr); assert(e.states()[0].frame == 3);
}

// With `clamp: false` the input extrapolates. A grouped layer must still end on
// its last group (frames / group - 1), not on the last frame: group 5 of 2
// would be frame 15 of 6, a picture that does not exist.
static void testGroupExtrapolates() {
  Engine e = engineFor(anim(6, linear("w.moon", "[0,1]", "[0,1]", ",\"clamp\":false,\"group\":3")));
  Pixels d; Frames f;
  const char* const values[] = {"5", "2", "1e9", "-5", "-1e9", "0", "1", "1.4", "1.6"};
  const int expected[] = {3, 3, 3, 0, 0, 0, 3, 3, 3};   // group 1 starts at frame 3
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    // The value is set before an update at a time each step, so a changed group restarts.
    const int shown = frameFor(e, values[i]);
    assert(shown == expected[i]);
    assert(shown >= 0 && shown < 6);
    f.paths.clear();
    assert(render(e, {Rect(0, 0, 240, 240)}, f, d));
    char path[32]; snprintf(path, sizeof(path), "img/a/%03d.png.sti", expected[i]);
    assert(f.paths.size() == 1 && f.paths[0] == path);
  }
  // 4 groups of 2: a steep, shifted mapping runs past both ends.
  Engine g = engineFor(anim(8, linear("w.moon", "[0,10]", "[1,2]", ",\"clamp\":false,\"group\":2")));
  assert(frameFor(g, "10") == 4);                    // group 2
  assert(frameFor(g, "20") == 6);                    // group 3, the last
  assert(frameFor(g, "100") == 6);                   // not group 11 = frame 22
  assert(frameFor(g, "-100") == 0);
  // One group of all the frames: only group 0 exists.
  Engine one = engineFor(anim(6, linear("w.moon", "[0,1]", "[0,0]", ",\"clamp\":false,\"group\":6")));
  assert(frameFor(one, "5") == 0 && frameFor(one, "-5") == 0);
  // Groups of one frame: the last group is the last frame, as without `group`.
  Engine single = engineFor(anim(6, linear("w.moon", "[0,5]", "[0,5]", ",\"clamp\":false,\"group\":1")));
  assert(frameFor(single, "50") == 5 && frameFor(single, "-50") == 0);
}

int main() {
  testGroupExtrapolates();
  testGroupParsing();
  testGroupRefusals();
  testGroupMapping();
  testGroupPlays();
  testGroupChange();
  testGroupOnce();
  testGroupOfOne();
  testGroupNeighbours();
  testGroupRender();
  testParsing();
  testRefusals();
  testLinearMapping();
  testStops();
  testMissingValue();
  testRepaint();
  testLoopingUnchanged();
  testRender();
  std::cout << "frame binding tests passed\n";
}
