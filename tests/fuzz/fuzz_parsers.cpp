// Deterministic mutation fuzzer for the parsers that take untrusted bytes:
// .stheme packages (STH1 container + STI1/STI2 images), theme manifests, theme
// index (the listing fields of every installed theme) and the settings JSON
// accepted by /api/import.
//
// Built by tests/fuzz/run.sh with -m32 and ASan+UBSan, with
// -fno-sanitize-recover, so the first memory error or undefined behaviour
// aborts the run. The default mode needs no fuzzing engine: it replays the
// seed corpus, then a fixed number of mutants drawn from a seeded PRNG, so a
// failing run can be repeated exactly. With -DFUZZ_LIBFUZZER and one of
// -DFUZZ_TARGET_PACKAGE/_MANIFEST/_META/_IMPORT it exposes the same targets to
// libFuzzer instead.
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "Settings.h"
#include "features/theme/ThemeCatalog.h"
#include "features/theme/ThemePackage.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#ifndef FUZZ_LIBFUZZER
#include <sanitizer/common_interface_defs.h>
#endif

FSClass LittleFS;
WiFiClass WiFi;
MDNSClass MDNS;
UpdateClass Update;
EspClass ESP;
using namespace smalltv;
using Bytes = std::string;

// ---------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------
namespace {

struct MemorySource : Source {
  Bytes data;
  uint32_t failAfter = 0xffffffffu;   // reads from this call on fail
  uint32_t reads = 0;
  uint32_t size() const override { return data.size(); }
  bool read(uint32_t offset, void* out, size_t count) override {
    if (reads++ >= failAfter) return false;
    if (offset > data.size() || count > data.size() - offset) return false;
    memcpy(out, data.data() + offset, count);
    return true;
  }
};

struct NullDisplay : Display {
  uint8_t glyphColumn(uint8_t c, uint8_t col) const override { return uint8_t(c * 31 + col); }
  void row(int x, int y, const uint16_t* pixels, int count) override {
    if (x < 0 || y < 0 || x + count > CanvasSize || y >= CanvasSize) abort();   // render() promised the canvas
    for (int i = 0; i < count; ++i) sink += pixels[i];
  }
  unsigned sink = 0;
};

// Small deterministic generator, so a run depends on nothing but its seed.
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9e3779b97f4a7c15ull + 1) {}
  uint32_t next() {
    s += 0x9e3779b97f4a7c15ull;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return uint32_t((z ^ (z >> 31)) >> 16);
  }
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
  bool chance(uint32_t oneIn) { return below(oneIn) == 0; }
};

unsigned accepted[4];   // inputs each target parsed successfully: shows the mutants get past validation

// Walks everything a loaded package offers to the renderer.
void exercise(Package& package, const Theme& theme, Rng& rng) {
  // y and count take any int. x stays within what render() can pass (layer
  // coordinates are limited to -240..479 by the parser): an x near INT_MAX
  // overflows `x + count` in Package::row(), but nothing calls it that way.
  static const int coords[] = {-1, 0, 1, 119, 239, 240, 241, 479, 100000, -100000, 0x7fffffff, int(0x80000000)};
  static const int xs[] = {-241, -240, -1, 0, 1, 119, 239, 240, 241, 479};
  for (int i = 0; i < 12; ++i) {
    AssetHandle handle = rng.chance(4) ? AssetHandle(rng.below(0x10000)) : AssetHandle(rng.below(8));
    uint16_t colors[240];
    uint8_t alpha[240];
    package.row(handle, coords[rng.below(12)], xs[rng.below(10)], coords[rng.below(12)], colors, alpha);
  }
  for (const auto& layer : theme.layers) {
    if (layer.type != LayerType::Image && layer.type != LayerType::Animation) continue;
    for (unsigned frame = 0; frame < layer.frames && frame < 4; ++frame) {
      AssetHandle h = package.resolve(assetPath(layer, frame));
      uint16_t colors[240];
      uint8_t alpha[240];
      package.row(h, 0, 0, 1, colors, alpha);
      package.row(h, layer.height ? layer.height - 1 : 0, 0, layer.width, colors, alpha);
    }
  }
  Engine engine;
  engine.setTheme(theme, 0);
  std::vector<ThemeValue> values;
  static const char* const junk[] = {"", "0", "-1", "-2147483648", "2147483647", "4294967296", "99999999999999999999", "1e308", "-1e308", "nan", "abc", "\xff\xfe"};
  for (const auto& source : theme.data)
    for (const auto& field : source.fields) values.push_back({source.id + "." + field.id, junk[rng.below(12)]});
  engine.setValues(values);
  NullDisplay display;
  tm now = {};
  now.tm_year = 126;
  now.tm_mday = 1;
  now.tm_hour = int(rng.below(24));
  uint32_t t = 0;
  for (int frame = 0; frame < 3; ++frame) {
    auto dirty = engine.update(t, &now);
    render(engine, dirty, package, display);
    t += rng.chance(8) ? 0xfffffff0u : 40 + rng.below(5000);   // includes a millis() wrap
  }
  if (!theme.layers.empty()) expandText(theme.layers[0].value, &now, values);
}

