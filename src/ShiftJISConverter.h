// Decode the MS932 variant used by beatoraja, including malformed-input recovery.

#pragma once
#include <cstddef>
#include <string>
namespace bms_parser {
namespace ShiftJISConverter {
void BytesToUTF8(const unsigned char *input, size_t size, std::string &result);
}

} // namespace bms_parser
