#pragma once

#include <stdexcept>

inline int runScratchStorageTests() {
  bms_parser::detail::ParserScratchArena arena;
  using Pair = std::pair<const int, std::string>;
  using Allocator = bms_parser::detail::ParserScratchAllocator<Pair>;
  for (int pass = 0; pass < 4; ++pass) {
    arena.reset();
    std::map<int, std::string, std::less<int>, Allocator> entries{Allocator(arena)};
    // Grow across many blocks, use nontrivial values, and then reuse the arena.
    for (int key = 4095; key >= 0; --key)
      entries.emplace(key, std::string(50 + key % 40, char('a' + pass)));
    for (const auto &entry : entries) {
      if (entry.second != std::string(50 + entry.first % 40, char('a' + pass))) {
        std::cerr << "scratch blocks overlap or fail to survive growth\n";
        return 1;
      }
    }
  }
  bool rejectedOverflow = false;
  try {
    Allocator allocator(arena);
    (void)allocator.allocate(std::numeric_limits<size_t>::max());
  } catch (const std::bad_array_new_length &) {
    rejectedOverflow = true;
  }
  ASSERT_EQ(true, rejectedOverflow, "scratch allocator rejects size overflow");
  bms_parser::detail::ParserNodePool pool;
  using NodeAllocator = bms_parser::detail::ParserNodeAllocator<Pair>;
  std::map<int, std::string, std::less<int>, NodeAllocator> first{NodeAllocator(pool)};
  std::map<int, std::string, std::less<int>, NodeAllocator> second{NodeAllocator(pool)};
  for (int pass = 0; pass < 4; ++pass) {
    for (int key = 0; key < 4096; ++key)
      first.emplace(key, std::string(50, 'a'));
    for (int key = 0; key < 4096; key += 2) first.erase(key);
    // Recycle erased nodes into another tree while odd nodes remain live.
    for (int key = 4095; key >= 0; --key)
      second.emplace(key, std::string(50, 'b'));
    for (const auto &[key, value] : first) {
      ASSERT_EQ(1, key % 2, "recycling preserves surviving keys");
      ASSERT_EQ(std::string(50, 'a'), value, "recycling preserves surviving values");
    }
    ASSERT_EQ(size_t{2048}, first.size(), "recycling preserves tree size");
    ASSERT_EQ(size_t{4096}, second.size(), "recycling supports a second tree");
    for (const auto &[key, value] : second) {
      (void)key;
      ASSERT_EQ(std::string(50, 'b'), value, "recycling separates live trees");
    }
    first.clear();
    second.clear();
  }
  bool rejectedValue = false;
  try {
    first.try_emplace(7, std::numeric_limits<size_t>::max(), 'x');
  } catch (const std::length_error &) {
    rejectedValue = true;
  }
  ASSERT_EQ(true, rejectedValue, "failed value construction propagates");
  ASSERT_EQ(true, first.empty(), "failed construction leaves the tree empty");
  first.emplace(7, std::string(50, 'c'));
  second.emplace(7, std::string(50, 'd'));
  ASSERT_EQ(std::string(50, 'c'), first.at(7), "pool survives failed construction");
  ASSERT_EQ(std::string(50, 'd'), second.at(7), "unwinding does not duplicate free nodes");
  // Allocator rebinds with another size and multi-element requests must not
  // consume incompatible free-list nodes. Check their storage under ASan too.
  bms_parser::detail::ParserNodeAllocator<unsigned char> bytesAllocator(pool);
  auto *bytes = bytesAllocator.allocate(4096);
  std::fill_n(bytes, 4096, 0xab);
  auto *small = bytesAllocator.allocate(1);
  *small = 0xcd;
  ASSERT_EQ(0xab, int(bytes[4095]), "fallback allocation remains independent");
  ASSERT_EQ(0xcd, int(*small), "different node size uses independent storage");
  bytesAllocator.deallocate(small, 1);
  bytesAllocator.deallocate(bytes, 4096);
  bool rejectedNodeOverflow = false;
  try {
    NodeAllocator allocator(pool);
    (void)allocator.allocate(std::numeric_limits<size_t>::max());
  } catch (const std::bad_array_new_length &) {
    rejectedNodeOverflow = true;
  }
  ASSERT_EQ(true, rejectedNodeOverflow, "node allocator rejects size overflow");
  return 0;
}
