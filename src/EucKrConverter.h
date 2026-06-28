#pragma once

#include <string>

namespace bms_parser {
namespace EucKrConverter {
void BytesToUTF8(const unsigned char *input, size_t size, std::string &result);
}
} // namespace bms_parser
