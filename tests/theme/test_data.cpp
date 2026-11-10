#include "ThemeData.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
using namespace smalltv;

// Counts every operator new, so a test can tell whether a code path allocates.
static std::atomic<size_t> g_news{0};
void* operator new(std::size_t n) {
  ++g_news;
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// A socket that can stop halfway through a response, or drip one byte at a time.
struct Response {
  std::string bytes;
  size_t position=0;
  uint32_t start=0, elapsed=0, spacing=0;
  bool closeAtEnd=false, cancel=false;
  uint32_t now() const { return start+elapsed; }
  bool cancelled() const { return cancel; }
  int available() const {
    return position<bytes.size() && elapsed>=position*spacing ? 1 : 0;
  }
  int read(uint8_t* dest,size_t count) {
    if(!count || !available()) return 0;
    *dest=bytes[position++];return 1;
  }
  bool connected() const { return !closeAtEnd || position<bytes.size(); }
  void idle() { ++elapsed;assert(elapsed<=3500); }
};

// ---- DataAges: how old each source's data is, "<source>.age" in minutes ----
static ThemeDataSource sourceNamed(const char* id, const char* fieldId = "t") {
  ThemeDataSource s;
  s.id = id;
  ThemeDataField f;
  f.id = fieldId;
  f.path = "t";
  s.fields.push_back(f);
  return s;
}
static const ThemeValue* valueOf(const std::vector<ThemeValue>& values, const char* key) {
  for (const auto& v : values) if (v.key == key) return &v;
  return nullptr;
}
static void testDataAges(const char* wiringFile) {
  const std::vector<ThemeDataSource> sources = {sourceNamed("w"), sourceNamed("air")};
  std::vector<ThemeValue> values;
  DataAges ages;
  ages.reset(2);
  ages.tick(1000);
  // Before the first success the age is unknown: -1, and no value at all.
  assert(ages.minutes(0) == -1 && ages.minutes(1) == -1);
  assert(!ages.publish(sources, values) && values.empty());
  // A success makes it 0, and it counts whole minutes from then on.
  ages.tick(5000);
  ages.result(0, true);
  assert(ages.minutes(0) == 0 && ages.minutes(1) == -1);
  assert(ages.publish(sources, values) && values.size() == 1);
  assert(valueOf(values, "w.age") && valueOf(values, "w.age")->value == "0");
  assert(!ages.publish(sources, values));                   // nothing changed since
  ages.tick(5000 + 59999);
  assert(ages.minutes(0) == 0 && !ages.publish(sources, values));
  ages.tick(5000 + 60000);
  assert(ages.minutes(0) == 1);
  assert(ages.publish(sources, values) && valueOf(values, "w.age")->value == "1");
  ages.tick(5000 + 60000 * 59 + 59999);
  assert(ages.minutes(0) == 59);
  ages.tick(5000 + 60000 * 60);
  assert(ages.minutes(0) == 60);
  assert(ages.publish(sources, values) && valueOf(values, "w.age")->value == "60");
  // A failed fetch changes nothing: the age keeps growing from the last success.
  ages.result(0, false);
  assert(ages.minutes(0) == 60);
  ages.tick(5000 + 60000 * 61);
  assert(ages.minutes(0) == 61);
  // Another source's success does not touch this one, and each has its own age.
  ages.result(1, true);
  assert(ages.minutes(0) == 61 && ages.minutes(1) == 0);
  ages.tick(5000 + 60000 * 63);
  assert(ages.minutes(0) == 63 && ages.minutes(1) == 2);
  assert(ages.publish(sources, values));
  assert(valueOf(values, "w.age")->value == "63" && valueOf(values, "air.age")->value == "2" && values.size() == 2);
  // A new success starts over.
  ages.result(0, true);
  assert(ages.minutes(0) == 0);
  assert(ages.publish(sources, values) && valueOf(values, "w.age")->value == "0");
  // Other values in the list are left alone.
  values.push_back({"w.t", "21"});
  ages.tick(5000 + 60000 * 64);
  assert(ages.publish(sources, values) && valueOf(values, "w.t")->value == "21");
  // Starting over (a new theme) makes every age unknown again and drops the values.
  ages.reset(2);
  assert(ages.minutes(0) == -1 && ages.minutes(1) == -1);
  assert(ages.publish(sources, values) && !valueOf(values, "w.age") && !valueOf(values, "air.age") && valueOf(values, "w.t"));
  assert(!ages.publish(sources, values));

  // The clock is millis(), which wraps after 49.7 days: the age runs through it.
  DataAges wrapped;
  wrapped.reset(1);
  wrapped.tick(0xffffff00u);
  wrapped.result(0, true);
  wrapped.tick(0xffffff00u + 30000);
  assert(wrapped.minutes(0) == 0);
  wrapped.tick(0xffffff00u + 60000);        // now 0x0000ea60 - 0x100: past the wrap
  assert(wrapped.minutes(0) == 1);
  wrapped.tick(0xffffff00u + 60000 * 90);
  assert(wrapped.minutes(0) == 90);

  // A gap with no ticks (the theme was released for a while) still counts.
  DataAges gap;
  gap.reset(1);
  gap.tick(100);
  gap.result(0, true);
  gap.tick(100 + 3600000);
  assert(gap.minutes(0) == 60);

  // The first tick only sets the time base: a success before it is age 0, not
  // the time since an unset base.
  DataAges first;
  first.reset(1);
  first.result(0, true);
  first.tick(4000000);
  assert(first.minutes(0) == 0);
  first.tick(4000000 + 120000);
  assert(first.minutes(0) == 2);

  // A new theme starts the time base over too: the first tick after reset()
  // adds nothing, however long ago the last tick of the old theme was.
  DataAges again;
  again.reset(1);
  again.tick(100);
  again.reset(1);
  again.result(0, true);
  again.tick(5000000);
  assert(again.minutes(0) == 0);

  // It stops at the largest age (about 49 days) and never wraps back to small.
  DataAges old;
  old.reset(1);
  old.tick(0);
  old.result(0, true);
  uint32_t now = 0;
  for (int i = 0; i < 6; ++i) { now += 0x80000000u; old.tick(now); }
  assert(old.minutes(0) == 71582);
  now += 60000;
  old.tick(now);
  assert(old.minutes(0) == 71582);
  assert(old.publish(sources, values) && valueOf(values, "w.age")->value == "71582");

  // A theme's own field called "age" keeps its meaning: nothing is added for it.
  const std::vector<ThemeDataSource> own = {sourceNamed("p", "age")};
  std::vector<ThemeValue> ownValues = {{"p.age", "41"}};
  DataAges person;
  person.reset(1);
  person.tick(0);
  person.result(0, true);
  person.tick(180000);
  assert(!person.publish(own, ownValues) && ownValues.size() == 1 && ownValues[0].value == "41");

  // What theme mode does with a finished fetch (collectFetch): a success merges
  // its values by key and makes that source's age 0; a failure, whatever its
  // reason, leaves both the values and the age alone.
  {
    const std::vector<ThemeDataSource> two = {sourceNamed("w"), sourceNamed("air")};
    std::vector<ThemeValue> all;
    DataAges a;
    a.reset(2);
    a.tick(0);
    DataFetch bad(two[0], 0);
    bad.values.push_back({"w.t", "99"});                    // a half-read reply must not count
    bad.finish(false);
    assert(!collectFetch(bad, a, all) && all.empty() && a.minutes(0) == -1);
    DataFetch good(two[0], 0);
    good.values.push_back({"w.t", "21"});
    good.finish(true);
    assert(collectFetch(good, a, all) && all.size() == 1 && all[0].value == "21" && a.minutes(0) == 0 && a.minutes(1) == -1);
    a.tick(60000 * 5);
    DataFetch again(two[0], 0);
    again.values.push_back({"w.t", "22"});
    again.finish(false);
    assert(!collectFetch(again, a, all) && all[0].value == "21" && a.minutes(0) == 5);   // grows from the last success
    DataFetch other(two[1], 1);
    other.values.push_back({"air.pm", "7"});
    other.finish(true);
    assert(collectFetch(other, a, all) && all.size() == 2 && a.minutes(0) == 5 && a.minutes(1) == 0);
    DataFetch update(two[0], 0);
    update.values.push_back({"w.t", "23"});
    update.finish(true);
    assert(collectFetch(update, a, all) && all.size() == 2 && all[0].value == "23" && a.minutes(0) == 0);
  }

  // The device-only part (ThemeMode.cpp) cannot run here, so check that it is
  // wired as above: the ages move on first, a finished fetch goes through
  // collectFetch, the values are published after it, and a new theme (or an
  // unload) starts the ages over. argv[1] names another copy of the file.
  {
    std::ifstream in(wiringFile);
    assert(in.good());                                       // a missing file must not pass as "no calls"
    std::stringstream text;
    text << in.rdbuf();
    const std::string code = text.str();
    const size_t tick = code.find("dataAges_.tick(millis());");
    const size_t collect = code.find("changed = smalltv::collectFetch(*result, dataAges_, dataValues_);");
    const size_t publish = code.find("if (dataAges_.publish(sources, dataValues_)) changed = true;");
    const size_t set = code.find("if (changed) engine_.setValues(dataValues_);");
    assert(tick != std::string::npos && collect != std::string::npos && publish != std::string::npos && set != std::string::npos);
    assert(tick < collect && collect < publish && publish < set);
    assert(code.find("dataAges_.tick(") == tick && code.find("dataAges_.tick(", tick + 1) == std::string::npos);
    // Only a spec 2 theme gets ages.
    assert(code.find("dataAges_.reset(theme.data.size(), theme.spec >= 2);") != std::string::npos);
    assert(code.find("dataAges_.reset(0);") != std::string::npos);
    // The reset for a new theme sits with the other data state that a new theme clears.
    const size_t clearValues = code.find("dataValues_.clear();\n        // Only a spec 2 theme can use \"<source>.age\"; a spec 1 theme gets none.\n        dataAges_.reset(");
    assert(clearValues != std::string::npos);
    // The values are not handed over every frame, only when they changed.
    const std::string guarded = "if (changed) engine_.setValues(dataValues_);";
    assert(code.find("engine_.setValues(dataValues_);", set + guarded.size()) != std::string::npos);   // the load's own call
    assert(code.find("engine_.setValues(dataValues_);") == set + 13);                      // none before the guarded one
  }

  // Out of range indexes are ignored (a stale result of an older theme).
  ages.reset(1);
  ages.result(7, true);
  assert(ages.minutes(7) == -1 && ages.minutes(0) == -1);
  DataAges nothing;                                          // no sources: any index is out of range
  nothing.reset(0);
  nothing.result(0, true);
  assert(nothing.minutes(0) == -1);
  std::cout << "data age tests passed\n";
}

// refreshData() runs on every service() call, about once per frame. With no
// change in the whole-minute value, publish() must not build a string. Counted
// as operator new calls; a source id longer than 11 characters makes
// "<id>.age" too long for std::string's inline buffer (15 chars in libstdc++'s
// new ABI), so the old code allocated for it on every call. The short ids of a
// real theme ("w", "d") never allocated, so they are counted too (printed).
static size_t newsDuringIdlePublish(const char* id, int calls) {
  const std::vector<ThemeDataSource> sources = {sourceNamed(id), sourceNamed("zz")};
  std::vector<ThemeValue> values;
  values.reserve(8);                                         // no growth of the vector itself
  DataAges ages;
  ages.reset(2);
  ages.tick(0);
  ages.result(0, true);
  ages.publish(sources, values);                             // the first call may build
  const size_t before = g_news.load();
  for (int i = 0; i < calls; ++i) {
    ages.tick(1000u + 7u * i);                               // well inside the same minute
    ages.publish(sources, values);
  }
  const size_t used = g_news.load() - before;
  assert(valueOf(values, (std::string(id) + ".age").c_str()) && ages.minutes(0) == 0);
  return used;
}
static void testDataAgePublishAllocations() {
  const size_t longId = newsDuringIdlePublish("weather_forecast_today", 1000);
  const size_t shortId = newsDuringIdlePublish("w", 1000);
  std::cout << "allocations in 1000 idle publish() calls: long id " << longId << ", short id " << shortId << std::endl;
  assert(longId == 0);
  assert(shortId == 0);
  // A change in the minute still publishes (and may allocate): the saving must
  // not lose an update.
  const std::vector<ThemeDataSource> sources = {sourceNamed("weather_forecast_today")};
  std::vector<ThemeValue> values;
  DataAges ages;
  ages.reset(1);
  ages.tick(0);
  ages.result(0, true);
  assert(ages.publish(sources, values) && values[0].value == "0");
  for (int i = 1; i <= 3; ++i) {
    ages.tick(60000u * i);
    assert(ages.publish(sources, values) && values[0].value == std::to_string(i));
    // Once published, the same minute costs nothing again.
    const size_t before = g_news.load();
    assert(!ages.publish(sources, values));
    assert(g_news.load() == before);
  }
  // Starting over (a new theme) makes the next publish look at every source
  // again, even one whose age was unknown before: a stale value goes away.
  DataAges fresh;
  std::vector<ThemeValue> mine;
  fresh.reset(1);
  fresh.publish(sources, mine);                              // unknown: nothing to add
  mine.push_back({"weather_forecast_today.age", "5"});
  fresh.reset(1);
  assert(fresh.publish(sources, mine) && mine.empty());
  // ... and a value that is already right is not reported as a change.
  fresh.reset(1);
  fresh.tick(0);
  fresh.result(0, true);
  mine.push_back({"weather_forecast_today.age", "0"});
  assert(!fresh.publish(sources, mine) && mine.size() == 1 && mine[0].value == "0");
  // A fetch result in the same minute (age back to 0) is a change too.
  ages.result(0, true);
  assert(ages.publish(sources, values) && values[0].value == "0");
}

// A spec 1 theme gets no "<source>.age" values (the parser would not accept the
// token in it): nothing is published, whatever the ticks and results.
static void testDataAgeSpec1() {
  const std::vector<ThemeDataSource> sources = {sourceNamed("w")};
  std::vector<ThemeValue> values;
  DataAges off;
  off.reset(1, false);
  off.tick(0);
  off.result(0, true);
  off.tick(600000);
  assert(!off.publish(sources, values) && values.empty());
  DataAges on;
  on.reset(1, true);
  on.tick(0);
  on.result(0, true);
  assert(on.publish(sources, values) && values.size() == 1);
  // A new theme can switch it on and off again.
  values.clear();
  on.reset(1, false);
  on.tick(0);
  on.result(0, true);
  assert(!on.publish(sources, values) && values.empty());
  on.reset(1);                                               // the default is on
  on.tick(0);
  on.result(0, true);
  assert(on.publish(sources, values) && values.size() == 1);
}

// ---- Fetch retry: after a failed fetch, 1, 2, 4, 8 minutes, then the interval ----
// Runs the fetches of one source the way refreshData does: each ends, the wait
// comes from fetchWaitMs, and the next one starts when it is due.
static std::vector<uint32_t> waitsAfterFailures(uint32_t interval, int failures) {
  uint8_t tries = 0;
  fetchStarted(tries);
  std::vector<uint32_t> waits;
  for (int i = 0; i < failures; ++i) waits.push_back(fetchWaitMs(interval, false, tries));
  return waits;
}
static void testFetchRetry(const char* wiringFile) {
  // The sequence at a long interval: 60, 120, 240, 480 s, then the interval.
  {
    const auto w = waitsAfterFailures(900, 8);
    const uint32_t want[] = {60000, 120000, 240000, 480000, 900000, 900000, 900000, 900000};
    for (int i = 0; i < 8; ++i) assert(w[i] == want[i]);
  }
  // With 300 s the fourth wait is capped, so the gaps are 60, 120, 240, 300, 300.
  {
    const auto w = waitsAfterFailures(300, 6);
    const uint32_t want[] = {60000, 120000, 240000, 300000, 300000, 300000};
    for (int i = 0; i < 6; ++i) assert(w[i] == want[i]);
  }
  // A wait is never longer than the interval, for any interval the parser
  // allows (10 to 86,400 s), and at most 4 waits are shorter than it.
  {
    const uint32_t intervals[] = {10, 11, 59, 60, 61, 120, 300, 480, 481, 900, 3600, 86400};
    for (uint32_t interval : intervals) {
      const auto w = waitsAfterFailures(interval, 40);
      int shorter = 0;
      for (uint32_t ms : w) {
        assert(ms <= interval * 1000u && ms > 0);
        if (ms < interval * 1000u) ++shorter;
      }
      assert(shorter <= 4);
      assert(w[39] == interval * 1000u);
    }
    const auto ten = waitsAfterFailures(10, 6);
    for (uint32_t ms : ten) assert(ms == 10000);               // never fetches less often than today
    const auto sixty = waitsAfterFailures(60, 6);
    for (uint32_t ms : sixty) assert(ms == 60000);
    const auto day = waitsAfterFailures(86400, 6);
    assert(day[0] == 60000 && day[3] == 480000 && day[4] == 86400000u);
  }
  // A success goes straight back to the interval, clears the count, and a later
  // failure starts over at one minute.
  {
    uint8_t tries = 0;
    fetchStarted(tries);
    assert(fetchWaitMs(900, false, tries) == 60000);
    assert(fetchWaitMs(900, false, tries) == 120000);
    assert(fetchWaitMs(900, false, tries) == 240000);
    assert(fetchFailures(tries) == 3);
    assert(fetchWaitMs(900, true, tries) == 900000);
    assert(fetchFailures(tries) == 0);
    assert(fetchWaitMs(900, false, tries) == 60000);
    assert(fetchWaitMs(900, false, tries) == 120000);
    // A success after the last short wait, and after the long ones, too.
    for (int i = 0; i < 10; ++i) fetchWaitMs(900, false, tries);
    assert(fetchWaitMs(900, true, tries) == 900000 && fetchFailures(tries) == 0);
    assert(fetchWaitMs(900, false, tries) == 60000);
  }
  // A long outage: thousands of failures never wrap the count back to short waits.
  {
    uint8_t tries = 0;
    fetchStarted(tries);
    for (int i = 0; i < 100000; ++i) {
      const uint32_t ms = fetchWaitMs(900, false, tries);
      if (i >= 4) assert(ms == 900000);
      assert(fetchFailures(tries) > 0);                       // the count never reads as "no failure"
    }
    assert(fetchFailures(tries) >= 5);
    // Counting right at the top of the range does not wrap either.
    tries = 255;
    assert(fetchWaitMs(900, false, tries) == 900000 && tries == 255 && fetchFailures(tries) == 254);
    tries = 254;
    assert(fetchWaitMs(900, false, tries) == 900000 && tries == 255);
    assert(fetchWaitMs(300, true, tries) == 300000 && fetchFailures(tries) == 0);
  }
  // A result for a source that was never marked started still counts as a
  // first failure (the first wait is one minute, not a shift by a negative).
  {
    uint8_t tries = 0;
    assert(fetchWaitMs(900, false, tries) == 60000 && fetchFailures(tries) == 1);
    uint8_t other = 0;
    assert(fetchWaitMs(900, true, other) == 900000 && fetchFailures(other) == 0 && other != 0);
  }
  // A source that was never started is due at once, whatever the clock says,
  // and starting it makes it a scheduled one.
  {
    uint8_t tries = 0;
    assert(fetchDue(tries, 0, 0) && fetchDue(tries, 12345, 0) && fetchDue(tries, 0xffffffffu, 5));
    fetchStarted(tries);
    assert(fetchFailures(tries) == 0 && !fetchDue(tries, 1000, 999) && fetchDue(tries, 1000, 1000));
    // Starting again (a source whose fetch was dropped and is due again) keeps the count.
    fetchWaitMs(900, false, tries);
    fetchWaitMs(900, false, tries);
    fetchStarted(tries);
    assert(fetchFailures(tries) == 2);
  }
  // The schedule works across the wrap of millis(): the next time is now plus
  // the wait, the due test is the signed difference, for every interval.
  {
    const uint32_t intervals[] = {10, 60, 300, 900, 86400};
    const uint32_t clocks[] = {0, 1000, 0x7fffffffu - 5000, 0x80000000u, 0xfffffff0u, 0xffffffffu - 100000, 0xffffffffu};
    for (uint32_t interval : intervals) {
      for (uint32_t now : clocks) {
        for (int failures = 0; failures < 7; ++failures) {
          uint8_t tries = 0;
          fetchStarted(tries);
          uint32_t wait = fetchWaitMs(interval, true, tries);
          for (int i = 0; i < failures; ++i) wait = fetchWaitMs(interval, false, tries);
          const uint32_t next = now + wait;
          assert(!fetchDue(tries, next, now));
          assert(!fetchDue(tries, next, now + (wait - 1)));
          assert(fetchDue(tries, next, now + wait));
          assert(fetchDue(tries, next, now + wait + 1000));
        }
      }
    }
  }

  // refreshData is device-only code, so check that it is wired as above: a
  // finished fetch gets its wait from fetchWaitMs, the count sits with the
  // other per-source state, a new theme (or an unload) clears it, and a
  // release keeps it. argv[1] names another copy of the file.
  {
    std::ifstream in(wiringFile);
    assert(in.good());
    std::stringstream text;
    text << in.rdbuf();
    const std::string code = text.str();
    const std::string wait = "dataNextMs_[result->index] = millis() + smalltv::fetchWaitMs(sources[result->index].interval, result->success, dataTries_[result->index]);";
    assert(code.find(wait) != std::string::npos);
    assert(code.find("fetchWaitMs(") == code.find(wait) + wait.find("fetchWaitMs(") && code.find("fetchWaitMs(", code.find(wait) + wait.size()) == std::string::npos);
    assert(code.find("interval * 1000") == std::string::npos);  // no second, plain schedule
    // The scan asks fetchDue, and marks a source started with fetchStarted.
    const size_t due = code.find("if (!smalltv::fetchDue(dataTries_[i], dataNextMs_[i], millis())) continue;");
    const size_t start = code.find("auto request = dataRequests_.start(sources[i], i);");
    const size_t started = code.find("smalltv::fetchStarted(dataTries_[i]);");
    assert(due != std::string::npos && start != std::string::npos && started != std::string::npos);
    assert(due < start && start < started);
    // A refused fetch is a failure: finish(false) is collected by the same take().
    assert(code.find("if (!startThemeDataFetch(request)) request->finish(false);") != std::string::npos);
    // The Wi-Fi check comes before any start, so no fetch (and no count) while it is down.
    const size_t wifi = code.find("WiFi.status() != WL_CONNECTED) return;");
    assert(wifi != std::string::npos && wifi < due);
    // Cleared with dataNextMs_ in a new theme and in unload(); not in release().
    assert(code.find("dataNextMs_.assign(theme.data.size(), 0);\n        dataTries_.assign(theme.data.size(), 0);") != std::string::npos);
    assert(code.find("dataNextMs_.clear();\n  dataTries_.clear();") != std::string::npos);
    assert(code.find("dataScheduled_") == std::string::npos);
    const size_t releaseAt = code.find("void ThemeMode::release(");
    const size_t releaseEnd = code.find("\n}\n", releaseAt);
    assert(releaseAt != std::string::npos && releaseEnd != std::string::npos);
    const std::string release = code.substr(releaseAt, releaseEnd - releaseAt);
    assert(release.find("dataTries_") == std::string::npos && release.find("keepData_ = true;") != std::string::npos);
  }
  std::cout << "theme data fetch retry tests passed\n";
}

// A field with the given id and path (ThemeDataField has a default member
// initializer, so it is no aggregate in C++11 and cannot be brace-initialised).
static ThemeDataField fieldOf(const std::string& id, const std::string& path) {
  ThemeDataField f;
  f.id=id;
  f.path=path;
  return f;
}

// ---- A reply read as it arrives, straight into the filtered parse ----
static ThemeDataSource valueSource() {
  ThemeDataSource s;
  s.id="d";
  s.fields.push_back(fieldOf("v","value"));
  return s;
}
// {"value":42,"pad":"xxx..."} of exactly `size` bytes (a made-up reply).
static std::string padded(size_t size) {
  std::string s=R"({"value":42,"pad":")";
  s+=std::string(size-s.size()-2,'x');
  return s+"\"}";
}
// {"value":42} followed by spaces up to `size` bytes.
static std::string spaced(size_t size) {
  std::string s=R"({"value":42})";
  return s+std::string(size-s.size(),' ');
}
// The tests here run on a 64-bit host, where ArduinoJson's first pool is 4 KB
// (1 KB on the device): they give the parse room and test the budget by
// itself (testFilter). The device budget is tested in test_memory.cpp.
static const size_t Roomy = 1 << 20;
static std::string manyOnes(size_t count);
static void testReplyStream() {
  std::vector<ThemeValue> values;
  // True when the reply was read whole and valid; then `values` holds the one
  // value, else it is exactly what it was.
  auto fetch=[&](Response& r,int length) {
    values.assign(1,ThemeValue{"keep","1"});
    const bool ok=readDataValues(r,length,valueSource(),values,Roomy);
    if(ok && !(values.size()==2 && values[0].key=="keep" && values[1]==(ThemeValue{"d.v","42"})))
      fprintf(stderr,"reply of %zu B (length %d) read, but values: %zu, last %s\n",r.bytes.size(),length,values.size(),values.empty()?"":(values.back().key+"="+values.back().value).c_str());
    if(ok) assert(values.size()==2 && values[0].key=="keep" && values[1]==(ThemeValue{"d.v","42"}));
    else assert(values.size()==1 && values[0].key=="keep");
    return ok;
  };
  Response complete;complete.bytes="{\"value\":42}";
  assert(fetch(complete,12) && complete.elapsed==0);
  Response stalled;stalled.bytes="{";
  assert(!fetch(stalled,12) && stalled.elapsed==3000);
  Response disconnected;disconnected.bytes="{";disconnected.closeAtEnd=true;
  assert(!fetch(disconnected,12));
  Response unknown;unknown.bytes=spaced(2048);unknown.closeAtEnd=true;
  assert(fetch(unknown,-1) && unknown.position==2048);
  Response tooLarge;tooLarge.bytes=spaced(2049);tooLarge.closeAtEnd=true;
  assert(!fetch(tooLarge,-1) && tooLarge.position<=2048);   // never read past the cap
  assert(tooLarge.elapsed==0);                              // and it fails at once, not at the deadline
  Response tooLargeSized;tooLargeSized.bytes=spaced(2049);
  assert(!fetch(tooLargeSized,2049) && tooLargeSized.position==0);
  Response atCap;atCap.bytes=spaced(2048);
  assert(fetch(atCap,2048) && atCap.position==2048);
  Response empty;empty.closeAtEnd=true;assert(!fetch(empty,0));
  Response zero;zero.bytes=spaced(100);zero.closeAtEnd=true;
  assert(!fetch(zero,0) && zero.position==0);   // "Content-Length: 0" is no reply, whatever follows
  Response nothing;nothing.closeAtEnd=true;assert(!fetch(nothing,-1));
  Response invalid;invalid.bytes=R"(<html>)";invalid.closeAtEnd=true;assert(!fetch(invalid,-1));
  Response cutOff;cutOff.bytes=R"({"value":)";cutOff.closeAtEnd=true;assert(!fetch(cutOff,-1));
  // Silence is not the end of a reply of unknown length: the document is
  // complete, but the connection never closes, so the fetch ends at the deadline.
  Response idleJson;idleJson.bytes="{\"value\":42}";
  assert(!fetch(idleJson,-1) && idleJson.elapsed==3000);
  // A slow trickle ends at the overall deadline, in the middle of the parse.
  Response slow;slow.bytes="{\"value\":42}";slow.spacing=900;
  assert(!fetch(slow,12) && slow.elapsed==3000);
  Response wrapped;wrapped.start=0xfffffff0;wrapped.bytes="{";
  assert(!fetch(wrapped,12) && wrapped.elapsed==3000);
  Response cancelled;cancelled.cancel=true;cancelled.bytes="{\"value\":42}";
  assert(!fetch(cancelled,12) && cancelled.elapsed==0 && cancelled.position==0);
  // A reply that drips one byte at a time still gets in, within the deadline.
  Response drip;drip.bytes=padded(1000);drip.spacing=2;drip.closeAtEnd=true;
  assert(fetch(drip,-1) && drip.position==1000);
  // A declared length is honoured: nothing past it is read (the next bytes are
  // not this reply), and a reply that stops short of it fails even when the
  // document in it is whole.
  Response longer;longer.bytes="{\"value\":42}trailing bytes";
  assert(fetch(longer,12) && longer.position==12);
  Response sized;sized.bytes=spaced(100);
  assert(fetch(sized,100) && sized.position==100);
  Response shortOfIt;shortOfIt.bytes=spaced(100);shortOfIt.closeAtEnd=true;
  assert(!fetch(shortOfIt,300));
  // No length: read until the close, everything counts toward the cap.
  Response untilClose;untilClose.bytes=spaced(600);untilClose.closeAtEnd=true;
  assert(fetch(untilClose,-1) && untilClose.position==600);
  // The budget given to readDataValues reaches the parse: a field that keeps a
  // long list (it names it) does not fit in 1,536 B (a pool is bigger on the
  // host and on the device), and does with room.
  ThemeDataSource listed;listed.id="l";listed.fields.push_back(fieldOf("all","a"));
  const std::string longList=R"({"a":)"+manyOnes(300)+"}";
  Response tight;tight.bytes=longList;tight.closeAtEnd=true;
  values.assign(1,ThemeValue{"keep","1"});
  assert(!readDataValues(tight,-1,listed,values,1536) && values.size()==1);
  Response roomy;roomy.bytes=longList;roomy.closeAtEnd=true;
  assert(readDataValues(roomy,-1,listed,values,Roomy) && values.size()==2 && values[1]==(ThemeValue{"l.all","--"}));
  // A header that is a nonsense length.
  Response negative;negative.bytes=spaced(100);negative.closeAtEnd=true;
  assert(!fetch(negative,-2));
  assert(MaxDataBody==2048 && DataTimeoutMs==3000);
}

