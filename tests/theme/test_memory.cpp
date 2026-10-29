// Peak heap of loading a theme package, counted the way the ESP8266 heap
// (umm_malloc) charges it: every allocation takes whole 8-byte blocks plus a
// 4-byte header. Build with -m32 so pointers, std::string and std::vector have
// the device's sizes. The device has roughly 6.5 KiB free while a theme runs,
// and listing or installing a theme loads a second one next to it.
#include "BlockList.h"
#include "ThemeCatalog.h"
#include "ThemePackage.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <malloc.h>
#include <new>
using namespace smalltv;

static size_t live = 0, peak = 0, largest = 0;
static bool counting = false;
static size_t charge(void* p) { return p ? (malloc_usable_size(p) + 4 + 7) / 8 * 8 : 0; }
static void* track(void* p, size_t requested) {
  if (counting && p) {
    live += charge(p);
    if (live > peak) peak = live;
    if (requested > largest) largest = requested;
  }
  return p;
}
static void untrack(void* p) { if (counting && p) live -= charge(p); }
extern "C" {
void* __real_malloc(size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
void* __wrap_malloc(size_t n) { return track(__real_malloc(n), n); }
void __wrap_free(void* p) { untrack(p); __real_free(p); }
void* __wrap_realloc(void* p, size_t n) {
  untrack(p);
  return track(__real_realloc(p, n), n);
}
}
void* operator new(size_t n) {
  void* p = __wrap_malloc(n);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept { __wrap_free(p); }
void operator delete(void* p, size_t) noexcept { __wrap_free(p); }

struct FileSource : Source {
  std::string bytes;
  explicit FileSource(const char* path) {
    std::ifstream in(path, std::ios::binary);
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  uint32_t size() const override { return bytes.size(); }
  bool read(uint32_t offset, void* out, size_t count) override {
    if (offset > bytes.size() || count > bytes.size() - offset) return false;
    memcpy(out, bytes.data() + offset, count);
    return true;
  }
};

// Both limits come from a real SmallTV-Ultra. With the running theme
// released and the web server holding its 2 KiB upload buffer, the lowest
// free heap seen when an install starts validating was 8,400 bytes (three
// runs); a load must stay well inside that, so the budget leaves 3.3 KiB
// spare. And a load must never need a block so large that a fragmented heap
// cannot supply it: 2340-byte requests failed with 6.6 KiB free but no free
// block above 1.8 KiB.
constexpr size_t LoadPeakBudget = 5120;
constexpr size_t LargestBlockBudget = 1024;
// What a running theme may hold. Without a theme the device has about 9.3 KiB
// free, and on the device a running theme costs about 0.5 KiB more than
// counted here (its open package file and the data fetcher). This budget
// leaves at least 5 KiB free while a theme runs, for the web server's
// requests and for mDNS and WiFi. With 3.8 KiB free, a web request starved
// both and crashed the device. The 5 KiB is a judgement, not a measured limit.
constexpr size_t RunningBudget = 3840;

struct NoDisplay : Display {
  uint8_t glyphColumn(uint8_t, uint8_t) const override { return 0x7f; }
  void row(int, int, const uint16_t*, int) override {}
};

struct Usage { size_t peak, loaded, theme, running, largest; };
// Peak while loading; what a loaded theme holds (the Theme plus the
// package's entry index, which ThemeMode keeps for reading assets); what the
// Theme alone holds; what it holds while it runs (the engine's per-layer
// state and expanded texts too, after a first frame is computed and drawn);
// and the biggest single request (the device needs that much in one
// contiguous block).
static Usage load(const char* path) {
  FileSource source(path);
  assert(!source.bytes.empty());
  Theme theme;
  std::string error;
  Usage usage{};
  live = peak = largest = 0;
  counting = true;
  {
    Package package(source);
    bool ok = package.load(theme, error);
    if (!ok) std::cerr << path << ": " << error << "\n";
    assert(ok);
    usage.loaded = live;
  }
  usage.peak = peak;
  usage.theme = live;
  // Running, the way ThemeMode holds it: the package index for reading
  // assets, and the engine with the theme moved into it.
  live = 0;
  {
    Package package(source);
    Theme loaded;
    assert(package.load(loaded, error));
    Engine engine;
    engine.setTheme(std::move(loaded), 0);
    tm time{};
    time.tm_year = 126; time.tm_mon = 9; time.tm_mday = 23; time.tm_wday = 5; time.tm_yday = 295;
    time.tm_hour = 16; time.tm_min = 15;
    std::vector<Rect> dirty = engine.update(1000, &time);
    NoDisplay display;
    assert(render(engine, dirty, package, display));
    usage.running = live;
  }
  usage.largest = largest;
  counting = false;
  return usage;
}

// A package with many files, named like real animation frames. Its index
// must not need one large block (each entry used to take about 40 bytes, so
// a vector of 41 needed 1,640 B in one piece), nor keep every path in RAM
// (another ~45 B each for paths like these), and every handle must still
// read its own file.
static void testManyEntries() {
  const unsigned frames = 64;
  // Paths with the same FNV-1a hash: looking either one up must still find
  // its own file. The first two hash to 0x08cad385; the second two
  // (0x0082b56a) are as long as each other and differ only after their
  // first 16 characters.
  const char* const twins[] = {"c/4a0f5.sti", "c/bec20.sti", "animations/twins/0089db.sti", "animations/twins/047828.sti"};
  const std::string manifest = R"({"spec":1,"theme":{"id":"many","name":"Many","author":"Me","version":"1"},)"
    R"("display":{"width":240,"height":240,"background":"#000000"},"layers":[]})";
  FileSource source("");
  auto u16 = [&](unsigned v) { source.bytes += char(v & 255); source.bytes += char(v >> 8); };
  auto u32 = [&](uint32_t v) { u16(v & 0xffff); u16(v >> 16); };
  auto image = [&](const std::string& path, unsigned pixel) {
    u16(path.size()); u32(14); source.bytes += path;
    source.bytes += "STI1"; u16(1); u16(1); source.bytes += std::string(4, '\0');
    u16(pixel);  // the one pixel identifies the file
  };
  auto frame = [](unsigned i) {
    char path[48];
    snprintf(path, sizeof(path), "animations/snow-showers/%03u.png.sti", i);
    return std::string(path);
  };
  source.bytes = "STH1";
  u16(frames + 5); u16(0);
  u16(10); u32(manifest.size()); source.bytes += "theme.json" + manifest;
  for (unsigned i = 0; i < frames; ++i) image(frame(i), i);
  for (unsigned t = 0; t < 4; ++t) image(twins[t], 1000 * (t + 1));
  Theme theme;
  std::string error;
  live = peak = largest = 0;
  counting = true;
  size_t withIndex = 0;
  {
    Package package(source);
    bool ok = package.load(theme, error);
    if (!ok) std::cerr << "many entries: " << error << "\n";
    assert(ok);
    withIndex = live;
    auto pixel = [&](const std::string& path) {
      AssetHandle handle = package.resolve(path);
      uint16_t color = 0;
      uint8_t alpha = 0;
      assert(handle != InvalidAsset && package.row(handle, 0, 0, 1, &color, &alpha));
      return color;
    };
    for (unsigned i : {0u, 31u, 63u}) assert(pixel(frame(i)) == i);
    for (unsigned t = 0; t < 4; ++t) assert(pixel(twins[t]) == 1000 * (t + 1));
    assert(package.resolve("c/00000.sti") == InvalidAsset && package.resolve("animations/snow-showers/064.png.sti") == InvalidAsset);
  }
  const size_t perEntry = (withIndex - live) / (frames + 5);
  counting = false;
  if (largest > LargestBlockBudget) fprintf(stderr, "%u entries: largest block %zu B\n", frames + 5, largest);
  assert(largest <= LargestBlockBudget);
  if (perEntry > 24) fprintf(stderr, "package index: %zu B per entry\n", perEntry);
  assert(perEntry <= 24);
}

// BlockList: the vector subset the engine uses, without one large block.
static void testBlockList() {
  BlockList<int> numbers;
  assert(numbers.size() == 0);
  numbers.reserve(40);
  assert(numbers.size() == 0);
  for (int i = 0; i < 40; ++i) numbers.push_back(i);
  assert(numbers.size() == 40 && numbers[0] == 0 && numbers[39] == 39);
  int expected = 0;
  for (int n : numbers) assert(n == expected++);
  assert(expected == 40);
  const BlockList<int>& constant = numbers;
  expected = 0;
  for (const int& n : constant) assert(n == expected++);
  numbers[5] = 50;
  BlockList<int> copy(numbers);
  assert(copy.size() == 40 && copy[5] == 50 && copy[39] == 39);
  copy.assign(3, 7);
  assert(copy.size() == 3 && copy[0] == 7 && copy[2] == 7 && numbers.size() == 40);
  copy = numbers;
  assert(copy.size() == 40 && copy[5] == 50);
  BlockList<int> moved(std::move(copy));
  assert(moved.size() == 40 && moved[39] == 39);
  BlockList<int> grown;
  int& made = grown.emplace_back();
  made = 9;
  assert(grown.size() == 1 && grown[0] == 9 && &grown[0] == &made);
  BlockList<int> listed = {1, 2, 3};
  assert(listed.size() == 3 && listed[2] == 3);
  listed = {4, 5};
  assert(listed.size() == 2 && listed[0] == 4 && listed[1] == 5);

  // A full theme's worth of layers: elements never move, and no single
  // request is larger than the budget, however many layers there are.
  live = peak = largest = 0;
  counting = true;
  {
    BlockList<Layer> layers;
    layers.reserve(MaxLayers);
    Layer first;
    first.value = "first";
    layers.push_back(first);
    const Layer* address = &layers[0];
    for (size_t i = 1; i < MaxLayers; ++i) layers.push_back(Layer());
    assert(&layers[0] == address && layers[0].value == "first" && layers.size() == MaxLayers);
  }
  counting = false;
  if (largest > LargestBlockBudget) fprintf(stderr, "BlockList<Layer>: largest block %zu B\n", largest);
  assert(largest <= LargestBlockBudget);

  // With the count reserved, a short list takes what its elements need, not
  // whole blocks: two blocks (4 + 1 layers) plus the small block index and
  // per-allocation headers, well under two full blocks (8 layers).
  live = 0;
  counting = true;
  {
    BlockList<Layer> five;
    five.reserve(5);
    for (int i = 0; i < 5; ++i) five.push_back(Layer());
    size_t kept = live;
    if (kept > 5 * sizeof(Layer) + 64) fprintf(stderr, "five layers keep %zu B, sizeof(Layer) %zu\n", kept, sizeof(Layer));
    assert(kept <= 5 * sizeof(Layer) + 64 && kept < 2 * BlockList<Layer>::PerBlock * sizeof(Layer));
  }
  counting = false;
}

// The listing reads the theme index one entry at a time, next to the running
// theme and the reply it builds: reading 16 entries and an over-long line
// must take no more heap than reading one entry, and no large block.
static size_t indexPeak(const std::string& text) {
  FileSource index("/dev/null");
  index.bytes = text;
  live = peak = largest = 0;
  counting = true;
  {
    ThemeIndexReader reader(index);
    InstalledTheme entry;
    while (reader.next(entry) != IndexEntry::End) {}
  }
  counting = false;
  return peak;
}
static void testThemeIndex() {
  InstalledTheme longest;
  longest.id = std::string(48, 'x');
  longest.name = std::string(96, '"');
  longest.author = std::string(96, '\\');
  longest.version = std::string(32, '"');
  longest.bytes = 4294967295u;
  longest.valid = true;
  const std::string line = themeIndexLine(longest);
  const size_t one = indexPeak(line);
  // A line far over MaxIndexLine and its strings over 96 bytes, then 16 of
  // the longest entries; the whole stays within MaxIndexBytes, or nothing
  // would be read at all.
  std::string many = "{\"revision\":1,\"id\":\"" + std::string(2000, 'n') + "\"}\n";
  for (int i = 0; i < 16; ++i) many += line;
  assert(many.size() <= MaxIndexBytes);
  const size_t sixteen = indexPeak(many);
  if (sixteen > one || largest > LargestBlockBudget)
    fprintf(stderr, "theme index: peak %zu B for one entry, %zu B for 16 and a long line, largest block %zu B\n", one, sixteen, largest);
  assert(sixteen <= one && largest <= LargestBlockBudget);
}

// The list (GET /api/themes) of 16 themes. 3.2.0 held it in one document
// (about 3.2 KB on the device with 16 short entries) and built the reply in
// one string next to it; the device ran out of heap there after a row of
// installs (issue #63). Held as ListedThemes the list takes at most half of
// that document with short fields like the device's, less than it with the
// longest fields the limits allow, and never a block over 512 B; writing the
// reply in pieces takes no heap. Every field differs between the themes, as
// on a device: the document keeps only one copy of equal strings.
struct CountingSink : Sink {
  size_t bytes = 0;
  bool write(const char*, size_t count) override {
    bytes += count;
    return true;
  }
};
struct ListUsage { size_t document, held, heldLargest, writing, writingLargest, bytes; };
static ListUsage listUsage(size_t idLength, size_t nameLength, size_t versionLength, size_t errorLength) {
  // Made before counting starts: only the list's own allocations count.
  auto field = [](char c, size_t length, int i) {
    std::string s = std::to_string(10 + i);
    return std::string(length > 2 ? length - 2 : 0, c) + s;
  };
  std::vector<std::string> ids, names, authors, versions, errors;
  for (int i = 0; i < 16; ++i) {
    ids.push_back(field('x', idLength, i));
    names.push_back(field('"', nameLength, i));
    authors.push_back(field('\\', nameLength, i));
    versions.push_back(field('"', versionLength, i));
    errors.push_back(errorLength ? field('e', errorLength, i) : "");
  }
  ListUsage u{};
  live = peak = largest = 0;
  counting = true;
  {
    JsonDocument doc;
    JsonArray list = doc["themes"].to<JsonArray>();
    for (int i = 0; i < 16; ++i) {
      JsonObject o = list.add<JsonObject>();
      o["id"] = ids[i];
      o["name"] = names[i];
      o["author"] = authors[i];
      o["version"] = versions[i];
      o["bytes"] = 4294967295u;
      o["valid"] = false;
      o["error"] = errors[i];
    }
  }
  counting = false;
  u.document = peak;
  BlockList<ListedTheme> themes;
  live = peak = largest = 0;
  counting = true;
  themes.reserve(16);
  for (int i = 0; i < 16; ++i) {
    ListedTheme& t = themes.emplace_back();
    t.set(ids[i].c_str(), names[i].c_str(), authors[i].c_str(), versions[i].c_str(), errors[i].c_str());
    t.bytes = 4294967295u;
  }
  counting = false;
  u.held = peak;
  u.heldLargest = largest;
  CountingSink sink;
  live = peak = largest = 0;
  counting = true;
  writeThemeList<128>(ids[0].c_str(), errors[0].c_str(), 786432, themes, sink);
  counting = false;
  u.writing = peak;
  u.writingLargest = largest;
  u.bytes = sink.bytes;
  return u;
}
static void testListReply() {
  const ListUsage small = listUsage(7, 7, 5, 0), longest = listUsage(48, 96, 32, 96);
  for (const ListUsage* u : {&small, &longest})
    if (u->heldLargest > 512 || u->writing || u->writingLargest)
      fprintf(stderr, "list: document %zu B; held %zu B, largest block %zu B; writing %zu B, largest block %zu B\n",
              u->document, u->held, u->heldLargest, u->writing, u->writingLargest);
  assert(small.held * 2 <= small.document && longest.held < longest.document);
  assert(small.heldLargest <= 512 && longest.heldLargest <= 512);
  assert(!small.writing && !longest.writing && !small.writingLargest && !longest.writingLargest);
  assert(longest.bytes > 16 * (48 + 2 * 96 + 2 * 32 + 96));
}

int main(int argc, char** argv) {
  testBlockList();
  testManyEntries();
  testThemeIndex();
  testListReply();
  const char* examples[] = {"examples/themes/pixel-room.stheme", "examples/themes/live-status.stheme",
                            "examples/themes/terminal-ops.stheme"};
  bool report = argc > 1;
  for (int i = 1; i < argc; ++i) {
    Usage u = load(argv[i]);
    printf("%-36s peak %5zu B  loaded %5zu B  theme %5zu B  running %5zu B  largest block %5zu B\n",
           argv[i], u.peak, u.loaded, u.theme, u.running, u.largest);
  }
  if (report) return 0;
  for (const char* path : examples) {
    Usage u = load(path);
    if (u.peak > LoadPeakBudget || u.largest > LargestBlockBudget || u.running > RunningBudget)
      fprintf(stderr, "%s: peak %zu B (budget %zu), largest block %zu B (budget %zu), running %zu B (budget %zu)\n",
              path, u.peak, LoadPeakBudget, u.largest, LargestBlockBudget, u.running, RunningBudget);
    assert(u.peak <= LoadPeakBudget);
    assert(u.largest <= LargestBlockBudget);
    assert(u.running <= RunningBudget);
  }
  std::cout << "theme memory tests passed\n";
}
