// The web password: a stored password is what locks the page. There is no
// separate "enabled" switch to forget. Tests cover the three ways settings
// reach the parser: the stored file at boot, a (partial) save from the page,
// and an import. All three use settingsApplyJson(), so the rules are the same.
// Built with the stand-in core headers of the fuzz target (see tests/auth/run.sh).
// All passwords here are made up.
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "Settings.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

// Stand-ins the real Settings.cpp links against (as in tests/sun/test_settings.cpp).
FSClass LittleFS;
EspClass ESP;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond, ...)                                                   \
  do {                                                                     \
    g_checks++;                                                            \
    if (!(cond)) {                                                         \
      g_failed++;                                                          \
      printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);               \
      printf(__VA_ARGS__);                                                 \
      printf("\n");                                                        \
    }                                                                      \
  } while (0)

static void apply(Settings& s, const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) { printf("bad test json: %s\n", json); g_failed++; return; }
  settingsApplyJson(s, doc.as<JsonObjectConst>());
}

// The file the firmware would write for these settings, as text.
static String fileText(const Settings& s) {
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>(), /*includeSecrets=*/true);
  String out;
  serializeJson(doc, out);
  return out;
}

// What /api/config says: no secrets.
static void configReply(const Settings& s, JsonDocument& doc) {
  settingsToJson(s, doc.to<JsonObject>(), /*includeSecrets=*/false);
}

static bool locked(const Settings& s) { return s.auth.pass.length() > 0; }

// ---- stored settings (loadSettings() = defaults, then settingsApplyJson) ----

static void testDefaults() {
  Settings s;
  s.setDefaults();
  CHECK(!locked(s), "a new device is open");
  CHECK(s.auth.user == "admin", "default user");
}

static void testStoredLocked() {
  // Stored enabled true + password: stays locked, keeps the password.
  Settings s;
  s.setDefaults();
  apply(s, "{\"auth\":{\"enabled\":true,\"user\":\"owner\",\"pass\":\"test-pass-01\"}}");
  CHECK(locked(s), "locked");
  CHECK(s.auth.pass == "test-pass-01", "password kept: %s", s.auth.pass.c_str());
  CHECK(s.auth.user == "owner", "user kept");
}

static void testStoredNeverInForce() {
  // Stored enabled false + password (the old page let you save a password with
  // the box unticked): the password was never in force. It must not start
  // locking after an update. It is dropped.
  Settings s;
  s.setDefaults();
  apply(s, "{\"auth\":{\"enabled\":false,\"user\":\"owner\",\"pass\":\"test-pass-02\"}}");
  CHECK(!locked(s), "open after load");
  CHECK(s.auth.pass.length() == 0, "password dropped: '%s'", s.auth.pass.c_str());
  CHECK(s.auth.user == "owner", "user kept");

  // Not a half state: the next save writes no password, and reading that file
  // back gives the same open device. The file does not say "locked" anywhere.
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>(), true);
  CHECK(doc["auth"]["pass"].as<String>() == "", "saved password is empty");
  CHECK(doc["auth"]["enabled"].as<bool>() == false, "saved enabled is false");
  CHECK(doc["auth"]["passSet"].as<bool>() == false, "saved passSet is false");
  Settings t;
  t.setDefaults();
  String text = fileText(s);
  JsonDocument back;
  CHECK(!deserializeJson(back, text), "saved file parses");
  settingsApplyJson(t, back.as<JsonObjectConst>());
  CHECK(!locked(t), "still open after the next boot");
}

static void testStoredNoEnabledKey() {
  // A file with a password and no "enabled" key. No file the firmware wrote
  // looks like this (toJson always writes enabled), so it was edited by hand,
  // and whoever wrote a password into it wants it. A password counts. The
  // owner knows it. The other reading would silently open a locked device.
  Settings s;
  s.setDefaults();
  apply(s, "{\"auth\":{\"user\":\"owner\",\"pass\":\"test-pass-03\"}}");
  CHECK(locked(s), "a password without 'enabled' locks");
  CHECK(s.auth.pass == "test-pass-03", "password kept");
  // No auth object at all (a config from before the password existed): open.
  Settings t;
  t.setDefaults();
  apply(t, "{\"hostname\":\"smalltv-test\",\"mode\":\"stocks\"}");
  CHECK(!locked(t), "no auth object: open");
  // Only enabled true, no password: nothing to check against, stays open.
  Settings u;
  u.setDefaults();
  apply(u, "{\"auth\":{\"enabled\":true}}");
  CHECK(!locked(u), "enabled with no password stays open");
}