// ---- The filter: what the parse keeps ----
static std::string manyOnes(size_t count) {
  std::string s="[";
  for(size_t i=0;i<count;++i) s+=i?",1":"1";
  return s+"]";
}
static void testFilter() {
  ThemeDataSource source;
  source.id="w";
  source.fields.push_back(fieldOf("now","current.t"));
  source.fields.push_back(fieldOf("hi","daily.hi"));
  source.fields.push_back(fieldOf("code","code"));
  source.fields.push_back(fieldOf("gone","absent.x"));
  // The least budget a small reply needs here: ArduinoJson's first pool of a
  // document (its size depends on the build: 1 KB on the device, 4 KB on a
  // 64-bit host) and the filter. `room` is that and a little more, but far less
  // than a second pool.
  auto smallest=[&](const std::string& body) {
    std::vector<ThemeValue> got;
    for(size_t budget=16;budget<=65536;budget+=16)
      if(extractThemeValues(body,source,got,budget)) return budget;
    return size_t(0);
  };
  const size_t base=smallest(R"({"current":{"t":21.5},"daily":{"hi":2},"code":3})");
  assert(base>=1024 && base<=8192);
  const size_t room=base+64;
  // Plenty that no field names: a long list, a deep object, other keys. With
  // `room` the reply parses only if all of that is dropped.
  const std::string junk=R"({"junk":)"+manyOnes(300)+R"(,"current":{"t":21.5,"u":{"deep":[1,2,3],"more":"text"},"w":9},)"
    R"("daily":{"time":["a","b"],"hi":17.5,"min":[1,2]},"code":3,"tail":{"a":{"b":{"c":1}}}})";
  std::vector<ThemeValue> values;
  assert(extractThemeValues(junk,source,values,room));
  assert(smallest(junk)<=base+32);   // dropping costs (next to) nothing: no second pool
  assert(values.size()==4);
  assert(values[0]==(ThemeValue{"w.now","21.5"}) && values[1]==(ThemeValue{"w.hi","17.5"}));
  assert(values[2]==(ThemeValue{"w.code","3"}) && values[3]==(ThemeValue{"w.gone","--"}));
  // The same reply with no limit gives the same values.
  std::vector<ThemeValue> unlimited;
  assert(extractThemeValues(junk,source,unlimited,0) && unlimited==values);
  // A reply that is not an object or a list is refused, whatever the filter
  // would skip: an error page must not pass as an empty reply (it would
  // overwrite the old values with "--" and count as a success).
  std::vector<ThemeValue> none2{{"keep","1"}};
  const char* const notRoots[]={"<html>404</html>","42","\"text\"","null","true","garbage {\"code\":3}","/* c */{\"code\":3}"};
  for(const char* text:notRoots) assert(!extractThemeValues(text,source,none2,0) && none2.size()==1);
  assert(extractThemeValues(" \r\n\t{\"code\":3}",source,none2,0) && none2.size()==5);
  none2.resize(1);
  assert(extractThemeValues("[1,2]",source,none2,0) && none2.size()==5 && none2[1].value=="--");
  // Too small a budget for even the filter: refused, not a half-built filter.
  std::vector<ThemeValue> tiny{{"keep","1"}};
  assert(!extractThemeValues(junk,source,tiny,64) && tiny.size()==1);
  // A source with no fields keeps nothing, and still refuses what is not JSON.
  ThemeDataSource none;none.id="n";
  std::vector<ThemeValue> noValues;
  assert(extractThemeValues(junk,none,noValues,300) && noValues.empty());
  assert(!extractThemeValues("{\"junk\":[1,",none,noValues,300));
  // Dropped parts are still checked: nesting deeper than 8 is refused even
  // where no field looks.
  assert(!extractThemeValues(R"({"junk":[[[[[[[[[1]]]]]]]]],"code":3})",source,noValues,0));
  assert(extractThemeValues(R"({"junk":[[[[[[[1]]]]]]],"code":3})",source,noValues,0));    // the object and 7 lists: 8 levels
  assert(!extractThemeValues(R"({"junk":[[[[[[[[1]]]]]]]],"code":3})",source,noValues,0));  // the object and 8 lists: 9 levels
  // The budget is a ceiling: a field that names a long list keeps the whole
  // list (its value reads "--", a list is not a value), so it is refused when
  // the list does not fit, and the values stay as they were.
  ThemeDataSource listed;listed.id="l";listed.fields.push_back(fieldOf("all","a"));
  const std::string big=R"({"a":)"+manyOnes(400)+"}";
  std::vector<ThemeValue> kept{{"keep","1"}};
  assert(!extractThemeValues(big,listed,kept,room) && kept.size()==1 && kept[0].key=="keep");
  assert(extractThemeValues(big,listed,kept,0) && kept.size()==2 && kept[1]==(ThemeValue{"l.all","--"}));
  // Two fields on one branch, in either order: the longer path does not undo
  // the shorter one and the other way round.
  ThemeDataSource shared;shared.id="s";
  shared.fields.push_back(fieldOf("obj","o"));
  shared.fields.push_back(fieldOf("inner","o.k"));
  shared.fields.push_back(fieldOf("n","o.n"));
  std::vector<ThemeValue> sharedValues;
  assert(extractThemeValues(R"({"o":{"k":"v","n":2},"z":1})",shared,sharedValues,0));
  assert(sharedValues[0].value=="--" && sharedValues[1].value=="v" && sharedValues[2].value=="2");
  ThemeDataSource reversed;reversed.id="s";
  reversed.fields.push_back(fieldOf("inner","o.k"));
  reversed.fields.push_back(fieldOf("obj","o"));
  reversed.fields.push_back(fieldOf("n","o.n"));
  sharedValues.clear();
  assert(extractThemeValues(R"({"o":{"k":"v","n":2},"z":1})",reversed,sharedValues,0));
  assert(sharedValues[0].value=="v" && sharedValues[1].value=="--" && sharedValues[2].value=="2");
  // A field that is a plain value and another that looks inside it: the plain
  // one stays (a longer path must not turn the kept value into a branch).
  ThemeDataSource plain;plain.id="p";
  plain.fields.push_back(fieldOf("s","s"));
  plain.fields.push_back(fieldOf("k","s.k"));
  std::vector<ThemeValue> plainValues;
  assert(extractThemeValues(R"({"s":"text"})",plain,plainValues,0));
  assert(plainValues[0].value=="text" && plainValues[1].value=="--");
  // A path with an empty part, or an empty path, finds nothing.
  ThemeDataSource odd;odd.id="o";
  odd.fields.push_back(fieldOf("a","a..b"));
  odd.fields.push_back(fieldOf("b",""));
  odd.fields.push_back(fieldOf("c",".a"));
  odd.fields.push_back(fieldOf("d","a"));
  std::vector<ThemeValue> oddValues;
  assert(extractThemeValues(R"({"a":5})",odd,oddValues,0) && oddValues.size()==4);
  assert(oddValues[0].value=="--" && oddValues[1].value=="--" && oddValues[2].value=="--" && oddValues[3].value=="5");
  // A part of a path is always an object's key (digits too); a path does not
  // go into a list.
  ThemeDataSource digits;digits.id="g";
  digits.fields.push_back(fieldOf("z","named.0"));
  digits.fields.push_back(fieldOf("o","named.1"));
  digits.fields.push_back(fieldOf("e","list.2"));
  std::vector<ThemeValue> digitValues;
  assert(extractThemeValues(R"({"named":{"0":"zero","1":"one"},"list":[7,8,9],"x":[1]})",digits,digitValues,0));
  assert(digitValues[0].value=="zero" && digitValues[1].value=="one" && digitValues[2].value=="--");
  std::cout<<"theme data filter tests passed\n";
}

