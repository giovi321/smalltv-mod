// c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc -I<ArduinoJson include dir>
// tests/test_storage.cpp -o /tmp/smalltv-storage-tests && /tmp/smalltv-storage-tests
#include "AtomicJson.h"
#include "SettingsTransaction.h"
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <iostream>

// Minimal in-memory stand-in for LittleFS: enough to exercise a short write
// (a byte budget that runs out mid-write), a failed rename, and a clean
// atomic save, without touching a real filesystem.
struct FakeFS {
  std::map<std::string, std::string> files;
  size_t budget = 10000;
  bool renameFails = false;
  struct Handle {
    FakeFS* fs;
    std::string path;
    bool failed;
  };
  struct File {
    std::shared_ptr<Handle> h;
    explicit operator bool() const { return bool(h); }
    size_t write(const uint8_t* bytes, size_t n) {
      size_t count = std::min(n, h->fs->budget);
      h->fs->budget -= count;
      h->fs->files[h->path].append(reinterpret_cast<const char*>(bytes), count);
      if (count != n) h->failed = true;
      return count;
    }
    size_t write(uint8_t byte) { return write(&byte, 1); }
    void flush() {}
    size_t size() const { return h->fs->files[h->path].size(); }
    bool getWriteError() const { return h->failed; }
    void close() {}
  };
  File open(const char* path, const char*) {
    files[path] = "";
    return File{std::make_shared<Handle>(Handle{this, path, false})};
  }
  bool rename(const char* source, const char* target) {
    if (renameFails) return false;
    files[target] = files[source];
    files.erase(source);
    return true;
  }
  bool remove(const char* path) { return files.erase(path); }
};
int main() {
  JsonDocument config;
  config["wifi"] = "working-network";
  config["hostname"] = "smalltv-1234";
  FakeFS fs;
  fs.files["/config.json"] = "old-settings";
  // A budget too small to hold the full write: saveJsonAtomically must fail
  // without ever renaming the short write over the working config.
  fs.budget = 5;
  assert(!saveJsonAtomically(fs, config, "/config.json", "/config.json.tmp"));
  assert(fs.files["/config.json"] == "old-settings");
  assert(!fs.files.count("/config.json.tmp"));
  // A complete, correct write whose rename still fails (e.g. no space for
  // the directory entry): the old config must survive untouched here too.
  fs.budget = 10000;
  fs.renameFails = true;
  assert(!saveJsonAtomically(fs, config, "/config.json", "/config.json.tmp"));
  assert(fs.files["/config.json"] == "old-settings");
  fs.renameFails = false;
  // The success path: the temporary becomes the target and is not left behind.
  assert(saveJsonAtomically(fs, config, "/config.json", "/config.json.tmp"));
  JsonDocument saved;
  assert(!deserializeJson(saved, fs.files["/config.json"]));
  assert(saved["hostname"] == "smalltv-1234");
  assert(!fs.files.count("/config.json.tmp"));

  struct State {
    int value;
  } state{1};
  bool savedState = false;
  // A failed save must roll the in-memory mutation back to its prior value.
  assert(!applyAndSaveSettings(state, [](State& s) { s.value = 2; }, [&](const State&) { return savedState; }));
  assert(state.value == 1);
  savedState = true;
  // A successful save keeps the mutation applied.
  assert(applyAndSaveSettings(state, [](State& s) { s.value = 3; }, [&](const State& s) { return savedState && s.value == 3; }));
  assert(state.value == 3);
  std::cout << "atomic settings storage tests passed\n";
}
