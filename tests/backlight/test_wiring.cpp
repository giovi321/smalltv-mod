// The ESP8266 backlight function in src/Gfx.cpp cannot run on this machine, so
// this reads its text: the order of its hardware calls, which the plan test
// (tests/backlight/test_backlight.cpp) cannot see. Only that one function is
// read. Comments and all white space are removed first, so a reformat, a
// comment, or a Windows checkout (CRLF) changes nothing. Nothing else in the
// tree is searched.
// c++ -std=c++11 -Wall -Wextra -Werror -g tests/backlight/test_wiring.cpp -o /tmp/smalltv-backlight-wiring
// Run it from the repository root; an optional argument names another copy of Gfx.cpp.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// The source without `//` comments and without any white space.
static std::string squeezed(const std::string& path) {
  std::ifstream f(path.c_str());
  std::string line, out;
  while (std::getline(f, line)) {
    const size_t comment = line.find("//");
    if (comment != std::string::npos) line.erase(comment);
    for (size_t i = 0; i < line.size(); ++i)
      if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r') out += line[i];
  }
  return out;
}
static int count(const std::string& hay, const std::string& needle) {
  int n = 0;
  for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) ++n;
  return n;
}
// The braces of the function that holds `anchor`: from its `{` to the matching `}`.
static std::string functionAround(const std::string& src, const std::string& anchor) {
  const size_t at = src.find(anchor);
  if (at == std::string::npos) return "";
  const size_t open = src.rfind('{', at);
  if (open == std::string::npos) return "";
  int depth = 0;
  for (size_t i = open; i < src.size(); ++i) {
    if (src[i] == '{') ++depth;
    else if (src[i] == '}' && --depth == 0) return src.substr(open, i - open + 1);
  }
  return "";
}

int main(int argc, char** argv) {
  const std::string src = squeezed(argc > 1 ? argv[1] : "src/Gfx.cpp");
  CHECK(!src.empty());
  // The ESP8266 branch is the one that asks the plan; the ESP32 branch does not.
  const std::string body = functionAround(src, "smalltv::backlightPlan(steps,inverted)");
  CHECK(!body.empty());

  // Nothing happens when the plan did not change.
  const size_t same = body.find("if(s_planValid&&smalltv::backlightPlanSame(plan,s_plan))return;");
  // A sigma-delta plan writes the duty first, so the pin never carries the old
  // duty, and attaches the pin only if it is not attached yet.
  const size_t duty = body.find("if(plan.sigmaDelta){sigmaDeltaWrite(0,plan.duty);");
  const size_t attach = body.find("if(!sigmaDeltaIsPinAttached(TFT_BL))sigmaDeltaAttachPin(TFT_BL);");
  // A fixed plan detaches the pin from the generator, then writes the level:
  // the pin is never written while it is attached.
  const size_t detach = body.find("}else{sigmaDeltaDetachPin(TFT_BL);");
  const size_t fixedWrite = body.find("digitalWrite(TFT_BL,plan.level?HIGH:LOW);");
  CHECK(same != std::string::npos);
  CHECK(duty != std::string::npos && attach != std::string::npos);
  CHECK(detach != std::string::npos && fixedWrite != std::string::npos);
  CHECK(same < duty && duty < attach && attach < detach && detach < fixedWrite);
  // One of each: no second write of the pin, no second attach.
  CHECK(count(body, "sigmaDeltaWrite(") == 1);
  CHECK(count(body, "sigmaDeltaAttachPin(") == 1);
  CHECK(count(body, "sigmaDeltaDetachPin(") == 1);
  CHECK(count(body, "digitalWrite(") == 1);
  // The plan is remembered after it was applied.
  CHECK(body.find("s_plan=plan;s_planValid=true;") != std::string::npos);

  if (failures) { std::printf("%d backlight wiring checks failed\n", failures); return 1; }
  std::printf("backlight wiring tests passed\n");
  return 0;
}
