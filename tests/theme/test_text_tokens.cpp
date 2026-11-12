// Text tokens added by spec 2 (#31 item 6): {H}, {MO}, {AMPM} and {device.*},
// and the ISO 8601 week number {WW}.
#include "ThemeEngine.h"
#include <cassert>
#include <cstdio>
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
                            "{device.focusGoal}", "{device.streak}", "{device.humidity}", "{WW}", "KW {WW}"})
    assert(parseTheme(manifest(2, value), theme, error));
  // Spec 1 keeps its set: the same text is refused like any unknown variable.
  for (const char* value : {"{H}", "{MO}", "{AMPM}", "{device.temperature}", "{device.streak}", "{WW}", "KW {WW}"}) {
    assert(!parseTheme(manifest(1, value), theme, error));
    assert(error.find("layers[0].value") != std::string::npos && error.find("declared clock/data variables") != std::string::npos);
  }
  // Names are exact, even in spec 2.
  for (const char* value : {"{h}", "{Mo}", "{ampm}", "{AM}", "{ww}", "{W}", "{WWW}", "{WW }", "{device.}", "{device.Streak}", "{device.unknown}", "{device.streak.x}", "{time.minute}", "{devices.streak}"})
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

// ---- {WW}: the ISO 8601 week number ----
// Weeks start on Monday and week 1 holds the year's first Thursday. So the
// first days of January can belong to the last week of the year before (52
// or 53), and the last days of December to week 1 of the next year.
static std::string weekOf(int year, int yday, int wday) {
  tm d{}; d.tm_year = year - 1900; d.tm_yday = yday; d.tm_wday = wday;
  return expandText("{WW}", &d);
}
// A reference that shares no code with the firmware: it counts days from
// 1970-01-01 (Howard Hinnant's civil-date arithmetic) and takes the week from
// the Thursday of the date's own week, because that Thursday sits in the ISO
// year the week belongs to.
static long daysFromCivil(long y, long m, long d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
  const long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
static long yearOfDay(long days) {
  const long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
  const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const long mp = (5 * doy + 2) / 153, month = mp < 10 ? mp + 3 : mp - 9;
  return yoe + era * 400 + (month <= 2);
}
static void testWeekToken() {
  // The dates a week number gets wrong when the year rules are off.
  struct Day { int year, month, day, yday, wday; const char* week; };
  const Day days[] = {
    {2026, 10, 23, 295, 5, "43"},   // an ordinary Friday
    {2026, 1, 1, 0, 4, "01"},       // Thu 1 Jan 2026: week 1 holds it
    {2027, 1, 1, 0, 5, "53"},       // Fri 1 Jan 2027: still 2026's week 53
    {2027, 1, 3, 2, 0, "53"},       // Sun 3 Jan 2027: the last day of that week
    {2027, 1, 4, 3, 1, "01"},       // Mon 4 Jan 2027: week 1
    {2026, 12, 28, 361, 1, "53"},   // Mon 28 Dec 2026: 2026 has 53 weeks
    {2026, 12, 31, 364, 4, "53"},   // Thu 31 Dec 2026
    {2024, 12, 30, 364, 1, "01"},   // Mon 30 Dec 2024: week 1 of 2025
    {2025, 12, 28, 361, 0, "52"},   // Sun 28 Dec 2025: the last day of 2025's week 52
    {2025, 12, 29, 362, 1, "01"},   // Mon 29 Dec 2025: week 1 of 2026
    {2020, 12, 31, 365, 4, "53"},   // Thu 31 Dec 2020: a leap year with 53 weeks
    {2021, 1, 3, 2, 0, "53"},       // Sun 3 Jan 2021: 2020's week 53
    {2004, 12, 31, 365, 5, "53"},   // Fri 31 Dec 2004: a leap year that starts on a Thursday
    {2005, 1, 1, 0, 6, "53"},       // Sat 1 Jan 2005: 2004's week 53
    {2012, 12, 31, 365, 1, "01"},   // Mon 31 Dec 2012: a leap year, already week 1 of 2013
    {2008, 12, 29, 363, 1, "01"},   // Mon 29 Dec 2008: week 1 of 2009
    {2023, 1, 1, 0, 0, "52"},       // Sun 1 Jan 2023: 2022's week 52
    {2023, 1, 2, 1, 1, "01"},       // Mon 2 Jan 2023
    {2000, 1, 1, 0, 6, "52"},       // Sat 1 Jan 2000: 1999's week 52
    {2024, 2, 29, 59, 4, "09"},     // the leap day: a week number below 10 has a leading zero
    {2100, 1, 1, 0, 5, "53"},       // 2100 is not a leap year; Fri 1 Jan is 2099's week 53
  };
  for (const Day& d : days) {
    const std::string got = weekOf(d.year, d.yday, d.wday);
    if (got != d.week) fprintf(stderr, "%04d-%02d-%02d: week %s, want %s\n", d.year, d.month, d.day, got.c_str(), d.week);
    assert(got == d.week);
  }
  // Every day from 1970 to 2105 against the reference.
  long checked = 0;
  for (long day = daysFromCivil(1970, 1, 1); day < daysFromCivil(2106, 1, 1); ++day) {
    const long year = yearOfDay(day), yday = day - daysFromCivil(year, 1, 1);
    const long wday = (day + 4) % 7;                          // 1970-01-01 was a Thursday; 0 is Sunday
    const long thursday = day - (wday + 6) % 7 + 3;
    const long weekYear = yearOfDay(thursday);
    const long want = (thursday - daysFromCivil(weekYear, 1, 1)) / 7 + 1;
    char text[32];
    snprintf(text, sizeof text, "%02ld", want);
    const std::string got = weekOf(static_cast<int>(year), static_cast<int>(yday), static_cast<int>(wday));
    if (got != text) fprintf(stderr, "day %ld (year %ld, yday %ld, wday %ld): week %s, want %s\n", day, year, yday, wday, got.c_str(), text);
    assert(got == text);
    ++checked;
  }
  assert(checked == 49673);                                   // the loop did run
  // A text can mix it with other tokens; an unset clock gives "--".
  tm t{}; t.tm_year = 126; t.tm_mon = 9; t.tm_mday = 15; t.tm_yday = 287; t.tm_wday = 4;   // Thu 15 Oct 2026
  assert(expandText("KW {WW} {YYYY}", &t) == "KW 42 2026");
  assert(expandText("KW {WW}", nullptr) == "KW --");
  // Fields out of range resolve like any value that cannot be resolved.
  assert(weekOf(2026, 366, 5) == "--" && weekOf(2026, -1, 5) == "--");
  assert(weekOf(2026, 100, 7) == "--" && weekOf(2026, 100, -1) == "--");
  // End to end: the text of a layer changes at the week boundary (Sunday to
  // Monday), and only then.
  Theme theme; std::string error;
  assert(parseTheme(manifest(2, "KW {WW}"), theme, error));
  Engine e; e.setTheme(theme, 0);
  tm sunday{}; sunday.tm_year = 126; sunday.tm_mon = 9; sunday.tm_mday = 25; sunday.tm_yday = 297; sunday.tm_wday = 0;
  e.update(0, &sunday);
  assert(e.states()[0].text == "KW 43");
  sunday.tm_hour = 23; sunday.tm_min = 59;
  assert(e.update(1, &sunday).empty() && e.states()[0].text == "KW 43");
  tm monday = sunday; monday.tm_hour = 0; monday.tm_min = 0; monday.tm_mday = 26; monday.tm_yday = 298; monday.tm_wday = 1;
  assert(!e.update(2, &monday).empty() && e.states()[0].text == "KW 44");
  // A spec 1 theme never reaches the token: validation refuses it.
  assert(!parseTheme(manifest(1, "KW {WW}"), theme, error));
}

int main() {
  testClockTokens();
  testSpecs();
  testDevice();
  testWeekToken();
  std::cout << "text token tests passed\n";
}
