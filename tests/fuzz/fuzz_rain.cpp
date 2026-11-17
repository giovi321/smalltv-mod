// Deterministic mutation fuzzer for the rain radar's PNG decoder
// (src/features/rain/RainPng.cpp): the radar tiles come from a third-party
// server over plain HTTP, so every byte of them is untrusted.
//
// Built by tests/fuzz/run.sh like fuzz_parsers.cpp: -m32 (the ESP8266's word
// size), ASan + UBSan, -fno-sanitize-recover, so the first memory error or
// undefined behaviour aborts the run. It replays the seed tiles in
// tests/fuzz/corpus/rain (written by make_rain_seeds.py), then a fixed
// number of mutants from a seeded PRNG. With -DFUZZ_LIBFUZZER it exposes
// the same target to libFuzzer instead (tests/fuzz/libfuzzer.sh rain).
//
// Beyond "no crash", every decode checks what the device relies on:
//   - a decode that succeeds wrote exactly 240 x 240 indices, each one an
//     entry of kRainColors;
//   - the result does not depend on what the LZ window held before: the
//     device reuses the window across the frames of a fetch without clearing
//     it, so each input is decoded twice, over two different fillings, and
//     both runs must agree byte for byte.
#include <FS.h>
#include "RainPng.h"
#include "RainData.h"
#include "RainHistoryMem.h"
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
using Bytes = std::string;

namespace {

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

unsigned accepted = 0;   // inputs decoded successfully: shows mutants get past validation

static uint8_t g_win[RAIN_WINDOW_PIXELS];

// The small-memory mode: ring in RAM, the rest in a RainHistoryMem (which
// also checks the decoder keeps to the history contract).
bool decodeRing(const Bytes& input, int cropK, Bytes& out, bool& contractOk) {
  static RainHistoryMem* hist = nullptr;
  delete hist;
  hist = new RainHistoryMem;
  memset(g_win, 0xA5, RAIN_RING_PIXELS);
  File src = File::reader(reinterpret_cast<const uint8_t*>(input.data()), input.size());
  File dst = File::writer(&out);
  bool ok = rainDecodeToFs(src, dst, RAIN_TILE_SIZE, cropK, g_win, hist);
  contractOk = !hist->faulted();
  return ok;
}

bool decodeOnce(const Bytes& input, int cropK, uint8_t fill, Rng* noise, Bytes& out) {
  if (noise) for (auto& b : g_win) b = uint8_t(noise->next());
  else memset(g_win, fill, sizeof g_win);
  File src = File::reader(reinterpret_cast<const uint8_t*>(input.data()), input.size());
  File dst = File::writer(&out);
  return rainDecodeToFs(src, dst, RAIN_TILE_SIZE, cropK, g_win);
}

void fuzzRain(const Bytes& input, uint64_t seed) {
  Rng rng(seed);
  // The device passes 8..240; the decoder clamps anything else itself.
  static const int crops[] = {8, 24, 120, 239, 240, 1, 2, 0, -1, 241, 256, 100000};
  const int cropK = crops[rng.chance(4) ? rng.below(12) : rng.below(5)];

  Bytes a, b;
  bool okA = decodeOnce(input, cropK, 0x00, nullptr, a);
  Rng noise(seed ^ 0x5eedull);
  bool okB = decodeOnce(input, cropK, 0xFF, rng.chance(2) ? &noise : nullptr, b);
  if (okA != okB || a != b) {
    fprintf(stderr, "fuzz: rain: the result depends on the window's old contents (ok %d/%d, %zu/%zu bytes)\n",
            okA, okB, a.size(), b.size());
    abort();
  }
  Bytes c;
  bool contractOk = true;
  bool okC = decodeRing(input, cropK, c, contractOk);
  if (!contractOk) {
    fprintf(stderr, "fuzz: rain: the small-memory mode broke the history contract\n");
    abort();
  }
  if (okA != okC || (okA && a != c)) {
    fprintf(stderr, "fuzz: rain: the small-memory mode differs from the full window (ok %d/%d, %zu/%zu bytes)\n",
            okA, okC, a.size(), c.size());
    abort();
  }
  if (!okA) return;
  ++accepted;
  if (a.size() != 240u * 240u) {
    fprintf(stderr, "fuzz: rain: decoded %zu bytes, expected 57600\n", a.size());
    abort();
  }
  for (unsigned char c : a)
    if (c >= RAIN_COLOR_COUNT) {
      fprintf(stderr, "fuzz: rain: colour index %u out of the palette\n", c);
      abort();
    }
}

}  // namespace

