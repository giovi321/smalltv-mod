// Built-in binding sources (#31 item 1): time.* and device.*, spec 2 only.
#include "ThemeEngine.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
using namespace smalltv;

static std::string manifest(int spec, const std::string& layers, const std::string& data = "") {
  return "{\"spec\":" + std::to_string(spec) + ",\"theme\":{\"id\":\"t\",\"name\":\"T\",\"author\":\"A\",\"version\":\"1\"},"
    "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"}," + data + "\"layers\":[" + layers + "]}";
}
// A rectangle whose width follows `source` through [in0, in1] -> [out0, out1].
static std::string bar(const std::string& id, int y, const std::string& source, int in0, int in1, int out0, int out1) {
  return "{\"id\":\"" + id + "\",\"type\":\"shape\",\"shape\":\"rectangle\",\"x\":10,\"y\":" + std::to_string(y) +
    ",\"width\":7,\"height\":6,\"fill\":\"#ff5a47\",\"bind\":{\"width\":{\"source\":\"" + source + "\",\"input\":[" +
    std::to_string(in0) + "," + std::to_string(in1) + "],\"output\":[" + std::to_string(out0) + "," + std::to_string(out1) + "]}}}";
}
static bool parse(const std::string& json, Theme& theme, std::string& error) { return parseTheme(json, theme, error); }
static tm clockAt(int h, int m, int s) {
  tm t{}; t.tm_year = 126; t.tm_mon = 8; t.tm_mday = 17; t.tm_wday = 4;
  t.tm_hour = h; t.tm_min = m; t.tm_sec = s; return t;
}
static int widthOf(const Engine& e, size_t layer = 0) { return e.states()[layer].resolved.width; }

// Every source, at both ends of its range and in between.
static void testValues() {
  struct Case { const char* source; int lo, hi; };
  const Case cases[] = {{"time.second", 0, 59}, {"time.minute", 0, 59}, {"time.hour", 0, 23}, {"time.hour12", 0, 11},
                        {"time.day", 1, 31}, {"time.month", 1, 12}, {"time.weekday", 0, 6}};
  for (const auto& c : cases) {
    Theme theme; std::string error;
    assert(parse(manifest(2, bar("b", 10, c.source, c.lo, c.hi, c.lo, c.hi)), theme, error));
    assert(theme.layers[0].timeBound);
    Engine e; e.setTheme(theme, 0);
    for (int v = c.lo; v <= c.hi; ++v) {
      tm t = clockAt(0, 0, 0);
      const std::string s = c.source;
      if (s == "time.second") t.tm_sec = v;
      else if (s == "time.minute") t.tm_min = v;
      else if (s == "time.hour") t.tm_hour = v;
      else if (s == "time.hour12") t.tm_hour = v + 12;   // 12..23 read back as 0..11
      else if (s == "time.day") t.tm_mday = v;
      else if (s == "time.month") t.tm_mon = v - 1;
      else t.tm_wday = v;
      e.update(v, &t);
      assert(widthOf(e) == v);
    }
  }
  // hour12: midnight and noon are 0, 13:00 is 1, 23:59 is 11.
  for (int h : {0, 12}) assert(timeValue(BuiltIn::TimeHour12, clockAt(h, 0, 0)) == 0);
  assert(timeValue(BuiltIn::TimeHour12, clockAt(13, 0, 0)) == 1);
  assert(timeValue(BuiltIn::TimeHour12, clockAt(23, 59, 59)) == 11);
  // secondOfDay spans 0..86399.
  assert(timeValue(BuiltIn::TimeSecondOfDay, clockAt(0, 0, 0)) == 0);
  assert(timeValue(BuiltIn::TimeSecondOfDay, clockAt(1, 2, 3)) == 3723);
  assert(timeValue(BuiltIn::TimeSecondOfDay, clockAt(23, 59, 59)) == 86399);
  // The same numeric rules as a fetched field: input clamping and rounding.
  Theme theme; std::string error;
  assert(parse(manifest(2, bar("b", 10, "time.secondOfDay", 0, 86399, 0, 240)), theme, error));
  Engine e; e.setTheme(theme, 0);
  tm noon = clockAt(12, 0, 0); e.update(0, &noon);
  assert(widthOf(e) == 120);                       // 43200 / 86399 * 240 = 120.0014
  tm end = clockAt(23, 59, 59); e.update(1, &end);
  assert(widthOf(e) == 240);
  // Color stops work on a built-in too.
  const std::string colors = "{\"id\":\"c\",\"type\":\"shape\",\"shape\":\"rectangle\",\"x\":0,\"y\":0,\"width\":5,\"height\":5,\"fill\":\"#ffffff\","
    "\"bind\":{\"fill\":{\"source\":\"time.hour\",\"stops\":[{\"at\":0,\"value\":\"#0000ff\"},{\"at\":12,\"value\":\"#ffff00\"}]}}}";
  assert(parse(manifest(2, colors), theme, error));
  Engine c; c.setTheme(theme, 0);
  tm morning = clockAt(11, 59, 59); c.update(0, &morning);
  const uint16_t blue = c.states()[0].resolved.fill;
  tm afternoon = clockAt(12, 0, 0); c.update(1, &afternoon);
  assert(c.states()[0].resolved.fill != blue && c.states()[0].resolved.fill == 0xffe0);
  // No clock yet: the static value stays, like a missing fetched field.
  Engine none; assert(parse(manifest(2, bar("b", 10, "time.second", 0, 59, 0, 59)), theme, error));
  none.setTheme(theme, 0); none.update(0, nullptr);
  assert(widthOf(none) == 7);
  tm now = clockAt(0, 0, 30); none.update(1, &now); assert(widthOf(none) == 30);
  none.update(2, nullptr); assert(widthOf(none) == 7);
}

