#pragma once

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
  return 0;
}
