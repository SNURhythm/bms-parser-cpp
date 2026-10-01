#pragma once

#include <cstddef>
#include <simdutf.h>

namespace bms_parser::detail {

inline size_t asciiPrefixLength(const unsigned char *input, size_t size) {
  if (size == 0) {
    return 0;
  }
  // count is the input length on success or the first non-ASCII byte on error.
  return simdutf::validate_ascii_with_errors(
             reinterpret_cast<const char *>(input), size)
      .count;
}

} // namespace bms_parser::detail