static void testSpecRules() {
  Theme theme; std::string error;
  const std::string layer = bar("b", 10, "time.second", 0, 59, 0, 59);
  assert(parse(manifest(2, layer), theme, error));
  // Spec 1 does not change: refused, with the reason.
  assert(!parse(manifest(1, layer), theme, error));
  assert(error.find("layers[0].bind.width.source") != std::string::npos && error.find("need \"spec\": 2") != std::string::npos);
  assert(!parse(manifest(1, bar("b", 10, "device.streak", 0, 9, 0, 9)), theme, error) && error.find("spec") != std::string::npos);
  // ... but a data source that happens to be called "time" keeps working in spec 1.
  const std::string data = "\"data\":[{\"id\":\"time\",\"url\":\"http://example.test/a.json\",\"interval\":60,\"fields\":[{\"id\":\"second\",\"path\":\"s\"}]}],";
  assert(parse(manifest(1, layer, data), theme, error) && theme.layers[0].bindings[0].builtin == BuiltIn::None);
  // ... and in spec 2 the name belongs to the built-ins.
  assert(!parse(manifest(2, layer, data), theme, error) && error.find("data[0].id") != std::string::npos);
  // Unknown names are refused.
  for (const char* bad : {"time.", "time.Second", "time.seconds", "time.Moon", "time.moons", "time.second.x", "device.", "device.unknown", "device.Streak"}) {
    assert(!parse(manifest(2, bar("b", 10, bad, 0, 59, 0, 59)), theme, error));
    assert(error.find("source") != std::string::npos);
  }
  assert(!parse(manifest(2, bar("b", 10, "time.foo", 0, 59, 0, 59)), theme, error) && error.find("unknown time.* or device.* name") != std::string::npos);
  // A name that is neither built-in nor declared is still refused as before.
  assert(!parse(manifest(2, bar("b", 10, "devices.streak", 0, 59, 0, 59)), theme, error) && error.find("declared data field") != std::string::npos);
  // A declared field still works in spec 2.
  const std::string weather = "\"data\":[{\"id\":\"w\",\"url\":\"http://example.test/a.json\",\"interval\":60,\"fields\":[{\"id\":\"t\",\"path\":\"t\"}]}],";
  assert(parse(manifest(2, bar("b", 10, "w.t", 0, 40, 0, 200), weather), theme, error));
  // A layer with no time.* binding is not time-bound.
  assert(!theme.layers[0].timeBound);
}