void fuzzPackage(const Bytes& input, uint64_t seed) {
  Rng rng(seed);
  MemorySource source;
  source.data = input;
  if (rng.chance(8)) source.failAfter = rng.below(40);
  Package package(source);
  Theme theme;
  std::string error;
  if (package.load(theme, error)) {
    ++accepted[0];
    exercise(package, theme, rng);
  }
  source.reads = 0;
  inspectInstalledTheme(source, rng.chance(2) ? theme.id : "pixel-room");
}

struct NoAssets : Assets {
  AssetHandle resolve(const std::string&) override { return InvalidAsset; }
  bool row(AssetHandle, int, int, int, uint16_t*, uint8_t*) override { return false; }
};

struct Stream : ManifestSource {
  const Bytes& b;
  explicit Stream(const Bytes& in) : b(in) {}
  size_t size() const override { return b.size(); }
  bool read(size_t offset, char* out, size_t count) override {
    if (offset > b.size() || count > b.size() - offset) return false;
    memcpy(out, b.data() + offset, count);
    return true;
  }
};

void fuzzManifest(const Bytes& input, uint64_t seed) {
  Rng rng(seed);
  Theme theme;
  std::string error;
  if (parseTheme(input, theme, error)) {
    ++accepted[1];
    NoAssets assets;
    Engine engine;
    engine.setTheme(theme, 0);
    NullDisplay display;
    tm now = {};
    now.tm_year = 126;
    now.tm_mday = 1;
    uint32_t t = 0;
    for (int i = 0; i < 3; ++i, t += 40 + rng.below(3000)) render(engine, engine.update(t, &now), assets, display);
  }
  // The same bytes through the streaming reader the package loader uses.
  Stream stream(input);
  Theme second;
  parseTheme(stream, second, error);
}

struct TextSink : Sink {
  Bytes data;
  size_t room = 0xffffffffu;
  bool write(const char* p, size_t n) override {
    if (n > room - data.size()) return false;
    data.append(p, n);
    return true;
  }
};

void fuzzMeta(const Bytes& input, uint64_t seed) {
  Rng rng(seed);
  MemorySource index;
  index.data = input;
  if (rng.chance(8)) index.failAfter = rng.below(40);
  {
    ThemeIndexReader reader(index);
    InstalledTheme entry;
    IndexEntry kind;
    bool usable = false;
    for (unsigned n = 0; (kind = reader.next(entry)) != IndexEntry::End; ++n) {
      if (n > input.size()) abort();   // every line takes at least one byte
      usable |= kind == IndexEntry::Usable;
    }
    if (usable) ++accepted[2];
  }
  // An install or a delete copies the index; a copy that succeeds must read
  // back as usable lines only.
  index.reads = 0;
  TextSink out;
  if (rng.chance(8)) out.room = rng.below(512);
  InstalledTheme added;
  added.id = "pixel-room";
  added.name = rng.chance(2) ? "Pixel \"Room\"" : Bytes(96, '\\');
  added.author = "Example";
  added.version = "1.0";
  added.bytes = rng.next();
  if (!copyThemeIndex(index, out, rng.chance(2) ? "pixel-room" : "other", rng.chance(2) ? &added : nullptr)) return;
  MemorySource copy;
  copy.data = out.data;
  ThemeIndexReader again(copy);
  InstalledTheme entry;
  for (IndexEntry kind; (kind = again.next(entry)) != IndexEntry::End;)
    if (kind != IndexEntry::Usable) abort();
}

void fuzzImport(const Bytes& input, uint64_t) {
  // handleImport(): the request body goes straight to deserializeJson().
  JsonDocument doc;
  if (deserializeJson(doc, input.data(), input.size())) return;
  ++accepted[3];
  Settings s;
  s.setDefaults();
  settingsApplyJson(s, doc.as<JsonObjectConst>());
  // What saveSettings() would write next. Reading every field back also walks
  // any fixed-size buffer the import filled.
  JsonDocument out;
  settingsToJson(s, out.to<JsonObject>(), true);
  Settings again;
  again.setDefaults();
  settingsApplyJson(again, out.as<JsonObjectConst>());
}

