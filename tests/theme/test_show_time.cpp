// `show` rules on the built-in time.* sources, with the optional "every" that
// takes the value modulo N first. Spec 2 only. A layer with such a rule is
// re-checked as the clock moves and repainted only when its visibility flips.
#include "ThemeEngine.h"
#include <cassert>
#include <cstdio>
#include <iostream>
using namespace smalltv;

static std::string manifest(int spec, const std::string& layers, const std::string& data = "") {
  return "{\"spec\":" + std::to_string(spec) + ",\"theme\":{\"id\":\"t\",\"name\":\"T\",\"author\":\"A\",\"version\":\"1\"},"
    "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"}," + data + "\"layers\":[" + layers + "]}";
}
// A text layer at (x, y) with the given `show` list (JSON, already bracketed).
static std::string label(const std::string& id, int x, int y, const std::string& show) {
  return "{\"id\":\"" + id + "\",\"type\":\"text\",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) +
    ",\"value\":\"AB\",\"size\":16,\"color\":\"#ffffff\",\"show\":" + show + "}";
}
static std::string rule(const std::string& source, const std::string& every, const std::string& in, const std::string& extra = "") {
  return "{\"source\":\"" + source + "\"" + (every.empty() ? "" : ",\"every\":" + every) + ",\"in\":" + in + extra + "}";
}
static const std::string weather = "\"data\":[{\"id\":\"w\",\"url\":\"http://example.test/a.json\",\"interval\":60,\"fields\":[{\"id\":\"t\",\"path\":\"t\"}]}],";
static bool parse(const std::string& json, Theme& theme, std::string& error) { return parseTheme(json, theme, error); }
static tm clockAt(int h, int m, int s) {
  tm t{}; t.tm_year = 126; t.tm_mon = 8; t.tm_mday = 17; t.tm_wday = 4;
  t.tm_hour = h; t.tm_min = m; t.tm_sec = s; return t;
}
static bool covers(const std::vector<Rect>& dirty, int x, int y) {
  for (const Rect& r : dirty) if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return true;
  return false;
}
static bool visible(const Engine& e, size_t layer) { return e.states()[layer].resolved.visible; }

