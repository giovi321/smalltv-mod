#pragma once
// The subset of std::vector the theme engine uses (push_back, reserve,
// assign, size, [], iteration), storing elements in blocks of at most about
// 768 bytes instead of one contiguous array. A fragmented ESP8266 heap often
// has several KiB free but no single block above 2 KiB, so a theme's layer
// list -- 180 bytes a layer -- must not need one block for all of them
// (issue #30). Elements never move while the list stays within what
// reserve() asked for, and reserve() also lets the last block be sized
// exactly, so a short list wastes no space.
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace smalltv {
template <class T>
class BlockList {
 public:
  // Elements per block: at least one, however large T is. Every block but the
  // last is full, which is what keeps indexing a division.
  static constexpr size_t PerBlock = sizeof(T) >= 768 ? 1 : 768 / sizeof(T);

  template <class List, class Value>
  class Iterator {
   public:
    Iterator(List* list, size_t index) : list_(list), index_(index) {}
    Value& operator*() const { return (*list_)[index_]; }
    Value* operator->() const { return &(*list_)[index_]; }
    Iterator& operator++() {
      ++index_;
      return *this;
    }
    bool operator==(const Iterator& other) const { return index_ == other.index_; }
    bool operator!=(const Iterator& other) const { return index_ != other.index_; }
   private:
    List* list_;
    size_t index_;
  };
  using iterator = Iterator<BlockList, T>;
  using const_iterator = Iterator<const BlockList, const T>;

  BlockList() = default;
  BlockList(std::initializer_list<T> values) { *this = values; }
  BlockList& operator=(std::initializer_list<T> values) {
    clear();
    reserve(values.size());
    for (const T& value : values) push_back(value);
    return *this;
  }
  // Copies element by element, so the copy keeps exact block sizes too.
  BlockList(const BlockList& other) { *this = other; }
  BlockList(BlockList&& other) noexcept { *this = std::move(other); }
  BlockList& operator=(const BlockList& other) {
    if (this == &other) return *this;
    clear();
    reserve(other.size_);
    for (const T& value : other) push_back(value);
    return *this;
  }
  BlockList& operator=(BlockList&& other) noexcept {
    blocks_ = std::move(other.blocks_);
    size_ = other.size_;
    reserved_ = other.reserved_;
    other.blocks_.clear();
    other.size_ = other.reserved_ = 0;
    return *this;
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  T& operator[](size_t index) { return blocks_[index / PerBlock][index % PerBlock]; }
  const T& operator[](size_t index) const { return blocks_[index / PerBlock][index % PerBlock]; }
  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, size_); }
  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, size_); }

  void reserve(size_t count) {
    if (count <= reserved_) return;
    reserved_ = count;
    blocks_.reserve((count + PerBlock - 1) / PerBlock);
  }
  // By reference, as std::vector does: a by-value T is a temporary on the
  // caller's stack, and the ESP8266's 4 KiB loop stack cannot spare 180
  // bytes for a Layer in the middle of a parse.
  void push_back(const T& value) {
    openBlock();
    blocks_.back().push_back(value);
    ++size_;
  }
  void push_back(T&& value) {
    openBlock();
    blocks_.back().push_back(std::move(value));
    ++size_;
  }
  // Default-constructs the new element in place and returns it, so a caller
  // can fill a large element without a copy of it on the stack.
  T& emplace_back() {
    openBlock();
    blocks_.back().emplace_back();
    ++size_;
    return blocks_.back().back();
  }
  void assign(size_t count, const T& value) {
    clear();
    reserve(count);
    for (size_t i = 0; i < count; ++i) push_back(value);
  }
  void clear() {
    blocks_.clear();
    size_ = reserved_ = 0;
  }
 private:
  // Starts a new block when the last one is full. It holds a full PerBlock,
  // or exactly what is still reserved when that is less.
  void openBlock() {
    if (size_ % PerBlock) return;
    size_t capacity = reserved_ > size_ && reserved_ - size_ < PerBlock ? reserved_ - size_ : PerBlock;
    blocks_.emplace_back();
    blocks_.back().reserve(capacity);
  }
  std::vector<std::vector<T>> blocks_;
  size_t size_ = 0, reserved_ = 0;
};
}