static void testFileWritten() {
  // The file written after this change: enabled mirrors the password, so an
  // older firmware (which reads enabled) still treats a locked device as locked.
  Settings s;
  s.setDefaults();
  apply(s, "{\"auth\":{\"pass\":\"test-pass-04\"}}");
  JsonDocument file;
  settingsToJson(s, file.to<JsonObject>(), true);
  CHECK(file["auth"]["enabled"].as<bool>() == true, "password set: file says enabled true");
  CHECK(file["auth"]["pass"].as<String>() == "test-pass-04", "file holds the password");
  CHECK(file["auth"]["passSet"].as<bool>() == true, "file passSet");
  // Round trip through the text of the file.
  Settings t;
  t.setDefaults();
  String text = fileText(s);
  JsonDocument back;
  CHECK(!deserializeJson(back, text), "parses");
  settingsApplyJson(t, back.as<JsonObjectConst>());
  CHECK(locked(t) && t.auth.pass == "test-pass-04", "round trip stays locked");
  // No password: enabled false.
  Settings o;
  o.setDefaults();
  JsonDocument openFile;
  settingsToJson(o, openFile.to<JsonObject>(), true);
  CHECK(openFile["auth"]["enabled"].as<bool>() == false, "no password: enabled false");
}

static void testConfigReply() {
  // /api/config: no password in it; passSet says whether there is one, and the
  // old "enabled" key stays for old pages, equal to passSet.
  Settings s;
  s.setDefaults();
  JsonDocument openReply;
  configReply(s, openReply);
  CHECK(openReply["auth"]["passSet"].as<bool>() == false, "open: passSet false");
  CHECK(openReply["auth"]["enabled"].as<bool>() == false, "open: enabled false");
  CHECK(!openReply["auth"]["pass"].is<const char*>(), "open: no pass key");
  apply(s, "{\"auth\":{\"pass\":\"test-pass-05\"}}");
  JsonDocument lockedReply;
  configReply(s, lockedReply);
  CHECK(lockedReply["auth"]["passSet"].as<bool>() == true, "locked: passSet true");
  CHECK(lockedReply["auth"]["enabled"].as<bool>() == true, "locked: enabled equals passSet");
  CHECK(!lockedReply["auth"]["pass"].is<const char*>(), "locked: password not in the reply");
}

// ---- saves from the page (settingsApplyJson on live settings) --------------

static void testSetPassword() {
  Settings s;
  s.setDefaults();
  // The new page sends no "enabled": a typed password locks the device.
  apply(s, "{\"auth\":{\"user\":\"admin\",\"pass\":\"test-pass-06\"}}");
  CHECK(locked(s) && s.auth.pass == "test-pass-06", "password set locks");
  // Blank password keeps the stored one.
  apply(s, "{\"auth\":{\"user\":\"admin\",\"pass\":\"\"}}");
  CHECK(s.auth.pass == "test-pass-06", "blank keeps the password");
  // A save without a pass key keeps it too.
  apply(s, "{\"auth\":{\"user\":\"someone\"}}");
  CHECK(s.auth.pass == "test-pass-06" && s.auth.user == "someone", "no pass key keeps it");
  // A new password replaces the old one.
  apply(s, "{\"auth\":{\"pass\":\"test-pass-07\"}}");
  CHECK(s.auth.pass == "test-pass-07", "replaced");
  // A save with no auth object at all leaves it alone.
  apply(s, "{\"hostname\":\"smalltv-test\"}");
  CHECK(locked(s) && s.auth.pass == "test-pass-07", "other saves leave it");
}

static void testOldPageSaves() {
  // A cached old page or an old script posts partial auth objects. None of
  // them may remove the password or unlock the device.
  Settings s;
  s.setDefaults();
  apply(s, "{\"auth\":{\"pass\":\"test-pass-08\"}}");
  apply(s, "{\"auth\":{\"enabled\":false}}");
  CHECK(locked(s) && s.auth.pass == "test-pass-08", "enabled false alone does not unlock");
  apply(s, "{\"auth\":{\"enabled\":false,\"user\":\"admin\"}}");
  CHECK(locked(s) && s.auth.pass == "test-pass-08", "enabled false + user does not unlock");
  // The old page with its box unticked and a password typed: that password was
  // 'not in force' for the old page. It is not taken; the stored one stays.
  apply(s, "{\"auth\":{\"enabled\":false,\"pass\":\"test-pass-09\"}}");
  CHECK(s.auth.pass == "test-pass-08", "enabled false + new pass: stored one stays: %s", s.auth.pass.c_str());
  // The old page with its box ticked and a password typed: sets it.
  apply(s, "{\"auth\":{\"enabled\":true,\"pass\":\"test-pass-10\"}}");
  CHECK(s.auth.pass == "test-pass-10", "enabled true + pass sets it");
  // enabled true without a password on an open device does not lock it.
  Settings o;
  o.setDefaults();
  apply(o, "{\"auth\":{\"enabled\":true}}");
  CHECK(!locked(o), "enabled true alone does not lock an open device");
  // Wrong types are ignored.
  apply(s, "{\"auth\":{\"enabled\":\"no\",\"pass\":5}}");
  CHECK(s.auth.pass == "test-pass-10", "wrong types ignored");
}