enum Target { PackageTarget, ManifestTarget, MetaTarget, ImportTarget, TargetCount };
const char* const targetNames[TargetCount] = {"package", "manifest", "meta", "import"};
void (*const targetFns[TargetCount])(const Bytes&, uint64_t) = {fuzzPackage, fuzzManifest, fuzzMeta, fuzzImport};

}  // namespace

#ifdef FUZZ_LIBFUZZER
// libFuzzer mode: one target per binary, chosen at build time.
#if defined(FUZZ_TARGET_PACKAGE)
#define FUZZ_TARGET PackageTarget
#elif defined(FUZZ_TARGET_MANIFEST)
#define FUZZ_TARGET ManifestTarget
#elif defined(FUZZ_TARGET_META)
#define FUZZ_TARGET MetaTarget
#elif defined(FUZZ_TARGET_IMPORT)
#define FUZZ_TARGET ImportTarget
#else
#error "define one of FUZZ_TARGET_PACKAGE, FUZZ_TARGET_MANIFEST, FUZZ_TARGET_META, FUZZ_TARGET_IMPORT"
#endif
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  uint64_t seed = 0;
  for (size_t i = 0; i < size && i < 8; ++i) seed = seed * 131 + data[i];
  targetFns[FUZZ_TARGET](Bytes(reinterpret_cast<const char*>(data), size), seed);
  return 0;
}
#else