// ---- The fetch gate and the fetch status ----
// ThemeDataClient.cpp is device-only code, so check that it is wired as
// described: a fetch is refused before it starts when the heap or the largest
// block is short, and once the connection is up the parse heap is checked
// before the reply is read; each refusal is counted; the reply is parsed from
// the stream (never read into a string first).
static void testFetchWiring(const char* clientFile) {
  std::ifstream in(clientFile);
  assert(in.good());
  std::stringstream text;
  text << in.rdbuf();
  const std::string code = text.str();
  // The start gate, before the fetch runs; the refusal is counted.
  const std::string gate = "if (ESP.getFreeHeap() < smalltv::FetchFreeHeap || platformMaxFreeBlock() < smalltv::FetchLargestBlock) {\n    noteFetch(Outcome::Refused);\n    return false;\n  }\n  request->finish(fetchThemeSource(*request));";
  assert(code.find(gate) != std::string::npos);
  // After GET() and before the reply is read: the heap for the parse.
  const size_t get = code.find("http.GET() != HTTP_CODE_OK");
  const size_t parseGate = code.find("if (ESP.getFreeHeap() < smalltv::FetchParseHeap) return Outcome::Refused;");
  const size_t parse = code.find("smalltv::readDataValues(stream, http.getSize(), request.source, request.values)");
  assert(get != std::string::npos && parseGate != std::string::npos && parse != std::string::npos);
  assert(get < parseGate && parseGate < parse);
  // The https check is a refusal too.
  assert(code.find("if (ESP.getFreeHeap() < 18000) return Outcome::Refused;") != std::string::npos);
  // No copy of the reply, and no document of everything in it.
  assert(code.find("readDataBody") == std::string::npos && code.find("std::string body") == std::string::npos);
  assert(code.find("deserializeJson") == std::string::npos);
  // A fetch dropped by a theme change is not noted; every other outcome is.
  assert(code.find("if (!request.cancelled()) noteFetch(outcome);") != std::string::npos);
  assert(code.find("return outcome == Outcome::Ok;") != std::string::npos);
  std::cout << "theme data fetch wiring tests passed\n";
}
// The status count: refused fetches since boot, and how the last one ended.
static void testFetchStats() {
  FetchStats stats;
  assert(!stats.any() && stats.refused == 0);
  stats.note(0);
  assert(stats.any() && stats.last == 1 && stats.refused == 0);
  stats.note(1);
  stats.note(1);
  assert(stats.last == 2 && stats.refused == 2);
  stats.note(2);
  assert(stats.last == 3 && stats.refused == 2);
  stats.note(3);
  stats.note(-1);
  assert(stats.last == 3 && stats.refused == 2);              // unknown outcomes are ignored
  stats.refused = 65535;
  stats.note(1);
  assert(stats.refused == 65535 && stats.last == 2);          // stops at the top, never wraps
  assert(sizeof(FetchStats) == 4);
  std::cout << "theme data fetch stats tests passed\n";
}

