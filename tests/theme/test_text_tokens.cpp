// Text tokens added by spec 2 (#31 item 6): {H}, {MO}, {AMPM} and {device.*}.
#include "ThemeEngine.h"
#include <cassert>
#include <iostream>
using namespace smalltv;

static std::string manifest(int spec, const std::string& value, const std::string& data = "") {
  return "{\"spec\":" + std::to_string(spec) + ",\"theme\":{\"id\":\"t\",\"name\":\"T\",\"author\":\"A\",\"version\":\"1\"},"
    "\"display\":{\"width\":240,\"height\":240,\"background\":\"#000000\"}," + data +
    "\"layers\":[{\"id\":\"t\",\"type\":\"text\",\"x\":0,\"y\":0,\"value\":\"" + value + "\",\"size\":16,\"color\":\"#ffffff\"}]}";
}
static tm clockAt(int h, int mo) {
  tm t{}; t.tm_year = 126; t.tm_mon = mo - 1; t.tm_mday = 17; t.tm_wday = 4; t.tm_hour = h; t.tm_min = 5; t.tm_sec = 9; return t;
}

static void testClockTokens() {
  // {H}: no leading zero, 0..23.
  assert(expandText("{H}", nullptr) == "--");
  const int hours[] = {0, 1, 9, 10, 12, 13, 23};
  const char* plain[] = {"0", "1", "9", "10", "12", "13", "23"};
  const char* twelve[] = {"12", "01", "09", "10", "12", "01", "11"};
  const char* ampm[] = {"AM", "AM", "AM", "AM", "PM", "PM", "PM"};
  for (int i = 0; i < 7; ++i) {
    tm t = clockAt(hours[i], 9);
    assert(expandText("{H}", &t) == plain[i]);
    assert(expandText("{hh}", &t) == twelve[i]);
    assert(expandText("{AMPM}", &t) == ampm[i]);
    assert(expandText("{hh}:{MM} {AMPM}", &t) == std::string(twelve[i]) + ":05 " + ampm[i]);
  }
  // {MO}: month number with a leading zero, 01..12.
  for (int m = 1; m <= 12; ++m) {
    tm t = clockAt(0, m);
    char want[8]; snprintf(want, sizeof want, "%02d", m);
    assert(expandText("{MO}", &t) == want);
  }
  tm bad = clockAt(0, 1); bad.tm_mon = 12;
  assert(expandText("{MO}", &bad) == "--");
  bad.tm_hour = 24; assert(expandText("{H}|{AMPM}", &bad) == "--|--");
  // No clock yet: all three fall back to "--", like the older tokens.
  assert(expandText("{MO}/{H}{AMPM}", nullptr) == "--/----");
  // {MONTH} and {WEEKDAY} were already there and are unchanged.
  tm t = clockAt(0, 9);
  assert(expandText("{MONTH} {WEEKDAY}", &t) == "September Thursday");
}

static void testSpecs() {
  Theme theme; std::string error;
  for (const char* value : {"{H}", "{MO}", "{AMPM}", "{H}:{MM} {AMPM} {MO}", "{device.temperature}", "{device.focusToday}",
                            "{device.focusGoal}", "{device.streak}", "{device.humidity}"})
    assert(parseTheme(manifest(2, value), theme, error));
  // Spec 1 keeps its set: the same text is refused like any unknown variable.
  for (const char* value : {"{H}", "{MO}", "{AMPM}", "{device.temperature}", "{device.streak}"}) {
    assert(!parseTheme(manifest(1, value), theme, error));
    assert(error.find("layers[0].value") != std::string::npos && error.find("declared clock/data variables") != std::string::npos);
  }
  // Names are exact, even in spec 2.
  for (const char* value : {"{h}", "{Mo}", "{ampm}", "{AM}", "{device.}", "{device.Streak}", "{device.unknown}", "{device.streak.x}", "{time.minute}", "{devices.streak}"})
    assert(!parseTheme(manifest(2, value), theme, error));
  // A spec 1 theme may still declare its own "device" data source and use it.
  const std::string data = "\"data\":[{\"id\":\"device\",\"url\":\"http://example.test/a.json\",\"interval\":60,\"fields\":[{\"id\":\"temperature\",\"path\":\"t\"}]}],";
  assert(parseTheme(manifest(1, "{device.temperature}", data), theme, error));
  Engine e; e.setTheme(theme, 0);
  e.setValues({{"device.temperature", "7"}});
  e.setDeviceValues({{"device.temperature", "99"}});         // a fetched field wins in spec 1
  tm t = clockAt(0, 1); e.update(0, &t);
  assert(e.states()[0].text == "7");
  e.setValues({});                                           // not fetched: "--", never the firmware's value
  e.update(1, &t);
  assert(e.states()[0].text == "--");
}

static void testDevice() {
  const std::vector<ThemeValue> none;
  tm t = clockAt(8, 3);
  assert(expandText("{device.temperature}", &t, none, none) == "--");
  const std::vector<ThemeValue> device = {{"device.temperature", "21.5"}, {"device.streak", "4"}};
  assert(expandText("{device.temperature}C {device.streak}d {device.humidity}%", &t, none, device) == "21.5C 4d --%");
  assert(expandText("{device.streak}", nullptr, none, device) == "4");   // no clock needed
  // End to end: the text follows setDeviceValues().
  Theme theme; std::string error;
  assert(parseTheme(manifest(2, "T {device.temperature} S {device.streak}"), theme, error));
  Engine e; e.setTheme(theme, 0);
  e.update(0, &t);
  assert(e.states()[0].text == "T -- S --");               // the firmware provides nothing
  e.setDeviceValues({{"device.temperature", "21.5"}});
  auto dirty = e.update(1, &t);
  assert(!dirty.empty() && e.states()[0].text == "T 21.5 S --");
  e.setDeviceValues({{"device.temperature", "21.5"}});     // unchanged: no repaint
  assert(e.update(2, &t).empty());
  e.setDeviceValues({{"device.temperature", "22"}, {"device.streak", "5"}});
  assert(!e.update(3, &t).empty() && e.states()[0].text == "T 22 S 5");
  e.setDeviceValues({});                                   // gone again
  e.update(4, &t); assert(e.states()[0].text == "T -- S --");
  // The longest text still fits the 128-character limit check.
  std::string text;
  for (int i = 0; i < 12; ++i) text += "{device.temperature}";
  assert(text.size() > 128 && !parseTheme(manifest(2, text), theme, error));
}

int main() {
  testClockTokens();
  testSpecs();
  testDevice();
  std::cout << "text token tests passed\n";
}
