// The allocator that holds a theme data parse to a ceiling (BudgetAllocator.h):
// what it counts, when it refuses, and that a refused request leaves what was
// already allocated intact.
#include "BudgetAllocator.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
using namespace smalltv;

int main() {
  {
    BudgetAllocator a(1000);
    assert(a.used() == 0);
    void* first = a.allocate(600);
    assert(first && a.used() == 600);
    assert(reinterpret_cast<uintptr_t>(first) % 8 == 0);   // the size in front keeps 8-alignment
    // Over the ceiling together, though each block alone is under it.
    assert(!a.allocate(401) && a.used() == 600);
    void* second = a.allocate(400);   // exactly to the ceiling
    assert(second && a.used() == 1000);
    assert(!a.allocate(1) && a.used() == 1000);
    // A free gives its bytes back: the room can be used again.
    a.deallocate(first);
    assert(a.used() == 400);
    void* third = a.allocate(600);
    assert(third && a.used() == 1000);
    a.deallocate(second);
    a.deallocate(third);
    assert(a.used() == 0);
    a.deallocate(nullptr);   // nothing to free
    assert(a.used() == 0);
  }
  {
    // A resize counts the difference, in both directions.
    BudgetAllocator a(1000);
    char* block = static_cast<char*>(a.allocate(100));
    assert(block);
    memset(block, 'x', 100);
    block = static_cast<char*>(a.reallocate(block, 700));
    assert(block && a.used() == 700 && block[0] == 'x' && block[99] == 'x');   // the content moves with it
    block = static_cast<char*>(a.reallocate(block, 200));
    assert(block && a.used() == 200 && block[99] == 'x');
    // A growth past the ceiling fails and the block stays valid, as it was.
    assert(!a.reallocate(block, 1001) && a.used() == 200 && block[0] == 'x');
    // ... but a growth to just the ceiling works.
    block = static_cast<char*>(a.reallocate(block, 1000));
    assert(block && a.used() == 1000);
    // The room others hold counts: 600 held by another block leaves 400.
    a.deallocate(block);
    char* one = static_cast<char*>(a.allocate(600));
    char* two = static_cast<char*>(a.allocate(100));
    assert(one && two && !a.reallocate(two, 501) && a.used() == 700);
    two = static_cast<char*>(a.reallocate(two, 400));
    assert(two && a.used() == 1000);
    // A resize of nothing is an allocation.
    a.deallocate(one);
    a.deallocate(two);
    void* fresh = a.reallocate(nullptr, 300);
    assert(fresh && a.used() == 300);
    a.deallocate(fresh);
    assert(a.used() == 0);
  }
  {
    // A limit of 0 is no limit.
    BudgetAllocator a(0);
    void* big = a.allocate(1u << 20);
    assert(big && a.used() == (1u << 20));
    a.deallocate(big);
  }
  {
    // Through ArduinoJson: a document that needs more than the ceiling reports
    // out of memory and does not crash; one that fits is whole.
    BudgetAllocator small(64);
    JsonDocument doc(&small);
    for (int i = 0; i < 100; ++i) doc.add(i);
    assert(doc.overflowed());
    BudgetAllocator roomy(1u << 20);
    JsonDocument fits(&roomy);
    for (int i = 0; i < 100; ++i) fits.add(i);
    assert(!fits.overflowed() && fits.size() == 100);
  }
  std::cout << "budget allocator tests passed\n";
  return 0;
}