int main(int argc, char** argv) {
  testFetchStats();
  testFetchWiring(argc > 2 ? argv[2] : "src/features/theme/ThemeDataClient.cpp");
  testDataAgeSpec1();
  testDataAgePublishAllocations();
  testDataAges(argc > 1 ? argv[1] : "src/features/theme/ThemeMode.cpp");
  testFetchRetry(argc > 1 ? argv[1] : "src/features/theme/ThemeMode.cpp");
  testFilter();
  testReplyStream();

  // On the ESP8266, available() lets the network stack run when it finds
  // nothing, and still returns that 0 if the rest of the reply and the
  // server's close arrived meanwhile. The socket then says "not connected"
  // with bytes waiting; they are part of the reply. Measured on a
  // SmallTV-Ultra with Open-Meteo: 414 of 550 bytes kept, 136 left waiting.
  struct LateRest {
    std::string bytes;
    size_t position=0, firstPart=0;
    bool arrived=false;
    uint32_t elapsed=0;
    uint32_t now() const { return elapsed; }
    bool cancelled() const { return false; }
    size_t limit() const { return arrived ? bytes.size() : firstPart; }
    int available() {
      if(position<limit()) return int(limit()-position);
      arrived=true;  // lands while available() yields; this call still says 0
      return 0;
    }
    int read(uint8_t* dest,size_t count) {
      size_t n=std::min(count,limit()-position);
      memcpy(dest,bytes.data()+position,n);position+=n;return int(n);
    }
    bool connected() const { return !arrived; }
    void idle() { ++elapsed;assert(elapsed<=3500); }
  };
  {
    std::vector<ThemeValue> values;
    LateRest late;late.bytes=padded(550);late.firstPart=414;
    assert(readDataValues(late,-1,valueSource(),values,Roomy) && values.size()==1 && values[0].value=="42" && late.position==550);
    values.clear();
    LateRest lateSized;lateSized.bytes=padded(550);lateSized.firstPart=414;
    assert(readDataValues(lateSized,550,valueSource(),values,Roomy) && values.size()==1 && lateSized.position==550);
    // The document is whole in the first part and the close comes with bytes
    // still waiting after it: they are read too, and counted.
    values.clear();
    LateRest tail;tail.bytes=spaced(550);tail.firstPart=40;
    assert(readDataValues(tail,-1,valueSource(),values,Roomy) && tail.position==550);
    // A declared length is a hard stop even when more is waiting in the same
    // read: the next bytes are not this reply's.
    values.clear();
    LateRest more;more.bytes=spaced(100)+"next reply";more.firstPart=more.bytes.size();
    assert(readDataValues(more,100,valueSource(),values,Roomy) && values.size()==1 && more.position==100);
  }

  // A slow old request must neither block animation ticks nor update a new theme.
  DataRequests requests;ThemeDataSource source;source.id="old";
  auto job=requests.start(source,0);assert(job);
  std::atomic<bool> release(false);
  std::thread worker([&] {
    while(!release.load()) std::this_thread::yield();
    job->values.push_back({"old.value","42"});job->finish(true);
  });
  Theme theme;Layer sprite;sprite.type=LayerType::Animation;sprite.frames=12;sprite.fps=8;
  sprite.width=sprite.height=4;theme.layers.push_back(sprite);Engine engine;engine.setTheme(theme,0);
  engine.update(0,nullptr);
  for(unsigned frame=1;frame<8;++frame) {
    assert(!requests.take());assert(!engine.update(frame*125,nullptr).empty());
    assert(engine.states()[0].frame==frame);
  }
  requests.cancel();assert(job->cancelled());assert(!requests.start(source,1));
  release.store(true);worker.join();assert(!requests.take() && !requests.busy());
  source.id="new";job=requests.start(source,1);assert(job);
  job->values.push_back({"new.value","7"});job->finish(true);
  auto result=requests.take();assert(result && result->success && result->index==1);
  assert(result->values[0].key=="new.value" && !requests.busy());
  job=requests.start(source,0);job->finish(false);result=requests.take();
  assert(result && !result->success && !requests.busy());
  // An asynchronous fetched text change resets scrolling once, on consumption.
  Theme scrolling;Layer text;text.value="{new.value}";text.size=8;
  text.scroll.enabled=true;text.scroll.width=24;text.scroll.speed=20;
  text.scroll.pauseMs=1000;text.scroll.gap=6;scrolling.layers.push_back(text);
  engine.setValues({{"new.value","ABCDEFGHIJ"}});engine.setTheme(scrolling,0);
  engine.update(0,nullptr);engine.update(1500,nullptr);
  assert(engine.states()[0].scrollOffset==10);
  job=requests.start(source,0);assert(job);
  std::thread refresh([&] {
    job->values.push_back({"new.value","ABCDEFGHIJKLMNO"});job->finish(true);
  });
  refresh.join();result=requests.take();assert(result&&result->success);
  engine.setValues(result->values);engine.update(1501,nullptr);
  assert(engine.states()[0].text=="ABCDEFGHIJKLMNO"&&engine.states()[0].scrollOffset==0);
  assert(engine.states()[0].scrollPauseUntil==2501);
  assert(!requests.take());engine.setValues(result->values);
  assert(engine.update(2501,nullptr).empty());
  engine.update(2551,nullptr);assert(engine.states()[0].scrollOffset==1);
  std::cout<<"theme data deadline and asynchronous result tests passed\n";
}