#ifdef FUZZ_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  uint64_t seed = size;
  for (size_t i = 0; i < size && i < 64; ++i) seed = seed * 31 + data[i];
  fuzzRain(Bytes(reinterpret_cast<const char*>(data), size), seed);
  return 0;
}
#else
namespace {

// ---- mutations --------------------------------------------------------------
struct Chunk { size_t at; uint32_t len; Bytes type; };   // `at` = offset of the length field

std::vector<Chunk> chunks(const Bytes& png) {
  std::vector<Chunk> out;
  size_t p = 8;
  while (p + 12 <= png.size()) {
    uint32_t len = (uint32_t(uint8_t(png[p])) << 24) | (uint32_t(uint8_t(png[p + 1])) << 16) |
                   (uint32_t(uint8_t(png[p + 2])) << 8) | uint8_t(png[p + 3]);
    out.push_back({p, len, png.substr(p + 4, 4)});
    if (len > png.size() - p - 12) break;   // the rest is one chunk's body
    p += 12 + len;
  }
  return out;
}

// Does chunk c's body (plus `tail` bytes after it) lie inside s? Written
// without `at + len`, which wraps on 32 bits for the extreme lengths the
// mutations write.
bool fits(const Bytes& s, const Chunk& c, size_t tail) {
  return c.at + 8 <= s.size() && c.len <= s.size() - c.at - 8 && tail <= s.size() - c.at - 8 - c.len;
}

void put32(Bytes& s, size_t at, uint32_t v) {
  if (at + 4 > s.size()) return;
  for (int i = 0; i < 4; ++i) s[at + i] = char(v >> (24 - 8 * i));
}

uint32_t extreme(Rng& rng) {
  static const uint32_t v[] = {0, 1, 2, 6, 7, 12, 13, 14, 255, 256, 257, 1025, 32768, 65535, 65536,
                               0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};
  return rng.chance(3) ? rng.next() : v[rng.below(sizeof v / sizeof v[0])];
}

// One mutant of a seed tile. Most of them change the deflate stream inside
// an IDAT chunk (that is where the decoder does its work); the others break
// the container: chunk lengths, IHDR fields, chunk order and count. CRCs are
// left as they are: the decoder does not check them.
Bytes mutate(const Bytes& base, Rng& rng) {
  Bytes s = base;
  std::vector<Chunk> cs = chunks(s);
  std::vector<size_t> idats;
  for (size_t i = 0; i < cs.size(); ++i) if (cs[i].type == "IDAT") idats.push_back(i);
  int rounds = 1 + int(rng.below(4));
  for (int r = 0; r < rounds && !s.empty(); ++r) {
    switch (rng.below(12)) {
      case 0: case 1: case 2: case 3: {   // flip bits inside the compressed data
        if (idats.empty()) break;
        const Chunk& c = cs[idats[rng.below(uint32_t(idats.size()))]];
        if (c.len == 0 || !fits(s, c, 0)) break;
        // Near the zlib header and the first block headers most often.
        uint32_t off = rng.chance(2) ? rng.below(c.len < 64 ? c.len : 64) : rng.below(c.len);
        s[c.at + 8 + off] ^= char(1 << rng.below(8));
        break;
      }
      case 4: {   // overwrite a byte of the compressed data
        if (idats.empty()) break;
        const Chunk& c = cs[idats[rng.below(uint32_t(idats.size()))]];
        if (c.len == 0 || !fits(s, c, 0)) break;
        s[c.at + 8 + rng.below(c.len)] = char(rng.next());
        break;
      }
      case 5: s.resize(rng.below(uint32_t(s.size()))); break;   // truncate
      case 6: {   // an extreme chunk length
        if (cs.empty()) break;
        put32(s, cs[rng.below(uint32_t(cs.size()))].at, extreme(rng));
        break;
      }
      case 7: {   // an IHDR field: size, depth, colour type, compression, filter, interlace
        if (cs.empty() || cs[0].type != "IHDR") break;
        size_t body = cs[0].at + 8;
        switch (rng.below(4)) {
          case 0: put32(s, body + rng.below(2) * 4, extreme(rng)); break;
          default: if (body + 13 <= s.size()) s[body + 8 + rng.below(5)] = char(rng.chance(2) ? rng.below(8) : rng.next()); break;
        }
        break;
      }
      case 8: {   // split an IDAT chunk in two (more, smaller segments)
        if (idats.empty()) break;
        const Chunk c = cs[idats[rng.below(uint32_t(idats.size()))]];
        if (c.len < 2 || !fits(s, c, 4)) break;
        uint32_t cut = 1 + rng.below(c.len - 1);
        Bytes first = s.substr(c.at + 8, cut), second = s.substr(c.at + 8 + cut, c.len - cut);
        Bytes rebuilt;
        Bytes h(4, '\0');
        put32(h, 0, cut);
        rebuilt += h + "IDAT" + first + Bytes(4, '\0');
        put32(h, 0, c.len - cut);
        rebuilt += h + "IDAT" + second + Bytes(4, '\0');
        s.replace(c.at, 12 + c.len, rebuilt);
        break;
      }
      case 9: {   // duplicate or move a chunk (IHDR twice, IDAT before IHDR, ...)
        if (cs.empty()) break;
        const Chunk c = cs[rng.below(uint32_t(cs.size()))];
        if (!fits(s, c, 4)) break;
        Bytes whole = s.substr(c.at, 12 + c.len);
        if (rng.chance(2)) s.erase(c.at, 12 + c.len);
        s.insert(8 + (rng.chance(2) ? 0 : rng.below(uint32_t(s.size() - 7))), whole);
        break;
      }
      case 10: {   // the zlib header
        if (idats.empty()) break;
        const Chunk& c = cs[idats[0]];
        if (c.len >= 2 && fits(s, c, 0)) s[c.at + 8 + rng.below(2)] = char(rng.next());
        break;
      }
      default: {   // anywhere at all
        s[rng.below(uint32_t(s.size()))] ^= char(1 << rng.below(8));
        break;
      }
    }
    cs = chunks(s);
    idats.clear();
    for (size_t i = 0; i < cs.size(); ++i) if (cs[i].type == "IDAT") idats.push_back(i);
  }
  return s;
}

// ---- driver -----------------------------------------------------------------
bool readFile(const std::string& path, Bytes& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

std::vector<std::string> listDir(const std::string& dir) {
  std::vector<std::string> out;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d))
      if (e->d_name[0] != '.') out.push_back(dir + "/" + e->d_name);
    closedir(d);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string crashDir = ".";
uint64_t currentIter = 0;
const Bytes* currentInput = nullptr;

void onDeath() {
  if (!currentInput) return;
  std::string path = crashDir + "/crash-rain-" + std::to_string(currentIter);
  if (FILE* f = fopen(path.c_str(), "wb")) {
    fwrite(currentInput->data(), 1, currentInput->size(), f);
    fclose(f);
    fprintf(stderr, "fuzz: input saved to %s\n", path.c_str());
  }
}

void run(const Bytes& input, uint64_t iter, uint64_t seed) {
  currentIter = iter;
  currentInput = &input;
  fuzzRain(input, seed + iter);
  currentInput = nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  uint64_t iters = 2000, seed = 1;
  if (const char* env = getenv("FUZZ_ITERS")) iters = strtoull(env, nullptr, 10);
  std::string root = ".", repro;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--iters") iters = strtoull(value(), nullptr, 10);
    else if (a == "--seed") seed = strtoull(value(), nullptr, 10);
    else if (a == "--root") root = value();
    else if (a == "--repro") repro = value();
    else if (a == "--crash-dir") crashDir = value();
    else if (a == "--target") { if (std::string(value()) != "rain") return 0; }   // run.sh passes the target through
    else {
      fprintf(stderr, "usage: %s [--iters N] [--seed S] [--root REPO] [--repro FILE] [--crash-dir DIR]\n", argv[0]);
      return 2;
    }
  }
  __sanitizer_set_death_callback(onDeath);

