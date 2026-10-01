#include "ShiftJISConverter.h"
#include "EncodingC.h"
#include "TextScan.h"
#include <limits>
#include <memory>
#include <stdexcept>

namespace bms_parser {
namespace {

constexpr uint32_t kEncodingInputEmpty = 0;
constexpr const char *kMs932Replacement = "\xef\xbf\xbd";

// Java MS932's assigned double-byte rows. Unassigned rows are not lead bytes
// for its error recovery, even when they are structurally valid Shift-JIS.
bool ms932AssignedLead(unsigned char byte) {
  return (byte >= 0x81 && byte <= 0x84) ||
         (byte >= 0x87 && byte <= 0x9f) ||
         (byte >= 0xe0 && byte <= 0xea) ||
         (byte >= 0xed && byte <= 0xee) ||
         (byte >= 0xf0 && byte <= 0xfc);
}

bool ms932SingleByte(unsigned char byte) {
  return byte < 0x80 || (byte >= 0xa1 && byte <= 0xdf);
}

// Only used for malformed input (or standalone 0x80). Valid double-byte
// mappings still come from encoding_c; recovery matches Java's MS932 decoder.
void decodeMs932WithErrors(detail::Decoder *decoder, const unsigned char *input,
                          size_t size, std::string &result) {
  result.clear();
  size_t index = 0;
  while (index < size) {
    const size_t asciiLength = detail::asciiPrefixLength(input + index, size - index);
    result.append(reinterpret_cast<const char *>(input + index), asciiLength);
    index += asciiLength;
    if (index == size) {
      break;
    }
    const unsigned char byte = input[index];
    if (byte >= 0xa1 && byte <= 0xdf) {
      const uint32_t codePoint = 0xff61 + byte - 0xa1;
      result.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
      result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
      result.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
      ++index;
      continue;
    }
    if (ms932AssignedLead(byte) && index + 1 < size) {
      detail::encoding_new_decoder_without_bom_handling_into(
          detail::SHIFT_JIS_ENCODING, decoder);
      uint8_t output[8];
      size_t inputLength = 2;
      size_t outputLength = sizeof(output);
      const uint32_t status = detail::decoder_decode_to_utf8_without_replacement(
          decoder, input + index, &inputLength, output, &outputLength, true);
      if (status == kEncodingInputEmpty) {
        result.append(reinterpret_cast<const char *>(output), outputLength);
        index += 2;
        continue;
      }
      // A character that can start another valid character must be retried.
      // Otherwise Java consumes both bytes as one unmappable sequence.
      const unsigned char next = input[index + 1];
      if (!ms932AssignedLead(next) && !ms932SingleByte(next)) {
        ++index;
      }
    }
    result.append(kMs932Replacement);
    ++index;
  }
}

} // namespace

void ShiftJISConverter::BytesToUTF8(const unsigned char *input, size_t size,
                                   std::string &result) {
  if (size == 0) {
    result.clear();
    return;
  }
  const std::unique_ptr<detail::Decoder, decltype(&detail::decoder_free)> decoder(
      detail::encoding_new_decoder_without_bom_handling(detail::SHIFT_JIS_ENCODING),
      detail::decoder_free);
  const size_t capacity = detail::decoder_max_utf8_buffer_length(decoder.get(), size);
  if (capacity == std::numeric_limits<size_t>::max()) {
    throw std::length_error("MS932 input is too large to decode");
  }
  result.resize(capacity);
  size_t inputLength = size;
  size_t outputLength = result.size();
  bool hadReplacements = false;
  const uint32_t status = detail::decoder_decode_to_utf8(
      decoder.get(), input, &inputLength,
      reinterpret_cast<uint8_t *>(result.data()), &outputLength, true,
      &hadReplacements);
  if (status != kEncodingInputEmpty || inputLength != size) {
    throw std::runtime_error("MS932 decoder did not consume the input");
  }
  result.resize(outputLength);
  // WHATWG Shift-JIS accepts standalone 0x80 as U+0080; Java MS932 replaces it.
  if (hadReplacements || result.find("\xc2\x80") != std::string::npos) {
    decodeMs932WithErrors(decoder.get(), input, size, result);
  }
}

} // namespace bms_parser
