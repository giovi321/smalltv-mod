#pragma once
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdlib>
#include <limits>

namespace smalltv {
// ArduinoJson's allocator with a ceiling: it never holds more than `limit`
// bytes at once, so a reply cannot make a parse take the heap the network
// driver needs. A request that would go over it fails (a null pointer, which
// ArduinoJson turns into an out-of-memory error), and the block that was
// there stays valid. Each block carries its size in front (8 bytes, so the
// block stays 8-aligned), which is how it knows what a free or a resize gives
// back. A limit of 0 means no limit (tests only).
class BudgetAllocator : public ArduinoJson::Allocator {
 public:
  explicit BudgetAllocator(size_t limit) : limit_(limit ? limit : std::numeric_limits<size_t>::max() / 2) {}
  // The bytes held now, not counting the 8 of each block.
  size_t used() const { return used_; }
  void* allocate(size_t size) override {
    if (size > limit_ - used_) return nullptr;
    char* block = static_cast<char*>(malloc(Header + size));
    if (!block) return nullptr;
    *reinterpret_cast<size_t*>(block) = size;
    used_ += size;
    return block + Header;
  }
  void deallocate(void* ptr) override {
    if (!ptr) return;
    char* block = static_cast<char*>(ptr) - Header;
    used_ -= *reinterpret_cast<size_t*>(block);
    free(block);
  }
  void* reallocate(void* ptr, size_t size) override {
    if (!ptr) return allocate(size);
    char* block = static_cast<char*>(ptr) - Header;
    const size_t old = *reinterpret_cast<size_t*>(block);
    if (size > old && size - old > limit_ - used_) return nullptr;
    char* moved = static_cast<char*>(realloc(block, Header + size));
    if (!moved) return nullptr;
    used_ = used_ - old + size;
    *reinterpret_cast<size_t*>(moved) = size;
    return moved + Header;
  }
 private:
  static constexpr size_t Header = 8;
  const size_t limit_;
  size_t used_ = 0;
};
}
