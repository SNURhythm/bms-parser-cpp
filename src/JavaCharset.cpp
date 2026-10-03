#include "JavaCharset.h"
#include "JavaCharsetData.h"
#include "TextScan.h"

namespace bms_parser::detail {
namespace {
struct Character { unsigned codepoint; size_t consumed; size_t matched; };
Character decode(const unsigned char *bytes, size_t size, LegacyCharset charset) {
  const unsigned a = bytes[0];
  if (a < 0x80) return {a, 1, 1};
  const bool ms932 = charset == LegacyCharset::Ms932;
  if (ms932 && a >= 0xa1 && a <= 0xdf) return {a + 0xfec0, 1, 1};
  const bool lead = ms932 ? ((a >= 0x81 && a <= 0x9f) || (a >= 0xe0 && a <= 0xfc))
                          : (a >= 0xa1 && a <= 0xfe);
  if (!lead || size < 2) return {0xfffd, 1, 0};
  const unsigned b = bytes[1];
  const auto invalidLength = ms932 ? kMs932InvalidPairLength[a] : kEucKrInvalidPairLength[a];
  uint32_t entry = 0xfffd | (invalidLength == 1 ? 0x40000 : 0);
  if (ms932 && b >= 0x40 && b <= 0xfc) {
    const size_t row = a <= 0x9f ? a - 0x81 : a - 0xe0 + 31;
    entry = kMs932Pairs[row * 189 + b - 0x40];
  } else if (!ms932 && b >= 0xa1 && b <= 0xfe) {
    entry = kJavaEucKrPairs[(a - 0xa1) * 94 + b - 0xa1];
  }
  const unsigned cp = entry & 0xffff;
  // Java leaves a separately decodable second byte after a malformed lead.
  return {cp, cp == 0xfffd && b < 0x80 ? size_t{1} : (entry & 0x40000) ? size_t{1} : size_t{2}, (entry >> 16) & 3};
}
}
size_t legacyRoundTripPrefix(const unsigned char *bytes, size_t size, LegacyCharset charset) {
  size_t i = 0;
  while (i < size) {
    i += asciiPrefixLength(bytes + i, size - i);
    if (i == size) break;
    const auto ch = decode(bytes + i, size - i, charset);
    if (ch.matched < ch.consumed) return i + ch.matched;
    i += ch.consumed;
  }
  return i;
}
void decodeJavaLegacy(const unsigned char *bytes, size_t size, LegacyCharset charset,
                      std::string &result) {
  result.clear();
  result.reserve(size);
  for (size_t i = 0; i < size;) {
    const size_t ascii = asciiPrefixLength(bytes + i, size - i);
    result.append(reinterpret_cast<const char *>(bytes + i), ascii);
    i += ascii;
    if (i == size) break;
    const auto ch = decode(bytes + i, size - i, charset);
    i += ch.consumed;
    const auto cp = ch.codepoint;
    if (cp < 0x800) {
      result.push_back(static_cast<char>(0xc0 | (cp >> 6)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
      result.push_back(static_cast<char>(0xe0 | (cp >> 12)));
      result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
  }
}
}
