// The backlight is driven by the hardware sigma-delta on the ESP8266 and by
// analogWrite on the ESP32 family, only through one function in Gfx.cpp, and
// nothing else in the tree touches the pin. The ESP8266 part cannot run here,
// so this reads the sources: a check on the wiring, not on the behaviour
// (tests/backlight/test_backlight.cpp holds the behaviour).
// c++ -std=c++11 -Wall -Wextra -Werror -g tests/backlight/test_wiring.cpp -o /tmp/smalltv-backlight-wiring
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::string slurp(const std::string& path) {
  std::ifstream f(path.c_str());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
static int count(const std::string& hay, const std::string& needle) {
  int n = 0;
  for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) ++n;
  return n;
}
static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r");
  return s.substr(a, b - a + 1);
}
// The preprocessor conditions open at byte offset `pos`, outermost first. An
// #else turns the open block into "#else of <condition>".
static std::vector<std::string> openAt(const std::string& src, size_t pos) {
  std::vector<std::string> open;
  size_t line = 0;
  while (line < pos && line < src.size()) {
    size_t eol = src.find('\n', line);
    if (eol == std::string::npos) eol = src.size();
    const std::string t = trim(src.substr(line, eol - line));
    if (t.compare(0, 3, "#if") == 0) open.push_back(t);
    else if (t.compare(0, 5, "#else") == 0) { if (!open.empty() && open.back().compare(0, 5, "#else") != 0) open.back() = "#else of " + open.back(); }
    else if (t.compare(0, 6, "#endif") == 0) { if (!open.empty()) open.pop_back(); }
    line = eol + 1;
  }
  return open;
}
// Is `pos` in the ESP32 branch, or in the ESP8266 (#else) branch, of an
// `#if defined(SMALLTV_FAMILY_ESP32)` block?
static bool inEsp32Branch(const std::string& src, size_t pos) {
  for (const std::string& c : openAt(src, pos))
    if (c == "#if defined(SMALLTV_FAMILY_ESP32)") return true;
  return false;
}
static bool inEsp8266Branch(const std::string& src, size_t pos) {
  for (const std::string& c : openAt(src, pos))
    if (c == "#else of #if defined(SMALLTV_FAMILY_ESP32)") return true;
  return false;
}
static size_t only(const std::string& src, const std::string& needle) {
  if (count(src, needle) != 1) { std::printf("FAIL: expected exactly one of: %s\n", needle.c_str()); failures++; return std::string::npos; }
  return src.find(needle);
}
static std::vector<std::string> sourceFiles() {
  std::vector<std::string> out;
  FILE* p = popen("find src -type f \\( -name '*.cpp' -o -name '*.h' -o -name '*.ino' -o -name '*.c' \\)", "r");
  if (!p) return out;
  char buf[512];
  while (fgets(buf, sizeof buf, p)) {
    std::string line = buf;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (!line.empty()) out.push_back(line);
  }
  pclose(p);
  return out;
}
// True when `name` is followed by a character that is not part of an identifier
// (so TFT_BL matches TFT_BL, and TFT_BL) but not TFT_BL_DEFAULT_INVERTED).
static bool mentionsPin(const std::string& src) {
  const std::string name = "TFT_BL";
  for (size_t p = src.find(name); p != std::string::npos; p = src.find(name, p + 1)) {
    const char c = p + name.size() < src.size() ? src[p + name.size()] : ' ';
    const bool ident = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    if (!ident) return true;
  }
  return false;
}

