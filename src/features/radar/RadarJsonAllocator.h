#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// Bound the entire filtered document, including allocator metadata. Keep a
// live heap reserve on ESP8266, even when a response contains many aircraft or
// huge strings. A failed allocation is reported by ArduinoJson as NoMemory.
class RadarJsonAllocator : public ArduinoJson::Allocator {
 public:
  using FreeHeap = size_t (*)();
  using Allocate = void* (*)(size_t);
  using Reallocate = void* (*)(void*, size_t);
  using Deallocate = void (*)(void*);
  RadarJsonAllocator(size_t limit, size_t reserve = 0, FreeHeap freeHeap = nullptr,
                     Allocate alloc = malloc, Reallocate reallocFn = realloc,
                     Deallocate freeFn = free)
      : limit_(limit), reserve_(reserve), freeHeap_(freeHeap),
        alloc_(alloc), realloc_(reallocFn), free_(freeFn) {}

  void* allocate(size_t size) override {
    if (!size || !fits(size, 0)) return nullptr;
    Header* h = static_cast<Header*>(alloc_(sizeof(Header) + size));
    if (!h) return nullptr;
    h->size = size;
    used_ += sizeof(Header) + size;
    return h + 1;
  }
  void deallocate(void* ptr) override {
    if (!ptr) return;
    Header* h = static_cast<Header*>(ptr) - 1;
    used_ -= sizeof(Header) + h->size;
    free_(h);
  }
  void* reallocate(void* ptr, size_t size) override {
    if (!ptr) return allocate(size);
    if (!size) { deallocate(ptr); return nullptr; }
    Header* h = static_cast<Header*>(ptr) - 1;
    size_t oldSize = h->size;
    if (size <= oldSize) {
      // ArduinoJson expects shrinking to succeed; retaining the block is safe
      // and keeps accounting honest without depending on realloc behavior.
      return ptr;
    }
    if (!fits(size, sizeof(Header) + oldSize)) return nullptr;
    Header* replacement = static_cast<Header*>(realloc_(h, sizeof(Header) + size));
    if (!replacement) return nullptr;  // original allocation remains valid
    replacement->size = size;
    used_ += size - oldSize;
    return replacement + 1;
  }
  size_t used() const { return used_; }
  static size_t overhead() { return sizeof(Header); }

 private:
  struct alignas(max_align_t) Header { size_t size; };
  bool fits(size_t size, size_t replaced) const {
    if (size > SIZE_MAX - sizeof(Header)) return false;
    size_t bytes = sizeof(Header) + size;
    size_t retained = used_ - replaced;
    if (retained > limit_ || bytes > limit_ - retained) return false;
    if (freeHeap_) {
      size_t freeBytes = freeHeap_();
      // realloc may allocate a full replacement before freeing the old block.
      if (bytes > freeBytes || reserve_ > freeBytes - bytes) return false;
    }
    return true;
  }
  size_t limit_, reserve_, used_ = 0;
  FreeHeap freeHeap_;
  Allocate alloc_;
  Reallocate realloc_;
  Deallocate free_;
};