// ---------------------------------------------------------------------------
// Mutators
// ---------------------------------------------------------------------------
namespace {

const uint32_t extremes[] = {0, 1, 2, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};
const unsigned extremeCount = sizeof extremes / sizeof *extremes;

void putLe(Bytes& b, size_t at, uint32_t v, int width) {
  for (int i = 0; i < width && at + i < b.size(); ++i) b[at + i] = char(v >> (8 * i));
}

// Plain byte-level mutation, valid for any input.
void mutateBytes(Bytes& b, Rng& rng) {
  switch (rng.below(9)) {
    case 0:   // replace a few bytes
      for (unsigned n = 1 + rng.below(4); n && !b.empty(); --n) b[rng.below(b.size())] = char(rng.next());
      break;
    case 1:   // flip a bit
      if (!b.empty()) b[rng.below(b.size())] ^= char(1 << rng.below(8));
      break;
    case 2:   // truncate
      b.resize(rng.below(b.size() + 1));
      break;
    case 3:   // write an extreme integer, 1/2/4 bytes wide
      if (!b.empty()) {
        static const int widths[] = {1, 2, 4};
        putLe(b, rng.below(b.size()), extremes[rng.below(extremeCount)], widths[rng.below(3)]);
      }
      break;
    case 4:   // delete a run
      if (!b.empty()) b.erase(rng.below(b.size()), 1 + rng.below(16));
      break;
    case 5:   // duplicate a run
      if (!b.empty()) {
        size_t at = rng.below(b.size()), n = 1 + rng.below(64);
        b.insert(rng.below(b.size() + 1), b.substr(at, n));
      }
      break;
    case 6:   // append garbage
      for (unsigned n = 1 + rng.below(32); n; --n) b += char(rng.next());
      break;
    case 7:   // swap two bytes
      if (b.size() > 1) std::swap(b[rng.below(b.size())], b[rng.below(b.size())]);
      break;
    default:  // long run of one byte
      b.insert(rng.below(b.size() + 1), Bytes(1 + rng.below(4096), char(rng.chance(2) ? 'A' : rng.next())));
  }
}

// JSON-aware mutation: acts on number tokens, values, strings and nesting.
void mutateJson(Bytes& b, Rng& rng) {
  static const char* const numbers[] = {"0", "-1", "-2147483648", "2147483647", "2147483648", "4294967295", "4294967296",
                                        "-4294967297", "99999999999999999999999", "1e308", "-1e308", "1e-308", "0.0000001",
                                        "65535", "65536", "-32769", "255", "256", "239", "240", "241", "479", "480", "10000", "10001"};
  static const char* const values[] = {"null", "true", "false", "[]", "{}", "\"\"", "\"\\u0000\"", "\"\\ud800\"", "[[[]]]", "-", "1e"};
  switch (rng.below(7)) {
    case 0: {   // replace a number token
      std::vector<std::pair<size_t, size_t>> spans;
      for (size_t i = 0; i < b.size();) {
        if (isdigit((unsigned char)b[i]) || (b[i] == '-' && i + 1 < b.size() && isdigit((unsigned char)b[i + 1]))) {
          size_t j = i + 1;
          while (j < b.size() && (isdigit((unsigned char)b[j]) || strchr(".eE+-", b[j]))) ++j;
          spans.push_back({i, j - i});
          i = j;
        } else {
          ++i;
        }
      }
      if (!spans.empty()) {
        auto span = spans[rng.below(spans.size())];
        b.replace(span.first, span.second, numbers[rng.below(sizeof numbers / sizeof *numbers)]);
      }
      break;
    }
    case 1: {   // replace a value with another type
      size_t colon = b.find(':', b.empty() ? 0 : rng.below(b.size()));
      if (colon != std::string::npos) {
        size_t end = b.find_first_of(",}]", colon);
        if (end != std::string::npos) b.replace(colon + 1, end - colon - 1, values[rng.below(sizeof values / sizeof *values)]);
      }
      break;
    }
    case 2: {   // lengthen a string
      size_t quote = b.find('"', b.empty() ? 0 : rng.below(b.size()));
      if (quote != std::string::npos) b.insert(quote + 1, Bytes(1 + rng.below(5000), rng.chance(2) ? 'x' : '\\'));
      break;
    }
    case 3: {   // deep nesting in place of a value
      size_t colon = b.find(':', b.empty() ? 0 : rng.below(b.size()));
      unsigned depth = 1 + rng.below(300);
      Bytes nest;
      bool arrays = rng.chance(2);
      for (unsigned i = 0; i < depth; ++i) nest += arrays ? "[" : "{\"a\":";
      b.insert(colon != std::string::npos ? colon + 1 : 0, nest);
      break;
    }
    case 4:   // drop a character (breaks a quote, comma or bracket)
      if (!b.empty()) b.erase(rng.below(b.size()), 1);
      break;
    case 5:   // truncate
      b.resize(rng.below(b.size() + 1));
      break;
    default:
      mutateBytes(b, rng);
  }
}

// Splits an .stheme into records so a mutation can change one field and
// still hand the loader a package whose lengths agree, which is the only way
// most mutants get past the index checks and into the manifest and images.
struct Record { Bytes path, payload; };
bool splitPackage(const Bytes& pkg, std::vector<Record>& out) {
  if (pkg.size() < 8 || pkg.compare(0, 4, "STH1")) return false;
  unsigned count = uint8_t(pkg[4]) | (uint8_t(pkg[5]) << 8);
  size_t pos = 8;
  for (unsigned i = 0; i < count; ++i) {
    if (pos + 6 > pkg.size()) return false;
    size_t pathLen = uint8_t(pkg[pos]) | (uint8_t(pkg[pos + 1]) << 8);
    size_t len = uint8_t(pkg[pos + 2]) | (uint8_t(pkg[pos + 3]) << 8) | (uint8_t(pkg[pos + 4]) << 16) | (size_t(uint8_t(pkg[pos + 5])) << 24);
    pos += 6;
    if (pos + pathLen + len > pkg.size()) return false;
    out.push_back({pkg.substr(pos, pathLen), pkg.substr(pos + pathLen, len)});
    pos += pathLen + len;
  }
  return true;
}
Bytes joinPackage(const std::vector<Record>& records) {
  Bytes out = "STH1";
  out.append(4, '\0');
  putLe(out, 4, uint32_t(records.size()), 2);
  for (const auto& r : records) {
    size_t at = out.size();
    out.append(6, '\0');
    putLe(out, at, uint32_t(r.path.size()), 2);
    putLe(out, at + 2, uint32_t(r.payload.size()), 4);
    out += r.path;
    out += r.payload;
  }
  return out;
}

Bytes mutatePackage(const Bytes& seed, Rng& rng) {
  std::vector<Record> records;
  if (!rng.chance(3) && splitPackage(seed, records) && !records.empty()) {
    Record& r = records[rng.below(records.size())];
    switch (rng.below(7)) {
      case 0:   // manifest through the JSON mutator
        for (auto& rec : records)
          if (rec.path == "theme.json") {
            mutateJson(rec.payload, rng);
            if (rng.chance(2)) mutateJson(rec.payload, rng);
          }
        break;
      case 1:   // image header fields: size, alpha flag, reserved bytes
        if (r.payload.size() >= 12) {
          size_t field = rng.below(12);
          putLe(r.payload, field, extremes[rng.below(extremeCount)], field >= 4 && field < 8 ? 2 : 1);
        }
        break;
      case 2:   // image payload shorter or longer than the header says
        if (r.payload.size() > 12) r.payload.resize(12 + rng.below(r.payload.size() - 11));
        else r.payload += Bytes(rng.below(64), 'x');
        break;
      case 3:   // path
        mutateBytes(r.path, rng);
        break;
      case 4:   // drop or duplicate a record
        if (rng.chance(2)) records.erase(records.begin() + rng.below(records.size()));
        else records.insert(records.begin() + rng.below(records.size()), records[rng.below(records.size())]);
        break;
      case 5:   // one byte of an STI2 image's runs, past its header, palette
                // and row table: most such images still load and get drawn
        if (r.payload.size() >= 12 && r.payload.compare(0, 4, "STI2") == 0) {
          size_t rows = 12 + uint8_t(r.payload[9]) * (r.payload[8] ? 3 : 2) +
                        4 * (uint8_t(r.payload[6]) | (uint8_t(r.payload[7]) << 8));
          if (rows < r.payload.size()) {
            size_t at = rows + rng.below(r.payload.size() - rows);
            r.payload[at] = rng.chance(2) ? char(rng.next()) : char(r.payload[at] ^ (1 << rng.below(8)));
            break;
          }
        }
        mutateBytes(r.payload, rng);
        break;
      default:
        mutateBytes(r.payload, rng);
    }
    Bytes out = joinPackage(records);
    if (rng.chance(3)) mutateBytes(out, rng);   // and sometimes break the container as well
    return out;
  }
  // Container-level lies: entry count, path lengths, payload lengths.
  Bytes out = seed;
  for (unsigned n = 1 + rng.below(3); n; --n) {
    if (rng.chance(3) && out.size() >= 8) putLe(out, 4 + rng.below(4), extremes[rng.below(extremeCount)], 2);
    else mutateBytes(out, rng);
  }
  return out;
}

Bytes mutateOne(Target target, const Bytes& seed, Rng& rng) {
  if (target == PackageTarget) return mutatePackage(seed, rng);
  Bytes b = seed;
  for (unsigned n = rng.chance(2) ? 1 : 1 + rng.below(3); n; --n) mutateJson(b, rng);
  return b;
}

// ---------------------------------------------------------------------------
// Corpus and driver
// ---------------------------------------------------------------------------
bool readFile(const std::string& path, Bytes& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}
std::vector<std::string> listDir(const std::string& dir) {
  std::vector<std::string> names;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d))
      if (e->d_name[0] != '.') names.push_back(dir + "/" + e->d_name);
    closedir(d);
  }
  std::sort(names.begin(), names.end());
  return names;
}

