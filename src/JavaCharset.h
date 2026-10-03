#pragma once
#include <cstddef>
#include <string>

namespace bms_parser::detail {
enum class LegacyCharset { EucKr, Ms932 };
// Number of leading bytes unchanged by Java's decode/encode round trip.
size_t legacyRoundTripPrefix(const unsigned char *bytes, size_t size, LegacyCharset charset);
void decodeJavaLegacy(const unsigned char *bytes, size_t size, LegacyCharset charset,
                      std::string &result);
}