// Only the layer whose value changed is repainted.
static void testRedraw() {
  Theme theme; std::string error;
  assert(parse(manifest(2, bar("sec", 20, "time.second", 0, 59, 1, 200) + "," + bar("min", 40, "time.minute", 0, 59, 1, 200) + "," +
    "{\"id\":\"txt\",\"type\":\"text\",\"x\":0,\"y\":100,\"value\":\"static\",\"size\":16,\"color\":\"#ffffff\"}"), theme, error));
  Engine e; e.setTheme(theme, 0);
  tm t = clockAt(10, 20, 30);
  auto dirty = e.update(0, &t);
  assert(dirty.size() == 1 && dirty[0].w == 240);            // first frame: everything
  assert(e.update(1, &t).empty());                           // same second: nothing
  t.tm_sec = 31;
  dirty = e.update(1000, &t);
  assert(dirty.size() == 1 && dirty[0].y == 20 && dirty[0].h == 6 && dirty[0].w < 240);   // the seconds bar only
  const int before = widthOf(e, 1);
  assert(widthOf(e, 1) == before);
  t.tm_sec = 0; t.tm_min = 21;
  dirty = e.update(2000, &t);
  assert(!dirty.empty() && widthOf(e, 1) != before);
  for (const auto& r : dirty) assert(r.h < 240 && r.w < 240);
  // The wall clock moving without the bound value changing repaints nothing.
  Theme dayOnly;
  assert(parse(manifest(2, bar("d", 20, "time.day", 1, 31, 1, 31)), dayOnly, error));
  Engine d; d.setTheme(dayOnly, 0);
  tm u = clockAt(1, 1, 1); d.update(0, &u);
  for (int s = 2; s < 20; ++s) { u.tm_sec = s; assert(d.update(s * 1000, &u).empty()); }
  u.tm_mday = 18; assert(d.update(30000, &u).size() == 1);
  // invalidate() still repaints the whole canvas.
  d.invalidate(); assert(d.update(30001, &u)[0].w == 240);
  // Losing the clock puts the layer back on its static value, and repaints it.
  assert(d.update(30002, nullptr).size() == 1 && widthOf(d) == 7);
}

static void testDevice() {
  Theme theme; std::string error;
  assert(parse(manifest(2, bar("h", 20, "device.humidity", 0, 100, 0, 200)), theme, error));
  assert(!theme.layers[0].timeBound && theme.layers[0].bindings[0].builtin == BuiltIn::Device);
  Engine e; e.setTheme(theme, 0);
  tm t = clockAt(0, 0, 0);
  e.update(0, &t);
  assert(widthOf(e) == 7);                                  // not provided: static value
  // A fetched value with the same key is not a device value.
  e.setValues({{"device.humidity", "50"}});
  assert(e.update(1, &t).empty() && widthOf(e) == 7);
  e.setDeviceValues({{"device.humidity", "50"}});
  auto dirty = e.update(2, &t);
  assert(dirty.size() == 1 && widthOf(e) == 100);
  e.setDeviceValues({{"device.humidity", "50"}});          // same again: nothing
  assert(e.update(3, &t).empty());
  e.setDeviceValues({{"device.temperature", "21"}});       // another name: back to static
  assert(e.update(4, &t).size() == 1 && widthOf(e) == 7);
  e.setDeviceValues({{"device.humidity", "n/a"}});          // not a number: static
  assert(e.update(5, &t).empty() && widthOf(e) == 7);
  e.setDeviceValues({{"device.humidity", "250"}});          // beyond the input range: clamped
  e.update(6, &t); assert(widthOf(e) == 200);
}