// A package small enough to read, with an opaque and a transparent pixel.
Bytes smallPackage() {
  Bytes image = "STI1";
  image += Bytes("\x02\x00\x01\x00\x01\x00\x00\x00", 8);
  image += Bytes("\x00\xf8\xff\xe0\x07\x00", 6);
  std::vector<Record> records;
  records.push_back({"theme.json", R"({"spec":1,"theme":{"id":"seed","name":"Seed","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"layers":[{"id":"bg","type":"image","x":0,"y":0,"source":"images/bg.png"},{"id":"t","type":"text","x":4,"y":4,"text":"{time}"}]})"});
  records.push_back({"images/bg.png.sti", image});
  return joinPackage(records);
}

// A spec 2 package that shows one image, for the STI2 files in tests/theme/sti.
Bytes imagePackage(const Bytes& image) {
  std::vector<Record> records;
  records.push_back({"theme.json", R"({"spec":2,"theme":{"id":"seed","name":"Seed","author":"Me","version":"1"},"display":{"width":240,"height":240,"background":"#000000"},"layers":[{"id":"img","type":"image","x":0,"y":0,"source":"images/img.png"}]})"});
  records.push_back({"images/img.png.sti", image});
  return joinPackage(records);
}

std::string currentTarget, crashDir = ".";
uint64_t currentIter = 0;
const Bytes* currentInput = nullptr;

// Called by the sanitizer runtime as it dies: keep the input that did it.
void onDeath() {
  if (!currentInput) return;
  std::string path = crashDir + "/crash-" + currentTarget + "-" + std::to_string(currentIter);
  if (FILE* f = fopen(path.c_str(), "wb")) {
    fwrite(currentInput->data(), 1, currentInput->size(), f);
    fclose(f);
    fprintf(stderr, "fuzz: input saved to %s\n", path.c_str());
  }
}

void run(Target target, const Bytes& input, uint64_t iter, uint64_t seed) {
  currentTarget = targetNames[target];
  currentIter = iter;
  currentInput = &input;
  targetFns[target](input, seed + iter);
  currentInput = nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  uint64_t iters = 2000, seed = 1;
  if (const char* env = getenv("FUZZ_ITERS")) iters = strtoull(env, nullptr, 10);
  std::string root = ".", only, repro;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--iters") iters = strtoull(value(), nullptr, 10);
    else if (a == "--seed") seed = strtoull(value(), nullptr, 10);
    else if (a == "--root") root = value();
    else if (a == "--target") only = value();
    else if (a == "--repro") repro = value();
    else if (a == "--crash-dir") crashDir = value();
    else {
      fprintf(stderr, "usage: %s [--iters N] [--seed S] [--root REPO] [--target package|manifest|meta|import] [--repro FILE] [--crash-dir DIR]\n", argv[0]);
      return 2;
    }
  }
  __sanitizer_set_death_callback(onDeath);

  if (!repro.empty()) {   // replay one saved input against one target
    Bytes input;
    for (int t = 0; t < TargetCount; ++t) {
      if (only != targetNames[t]) continue;
      if (!readFile(repro, input)) {
        fprintf(stderr, "cannot read %s\n", repro.c_str());
        return 2;
      }
      run(Target(t), input, 0, seed);
      printf("fuzz: %s did not fail on %s\n", targetNames[t], repro.c_str());
      return 0;
    }
    fprintf(stderr, "--repro needs --target\n");
    return 2;
  }

  std::vector<Bytes> corpus[TargetCount];
  corpus[PackageTarget].push_back(smallPackage());
  Bytes data;
  for (const auto& p : listDir(root + "/examples/themes")) {
    if (p.size() > 7 && p.compare(p.size() - 7, 7, ".stheme") == 0 && readFile(p, data)) corpus[PackageTarget].push_back(data);
    if (readFile(p + "/theme.json", data)) corpus[ManifestTarget].push_back(data);
  }
  // The bundled themes are spec 1 (STI1 only); STI2 rows and palettes are
  // reached through the shared test files, one package each. Only `.sti`
  // files: the Python tests leave a __pycache__ folder there in CI.
  for (const auto& p : listDir(root + "/tests/theme/sti"))
    if (p.size() > 4 && p.compare(p.size() - 4, 4, ".sti") == 0 && readFile(p, data) && data.compare(0, 4, "STI2") == 0)
      corpus[PackageTarget].push_back(imagePackage(data));
  for (const auto& p : listDir(root + "/tests/fuzz/corpus/meta"))
    if (readFile(p, data)) corpus[MetaTarget].push_back(data);
  for (const auto& p : listDir(root + "/tests/fuzz/corpus/import"))
    if (readFile(p, data)) corpus[ImportTarget].push_back(data);
  {   // a manifest seed from the small package, and the device's own export
    std::vector<Record> records;
    splitPackage(smallPackage(), records);
    corpus[ManifestTarget].push_back(records[0].payload);
    Settings s;
    s.setDefaults();
    JsonDocument doc;
    settingsToJson(s, doc.to<JsonObject>(), true);
    std::string json;
    serializeJson(doc, json);
    corpus[ImportTarget].push_back(json);
  }

  int failures = 0;
  for (int t = 0; t < TargetCount; ++t) {
    if (!only.empty() && only != targetNames[t]) continue;
    if (corpus[t].empty()) {
      fprintf(stderr, "fuzz: no seeds for %s (run from the repository root or pass --root)\n", targetNames[t]);
      ++failures;
      continue;
    }
    uint64_t cases = 0;
    for (const auto& seedInput : corpus[t]) run(Target(t), seedInput, cases++, seed);
    Rng rng(seed * 1000 + t);
    for (uint64_t i = 0; i < iters; ++i) {
      const Bytes& base = corpus[t][i % corpus[t].size()];
      Bytes mutant = mutateOne(Target(t), base, rng);
      run(Target(t), mutant, cases++, seed);
    }
    printf("fuzz: %-8s %zu seeds + %llu mutants ok (%u of %llu cases parsed)\n", targetNames[t], corpus[t].size(),
           (unsigned long long)iters, accepted[t], (unsigned long long)cases);
  }
  return failures ? 1 : 0;
}
#endif