static void testRemove() {
  Settings s;
  s.setDefaults();
  apply(s, "{\"auth\":{\"user\":\"owner\",\"pass\":\"test-pass-11\"}}");
  apply(s, "{\"auth\":{\"removePass\":true}}");
  CHECK(!locked(s), "removePass opens the device");
  CHECK(s.auth.user == "owner", "user kept");
  JsonDocument file;
  settingsToJson(s, file.to<JsonObject>(), true);
  CHECK(file["auth"]["pass"].as<String>() == "" && file["auth"]["enabled"].as<bool>() == false,
        "saved file is open");
  JsonDocument reply;
  configReply(s, reply);
  CHECK(reply["auth"]["passSet"].as<bool>() == false, "reply says open");
  // removePass false (or a wrong type) does nothing.
  apply(s, "{\"auth\":{\"pass\":\"test-pass-12\"}}");
  apply(s, "{\"auth\":{\"removePass\":false}}");
  CHECK(locked(s), "removePass false does nothing");
  apply(s, "{\"auth\":{\"removePass\":\"yes\"}}");
  CHECK(locked(s), "removePass of the wrong type does nothing");
  apply(s, "{\"auth\":{\"removePass\":1}}");
  CHECK(locked(s), "removePass 1 does nothing");
  // removePass and a new password together: the device stays locked, with the new one.
  apply(s, "{\"auth\":{\"removePass\":true,\"pass\":\"test-pass-13\"}}");
  CHECK(locked(s) && s.auth.pass == "test-pass-13", "both together: the new password wins");
  // Removing on an open device is harmless.
  Settings o;
  o.setDefaults();
  apply(o, "{\"auth\":{\"removePass\":true}}");
  CHECK(!locked(o), "open stays open");
}

// ---- import (same parser, on live settings) ---------------------------------

static void testImport() {
  // An older export from a device that was locked: enabled true + password.
  // Importing it locks at once. That is expected: the device was locked.
  Settings s;
  s.setDefaults();
  apply(s, "{\"hostname\":\"smalltv-test\",\"auth\":{\"enabled\":true,\"user\":\"admin\",\"pass\":\"test-pass-14\"}}");
  CHECK(locked(s) && s.auth.pass == "test-pass-14", "import of a locked export locks");

  // An older export with a password that was never in force: dropped.
  Settings t;
  t.setDefaults();
  apply(t, "{\"hostname\":\"smalltv-test\",\"auth\":{\"enabled\":false,\"user\":\"admin\",\"pass\":\"test-pass-15\"}}");
  CHECK(!locked(t), "import: password that was never in force is dropped");

  // The same import onto a device that is locked now: the lock stays as it is.
  // An import never opens a device; only the Remove request does.
  Settings u;
  u.setDefaults();
  apply(u, "{\"auth\":{\"pass\":\"test-pass-16\"}}");
  apply(u, "{\"hostname\":\"smalltv-test\",\"auth\":{\"enabled\":false,\"user\":\"admin\",\"pass\":\"test-pass-15\"}}");
  CHECK(locked(u) && u.auth.pass == "test-pass-16", "import of an old open export does not unlock or change the lock");
  // An export from an open device (empty password) onto a locked one: same.
  apply(u, "{\"auth\":{\"enabled\":false,\"user\":\"admin\",\"pass\":\"\"}}");
  CHECK(locked(u) && u.auth.pass == "test-pass-16", "import of an open export leaves the lock");
  // An export without an auth object leaves it alone.
  apply(u, "{\"hostname\":\"other-test\"}");
  CHECK(locked(u), "import without auth leaves the lock");
}

static void testLimits() {
  Settings s;
  s.setDefaults();
  char longPass[200];
  memset(longPass, 'x', sizeof(longPass) - 1);
  longPass[sizeof(longPass) - 1] = 0;
  String json = String("{\"auth\":{\"user\":\"\",\"pass\":\"") + longPass + "\"}}";
  apply(s, json.c_str());
  CHECK(s.auth.pass.length() == 63, "password cut to the limit: %d", (int)s.auth.pass.length());
  CHECK(s.auth.user == "admin", "empty user falls back to admin");
}

int main() {
  testDefaults();
  testStoredLocked();
  testStoredNeverInForce();
  testStoredNoEnabledKey();
  testFileWritten();
  testConfigReply();
  testSetPassword();
  testOldPageSaves();
  testRemove();
  testImport();
  testLimits();
  printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