static void testParse() {
  Theme theme; std::string error;
  // With and without "every"; the rule keeps its source, its modulus and the layer is time-bound.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.second", "10", "[[0,4]]") + "]")), theme, error));
  assert(theme.show.size() == 1 && theme.show[0].every == 10 && theme.show[0].count == 1);
  assert(theme.show[0].builtin == BuiltIn::TimeSecond && theme.layers[0].timeBound);
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.hour", "", "[[22,23],[0,5]]") + "]")), theme, error));
  assert(theme.show[0].every == 0 && theme.show[0].builtin == BuiltIn::TimeHour && theme.show[0].count == 2 && theme.layers[0].timeBound);
  // Every time source `bind` takes.
  for (const char* name : {"time.second", "time.minute", "time.hour", "time.hour12", "time.secondOfDay", "time.day",
                           "time.month", "time.weekday", "time.moon"})
    assert(parse(manifest(2, label("a", 5, 7, "[" + rule(name, "", "[[0,9]]") + "]")), theme, error));
  // The ends of the modulus.
  for (const char* ok : {"2", "3", "3599", "3600"})
    assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.second", ok, "[[0,1]]") + "]")), theme, error) && theme.show[0].every == atoi(ok));
  // A data field next to a time rule: the data rule is no time rule, the layer is time-bound by the other one.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("w.t", "", "[[0,9]]") + "," + rule("time.second", "10", "[[0,4]]") + "]"), weather), theme, error));
  assert(theme.show[0].builtin == BuiltIn::None && theme.show[1].builtin == BuiltIn::TimeSecond && theme.layers[0].timeBound);
  // A data rule alone leaves the layer not time-bound.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("w.t", "", "[[0,9]]") + "]"), weather), theme, error));
  assert(!theme.layers[0].timeBound && theme.show[0].every == 0 && theme.show[0].builtin == BuiltIn::None);

  struct Bad { std::string show; std::string where; std::string text; int spec; };
  const std::vector<Bad> bad = {
    // "every" is an integer from 2 to 3600.
    {rule("time.second", "0", "[[0,1]]"), "layers[0].show[0].every:", "expected an integer from 2 to 3600", 2},
    {rule("time.second", "1", "[[0,1]]"), "layers[0].show[0].every:", "expected an integer from 2 to 3600", 2},
    {rule("time.second", "3601", "[[0,1]]"), "layers[0].show[0].every:", "expected an integer from 2 to 3600", 2},
    {rule("time.second", "-10", "[[0,1]]"), "layers[0].show[0].every:", "expected an integer from 2 to 3600", 2},
    {rule("time.second", "2.5", "[[0,1]]"), "layers[0].show[0].every:", "expected an integer from 2 to 3600", 2},
    {rule("time.second", "10.0", "[[0,1]]"), "layers[0].show[0].every:", "expected an integer from 2 to 3600", 2},
    {rule("time.second", "\"10\"", "[[0,1]]"), "layers[0].show[0].every:", "expected an integer from 2 to 3600", 2},
    {rule("time.second", "null", "[[0,1]]"), "layers[0].show[0].every:", "expected an integer from 2 to 3600", 2},
    {rule("time.second", "true", "[[0,1]]"), "layers[0].show[0].every:", "expected an integer from 2 to 3600", 2},
    // "every" belongs to the time sources only.
    {rule("w.t", "10", "[[0,1]]"), "layers[0].show[0].every:", "only for a time.* source", 2},
    // Only time.*: not device.*, not an unknown name.
    {rule("device.temperature", "", "[[0,1]]"), "layers[0].show[0].source:", "expected a declared data field or a time.* source", 2},
    {rule("time.foo", "", "[[0,1]]"), "layers[0].show[0].source:", "unknown time.* name", 2},
    {rule("time.Second", "", "[[0,1]]"), "layers[0].show[0].source:", "unknown time.* name", 2},
    // Spec 1 has no `show` at all, so no time sources either (the message of every spec 2 feature).
    {rule("time.second", "", "[[0,1]]"), "layers[0].show:", "needs \"spec\": 2", 1},
    // The range rules stay the same.
    {rule("time.second", "10", "[[5,1]]"), "layers[0].show[0].in[0]:", "low must not be above high", 2},
    {rule("time.second", "10", "[]"), "layers[0].show[0].in:", "expected 1 to 4 ranges", 2},
  };
  for (const Bad& b : bad) {
    const bool ok = parse(manifest(b.spec, label("a", 5, 7, "[" + b.show + "]"), weather), theme, error);
    if (ok || error.find(b.where) != 0 || error.find(b.text) == std::string::npos) {
      std::cerr << b.show << " -> " << (ok ? "accepted" : error) << "\n";
      assert(false);
    }
  }
  // A second rule's error names the second rule.
  assert(!parse(manifest(2, label("a", 5, 7, "[" + rule("time.second", "", "[[0,9]]") + "," + rule("time.second", "1", "[[0,9]]") + "]")), theme, error)
         && error.find("layers[0].show[1].every:") == 0);
  // The limits of rules and ranges per layer are unchanged.
  std::string five;
  for (int i = 0; i < 5; ++i) five += std::string(i ? "," : "") + rule("time.second", "10", "[[0,4]]");
  assert(!parse(manifest(2, label("a", 5, 7, "[" + five + "]")), theme, error) && error.find("layers[0].show:") == 0);
  assert(parse(manifest(2, label("a", 5, 7, "[" + five.substr(0, five.rfind(",{")) + "]")), theme, error) && theme.show.size() == 4);
}

// Layer 0 is on for seconds 0-4 of every 10, layer 1 is its complement.
static std::string turns() {
  return manifest(2, label("a", 5, 7, "[" + rule("time.second", "10", "[[0,4]]") + "]") + "," +
                     label("b", 100, 100, "[" + rule("time.second", "10", "[[5,9]]") + "]"));
}
static void testVisibility() {
  Theme theme; std::string error;
  assert(parse(turns(), theme, error));
  Engine e; e.setTheme(theme, 0);
  uint32_t now = 0;
  for (int minute = 0; minute < 3; ++minute) {
    for (int s = 0; s < 60; ++s) {
      tm t = clockAt(10, minute, s);
      e.update(now += 1000, &t);
      const bool first = s % 10 < 5;
      if (visible(e, 0) != first || visible(e, 1) == first || e.states()[0].bounds.empty() == first || e.states()[1].bounds.empty() != first) {
        std::cerr << "minute " << minute << " second " << s << ": a " << visible(e, 0) << " b " << visible(e, 1) << "\n";
        assert(false);
      }
    }
  }
  // No "every": the plain value. The minute wrap 59 -> 0 turns the layer on again.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.second", "", "[[0,4]]") + "]")), theme, error));
  Engine w; w.setTheme(theme, 0);
  tm t = clockAt(10, 20, 59); w.update(1, &t); assert(!visible(w, 0));
  t = clockAt(10, 21, 0); w.update(2, &t); assert(visible(w, 0));
  // The hour wraps with a modulus too: time.hour every 12.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.hour", "12", "[[6,17]]") + "]")), theme, error));
  Engine h; h.setTheme(theme, 0);
  for (int hour = 0; hour < 24; ++hour) {
    tm c = clockAt(hour, 0, 0); h.update(hour + 1, &c);
    assert(visible(h, 0) == (hour % 12 >= 6));
  }
  // The modulus is not applied to a value that is smaller: seconds 0-59, every 60.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.second", "60", "[[59,59]]") + "]")), theme, error));
  Engine m; m.setTheme(theme, 0);
  for (int s : {0, 58, 59}) { tm c = clockAt(0, 0, s); m.update(s + 1, &c); assert(visible(m, 0) == (s == 59)); }
  // The smallest modulus, 2: every other second.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.second", "2", "[[0,0]]") + "]")), theme, error));
  Engine two; two.setTheme(theme, 0);
  for (int s = 0; s < 60; ++s) { tm c = clockAt(0, 0, s); two.update(s + 1, &c); assert(visible(two, 0) == (s % 2 == 0)); }
  // Several rules must all hold, a data rule included.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.second", "10", "[[0,4]]") + "," + rule("w.t", "", "[[0,9]]") + "]"), weather), theme, error));
  Engine both; both.setTheme(theme, 0);
  tm c = clockAt(0, 0, 2); both.update(1, &c); assert(!visible(both, 0));   // no data yet
  both.setValues({{"w.t", "5"}}); both.update(2, &c); assert(visible(both, 0));
  c = clockAt(0, 0, 7); both.update(3, &c); assert(!visible(both, 0));
  both.setValues({{"w.t", "50"}}); c = clockAt(0, 0, 3); both.update(4, &c); assert(!visible(both, 0));
}

