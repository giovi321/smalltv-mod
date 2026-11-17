// In-memory LittleFS for the rain host tests: files are byte vectors in a
// map, a File reads or writes the map entry directly, and Dir walks the
// names under a directory. The test can inject failures (rename, open).
#pragma once
#include "rain_host.h"
#include <map>
#include <memory>
#include <vector>

struct FakeFs {
  std::map<std::string, std::vector<uint8_t>> files;
  std::map<std::string, bool> dirs;
  int failRenameTo = 0;          // >0: fail the next renames whose target contains failRenameName
  std::string failRenameName;
  size_t capacity = 0;           // >0: total bytes the files may hold; writes past it come up short
  long shortAfter = -1;          // >=0: bytes still accepted before writes come up short,
                                 // whatever info() says (a filesystem fuller than reported)
  std::string shortName;         // only for files whose path contains this
  size_t used() const { size_t n = 0; for (auto& f : files) n += f.second.size(); return n; }
};
struct FSInfo { size_t totalBytes = 0, usedBytes = 0, blockSize = 8192; };
extern FakeFs g_fs;

class File : public Stream {
 public:
  File() {}
  File(const std::string& p, bool w) : path_(p), open_(true), write_(w) {}
  explicit operator bool() const { return open_; }
  void close() { open_ = false; }
  int read() override {
    if (!open_ || write_) return -1;
    auto& d = g_fs.files[path_];
    return pos_ < d.size() ? d[pos_++] : -1;
  }
  int read(uint8_t* buf, size_t n) {
    size_t got = readBytes(buf, n);
    return (int)got;
  }
  int peek() { if (!open_) return -1; auto& d = g_fs.files[path_]; return pos_ < d.size() ? d[pos_] : -1; }
  size_t write(uint8_t c) { return write(&c, 1); }
  size_t write(const uint8_t* b, size_t n) {
    if (!open_ || !write_) return 0;
    if (g_fs.capacity) {
      size_t u = g_fs.used();
      size_t room = u < g_fs.capacity ? g_fs.capacity - u : 0;
      if (n > room) n = room;   // LittleFS full: a short write
    }
    if (g_fs.shortAfter >= 0 && path_.find(g_fs.shortName) != std::string::npos) {
      if ((long)n > g_fs.shortAfter) n = (size_t)g_fs.shortAfter;
      g_fs.shortAfter -= (long)n;
    }
    auto& d = g_fs.files[path_];
    d.insert(d.end(), b, b + n);
    return n;
  }
  void flush() {}
  bool seek(size_t p) { pos_ = p; return open_; }
  size_t position() const { return pos_; }
  size_t size() const { auto it = g_fs.files.find(path_); return it == g_fs.files.end() ? 0 : it->second.size(); }
  bool getWriteError() const { return false; }
  bool isDirectory() const { return false; }
  const char* name() const { size_t s = path_.rfind('/'); return path_.c_str() + (s == std::string::npos ? 0 : s + 1); }
 private:
  std::string path_;
  bool open_ = false, write_ = false;
  size_t pos_ = 0;
};

class Dir {
 public:
  explicit Dir(const std::string& d) : dir_(d + "/") {}
  bool next() {
    // Walk the names that follow the current one (the map may change between calls).
    auto it = cur_.empty() ? g_fs.files.lower_bound(dir_) : g_fs.files.upper_bound(cur_);
    for (; it != g_fs.files.end(); ++it) {
      if (it->first.compare(0, dir_.size(), dir_) != 0) break;
      if (it->first.find('/', dir_.size()) != std::string::npos) continue;
      cur_ = it->first;
      return true;
    }
    return false;
  }
  String fileName() const { return String(cur_.substr(dir_.size())); }
 private:
  std::string dir_, cur_;
};

struct FSClass {
  bool begin() { return true; }
  bool format() { g_fs.files.clear(); return true; }
  File open(const char* p, const char* mode) {
    std::string path(p);
    if (mode[0] == 'w') { g_fs.files[path].clear(); return File(path, true); }
    if (!g_fs.files.count(path)) return File();
    return File(path, false);
  }
  File open(const String& p, const char* mode) { return open(p.c_str(), mode); }
  bool exists(const char* p) { return g_fs.files.count(p) || g_fs.dirs.count(p); }
  bool mkdir(const char* p) { g_fs.dirs[p] = true; return true; }
  bool rmdir(const char* p) {
    std::string d = std::string(p) + "/";
    for (auto& f : g_fs.files) if (f.first.compare(0, d.size(), d) == 0) return false;   // not empty
    return g_fs.dirs.erase(p) > 0;
  }
  bool info(FSInfo& i) {
    i.totalBytes = g_fs.capacity ? g_fs.capacity : 1048576;
    i.usedBytes = g_fs.used();
    return true;
  }
  bool remove(const char* p) { return g_fs.files.erase(p) > 0; }
  bool remove(const String& p) { return remove(p.c_str()); }
  bool rename(const char* a, const char* b) {
    if (g_fs.failRenameTo > 0 && std::string(b).find(g_fs.failRenameName) != std::string::npos) {
      g_fs.failRenameTo--;
      return false;
    }
    auto it = g_fs.files.find(a);
    if (it == g_fs.files.end()) return false;
    auto data = it->second;
    g_fs.files.erase(it);
    g_fs.files[b] = data;   // lfs_rename replaces an existing target
    return true;
  }
  Dir openDir(const char* d) { return Dir(d); }
};
extern FSClass LittleFS;
