#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace bms_parser::detail {

// Private, per-parse storage for short-lived tree nodes. Reset only after every
// container using the arena has been destroyed. Blocks are reused by subsequent
// measures and freed when the parse returns; no state is shared between workers.
class ParserScratchArena {
  struct Release {
    void operator()(void *memory) const noexcept { ::operator delete(memory); }
  };
  struct Block {
    std::unique_ptr<void, Release> data;
    size_t bytes;
    size_t used = 0;
  };
  std::vector<Block> blocks;
  size_t current = 0;

public:
  void reset() noexcept {
    current = 0;
    for (auto &block : blocks) block.used = 0;
  }

  void releaseExcess(size_t maximumRetainedBytes) noexcept {
    size_t remaining = maximumRetainedBytes;
    for (const auto &block : blocks) {
      if (block.bytes > remaining) {
        std::vector<Block>().swap(blocks);
        current = 0;
        return;
      }
      remaining -= block.bytes;
    }
    reset();
  }

  void *allocate(size_t bytes, size_t alignment) {
    for (;;) {
      if (current == blocks.size()) {
        const size_t capacity = std::max(bytes, size_t{16 * 1024});
        blocks.push_back({std::unique_ptr<void, Release>(::operator new(capacity)),
                          capacity});
      }
      auto &block = blocks[current];
      const size_t padding = (alignment - block.used % alignment) % alignment;
      if (padding <= block.bytes - block.used &&
          bytes <= block.bytes - block.used - padding) {
        auto *result = static_cast<unsigned char *>(block.data.get()) +
                       block.used + padding;
        block.used += padding + bytes;
        return result;
      }
      ++current;
    }
  }
};

template <typename T> struct ParserScratchAllocator {
  using value_type = T;
  ParserScratchArena *arena;

  explicit ParserScratchAllocator(ParserScratchArena &storage) noexcept
      : arena(&storage) {}
  template <typename U>
  ParserScratchAllocator(const ParserScratchAllocator<U> &other) noexcept
      : arena(other.arena) {}

  T *allocate(size_t count) {
    static_assert(alignof(T) <= alignof(std::max_align_t));
    if (count > std::numeric_limits<size_t>::max() / sizeof(T))
      throw std::bad_array_new_length();
    return static_cast<T *>(arena->allocate(count * sizeof(T), alignof(T)));
  }
  void deallocate(T *, size_t) noexcept {}

  template <typename U>
  bool operator==(const ParserScratchAllocator<U> &other) const noexcept {
    return arena == other.arena;
  }
  template <typename U>
  bool operator!=(const ParserScratchAllocator<U> &other) const noexcept {
    return !(*this == other);
  }
};

// One node-size class shared by the parser's lane maps. Erasure returns nodes
// immediately, so memory follows the maximum live set rather than total notes.
// The owning parser must outlive all maps; this pool is never shared by threads.
class ParserNodePool {
  struct FreeNode { FreeNode *next; };
  ParserScratchArena arena;
  FreeNode *free = nullptr;
  size_t nodeBytes = 0;
  size_t nodeAlignment = 0;

public:
  ParserNodePool() = default;
  ParserNodePool(const ParserNodePool &) = delete;
  ParserNodePool &operator=(const ParserNodePool &) = delete;

  void *allocate(size_t bytes, size_t alignment) {
    bytes = std::max(bytes, sizeof(FreeNode));
    alignment = std::max(alignment, alignof(FreeNode));
    if (nodeBytes == 0) {
      nodeBytes = bytes;
      nodeAlignment = alignment;
    }
    // Allocator rebinds of a different size remain valid but are not pooled.
    if (bytes != nodeBytes || alignment != nodeAlignment)
      return ::operator new(bytes);
    if (free) {
      auto *node = free;
      free = node->next;
      return node;
    }
    return arena.allocate(bytes, alignment);
  }

  void deallocate(void *memory, size_t bytes, size_t alignment) noexcept {
    bytes = std::max(bytes, sizeof(FreeNode));
    alignment = std::max(alignment, alignof(FreeNode));
    if (bytes != nodeBytes || alignment != nodeAlignment) {
      ::operator delete(memory);
      return;
    }
    free = new (memory) FreeNode{free};
  }
};

template <typename T> struct ParserNodeAllocator {
  using value_type = T;
  ParserNodePool *pool;

  explicit ParserNodeAllocator(ParserNodePool &storage) noexcept : pool(&storage) {}
  template <typename U>
  ParserNodeAllocator(const ParserNodeAllocator<U> &other) noexcept : pool(other.pool) {}

  T *allocate(size_t count) {
    static_assert(alignof(T) <= alignof(std::max_align_t));
    if (count > std::numeric_limits<size_t>::max() / sizeof(T))
      throw std::bad_array_new_length();
    if (count != 1) return static_cast<T *>(::operator new(count * sizeof(T)));
    return static_cast<T *>(pool->allocate(sizeof(T), alignof(T)));
  }
  void deallocate(T *memory, size_t count) noexcept {
    if (count != 1) ::operator delete(memory);
    else pool->deallocate(memory, sizeof(T), alignof(T));
  }
  template <typename U>
  bool operator==(const ParserNodeAllocator<U> &other) const noexcept {
    return pool == other.pool;
  }
  template <typename U>
  bool operator!=(const ParserNodeAllocator<U> &other) const noexcept {
    return !(*this == other);
  }
};

} // namespace bms_parser::detail