// ---- time.moon: the moon's age in whole days, 0-29 -------------------------
static tm utcAt(int y, int mo, int d, int h, int mi, int s) {
  tm t{}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = s; return t;
}
static int moonAge(const tm& t) { return static_cast<int>(timeValue(BuiltIn::TimeMoon, t)); }
static int moonAt(int y, int mo, int d, int h, int mi, int s) { return moonAge(utcAt(y, mo, d, h, mi, s)); }

// The age at 150 new and full moons from the US Naval Observatory (UTC), 2020
// to 2030. The formula is off by -0.75 to +0.40 days (measured), so a new moon
// reads 28, 29 or 0, and a full moon 14 or 15.
static void testMoonAgainstUsno() {
  std::ifstream in("tests/theme/moon_phases.txt");
  assert(in.good());                                       // a missing file must not pass as "no rows"
  std::string line;
  int newMoons = 0, fullMoons = 0, seen29 = 0, seen0 = 0, seen14 = 0, seen15 = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    int y, mo, d, h, mi; char kind;
    assert(sscanf(line.c_str(), "%d-%d-%d %d:%d %c", &y, &mo, &d, &h, &mi, &kind) == 6);
    const int age = moonAt(y, mo, d, h, mi, 0);
    if (kind == 'N') {
      ++newMoons;
      assert(age == 28 || age == 29 || age == 0);
      (age == 0 ? seen0 : seen29)++;
    } else {
      ++fullMoons;
      assert(age == 14 || age == 15);
      (age == 14 ? seen14 : seen15)++;
    }
  }
  assert(newMoons == 75 && fullMoons == 75);
  assert(seen29 > 0 && seen0 > 0 && seen14 > 0 && seen15 > 0);   // the error really goes both ways
}

// Day by day: the age goes up by one, and falls from 29 to 0 once per month.
static void testMoonWalk() {
  static const int monthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int y = 2026, mo = 1, d = 1;
  int last = moonAt(y, mo, d, 0, 30, 0), wraps = 0, steps = 0;
  for (int day = 0; day < 400; ++day) {
    for (int h = 0; h < 24; ++h) {                        // every hour of 400 days
      const int age = moonAt(y, mo, d, h, 30, 0);
      assert(age >= 0 && age <= 29);
      if (age == last) continue;
      assert(age == last + 1 || (last == 29 && age == 0));   // one step at a time, never a jump
      if (age == 0) ++wraps;
      ++steps; last = age;
    }
    if (++d > monthDays[mo - 1]) { d = 1; if (++mo > 12) { mo = 1; ++y; } }
  }
  assert(wraps >= 13 && wraps <= 14);                     // 400 days hold 13.5 months
  assert(steps >= 13 * 29);
}

