#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bms_parser::detail {

// Inspect complete words only: memcpy supports unaligned input without aliasing
// violations, and the scalar tail never reads beyond the supplied buffer.
// The legacy Shift-JIS table maps '\\', '~', and DEL to different code points.
template <bool PreserveShiftJisMapping = false>
inline size_t asciiPrefixLength(const unsigned char *input, size_t size) {
  constexpr uint64_t highBits = UINT64_C(0x8080808080808080);
  constexpr uint64_t lowBits = UINT64_C(0x0101010101010101);
  size_t index = 0;
  while (size - index >= sizeof(uint64_t)) {
    uint64_t word;
    std::memcpy(&word, input + index, sizeof(word));
    if (word & highBits) {
      break;
    }
    if constexpr (PreserveShiftJisMapping) {
      const uint64_t backslashes = word ^ (lowBits * 0x5c);
      const uint64_t tildeOrDel = (word | lowBits) ^ (lowBits * 0x7f);
      if (((backslashes - lowBits) & ~backslashes & highBits) ||
          ((tildeOrDel - lowBits) & ~tildeOrDel & highBits)) {
        break;
      }
    }
    index += sizeof(word);
  }
  while (index < size && input[index] < 0x80) {
    if constexpr (PreserveShiftJisMapping) {
      if (input[index] == 0x5c || input[index] >= 0x7e) {
        break;
      }
    }
    ++index;
  }
  return index;
}

} // namespace bms_parser::detail