static void testRedraw() {
  Theme theme; std::string error;
  assert(parse(turns(), theme, error));
  Engine e; e.setTheme(theme, 0);
  tm t = clockAt(10, 0, 3);
  auto first = e.update(0, &t);
  assert(first.size() == 1 && first[0].w == 240);        // the first frame paints everything
  assert(e.update(1, &t).empty());                         // same second: nothing
  t.tm_sec = 4; assert(e.update(1000, &t).empty());        // still the same turn: nothing, not even the clock-bound check
  // Flip at second 5: layer a hides (its old area is repainted), layer b shows.
  t.tm_sec = 5;
  auto flip = e.update(2000, &t);
  assert(!visible(e, 0) && visible(e, 1) && e.states()[0].bounds.empty() && !e.states()[1].bounds.empty());
  assert(covers(flip, 6, 8) && covers(flip, 101, 101));   // a's old text is cleared, b's is drawn
  assert(!covers(flip, 120, 200));                        // and nothing else
  for (int s = 6; s < 10; ++s) { t.tm_sec = s; assert(e.update(1000 * s, &t).empty()); }
  // Flip back across the wrap 59 -> 0 of the second, 9 -> 10 of the turn.
  t.tm_sec = 10;
  auto back = e.update(10000, &t);
  assert(visible(e, 0) && !visible(e, 1) && covers(back, 6, 8) && covers(back, 101, 101));
  // A minute change with the same turn paints nothing.
  tm same = clockAt(10, 0, 59); e.update(59000, &same);          // second 59: turn 5-9, b
  same = clockAt(10, 1, 59); assert(e.update(60000, &same).empty());
  // The layer hidden by the flip stays hidden through an unrelated data change.
  e.setValues({{"w.t", "1"}}); assert(e.update(61000, &same).empty());
  // The clock going away and coming back: with no clock the rule does not hold (missing is false).
  auto noClock = e.update(62000, nullptr);
  assert(!visible(e, 0) && !visible(e, 1) && covers(noClock, 101, 101));
  tm back2 = clockAt(10, 2, 1); auto up = e.update(63000, &back2);
  assert(visible(e, 0) && !visible(e, 1) && covers(up, 6, 8));
}

static void testNoClock() {
  Theme theme; std::string error;
  // "missing": true holds until the clock is set.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.second", "10", "[[0,4]]", ",\"missing\":true") + "]")), theme, error));
  assert(theme.show[0].missing);
  Engine e; e.setTheme(theme, 0);
  e.update(0, nullptr); assert(visible(e, 0));
  tm t = clockAt(0, 0, 7); auto hide = e.update(1, &t);
  assert(!visible(e, 0) && covers(hide, 6, 8));
  // Without it the layer is hidden until the clock is there.
  assert(parse(manifest(2, label("a", 5, 7, "[" + rule("time.second", "10", "[[0,4]]") + "]")), theme, error));
  Engine n; n.setTheme(theme, 0);
  n.update(0, nullptr); assert(!visible(n, 0));
  t = clockAt(0, 0, 1); auto show = n.update(1, &t);
  assert(visible(n, 0) && covers(show, 6, 8));
}

int main() {
  testParse();
  testVisibility();
  testRedraw();
  testNoClock();
  std::cout << "show time tests passed\n";
}
