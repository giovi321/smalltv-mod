// The crash record is wired in the places that matter. The ESP8266 callback and
// the status handler cannot run here, so this reads the sources: a check on the
// wiring and on the order of the callback's steps, not on the behaviour
// (tests/test_crash_record.cpp and tests/status/test_status.cpp hold that).
// c++ -std=c++11 -Wall -Wextra -Werror -g tests/test_crash_wiring.cpp -o /tmp/smalltv-crash-wiring && /tmp/smalltv-crash-wiring
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::string slurp(const char* path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
// The text from the first `start` to the next "\n}\n" (the end of a function).
static std::string body(const std::string& src, const std::string& start) {
  size_t a = src.find(start);
  if (a == std::string::npos) return "";
  size_t b = src.find("\n}\n", a);
  return src.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
static size_t at(const std::string& s, const std::string& needle, size_t from = 0) { return s.find(needle, from); }

int main() {
  const std::string plat = slurp("src/Platform.cpp");
  const std::string web = slurp("src/WebPortal.cpp");
  CHECK(!plat.empty() && !web.empty());

  // The build-time guard on the record's place is the real limit, 128 words
  // (512 bytes of RTC user memory), and keeps the OTA words (0..31) clear.
  CHECK(at(plat, "sizeof(CrashRtc) / 4 + CRASH_RTC_WORD <= 128") != std::string::npos);
  CHECK(at(plat, "CRASH_RTC_WORD >= 32") != std::string::npos);
  CHECK(at(plat, "<= 112") == std::string::npos);

  // The callback: head (trace, failed allocation, lows), first write, then the
  // heap walk, then the second write. The walk must come after the first write.
  const std::string cb = body(plat, "extern \"C\" void custom_crash_callback(");
  CHECK(!cb.empty());
  const size_t trace = at(cb, "crashCollectTrace(");
  const size_t head = at(cb, "crashFillHead(");
  const size_t store = at(cb, "crashStore(r)");
  const size_t walk = at(cb, "ESP.getMaxFreeBlockSize()");
  const size_t seal = at(cb, "crashSealBlock(");
  const size_t write2 = at(cb, "ESP.rtcUserMemoryWrite(", store == std::string::npos ? 0 : store);
  CHECK(trace != std::string::npos && head != std::string::npos && store != std::string::npos);
  CHECK(walk != std::string::npos && seal != std::string::npos && write2 != std::string::npos);
  CHECK(trace < head && head < store);
  CHECK(store < walk);                 // the walk can hang: the first write is already in RTC memory
  CHECK(walk < write2 && seal < write2);   // the second write comes last, with the block in it
  CHECK(at(cb, "getMaxFreeBlockSize", walk + 10) == std::string::npos);   // one walk only
  CHECK(at(cb, "umm_last_fail_alloc_size") != std::string::npos && at(cb, "umm_last_fail_alloc_addr") != std::string::npos);
  CHECK(at(cb, "s_lows.heap()") != std::string::npos && at(cb, "s_lows.block()") != std::string::npos);
  CHECK(at(cb, "crashMoreAfter(") != std::string::npos);
  CHECK(at(cb, "crashLoad(r)") != std::string::npos && at(cb, "crashLoad(r)") < at(cb, "memset(&r"));   // old count is read before the wipe
  // Nothing in the callback may allocate.
  CHECK(at(cb, "String") == std::string::npos && at(cb, "new ") == std::string::npos && at(cb, "malloc") == std::string::npos);

  // The sampler runs from the boot call; the lows restart at boot and through
  // the platform call (which the clear handler makes, below).
  const std::string begin = body(plat, "void platformCrashRecordBegin() {\n");   // not the ESP32 stub, which has {} on one line
  CHECK(at(begin, "os_timer_arm(&s_lowsTimer, 100, true)") != std::string::npos);
  CHECK(at(begin, "os_timer_arm(") < at(begin, "ESP.getResetInfoPtr()"));   // before any early return
  CHECK(at(begin, "crashAdoptFlags(") != std::string::npos);
  CHECK(at(begin, "CRASH_DETAIL") == std::string::npos);   // a record made from rst_info measured nothing
  CHECK(at(begin, "crashMoreAfter(") != std::string::npos);
  CHECK(at(plat, "void platformCrashLowsReset() { s_lows.reset(); }") != std::string::npos);
  CHECK(at(plat, "void platformCrashLowsReset() {}") != std::string::npos);   // the ESP32 targets: a no-op

  // The reader passes every measured value on.
  const std::string get = body(plat, "bool platformCrashRecordGet(PlatformCrashRecord& out) {\n");
  const char* outs[] = {"out.hasDetail   = (r.flags & CRASH_DETAIL)", "out.heap     = r.heapBlock", "out.hasBlock = crashBlockKnown",
                        "out.maxBlock = r.heapBlock", "out.failSize = r.failSize", "out.failAddr = r.failAddr",
                        "out.hasLows  = r.low", "out.lowHeap  = r.low", "out.lowBlk   = r.low",
                        "out.trace[i] = r.trace[i]", "out.more = r.more"};
  for (const char* o : outs) CHECK(at(get, o) != std::string::npos);
  CHECK(at(get, "crashBlockKnown(") != std::string::npos);

  // The status handler: every field to the writer.
  const std::string st = body(web, "static void handleStatus(");
  CHECK(!st.empty());
  // One record, no second copy of it (the copy cost 224 B of the 4 KB stack,
  // TRAPS T12): the platform's snapshot goes to the writer as it is, which
  // writes the register hex digit by digit (no printf buffers either).
  CHECK(at(st, "PlatformCrashRecord cr") != std::string::npos);
  CHECK(at(st, "platformCrashRecordGet(cr)") != std::string::npos);
  CHECK(at(st, "smalltv::writeStatusCrash(out, cr)") != std::string::npos);
  CHECK(at(st, "StatusCrash ") == std::string::npos);   // the old second struct (not writeStatusCrash)
  CHECK(at(st, "k.hasDetail") == std::string::npos && at(st, "k.trace") == std::string::npos &&
        at(st, "cr.") == std::string::npos);   // no field is copied
  CHECK(at(st, "snprintf") == std::string::npos && at(st, "char cause") == std::string::npos);
  CHECK(at(st, "noinline") == std::string::npos);   // one function: a split makes the stack worse (T13)
  const std::string plath = slurp("src/Platform.h");
  CHECK(at(plath, "typedef CrashSnapshot PlatformCrashRecord;") != std::string::npos);
  CHECK(at(plath, "#include \"CrashRecord.h\"") != std::string::npos);
  const std::string statush = slurp("src/StatusJson.h");
  CHECK(at(statush, "struct StatusCrash") == std::string::npos);
  CHECK(at(statush, "writeStatusCrash(Out& out, const CrashSnapshot& k)") != std::string::npos);
  CHECK(at(statush, "snprintf") == std::string::npos);
  // The lows restart at boot and on a crash clear, never on a status read
  // (review of #137, point 2): a read of the status page would otherwise
  // cut the window a later crash record covers to the time since that read.
  CHECK(at(st, "platformCrashLowsReset") == std::string::npos);
  const std::string clr = body(web, "static void handleCrashClear() {");
  CHECK(!clr.empty());
  CHECK(at(clr, "platformCrashRecordClear()") != std::string::npos);
  CHECK(at(clr, "platformCrashLowsReset()") != std::string::npos);
  CHECK(at(begin, "s_lows.reset()") != std::string::npos);   // and at boot
  size_t resets = 0;   // the web code calls it in one place only
  for (size_t p = at(web, "platformCrashLowsReset"); p != std::string::npos; p = at(web, "platformCrashLowsReset", p + 1)) resets++;
  CHECK(resets == 1);

  if (failures) { std::printf("%d wiring check(s) failed\n", failures); return 1; }
  std::printf("crash record wiring tests passed\n");
  return 0;
}