// The reference new moon, the month length, and what lies before the epoch.
static void testMoonEpoch() {
  assert(moonAt(2000, 1, 6, 18, 14, 0) == 0);             // the reference new moon, to the second
  assert(moonAt(2000, 1, 6, 18, 13, 59) == 29);           // one second before it: the end of the last month
  assert(moonAt(2000, 1, 7, 18, 13, 59) == 0);
  assert(moonAt(2000, 1, 7, 18, 14, 0) == 1);
  assert(moonAt(2000, 1, 6, 0, 0, 0) == 28);              // 18 h 14 min earlier: 28.77 days into the last month
  // One month later: 29 d 12 h 44 min 3 s (29.530588 days, to the second).
  assert(moonAt(2000, 2, 5, 6, 58, 2) == 29);
  assert(moonAt(2000, 2, 5, 6, 58, 3) == 0);
  // The same month boundary before the reference (negative side of the modulo).
  assert(moonAt(1999, 12, 8, 5, 29, 57) == 0);
  assert(moonAt(1999, 12, 8, 5, 29, 56) == 29);
  assert(moonAt(2000, 1, 5, 5, 29, 57) == 28);            // a day boundary on that side: 28 days into the month
  assert(moonAt(2000, 1, 5, 5, 29, 56) == 27);
  // A leap day and a century that is not a leap year change nothing but the date.
  assert(moonAt(2000, 2, 29, 12, 0, 0) == moonAt(2000, 2, 28, 12, 0, 0) + 1);
  assert(moonAt(2100, 3, 1, 12, 0, 0) == moonAt(2100, 2, 28, 12, 0, 0) + 1);   // 2100 has no Feb 29
  // Before 2000, and the clock a device shows before it is set (1970): still 0-29.
  assert(moonAt(1970, 1, 1, 0, 0, 0) == 22);              // 6.9 days before the 1970-01-07 new moon
  assert(moonAt(1970, 1, 7, 20, 35, 0) == 29);            // that new moon (formula is 0.4 days late)
  assert(moonAt(1999, 12, 22, 2, 0, 0) == 13);            // full moon was 1999-12-22 17:31 UTC
  assert(moonAt(1900, 1, 1, 0, 0, 0) >= 0 && moonAt(1900, 1, 1, 0, 0, 0) <= 29);
  // Far ahead: the 32-bit year 2038 and the year 2100 stay in range.
  assert(moonAt(2038, 1, 19, 3, 14, 7) == 12);
  assert(moonAt(2100, 3, 1, 0, 0, 0) == 19);
  // The weekday and the other fields of tm are not read.
  tm t = utcAt(2026, 5, 1, 12, 0, 0);
  const int age = moonAge(t);
  t.tm_wday = 6; t.tm_yday = 200; t.tm_isdst = 1;
  assert(moonAge(t) == age);
}

static void testMoonBinding() {
  Theme theme; std::string error;
  assert(parse(manifest(2, bar("m", 10, "time.moon", 0, 29, 0, 87)), theme, error));   // 3 pixels a day
  assert(theme.layers[0].timeBound && theme.layers[0].bindings[0].builtin == BuiltIn::TimeMoon);
  assert(!parse(manifest(1, bar("m", 10, "time.moon", 0, 29, 0, 87)), theme, error) && error.find("need \"spec\": 2") != std::string::npos);
  Engine e; e.setTheme(theme, 0);
  tm t = utcAt(2000, 1, 6, 18, 14, 0);
  e.update(0, &t); assert(widthOf(e) == 0);
  t = utcAt(2000, 1, 8, 1, 0, 0);                         // age 1.27 days -> 1
  assert(e.update(1, &t).size() == 1 && widthOf(e) == 3);
  t.tm_hour = 17;                                         // still age 1: nothing repaints
  assert(e.update(2, &t).empty());
  t = utcAt(2000, 1, 8, 18, 14, 0);                       // age 2.0 -> 2
  assert(e.update(3, &t).size() == 1 && widthOf(e) == 6);
  t = utcAt(2000, 1, 6, 18, 13, 59);                      // 29 -> full width
  e.update(4, &t); assert(widthOf(e) == 87);
  // No clock yet: the static value, as for the other time.* sources.
  Engine none; none.setTheme(theme, 0); none.update(0, nullptr);
  assert(widthOf(none) == 7);
  // A frame is picked with color stops too, e.g. a different color at the full moon.
  const std::string colors = "{\"id\":\"c\",\"type\":\"shape\",\"shape\":\"rectangle\",\"x\":0,\"y\":0,\"width\":5,\"height\":5,\"fill\":\"#ffffff\","
    "\"bind\":{\"fill\":{\"source\":\"time.moon\",\"stops\":[{\"at\":0,\"value\":\"#0000ff\"},{\"at\":15,\"value\":\"#ffff00\"}]}}}";
  assert(parse(manifest(2, colors), theme, error));
  Engine c; c.setTheme(theme, 0);
  tm full = utcAt(2000, 1, 21, 18, 14, 0);                // 15 days after the new moon
  c.update(0, &full); assert(c.states()[0].resolved.fill == 0xffe0);
}

int main() {
  testValues();
  testSpecRules();
  testRedraw();
  testDevice();
  testMoonAgainstUsno();
  testMoonWalk();
  testMoonEpoch();
  testMoonBinding();
  std::cout << "builtin source tests passed\n";
}
