// Build options for forks (src/BuildInfo.h):
//   -D FW_VERSION_SUFFIX=\"-fork\"   the label shown to people gets a suffix
//   -D FW_NO_GITHUB_UPDATE           the GitHub self-update is left out
// Built twice by tests/buildflags/run.sh: with no flag (every normal image),
// and with both flags (-DEXPECT_FORK).
//
// FW_VERSION (src/config.h) is never changed: it is the release number, the
// release workflow edits that one line, and the update check compares it.
// The label with the suffix is FW_VERSION_FULL, for display only.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <ArduinoJson.h>

#define FW_VERSION "9.8.7"   // made up: the test does not follow the real release
#include "BuildInfo.h"

static JsonDocument parse(const char* text) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, text);
  assert(!err);
  return doc;
}

int main() {
  // The release number is untouched in both builds.
  assert(std::strcmp(FW_VERSION, "9.8.7") == 0);
#ifdef EXPECT_FORK
  assert(std::strcmp(FW_VERSION_FULL, "9.8.7-fork") == 0);
  assert(FW_GITHUB_UPDATE == 0);
#else
  // No flag: the label is the release number and the update stays on, so
  // every other image builds as before.
  assert(std::strcmp(FW_VERSION_FULL, "9.8.7") == 0);
  assert(FW_GITHUB_UPDATE == 1);
#endif
  // The label starts with the release number, so a number read from it is
  // the same one.
  int a = 0, b = 0, c = 0;
  assert(std::sscanf(FW_VERSION_FULL, "%d.%d.%d", &a, &b, &c) == 3 && a == 9 && b == 8 && c == 7);
  // The label joins with other string literals, like the firmware's own
  // "... (" FW_VERSION_FULL ")".
  assert(std::strcmp("v " FW_VERSION_FULL, std::string("v " FW_VERSION_FULL).c_str()) == 0);

  // What the web UI is told when it asks about updates in a build that has
  // them off. The current version in it is the plain number.
  assert(std::strcmp(FW_UPDATE_OFF_MSG, "updates are off in this build") == 0);
  JsonDocument check = parse(FW_UPDATE_OFF_CHECK_REPLY);
  assert(check["ok"].is<bool>() && check["ok"].as<bool>() == false);
  assert(check["disabled"].is<bool>() && check["disabled"].as<bool>() == true);
  assert(check["newer"].is<bool>() && check["newer"].as<bool>() == false);
  assert(std::strcmp(check["error"] | "", FW_UPDATE_OFF_MSG) == 0);
  assert(std::strcmp(check["current"] | "", FW_VERSION) == 0);
  assert(std::strcmp(check["latest"] | "x", "") == 0);
  JsonDocument self = parse(FW_UPDATE_OFF_SELF_REPLY);
  assert(self["ok"].is<bool>() && self["ok"].as<bool>() == false);
  assert(std::strcmp(self["error"] | "", FW_UPDATE_OFF_MSG) == 0);
  std::printf("build info tests passed (%s, update %s)\n", FW_VERSION_FULL, FW_GITHUB_UPDATE ? "on" : "off");
  return 0;
}
