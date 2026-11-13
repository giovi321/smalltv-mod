#include "features/radar/RadarJsonAllocator.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static size_t heapBytes;
static size_t freeHeap() { return heapBytes; }
static bool failAlloc = false, failRealloc = false;
static void* allocate(size_t size) { return failAlloc ? nullptr : malloc(size); }
static void* reallocate(void* p, size_t n) { return failRealloc ? nullptr : realloc(p, n); }

int main() {
  const size_t h = RadarJsonAllocator::overhead();
  RadarJsonAllocator a(128 + h, 4096, freeHeap, allocate, reallocate);
  heapBytes = 4096 + 128 + h;
  void* p = a.allocate(128);
  assert(p && a.used() == 128 + h);
  assert(reinterpret_cast<uintptr_t>(p) % alignof(max_align_t) == 0);
  memset(p, 0x5a, 128);
  assert(!a.allocate(1));  // exact total budget, metadata included
  assert(!a.reallocate(p, 129));
  assert(a.reallocate(p, 64) == p && a.used() == 128 + h);
  a.deallocate(p);
  assert(a.used() == 0);
  heapBytes--;
  assert(!a.allocate(128));  // one byte below exact reserve boundary
  assert(!a.allocate(SIZE_MAX));
  assert(!a.allocate(0));
  heapBytes++;
  failAlloc = true;
  assert(!a.allocate(128) && a.used() == 0);
  failAlloc = false;
  p = a.allocate(64);
  memset(p, 0x5a, 64);
  failRealloc = true;
  assert(!a.reallocate(p, 128));
  assert(a.used() == 64 + h && static_cast<unsigned char*>(p)[63] == 0x5a);
  failRealloc = false;
  heapBytes = 4096 + 128 + h - 1;
  assert(!a.reallocate(p, 128));
  assert(a.used() == 64 + h);
  heapBytes++;
  p = a.reallocate(p, 128);
  assert(p && a.used() == 128 + h && static_cast<unsigned char*>(p)[63] == 0x5a);
  assert(a.reallocate(p, 0) == nullptr && a.used() == 0);
  a.deallocate(nullptr);
  // Exact platform ceilings include allocator headers and remain independent
  // of host ABI alignment or the size of an incoming response.
  for (size_t limit : {size_t(20 * 1024), size_t(64 * 1024)}) {
    RadarJsonAllocator bounded(limit);
    void* exact = bounded.allocate(limit - h);
    assert(exact && bounded.used() == limit);
    assert(!bounded.allocate(1) && !bounded.reallocate(exact, limit - h + 1));
    bounded.deallocate(exact);
    assert(bounded.used() == 0);
  }
  printf("radar allocator boundaries passed\n");
}