int main() {
  const std::string gfx = slurp("src/Gfx.cpp");
  const std::string plat = slurp("src/Platform.h");
  CHECK(!gfx.empty());
  CHECK(!plat.empty());

  // ---- the whole tree: one place drives the pin ----
  const std::vector<std::string> files = sourceFiles();
  CHECK(files.size() > 20);
  for (const std::string& f : files) {
    const std::string s = slurp(f);
    CHECK(!s.empty());   // a path that cannot be read would hide a file from the scan
    const bool board = f.find("src/board_") == 0 || f == "src/config.h";
    const bool isGfx = f == "src/Gfx.cpp";
    if (!board && !isGfx && mentionsPin(s)) { std::printf("FAIL: %s uses TFT_BL\n", f.c_str()); failures++; }
    if (!isGfx && s.find("analogWrite(") != std::string::npos) { std::printf("FAIL: %s calls analogWrite\n", f.c_str()); failures++; }
    if (!isGfx && f != "src/BacklightPlan.h" && s.find("sigmaDelta") != std::string::npos) { std::printf("FAIL: %s uses sigmaDelta\n", f.c_str()); failures++; }
    if (!isGfx && (s.find("tone(") != std::string::npos || s.find("startWaveform") != std::string::npos ||
                   s.find("analogWriteFreq") != std::string::npos))
      { std::printf("FAIL: %s uses another generator\n", f.c_str()); failures++; }
  }
  // Every caller of the level goes through gfxSetBrightness / ...Steps.
  CHECK(count(slurp("src/main.cpp"), "gfxSetBrightness") >= 2);

  // ---- Gfx.cpp: the ESP32 branch keeps analogWrite, the ESP8266 branch has the sigma-delta ----
  const size_t aw = only(gfx, "analogWrite(TFT_BL, inverted ? 255 - steps : steps);");
  CHECK(aw != std::string::npos && inEsp32Branch(gfx, aw));
  CHECK(count(gfx, "analogWrite(") == 1);
  CHECK(gfx.find("analogWriteRange") == std::string::npos);
  CHECK(gfx.find("analogWriteFreq") == std::string::npos);

  const char* sd[] = {"sigmaDeltaEnable();", "sigmaDeltaSetPrescaler(smalltv::BL_SD_PRESCALER);",
                      "sigmaDeltaWrite(0, plan.duty);",
                      "if (!sigmaDeltaIsPinAttached(TFT_BL)) sigmaDeltaAttachPin(TFT_BL);",
                      "sigmaDeltaDetachPin(TFT_BL);"};
  size_t at[5];
  for (int i = 0; i < 5; i++) {
    at[i] = only(gfx, sd[i]);
    CHECK(at[i] != std::string::npos && inEsp8266Branch(gfx, at[i]));
  }
  // The duty is written before the pin is attached (no burst of the old duty),
  // and the pin is detached before the fixed level is written (never write the
  // pin while it is attached).
  CHECK(at[2] < at[3]);
  const size_t dw = only(gfx, "digitalWrite(TFT_BL, plan.level ? HIGH : LOW);");
  CHECK(dw != std::string::npos && inEsp8266Branch(gfx, dw));
  CHECK(count(gfx, "digitalWrite(TFT_BL") == 1);
  CHECK(at[4] < dw);
  CHECK(gfx.find("sigmaDeltaDetachPin(TFT_BL);\n    digitalWrite(TFT_BL, plan.level ? HIGH : LOW);") != std::string::npos);
  // The duty write is in the sigma-delta branch of the plan, the fixed write in the other.
  CHECK(gfx.find("if (plan.sigmaDelta) {\n    sigmaDeltaWrite(0, plan.duty);") != std::string::npos);
  CHECK(gfx.find("  } else {\n    sigmaDeltaDetachPin(TFT_BL);") != std::string::npos);
  // Nothing happens when the plan did not change.
  CHECK(gfx.find("if (s_planValid && smalltv::backlightPlanSame(plan, s_plan)) return;") != std::string::npos);
  CHECK(gfx.find("s_plan = plan;\n  s_planValid = true;") != std::string::npos);
  CHECK(gfx.find("const smalltv::BacklightPlan plan = smalltv::backlightPlan(steps, inverted);") != std::string::npos);
  CHECK(gfx.find("#include \"BacklightPlan.h\"") != std::string::npos);

  // The set-up runs once, in gfxBegin, before the first level is written.
  const size_t begin = only(gfx, "void gfxBegin(const Settings& s) {");
  const size_t init = gfx.find("  backlightBegin();\n  gfxSetBrightness(s.brightness, s.backlightInverted);");
  CHECK(init != std::string::npos && init > begin);
  CHECK(count(gfx, "backlightBegin()") == 3);   // two definitions and the call
  CHECK(count(gfx, "platformAnalogWriteInit(TFT_BL)") == 1);   // the ESP32 branch's
  const size_t paw = gfx.find("platformAnalogWriteInit(TFT_BL)");
  CHECK(paw != std::string::npos && inEsp32Branch(gfx, paw));
  CHECK(gfx.find("void gfxSetBrightnessSteps(uint8_t steps, bool inverted) {\n  backlightSet(steps, inverted);\n}") != std::string::npos);

  // ---- Platform.h: the ESP8266 branch no longer sets up analogWrite ----
  CHECK(plat.find("analogWriteRange") == std::string::npos);
  CHECK(plat.find("analogWrite(") == std::string::npos);

  if (failures) { std::printf("%d backlight wiring checks failed\n", failures); return 1; }
  std::printf("backlight wiring tests passed\n");
  return 0;
}