  if (!repro.empty()) {
    Bytes input;
    if (!readFile(repro, input)) {
      fprintf(stderr, "cannot read %s\n", repro.c_str());
      return 2;
    }
    run(input, 0, seed);
    printf("fuzz: rain did not fail on %s\n", repro.c_str());
    return 0;
  }

  std::vector<Bytes> corpus;
  Bytes data;
  for (const auto& p : listDir(root + "/tests/fuzz/corpus/rain"))
    if (p.size() > 4 && p.compare(p.size() - 4, 4, ".png") == 0 && readFile(p, data)) corpus.push_back(data);
  if (corpus.empty()) {
    fprintf(stderr, "fuzz: no seeds for rain (run from the repository root or pass --root)\n");
    return 1;
  }
  uint64_t cases = 0;
  unsigned seedsOk = 0;
  for (const auto& s : corpus) {
    unsigned before = accepted;
    run(s, cases++, seed);
    seedsOk += accepted - before;
  }
  if (seedsOk != corpus.size()) {   // every seed is a valid tile: a refusal is a decoder bug
    fprintf(stderr, "fuzz: rain: only %u of %zu seed tiles decoded\n", seedsOk, corpus.size());
    return 1;
  }
  Rng rng(seed * 1000 + 99);
  for (uint64_t i = 0; i < iters; ++i) run(mutate(corpus[i % corpus.size()], rng), cases++, seed);
  printf("fuzz: %-8s %zu seeds + %llu mutants ok (%u of %llu cases parsed)\n", "rain", corpus.size(),
         (unsigned long long)iters, accepted, (unsigned long long)cases);
  return 0;
}
#endif
