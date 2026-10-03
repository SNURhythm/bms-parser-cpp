/*
 * Copyright (C) 2024 VioletXF, khoeun03
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Parser.h"
#include "JavaCharset.h"
#include "EucKrConverter.h"
#include "LandmineNote.h"
#include "LongNote.h"
#include "Measure.h"
#include "Note.h"
#include "ShiftJISConverter.h"
#include "TimeLine.h"
#include "TextScan.h"
#include "ParserScratch.h"
#include "ParserNotes.h"
#include <cwctype>
#include <iterator>
#include <random>

#include "SHA256.h"
#include "md5.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <limits>
#include <memory>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

#ifndef BMS_PARSER_VERBOSE
#define BMS_PARSER_VERBOSE 0
#endif

// Java evaluates these double operations separately (no fused multiply-add).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace {

// Header offsets in BMSDecoder are Java UTF-16 code-unit offsets. Preserve
// whole UTF-8 characters; a sliced surrogate has Java's UTF-8 replacement '?'.
size_t utf8CharacterBytes(std::string_view value, size_t index) {
  const auto c = static_cast<unsigned char>(value[index]);
  const size_t width = c >= 0xc2 && c <= 0xdf ? 2 :
                       c >= 0xe0 && c <= 0xef ? 3 :
                       c >= 0xf0 && c <= 0xf4 ? 4 : 1;
  if (index + width > value.size()) return 1;
  for (size_t n = 1; n < width; ++n)
    if ((static_cast<unsigned char>(value[index + n]) & 0xc0) != 0x80) return 1;
  return width;
}

size_t javaStringLength(std::string_view value) {
  size_t length = 0;
  for (size_t i = 0; i < value.size();) {
    const size_t width = utf8CharacterBytes(value, i);
    length += width == 4 ? 2 : 1;
    i += width;
  }
  return length;
}

std::string javaSubstring(std::string_view value, size_t first,
                          size_t count = std::string_view::npos) {
  std::string result;
  size_t unit = 0;
  const size_t last = count == std::string_view::npos ? count : first + count;
  for (size_t i = 0; i < value.size();) {
    const size_t width = utf8CharacterBytes(value, i);
    const size_t units = width == 4 ? 2 : 1;
    if (unit >= first && unit + units <= last) result.append(value.substr(i, width));
    else if (unit < last && unit + units > first) result.push_back('?');
    i += width;
    unit += units;
    if (unit >= last) break;
  }
  return result;
}

std::string javaTrimmedHeaderValue(std::string_view value) {
  while (!value.empty() &&
         static_cast<unsigned char>(value.front()) <= 0x20) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         static_cast<unsigned char>(value.back()) <= 0x20) {
    value.remove_suffix(1);
  }
  return std::string(value);
}

std::string resourcePath(std::string_view value, bool shiftJis) {
  auto path = javaTrimmedHeaderValue(value);
  std::replace(path.begin(), path.end(), '\\', '/');
  // The legacy Shift-JIS converter renders byte 0x5c as a yen sign. In BMS
  // resource paths that byte is the Windows directory separator (MS932).
  // Other decoders can produce a literal Unicode yen sign in a filename.
  if (shiftJis) {
    for (auto pos = path.find("\xc2\xa5"); pos != std::string::npos;
         pos = path.find("\xc2\xa5", pos + 1)) path.replace(pos, 2, "/");
  }
  return path;
}

// Double.parseDouble grammar, including Java type suffixes, underflow and
// non-finite literals. Individual headers apply the reference's value checks.
bool parseJavaDouble(std::string_view value, double &result) {
  auto text = javaTrimmedHeaderValue(value);
  if (text.empty()) return false;
  size_t offset = (text.front() == '+' || text.front() == '-') ? 1 : 0;
  const std::string_view unsignedText(text.data() + offset, text.size() - offset);
  if (unsignedText == "NaN") {
    result = std::numeric_limits<double>::quiet_NaN();
    return true;
  }
  if (unsignedText == "Infinity") {
    result = text.front() == '-' ? -std::numeric_limits<double>::infinity()
                                 : std::numeric_limits<double>::infinity();
    return true;
  }
  const bool hex = offset + 1 < text.size() && text[offset] == '0' &&
                   (text[offset + 1] == 'x' || text[offset + 1] == 'X');
  if (hex) offset += 2;
  const auto digits = [&](bool hexadecimal) {
    const size_t begin = offset;
    while (offset < text.size()) {
      const char c = text[offset];
      if (!(c >= '0' && c <= '9') &&
          !(hexadecimal && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))) break;
      ++offset;
    }
    return offset - begin;
  };
  size_t mantissaDigits = digits(hex);
  if (offset < text.size() && text[offset] == '.') {
    ++offset;
    mantissaDigits += digits(hex);
  }
  if (!mantissaDigits) return false;
  const bool exponent = offset < text.size() &&
      (hex ? text[offset] == 'p' || text[offset] == 'P'
           : text[offset] == 'e' || text[offset] == 'E');
  if (hex && !exponent) return false;
  if (exponent) {
    ++offset;
    if (offset < text.size() && (text[offset] == '+' || text[offset] == '-')) ++offset;
    if (!digits(false)) return false;
  }
  if (offset < text.size() && (text[offset] == 'd' || text[offset] == 'D' ||
                              text[offset] == 'f' || text[offset] == 'F')) {
    if (offset + 1 != text.size()) return false;
    text.pop_back();
  } else if (offset != text.size()) return false;
  result = std::strtod(text.c_str(), nullptr);
  return true;
}

// Integer.parseInt accepts Character.digit(char, radix), including BMP
// decimal digits and full-width Latin letters. Supplementary pairs are invalid.
bool parseInteger(std::string_view value, int &result, int radix = 10) {
  const auto text = javaTrimmedHeaderValue(value);
  if (text.empty()) return false;
  size_t offset = 0;
  const bool negative = text[0] == '-';
  if (text[0] == '+' || negative) ++offset;
  if (offset == text.size()) return false;
  const unsigned int zeros[] = {
    0x0030, 0x0660, 0x06f0, 0x07c0, 0x0966, 0x09e6, 0x0a66,
    0x0ae6, 0x0b66, 0x0be6, 0x0c66, 0x0ce6, 0x0d66, 0x0de6,
    0x0e50, 0x0ed0, 0x0f20, 0x1040, 0x1090, 0x17e0, 0x1810,
    0x1946, 0x19d0, 0x1a80, 0x1a90, 0x1b50, 0x1bb0, 0x1c40,
    0x1c50, 0xa620, 0xa8d0, 0xa900, 0xa9d0, 0xa9f0, 0xaa50,
    0xabf0, 0xff10};
  long long number = 0;
  while (offset < text.size()) {
    unsigned int c = static_cast<unsigned char>(text[offset++]);
    if (c >= 0x80) {
      int trailing = c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xc2 && c < 0xe0 ? 1 : -1;
      if (trailing < 0 || offset + trailing > text.size()) return false;
      c &= trailing == 2 ? 0x0f : 0x1f;
      for (int i = 0; i < trailing; ++i) {
        const auto next = static_cast<unsigned char>(text[offset++]);
        if ((next & 0xc0) != 0x80) return false;
        c = (c << 6) | (next & 0x3f);
      }
    }
    if (radix == 36) {
      // LNOBJ applies String.toUpperCase before Integer.parseInt. These are
      // the BMP expansions that become valid radix-36 Latin letters.
      const std::pair<unsigned int, const char *> expansions[] = {
        {0x00df, "SS"}, {0x0131, "I"}, {0x017f, "S"}, {0xfb00, "FF"},
        {0xfb01, "FI"}, {0xfb02, "FL"}, {0xfb03, "FFI"}, {0xfb04, "FFL"},
        {0xfb05, "ST"}, {0xfb06, "ST"}};
      bool expanded = false;
      for (const auto &[codePoint, letters] : expansions) if (c == codePoint) {
        for (const char *letter = letters; *letter; ++letter) {
          number = number * radix + (*letter - 'A' + 10);
          if (number > (negative ? 2147483648LL : 2147483647LL)) return false;
        }
        expanded = true;
        break;
      }
      if (expanded) continue;
    }
    int digit = -1;
    for (auto zero : zeros) if (c >= zero && c < zero + 10) digit = c - zero;
    if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
    if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
    if (c >= 0xff21 && c <= 0xff3a) digit = c - 0xff21 + 10;
    if (c >= 0xff41 && c <= 0xff5a) digit = c - 0xff41 + 10;
    if (digit < 0 || digit >= radix) return false;
    number = number * radix + digit;
    if (number > (negative ? 2147483648LL : 2147483647LL)) return false;
  }
  result = static_cast<int>(negative ? -number : number);
  return true;
}

bool hasUtf8Bom(const std::vector<unsigned char> &bytes) {
  return bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb &&
         bytes[2] == 0xbf;
}

bool hasUtf16LeBom(const std::vector<unsigned char> &bytes) {
  return bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe;
}

bool hasUtf16BeBom(const std::vector<unsigned char> &bytes) {
  return bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff;
}


void appendUtf8CodePoint(uint32_t codePoint, std::string &result) {
  if (codePoint <= 0x7F) {
    result.push_back(static_cast<char>(codePoint));
  } else if (codePoint <= 0x7FF) {
    result.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else if (codePoint <= 0xFFFF) {
    result.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
    result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    result.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
    result.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
    result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
}

void utf16BytesToUtf8(const std::vector<unsigned char> &bytes, size_t offset,
                      bool littleEndian, std::string &result) {
  constexpr uint32_t kReplacementCodePoint = 0xFFFD;
  result.clear();
  result.reserve(bytes.size());

  auto readUnit = [&](size_t index) -> uint16_t {
    if (littleEndian) {
      return static_cast<uint16_t>(bytes[index] |
                                   (static_cast<uint16_t>(bytes[index + 1])
                                    << 8));
    }
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[index]) << 8) |
                                 bytes[index + 1]);
  };

  size_t index = offset;
  while (index + 1 < bytes.size()) {
    const uint16_t unit = readUnit(index);
    index += 2;

    uint32_t codePoint = unit;
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      if (index + 1 < bytes.size()) {
        const uint16_t low = readUnit(index);
        index += 2; // Java consumes a malformed surrogate pair together.
        if (low >= 0xDC00 && low <= 0xDFFF) {
          codePoint = 0x10000 + (((unit - 0xD800) << 10) | (low - 0xDC00));
        } else {
          codePoint = kReplacementCodePoint;
        }
      } else {
        index = bytes.size(); // Include an odd trailing byte in this error.
        codePoint = kReplacementCodePoint;
      }
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      codePoint = kReplacementCodePoint;
    }

    appendUtf8CodePoint(codePoint, result);
  }
  if (index < bytes.size()) appendUtf8CodePoint(kReplacementCodePoint, result);
}

bool asciiWhitespace(unsigned char c) { return c == ' ' || c == '\t'; }

bool asciiEqualsIgnoreCase(const std::vector<unsigned char> &bytes, size_t pos,
                           size_t end, std::string_view expected) {
  if (pos + expected.size() > end) {
    return false;
  }
  for (size_t i = 0; i < expected.size(); ++i) {
    unsigned char actual = bytes[pos + i];
    if (actual >= 'a' && actual <= 'z') {
      actual = static_cast<unsigned char>(actual - ('a' - 'A'));
    }
    if (actual != static_cast<unsigned char>(expected[i])) {
      return false;
    }
  }
  return true;
}

std::string normalizeCharsetName(const std::vector<unsigned char> &bytes,
                                 size_t start, size_t end) {
  while (start < end && asciiWhitespace(bytes[start])) {
    ++start;
  }
  while (end > start && asciiWhitespace(bytes[end - 1])) {
    --end;
  }
  if (end > start + 1 &&
      ((bytes[start] == '"' && bytes[end - 1] == '"') ||
       (bytes[start] == '\'' && bytes[end - 1] == '\''))) {
    ++start;
    --end;
  }

  std::string normalized;
  for (size_t i = start; i < end; ++i) {
    unsigned char c = bytes[i];
    if (c >= 'a' && c <= 'z') {
      c = static_cast<unsigned char>(c - ('a' - 'A'));
    }
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      normalized.push_back(static_cast<char>(c));
    }
  }
  return normalized;
}

std::string declaredCharset(const std::vector<unsigned char> &bytes) {
  const size_t start = hasUtf8Bom(bytes) ? 3 : 0;
  size_t searchStart = start;
  while (searchStart < bytes.size()) {
    // Search the whole file, including declarations after chart data. Only
    // examine line endings once a possible declaration has been found.
    const auto *found = static_cast<const unsigned char *>(std::memchr(
        bytes.data() + searchStart, '#', bytes.size() - searchStart));
    if (found == nullptr) {
      break;
    }
    const size_t lineStart = static_cast<size_t>(found - bytes.data());
    const size_t pos = lineStart + 1;
    searchStart = pos;
    if (lineStart == start || bytes[lineStart - 1] == '\n' ||
        bytes[lineStart - 1] == '\r') {
      for (std::string_view header : {"CHARSET", "ENCODING"}) {
        if (!asciiEqualsIgnoreCase(bytes, pos, bytes.size(), header)) {
          continue;
        }
        size_t valueStart = pos + header.size();
        if (valueStart < bytes.size() && !asciiWhitespace(bytes[valueStart]) &&
            bytes[valueStart] != '\n' && bytes[valueStart] != '\r') {
          continue;
        }
        size_t lineEnd = valueStart;
        while (lineEnd < bytes.size() && bytes[lineEnd] != '\n' &&
               bytes[lineEnd] != '\r') {
          ++lineEnd;
        }
        while (valueStart < lineEnd && asciiWhitespace(bytes[valueStart])) {
          ++valueStart;
        }
        return normalizeCharsetName(bytes, valueStart, lineEnd);
      }
    }
  }
  return "";
}

bool charsetIsUtf8(const std::string &charset) {
  return charset == "UTF8" || charset == "UTF8BOM";
}

bool charsetIsShiftJis(const std::string &charset) {
  return charset == "SHIFTJIS" || charset == "SJIS" || charset == "CP932" ||
         charset == "MS932" || charset == "WINDOWS31J";
}

bool charsetIsEucKr(const std::string &charset) {
  return charset == "EUCKR" || charset == "KSC5601" ||
         charset == "KSX1001";
}

// Java replaces one malformed UTF-8 subsequence, preserving a following ASCII
// byte. A complete encoded surrogate is a single malformed subsequence.
void javaUtf8(const unsigned char *bytes, size_t size, std::string &result) {
  result.clear();
  result.reserve(size);
  size_t i = 0;
  while (i < size) {
    const size_t ascii = bms_parser::detail::asciiPrefixLength(bytes + i, size - i);
    result.append(reinterpret_cast<const char *>(bytes + i), ascii);
    i += ascii;
    if (i == size) break;
    const unsigned a = bytes[i];
    size_t width = a >= 0xc2 && a <= 0xdf ? 2 :
                   a >= 0xe0 && a <= 0xef ? 3 :
                   a >= 0xf0 && a <= 0xf4 ? 4 : 1;
    size_t consumed = 1;
    unsigned cp = a & (width == 2 ? 0x1f : width == 3 ? 0xf : 7);
    while (consumed < width && i + consumed < size) {
      const unsigned b = bytes[i + consumed];
      if ((b & 0xc0) != 0x80 ||
          (consumed == 1 && ((a == 0xe0 && b < 0xa0) ||
           (a == 0xf0 && b < 0x90) || (a == 0xf4 && b > 0x8f)))) break;
      cp = (cp << 6) | (b & 0x3f);
      ++consumed;
    }
    if (width > 1 && consumed == width && !(cp >= 0xd800 && cp <= 0xdfff))
      result.append(reinterpret_cast<const char *>(bytes + i), width);
    else appendUtf8CodePoint(0xfffd, result);
    i += consumed;
  }
}

void utf32BytesToUtf8(const std::vector<unsigned char> &bytes, size_t offset,
                      bool little, std::string &content) {
  content.clear();
  for (size_t i = offset; i + 3 < bytes.size(); i += 4) {
    uint32_t cp = 0;
    for (int j = 0; j < 4; ++j) cp = (cp << 8) | bytes[i + (little ? 3 - j : j)];
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
    appendUtf8CodePoint(cp, content);
  }
  if ((bytes.size() - offset) % 4) appendUtf8CodePoint(0xfffd, content);
}

// Reencode decoded UTF text to count the unchanged byte prefix, matching the
// reference's > length - 4 tolerance (including a cut character at 64 KiB).
size_t unicodeRoundTripPrefix(const std::vector<unsigned char> &bytes,
                              const std::string &decoded, int width, bool little) {
  size_t matched = 0;
  for (size_t i = 0; i < decoded.size();) {
    unsigned a = static_cast<unsigned char>(decoded[i++]);
    unsigned cp = a;
    int continuation = a < 0x80 ? 0 : a < 0xe0 ? 1 : a < 0xf0 ? 2 : 3;
    if (continuation) cp &= (1u << (6 - continuation)) - 1;
    while (continuation--) cp = (cp << 6) | (static_cast<unsigned char>(decoded[i++]) & 0x3f);
    unsigned char encoded[4];
    size_t count = 0;
    auto unit = [&](unsigned value, int n) {
      for (int j = 0; j < n; ++j) encoded[count++] = static_cast<unsigned char>(value >> (8 * (little ? j : n - 1 - j)));
    };
    if (width == 4) unit(cp, 4);
    else if (cp < 0x10000) unit(cp, 2);
    else { cp -= 0x10000; unit(0xd800 + (cp >> 10), 2); unit(0xdc00 + (cp & 0x3ff), 2); }
    for (size_t j = 0; j < count; ++j) {
      if (matched == bytes.size() || bytes[matched] != encoded[j]) return matched;
      ++matched;
    }
  }
  return matched;
}

// Returns whether the legacy Shift-JIS decoder produced the text.
bool decodeBmsText(const std::vector<unsigned char> &bytes,
                   std::string &content) {
  if (bytes.size() >= 4 &&
      ((bytes[0] == 0xff && bytes[1] == 0xfe && bytes[2] == 0 && bytes[3] == 0) ||
       (bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0xfe && bytes[3] == 0xff))) {
    const bool little = bytes[0] == 0xff;
    utf32BytesToUtf8(bytes, 4, little, content);
    return false;
  }
  if (hasUtf16LeBom(bytes)) {
    utf16BytesToUtf8(bytes, 0, true, content);
    return false;
  }
  if (hasUtf16BeBom(bytes)) {
    utf16BytesToUtf8(bytes, 0, false, content);
    return false;
  }

  const std::string charset = declaredCharset(bytes);
  if (hasUtf8Bom(bytes) || charsetIsUtf8(charset)) {
    javaUtf8(bytes.data(), bytes.size(), content);
    return false;
  }
  if (charsetIsEucKr(charset)) {
    bms_parser::EucKrConverter::BytesToUTF8(
        bytes.data(), bytes.size(), content);
    return false;
  }
  if (charsetIsShiftJis(charset)) {
    bms_parser::ShiftJISConverter::BytesToUTF8(
        bytes.data(), bytes.size(), content);
    return true;
  }
  using bms_parser::detail::LegacyCharset;
  const size_t length = std::min(bytes.size(), size_t{64 * 1024});
  const bool hasMarker = std::find_if(bytes.begin(), bytes.begin() + length,
      [](unsigned char c) { return c == '#' || c == '\r' || c == '\n'; }) != bytes.begin() + length;
  const auto accepted = [length](size_t prefix) { return prefix + 4 > length; };
  for (const auto encoding : {LegacyCharset::EucKr, LegacyCharset::Ms932}) {
    if (hasMarker && accepted(bms_parser::detail::legacyRoundTripPrefix(bytes.data(), length, encoding))) {
      bms_parser::detail::decodeJavaLegacy(bytes.data(), bytes.size(), encoding, content);
      return encoding == LegacyCharset::Ms932;
    }
  }
  std::string probe;
  javaUtf8(bytes.data(), length, probe);
  size_t matched = 0;
  while (matched < length && matched < probe.size() && bytes[matched] == static_cast<unsigned char>(probe[matched])) ++matched;
  if (hasMarker && accepted(matched)) {
    javaUtf8(bytes.data(), bytes.size(), content);
    return false;
  }
  const std::vector<unsigned char> sample(bytes.begin(), bytes.begin() + length);
  for (const int width : {2, 4}) {
    for (const bool little : {false, true}) {
      if (width == 2) utf16BytesToUtf8(sample, 0, little, probe);
      else utf32BytesToUtf8(sample, 0, little, probe);
      if (probe.find_first_of("#\r\n") != std::string::npos &&
          accepted(unicodeRoundTripPrefix(sample, probe, width, little))) {
        if (width == 2) utf16BytesToUtf8(bytes, 0, little, content);
        else utf32BytesToUtf8(bytes, 0, little, content);
        return false;
      }
    }
  }
  bms_parser::detail::decodeJavaLegacy(bytes.data(), bytes.size(), LegacyCharset::Ms932, content);
  return true;
}

bool finitePositive(double value) {
  return std::isfinite(value) && value > 0.0;
}

// Java narrowing conversion saturates infinities/overflow and maps NaN to zero.
long long javaLong(double value) {
  if (std::isnan(value)) return 0;
  if (value >= static_cast<double>(std::numeric_limits<long long>::max()))
    return std::numeric_limits<long long>::max();
  if (value <= static_cast<double>(std::numeric_limits<long long>::min()))
    return std::numeric_limits<long long>::min();
  return static_cast<long long>(value);
}

int guessedBeatsForScale(double scale) {
  if (!finitePositive(scale)) {
    return 4;
  }
  const int beats = static_cast<int>(std::lround(std::min(scale, 4.0) * 4.0));
  return std::clamp(beats, 1, 16);
}

constexpr int EarlyAudibleMeasureLimit = 4;
constexpr int EarlyAudibleWeight = 4;
constexpr int StartingMeasureWeight = 64;
constexpr int StartingMeasureDecayShift = 2;
constexpr int MaxStartingMeasureDecayShift = 6;
constexpr int MinSanePrepMeasureBeats = 2;
constexpr int MaxSanePrepMeasureBeats = 8;
constexpr double MinSanePrepMeasureBpm = 30.0;
constexpr double MaxSanePrepMeasureBpm = 400.0;

bool isSanePrepMeasureBeats(int beats) {
  return beats >= MinSanePrepMeasureBeats && beats <= MaxSanePrepMeasureBeats;
}

double prepMeasureBpm(int beats, long long durationMicros) {
  if (durationMicros <= 0) {
    return 0.0;
  }
  return std::round(
      60000000.0 * static_cast<double>(beats) /
      static_cast<double>(durationMicros));
}

bool isSanePrepMeasureTiming(int beats, long long durationMicros) {
  if (!isSanePrepMeasureBeats(beats)) {
    return false;
  }
  const double effectiveBpm = prepMeasureBpm(beats, durationMicros);
  return std::isfinite(effectiveBpm) &&
         effectiveBpm >= MinSanePrepMeasureBpm &&
         effectiveBpm <= MaxSanePrepMeasureBpm;
}

int startingMeasureWeight(int saneSignalMeasureIndex) {
  if (saneSignalMeasureIndex < 0) {
    return 1;
  }
  const int shift = std::min(saneSignalMeasureIndex * StartingMeasureDecayShift,
                             MaxStartingMeasureDecayShift);
  return std::max(1, StartingMeasureWeight >> shift);
}

int tripleTimelineCandidate(int timelineCount) {
  if (timelineCount >= 6) {
    return 6;
  }
  if (timelineCount == 3) {
    return 3;
  }
  return 0;
}

struct OpeningTripleCandidateTracker {
  enum class State {
    Searching,
    CountingRun,
    Finalized,
  };

  static constexpr int MinTimelineSignalMeasures = 4;

  State state = State::Searching;
  int threeTimelineMeasures = 0;
  int sixTimelineMeasures = 0;
  int signalMeasures = 0;
  int candidate = 0;
  bool pairedTripleLeadIn = false;
  bool previousExplicitSix = false;
  bool secondPreviousExplicitSix = false;

  void observe(int measureIdx, int beats, bool explicitSectionRate,
               bool hasPrepTimingContent, int prepTimingTimelineCount) {
    const bool explicitSix = explicitSectionRate && beats == 6;
    if (state == State::CountingRun &&
        !(explicitSectionRate && hasPrepTimingContent && beats == 3)) {
      finalizeRun();
    }

    if (state == State::Searching && measureIdx > 0) {
      if (!explicitSectionRate && !hasPrepTimingContent) {
        rememberExplicitSix(explicitSix);
        return;
      }
      if (!hasPrepTimingContent) {
        rememberExplicitSix(explicitSix);
        return;
      }
      if (!explicitSectionRate || beats != 3) {
        if (explicitSix && previousExplicitSix) {
          rememberExplicitSix(explicitSix);
          return;
        }
        state = State::Finalized;
        rememberExplicitSix(explicitSix);
        return;
      }

      state = State::CountingRun;
      pairedTripleLeadIn = previousExplicitSix && secondPreviousExplicitSix;
    }

    if (state == State::CountingRun) {
      addTimelineSignal(prepTimingTimelineCount);
    }

    rememberExplicitSix(explicitSix);
  }

  void finalize() {
    if (state == State::CountingRun) {
      finalizeRun();
    }
  }

private:
  void addTimelineSignal(int timelineCount) {
    const int timelineCandidate = tripleTimelineCandidate(timelineCount);
    if (timelineCandidate == 3) {
      ++threeTimelineMeasures;
      ++signalMeasures;
    } else if (timelineCandidate == 6) {
      ++sixTimelineMeasures;
      ++signalMeasures;
    }
  }

  void finalizeRun() {
    if (signalMeasures < MinTimelineSignalMeasures) {
      candidate = 0;
    } else if (pairedTripleLeadIn) {
      candidate = 3;
    } else if (sixTimelineMeasures > threeTimelineMeasures) {
      candidate = 6;
    } else if (threeTimelineMeasures > sixTimelineMeasures) {
      candidate = 3;
    } else {
      candidate = 0;
    }
    state = State::Finalized;
  }

  void rememberExplicitSix(bool explicitSix) {
    secondPreviousExplicitSix = previousExplicitSix;
    previousExplicitSix = explicitSix;
  }
};

template <typename T>
void addDuration(std::map<T, long long> &durations, std::vector<T> &order,
                 T key, long long durationMicros) {
  if constexpr (std::is_floating_point_v<T>) {
    if (!finitePositive(key) || durationMicros <= 0) {
      return;
    }
  } else {
    if (durationMicros <= 0) {
      return;
    }
  }
  if (durations.find(key) == durations.end()) {
    order.push_back(key);
  }
  auto &total = durations[key];
  total += std::min(durationMicros, std::numeric_limits<long long>::max() - total);
}

template <typename T>
T mostPrevalentValue(const std::map<T, long long> &durations,
                     const std::vector<T> &order, T fallback) {
  T best = fallback;
  long long bestDuration = 0;
  for (const T value : order) {
    const auto it = durations.find(value);
    if (it != durations.end() && it->second > bestDuration) {
      best = value;
      bestDuration = it->second;
    }
  }
  return bestDuration > 0 ? best : fallback;
}

void addPrepBeatBpmDuration(
    std::map<int, std::map<double, long long>> &durations,
    std::map<int, std::vector<double>> &order, int beats,
    long long measureDurationMicros, long long weightedDurationMicros) {
  if (!isSanePrepMeasureTiming(beats, measureDurationMicros)) {
    return;
  }
  addDuration(durations[beats], order[beats],
              prepMeasureBpm(beats, measureDurationMicros),
              weightedDurationMicros);
}

double mostPrevalentPrepBeatBpm(
    const std::map<int, std::map<double, long long>> &durations,
    const std::map<int, std::vector<double>> &order, int beats) {
  const auto durationIt = durations.find(beats);
  const auto orderIt = order.find(beats);
  if (durationIt == durations.end() || orderIt == order.end()) {
    return 0.0;
  }
  return mostPrevalentValue(durationIt->second, orderIt->second, 0.0);
}

int guessedBeatsPerMeasure(const std::map<int, long long> &durations,
                           const std::vector<int> &order,
                           int openingCandidate) {
  return openingCandidate != 0
             ? openingCandidate
             : mostPrevalentValue(durations, order, 4);
}

} // namespace

namespace bms_parser {
enum Channel {
  LaneAutoplay = 1,
  SectionRate = 2,
  BpmChange = 3,
  BgaPlay = 4,
  PoorPlay = 6,
  LayerPlay = 7,
  BpmChangeExtend = 8,
  Stop = 9,

  P1KeyBase = 1 * 36 + 1,
  P2KeyBase = 2 * 36 + 1,
  P1InvisibleKeyBase = 3 * 36 + 1,
  P2InvisibleKeyBase = 4 * 36 + 1,
  P1LongKeyBase = 5 * 36 + 1,
  P2LongKeyBase = 6 * 36 + 1,
  P1MineKeyBase = 13 * 36 + 1,
  P2MineKeyBase = 14 * 36 + 1,

  Scroll = 1020,
  Speed = 1033
};

namespace KeyAssign {
const int Beat7[] = {0, 1, 2, 3, 4, 7, -1, 5, 6, 8, 9, 10, 11, 12, 15, -1, 13, 14};
const int Beat4[] = {0, 1, -1, 3, 4, -1, -1, -1, -1,
                     -1, -1, -1, -1, -1, -1, -1, -1, -1};
const int Beat6[] = {0, 1, 2, -1, 4, -1, -1, 5, 6,
                     -1, -1, -1, -1, -1, -1, -1, -1, -1};
const int Beat8[] = {0, 1, 2, 3, 4, 7, -1, 5, 6,
                     -1, -1, -1, -1, -1, -1, -1, -1, -1};
int PopN[] = {0, 1, 2, 3, 4, -1, -1, -1, -1, -1, 5, 6, 7, 8, -1, -1, -1, -1};

const int *Scratchless(int keyMode) {
  switch (keyMode) {
  case 4:
    return Beat4;
  case 6:
    return Beat6;
  case 8:
    return Beat8;
  default:
    return Beat7;
  }
}
} // namespace KeyAssign

constexpr int TempKey = 16;

Parser::Parser() : BpmTable{}, StopLengthTable{}, ScrollTable{}, SpeedTable{} {
  std::random_device seeder;
  Seed = seeder();
}

bool Parser::IsSupportedRandomPrng(const std::string &RandomPrng) {
  return RandomPrng == RandomPrngId;
}

bool Parser::SetRandomPrng(const std::string &RandomPrng) {
  if (!IsSupportedRandomPrng(RandomPrng)) {
    return false;
  }
  this->RandomPrng = RandomPrng;
  return true;
}

const std::string &Parser::GetRandomPrng() const { return RandomPrng; }

void Parser::SetRandomSeed(unsigned int RandomSeed) { Seed = RandomSeed; }

unsigned int Parser::GetRandomSeed() const { return Seed; }

void Parser::SetRandomValues(const std::vector<int> &RandomValues) {
  this->RandomValues = RandomValues;
}

const std::vector<int> &Parser::GetRandomValues() const {
  return RandomValues;
}

int Parser::NoWav = -1;
int Parser::MetronomeWav = -2;

inline bool Parser::MatchHeader(const std::string_view &str,
                                const std::string_view &headerUpper) {
  auto size = headerUpper.length();
  if (str.length() < size) {
    return false;
  }
  for (size_t i = 0; i < size; ++i) {
    // Most charts already use uppercase ASCII. Keep the locale-sensitive
    // fallback for lowercase/non-ASCII (notably 'i' in a Turkish locale).
    if (str[i] == headerUpper[i]) {
      continue;
    }
    if ((str[i] >= 'A' && str[i] <= 'Z') ||
        std::towupper(str[i]) != headerUpper[i]) {
      return false;
    }
  }
  return true;
}

void Parser::Parse(const std::filesystem::path &fpath, Chart **chart,
                   bool addReadyMeasure, bool metaOnly,
                   std::atomic_bool &bCancelled) {
  *chart = nullptr;
  if (bCancelled) return;
  std::ifstream file(fpath, std::ios::binary | std::ios::ate);
  if (!file) return;
  const auto size = file.tellg();
  if (size < 0) return;
  std::vector<unsigned char> bytes(static_cast<size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char *>(bytes.data()), size)) return;
  Parse(bytes, chart, addReadyMeasure, metaOnly, bCancelled, fpath);
  if (*chart) {
    (*chart)->Meta.BmsPath = fpath;
    (*chart)->Meta.Folder = fpath.parent_path();
  }
}

namespace {
bool isPmsSource(const std::filesystem::path &sourcePath) {
  auto extension = sourcePath.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; });
  return extension == ".pms";
}
} // namespace

void Parser::Parse(const std::vector<unsigned char> &bytes, Chart **chart,
                   bool addReadyMeasure, bool metaOnly,
                   std::atomic_bool &bCancelled) {
  Parse(bytes, chart, addReadyMeasure, metaOnly, bCancelled, {});
}

void Parser::Parse(const std::vector<unsigned char> &bytes, Chart **chart,
                   bool addReadyMeasure, bool metaOnly,
                   std::atomic_bool &bCancelled,
                   const std::filesystem::path &sourcePath) {
  ParseInternal(bytes, chart, addReadyMeasure, metaOnly, bCancelled, nullptr,
                isPmsSource(sourcePath));
}

std::optional<ChartScanResult>
Parser::Scan(const std::vector<unsigned char> &bytes,
             std::atomic_bool &bCancelled) {
  return Scan(bytes, bCancelled, {});
}

std::optional<ChartScanResult>
Parser::Scan(const std::vector<unsigned char> &bytes,
             std::atomic_bool &bCancelled,
             const std::filesystem::path &sourcePath) {
  if (bCancelled) return std::nullopt;
  ChartScanResult result;
  Chart *raw = nullptr;
  try {
    ParseInternal(bytes, &raw, false, false, bCancelled, &result,
                  isPmsSource(sourcePath));
  } catch (...) {
    delete raw;
    throw;
  }
  std::unique_ptr<Chart> chart(raw);
  if (bCancelled || chart == nullptr) return std::nullopt;
  result.Meta = std::move(chart->Meta);
  result.HasBga = !chart->BmpTable.empty();
  return result;
}

std::optional<ChartScanResult>
Parser::Scan(const std::filesystem::path &path, std::atomic_bool &bCancelled) {
  if (bCancelled) return std::nullopt;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return std::nullopt;
  const auto size = file.tellg();
  if (size < 0) return std::nullopt;
  std::vector<unsigned char> bytes(static_cast<size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
    return std::nullopt;
  auto result = Scan(bytes, bCancelled, path);
  if (result) {
    result->Meta.BmsPath = path;
    result->Meta.Folder = path.parent_path();
  }
  return result;
}

void Parser::ParseInternal(const std::vector<unsigned char> &bytes, Chart **chart,
                          bool addReadyMeasure, bool metaOnly,
                          std::atomic_bool &bCancelled, ChartScanResult *scan,
                          bool pms) {
  // Scan traverses all full-mode events for identical timing/statistics. Legacy
  // metaOnly keeps its existing shortcuts; neither mode needs lane-note objects.
  const bool materialize = !metaOnly && scan == nullptr;
  BpmTable.clear();
  StopLengthTable.clear();
  ScrollTable.clear();
  SpeedTable.clear();
  UseBase62 = false;
  Lnobj = -1;
#if BMS_PARSER_VERBOSE == 1
  auto startTime = std::chrono::high_resolution_clock::now();
#endif
  *chart = nullptr;
  auto ownedChart = std::make_unique<Chart>();
  auto new_chart = ownedChart.get();
  new_chart->Meta.Player = 0; // BMSModel's unspecified PLAYER value.
  new_chart->Meta.RandomSeed = Seed;
  new_chart->Meta.RandomPrng = RandomPrng;


  if (bCancelled) {
    return;
  }

  std::deque<std::string> normalizedChannelData;
  auto measures =
      std::unordered_map<int, std::vector<std::pair<int, std::string_view>>>();

  // Keep hashing synchronous. Library scans already parallelize at the file
  // level; spawning two extra threads per tiny chart creates heavy thread churn.
#if BMS_PARSER_VERBOSE == 1
  auto md5StartTime = std::chrono::high_resolution_clock::now();
#endif
  MD5 md5;
  md5.update(bytes.data(), bytes.size());
  md5.finalize();
  new_chart->Meta.MD5 = md5.hexdigest();
#if BMS_PARSER_VERBOSE == 1
  std::cout << "Hashing MD5 took "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - md5StartTime)
                   .count()
            << "\n";
#endif
#if BMS_PARSER_VERBOSE == 1
  auto sha256StartTime = std::chrono::high_resolution_clock::now();
#endif
  new_chart->Meta.SHA256 = sha256(bytes);
#if BMS_PARSER_VERBOSE == 1
  std::cout << "Hashing SHA256 took "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - sha256StartTime)
                   .count()
            << "\n";
#endif

  // std::cout<<"file size: "<<size<<std::endl;
  // bytes to std::string
#if BMS_PARSER_VERBOSE == 1
  auto midStartTime = std::chrono::high_resolution_clock::now();
#endif
  std::string content;
  const bool shiftJis = decodeBmsText(bytes, content);
  // BufferedReader.readLine accepts CR, LF and CRLF. Extra empty lines from
  // CRLF are ignored; hashes above still use the original bytes.
  std::replace(content.begin(), content.end(), '\r', '\n');
#if BMS_PARSER_VERBOSE == 1
  std::cout << "BMS text decoding took "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - midStartTime)
                   .count()
            << "\n";
#endif
  // std::wcout<<content<<std::endl;
  std::vector<int> RandomStack;
  std::vector<bool> ConditionalStack;
  const auto isSkipping = [&]() {
    return !ConditionalStack.empty() && ConditionalStack.back();
  };
  // init prng with seed
  std::mt19937_64 Prng(Seed);

  // Views remain valid until all measures are consumed: content is immutable.
  const std::string_view text(content);
  // BASE applies to the entire file, including definitions preceding it.
  // Channel numbers themselves always use base 36.
  for (size_t start = 0; start < text.size();) {
    const auto end = text.find('\n', start);
    const auto line = text.substr(start, end == std::string_view::npos
                                            ? text.size() - start : end - start);
    if (line.size() > 5 && MatchHeader(line, "#BASE") &&
        line[5] == ' ') {
      int base = 36;
      UseBase62 = parseInteger(line.substr(6), base) && base == 62;
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  size_t lineStart = 0;
#if BMS_PARSER_VERBOSE == 1
  midStartTime = std::chrono::high_resolution_clock::now();
#endif
  auto lastMeasure = -1;
  while (lineStart < text.size()) {
    const size_t newline = text.find('\n', lineStart);
    const size_t lineEnd =
        newline == std::string_view::npos ? text.size() : newline;
    std::string_view line = text.substr(lineStart, lineEnd - lineStart);
    lineStart = newline == std::string_view::npos ? text.size() : newline + 1;
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (bCancelled) {
      return;
    }
    // std::cout << line << std::endl;
    if (line.size() > 1 && (line[0] == '%' || line[0] == '@')) {
      const size_t space = line.find(' ');
      if (space != std::string_view::npos && space + 1 < line.size())
        new_chart->Meta.Values[std::string(line.substr(1, space - 1))] = std::string(line.substr(space + 1));
    }
    if (line.size() <= 1 || line[0] != L'#')
      continue;
    if (bCancelled) {
      return;
    }

    const size_t lineUnits = javaStringLength(line);
    const auto directive = [&](std::string_view name) {
      return MatchHeader(line, name);
    };
    if (directive("#RANDOM")) {
      if (lineUnits < 8) return; // Java substring failure aborts decoding.
      int n;
      if (parseInteger(javaSubstring(line, 8), n)) {
        const size_t index = new_chart->Meta.RandomValues.size();
        const int selected = index < RandomValues.size() ? RandomValues[index] :
            static_cast<int>(std::generate_canonical<double, 53>(Prng) * n) + 1;
        new_chart->Meta.RandomValues.push_back(selected);
        RandomStack.push_back(selected);
      }
      continue;
    }
    if (directive("#IF")) {
      if (!RandomStack.empty()) {
        if (lineUnits < 4) return;
        int n;
        if (parseInteger(javaSubstring(line, 4), n))
          ConditionalStack.push_back(RandomStack.back() != n);
      }
      continue;
    }
    if (directive("#ENDIF")) {
      if (!ConditionalStack.empty()) ConditionalStack.pop_back();
      continue;
    }
    if (directive("#ENDRANDOM")) {
      if (!RandomStack.empty()) RandomStack.pop_back();
      continue;
    }
    if (isSkipping()) continue;
    if (directive("#4K")) {
      ParseHeader(new_chart, "4K", "", "");
      continue;
    }
    if (directive("#6K")) {
      ParseHeader(new_chart, "6K", "", "");
      continue;
    }
    if (directive("#8K")) {
      ParseHeader(new_chart, "8K", "", "");
      continue;
    }

    if (lineUnits >= 7 && std::isdigit(static_cast<unsigned char>(line[1])) &&
        std::isdigit(static_cast<unsigned char>(line[2])) &&
        std::isdigit(static_cast<unsigned char>(line[3]))) {
      const int measure =
          (line[1] - '0') * 100 + (line[2] - '0') * 10 + (line[3] - '0');
      lastMeasure = std::max(lastMeasure, measure);
      const std::string_view ch = line.substr(4, 2);
      const int channel = ParseInt(ch, true);
      const auto colon = line.find(':');
      std::string_view value = line.substr(colon == std::string_view::npos ? 0 : colon + 1);
      if (channel != SectionRate && std::any_of(value.begin(), value.end(),
          [](unsigned char c) { return c >= 0x80; })) {
        std::string cells;
        for (size_t i = 0; i < value.size();) {
          const auto c = static_cast<unsigned char>(value[i++]);
          if (c < 0x80) cells.push_back(c);
          else {
            cells.append(c >= 0xf0 ? 2 : 1, '?');
            while (i < value.size() && (static_cast<unsigned char>(value[i]) & 0xc0) == 0x80) ++i;
          }
        }
        normalizedChannelData.push_back(std::move(cells));
        value = normalizedChannelData.back();
      }
      measures[measure].emplace_back(channel, value);
    } else {
      if (MatchHeader(line, "#WAV")) {
        if (!materialize) {
          continue;
        }
        if (lineUnits < 8) {
          continue;
        }
        const auto xx = javaSubstring(line, 4, 2);
        const auto value = javaSubstring(line, 7);
        ParseHeader(new_chart, "WAV", xx, std::string(value), shiftJis);
      } else if (MatchHeader(line, "#BMP")) {
        if (metaOnly) {
          continue;
        }
        if (lineUnits < 8) {
          continue;
        }
        const auto xx = javaSubstring(line, 4, 2);
        const auto value = javaSubstring(line, 7);
        ParseHeader(new_chart, "BMP", xx, std::string(value), shiftJis);
      } else if (MatchHeader(line, "#STOP")) {
        if (lineUnits < 8) {
          continue;
        }
        const auto xx = javaSubstring(line, 5, 2);
        const auto value = javaSubstring(line, 8);
        ParseHeader(new_chart, "STOP", xx, std::string(value));
      } else if (MatchHeader(line, "#BPM")) {
        if (lineUnits <= 4 || (line[4] != ' ' && lineUnits < 7)) return;
        if (lineUnits > 4 && line[4] == ' ') {
          const auto value = javaSubstring(line, 5);
          ParseHeader(new_chart, "BPM", "", std::string(value));
        } else {
          if (lineUnits < 7) {
            continue;
          }
          const auto xx = javaSubstring(line, 4, 2);
          const auto value = javaSubstring(line, 7);
          ParseHeader(new_chart, "BPM", xx, std::string(value));
        }
      } else if (MatchHeader(line, "#SCROLL")) {
        if (lineUnits < 10) {
          continue;
        }
        const auto xx = javaSubstring(line, 7, 2);
        const auto value = javaSubstring(line, 10);
        ParseHeader(new_chart, "SCROLL", xx, std::string(value));
      } else if (MatchHeader(line, "#SPEED")) {
        if (lineUnits < 9) {
          continue;
        }
        const auto xx = javaSubstring(line, 6, 2);
        const auto value = javaSubstring(line, 9);
        ParseHeader(new_chart, "SPEED", xx, std::string(value));
      } else {
        static constexpr std::string_view commands[] = {
          "PLAYER", "GENRE", "TITLE", "SUBTITLE", "ARTIST", "SUBARTIST",
          "PLAYLEVEL", "RANK", "DEFEXRANK", "TOTAL", "VOLWAV", "STAGEFILE", "BACKBMP",
          "PREVIEW", "LNOBJ", "LNMODE", "DIFFICULTY", "BANNER"};
        for (auto command : commands) {
          if (lineUnits > command.size() + 2 &&
              MatchHeader(javaSubstring(line, 1), command)) {
            const auto value = javaTrimmedHeaderValue(javaSubstring(line, command.size() + 2));
            if (command == "LNOBJ" && UseBase62) {
              size_t chars = 0;
              for (unsigned char c : value)
                if ((c & 0xc0) != 0x80) chars += c >= 0xf0 ? 2 : 1;
              if (chars < 2) return;
            }
            ParseHeader(new_chart, command, "", value, shiftJis);
            break;
          }
        }
      }
    }
  }
#if BMS_PARSER_VERBOSE == 1
  std::cout << "Parsing headers took "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - midStartTime)
                   .count()
            << "\n";
#endif
  if (bCancelled) {
    return;
  }
  if (pms) {
    new_chart->Meta.KeyMode = 9;
    new_chart->Meta.IsDP = false;
  }
  if (!pms && !new_chart->Meta.IsScratchlessKeyMode()) {
    const int bases[] = {P1KeyBase, P2KeyBase, P1InvisibleKeyBase, P2InvisibleKeyBase,
                         P1LongKeyBase, P2LongKeyBase, P1MineKeyBase, P2MineKeyBase};
    for (const auto &[measure, rows] : measures) for (const auto &[channel, data] : rows) {
      for (int base : bases) {
        if (channel < base || channel >= base + 9) continue;
        bool active = false;
        for (size_t i = 0; i + 1 < data.size(); i += 2)
          if (ParseInt(data.substr(i, 2)) > 0) { active = true; break; }
        if (!active) break;
        if (channel - base >= 7) {
          if (new_chart->Meta.KeyMode == 5) new_chart->Meta.KeyMode = 7;
          else if (new_chart->Meta.KeyMode == 10) new_chart->Meta.KeyMode = 14;
        }
        if (base == P2KeyBase || base == P2InvisibleKeyBase ||
            base == P2LongKeyBase || base == P2MineKeyBase) {
          if (new_chart->Meta.KeyMode == 5) new_chart->Meta.KeyMode = 10;
          else if (new_chart->Meta.KeyMode == 7) new_chart->Meta.KeyMode = 14;
          new_chart->Meta.IsDP = true;
        }
        break;
      }
    }
  }
  double initialBpm = new_chart->Meta.Bpm;
  if (!(initialBpm > 0) && (lastMeasure >= 0 || !metaOnly)) {
    // A BPM object at the origin can supply the initial tempo too.
    const auto first = measures.find(0);
    if (first != measures.end()) {
      for (const auto &[channel, data] : first->second) {
        if (data.size() < 2) continue;
        const auto cell = data.substr(0, 2);
        if (channel == BpmChange) {
          const int bpm = ParseHex(cell);
          if (bpm > 0) initialBpm = bpm;
        } else if (channel == BpmChangeExtend) {
          const auto found = BpmTable.find(ParseInt(cell));
          if (found != BpmTable.end() && cell != "00") initialBpm = found->second;
        }
      }
    }
  }
  // Explicit metadata-only inspection remains available without timing.
  if (addReadyMeasure && !(initialBpm > 0)) return;
  if (!metaOnly) lastMeasure = std::max(lastMeasure, 0);
  if (addReadyMeasure) {
    for (int measure = lastMeasure; measure >= 0; --measure) {
      const auto found = measures.find(measure);
      if (found != measures.end()) {
        auto events = std::move(found->second);
        measures.erase(found);
        measures[measure + 1] = std::move(events);
      }
    }
    ++lastMeasure;
    measures[0] = std::vector<std::pair<int, std::string_view>>();
    measures[0].emplace_back(LaneAutoplay, "********");
  }

  double timePassed = 0;
  struct JavaTimelineState {
    double position = 0, time = 0, bpm = 0, scroll = 1, speed = 1;
    long long stop = 0;
  };
  std::map<double, JavaTimelineState> javaTimelines;
  javaTimelines.emplace(0, JavaTimelineState{0, 0, new_chart->Meta.Bpm});
  std::map<double, TimeLine *> globalTimelines;
  detail::ParserNotes parsedNotes(NoWav, new_chart->Meta.LnMode);
  auto currentBpm = initialBpm;
  auto minBpm = new_chart->Meta.Bpm;
  auto maxBpm = new_chart->Meta.Bpm;
#if BMS_PARSER_VERBOSE == 1
  midStartTime = std::chrono::high_resolution_clock::now();
#endif
  double measureBeatPosition = 0;
  std::map<double, long long> bpmDurations;
  std::vector<double> bpmOrder;
  std::map<int, long long> weightedBeatDurations;
  std::vector<int> weightedBeatOrder;
  std::map<int, std::map<double, long long>> prepBeatBpmDurations;
  std::map<int, std::vector<double>> prepBeatBpmOrder;
  int saneSignalMeasures = 0;
  int earlyAudibleMeasures = 0;
  OpeningTripleCandidateTracker openingTripleCandidate;
  // Only the unique count is used. Reuse contiguous scratch storage instead
  // of allocating a tree node for every distinct position in every measure.
  std::vector<std::pair<unsigned long long, unsigned long long>> prepTimingPositions;
  detail::ParserScratchArena timelineNodes;
  std::deque<TimeLine> scratchTimelines;
  TimeLine carriedTimeline(0, true);
  for (auto measureIdx = 0; measureIdx <= lastMeasure; ++measureIdx) {
    if (bCancelled) {
      return;
    }
    if (measures.find(measureIdx) == measures.end()) {
      measures[measureIdx] = std::vector<std::pair<int, std::string_view>>();
    }

    // gcd (int, int)
    Measure scratchMeasure;
    auto ownedMeasure = materialize ? std::make_unique<Measure>() : nullptr;
    auto measure = materialize ? ownedMeasure.get() : &scratchMeasure;
    bool explicitSectionRate = false;
    bool measureHasPrepTimingContent = false;
    bool measureHasAudibleContent = false;
    prepTimingPositions.clear();
    // The previous measure's map is already destroyed. Non-retained timelines
    // live in a deque so their addresses stay stable without one allocation per
    // object; full charts retain their existing new/delete ownership contract.
    timelineNodes.reset();

    // NOTE: this should be an ordered map
    struct TimelineEntry {
      TimeLine *timeline = nullptr;
      std::unique_ptr<TimeLine> owned;
      bool hasStop = false;
    };
    using TimelineAllocator =
        detail::ParserScratchAllocator<std::pair<const double, TimelineEntry>>;
    std::map<double, TimelineEntry, std::less<double>, TimelineAllocator>
        timelines{TimelineAllocator(timelineNodes)};
    // Section computes its final rate before placing any channel objects.
    for (const auto &[channel, data] : measures[measureIdx]) {
      double scale;
      if (channel == SectionRate && parseJavaDouble(data, scale) && scale > 0 &&
          std::isfinite(measureBeatPosition + scale) &&
          measureBeatPosition + scale > measureBeatPosition) {
        measure->Scale = scale;
        explicitSectionRate = true;
      }
    }
    const auto ensureTimeline = [&](double position) {
      const double section = measureBeatPosition + position * measure->Scale;
      auto result = timelines.try_emplace(section);
      if (result.second) {
        auto &entry = result.first->second;
        const auto previous = globalTimelines.find(section);
        if (previous != globalTimelines.end()) entry.timeline = previous->second;
        else {
          if (materialize) {
            entry.owned = std::make_unique<TimeLine>(TempKey, false);
            entry.timeline = entry.owned.get();
          } else {
            scratchTimelines.emplace_back(TempKey, true);
            entry.timeline = &scratchTimelines.back();
          }
          globalTimelines[section] = entry.timeline;
        }
      }
      return result.first;
    };
    ensureTimeline(0);
    struct Controls {
      std::optional<double> bpm, stop, scroll, speed;
    };
    std::map<double, Controls> controls;
    std::vector<double> insertionOrder;

    for (auto &pair : measures[measureIdx]) {
      if (bCancelled) {
        break;
      }
      auto channel = pair.first;
      auto &data = pair.second;
      if (channel == SectionRate) continue;

      const bool scratchlessKeyMode =
          new_chart->Meta.IsScratchlessKeyMode();
      const auto *keyAssign =
          pms ? KeyAssign::PopN : scratchlessKeyMode ? KeyAssign::Scratchless(new_chart->Meta.KeyMode)
                             : KeyAssign::Beat7;
      auto laneNumber = 0; // NOTE: This is intentionally set to 0, not -1!
      if (channel >= P1KeyBase && channel < P1KeyBase + 9) {
        laneNumber = keyAssign[channel - P1KeyBase];
        channel = P1KeyBase;
      } else if (channel >= P2KeyBase && channel < P2KeyBase + 9) {
        laneNumber = keyAssign[channel - P2KeyBase + 9];
        channel = P1KeyBase;
      } else if (channel >= P1InvisibleKeyBase &&
                 channel < P1InvisibleKeyBase + 9) {
        laneNumber = keyAssign[channel - P1InvisibleKeyBase];
        channel = P1InvisibleKeyBase;
      } else if (channel >= P2InvisibleKeyBase &&
                 channel < P2InvisibleKeyBase + 9) {
        laneNumber = keyAssign[channel - P2InvisibleKeyBase + 9];
        channel = P1InvisibleKeyBase;
      } else if (channel >= P1LongKeyBase && channel < P1LongKeyBase + 9) {
        laneNumber = keyAssign[channel - P1LongKeyBase];
        channel = P1LongKeyBase;
      } else if (channel >= P2LongKeyBase && channel < P2LongKeyBase + 9) {
        laneNumber = keyAssign[channel - P2LongKeyBase + 9];
        channel = P1LongKeyBase;
      } else if (channel >= P1MineKeyBase && channel < P1MineKeyBase + 9) {
        laneNumber = keyAssign[channel - P1MineKeyBase];
        channel = P1MineKeyBase;
      } else if (channel >= P2MineKeyBase && channel < P2MineKeyBase + 9) {
        laneNumber = keyAssign[channel - P2MineKeyBase + 9];
        channel = P1MineKeyBase;
      }

      if (laneNumber == -1) {
        continue;
      }
      const auto dataCount = data.length() / 2;
      if (channel == PoorPlay) {
        if (scan != nullptr) continue;
        BgaPoorSequence sequence;
        sequence.Frames.reserve(dataCount);
        int singleId = 0;
        for (size_t j = 0; j < dataCount; ++j) {
          if (bCancelled) return;
          int id = ParseInt(data.substr(j * 2, 2));
          if (id < 0) id = 0;
          sequence.Frames.push_back(id);
          if (id != 0) {
            if (singleId == 0) singleId = id;
            else if (singleId != id) singleId = -1;
          }
        }
        if (singleId != -1) sequence.Frames.assign(1, singleId);
        for (int &id : sequence.Frames) {
          if (new_chart->BmpTable.count(id)) RegisterReferencedBmpId(new_chart, id, metaOnly);
          else id = BgaSequenceBlank;
        }
        ensureTimeline(0.0)->second.timeline->BgaPoor = std::move(sequence);
        continue;
      }
      const bool channelCanAnchorPrepTiming =
          channel == LaneAutoplay || channel == BpmChange ||
          channel == BpmChangeExtend || channel == Stop || channel == Scroll ||
          channel == Speed || channel == P1KeyBase || channel == P1InvisibleKeyBase ||
          channel == P1LongKeyBase || channel == P1MineKeyBase;
      const bool channelHasAudibleContent =
          channel == LaneAutoplay || channel == P1KeyBase ||
          channel == P1InvisibleKeyBase || channel == P1LongKeyBase ||
          channel == P1MineKeyBase;
      if (!channelCanAnchorPrepTiming && channel != BgaPlay && channel != LayerPlay)
        continue;
      for (size_t j = 0; j < dataCount; ++j) {
        if (bCancelled) {
          break;
        }
        const char *cell = data.data() + j * 2;
        if (cell[0] == '0' && cell[1] == '0') {
          if (timelines.empty() && j == 0) {
            ensureTimeline(0.0); // add ghost timeline
          }

          continue;
        }
        const std::string_view val(cell, 2);
        const int object = channel == BpmChange ? ParseHex(val) : ParseInt(val);
        // Only the synthetic ready measure may use the internal click token.
        // Authored measures have already shifted by one when it is enabled.
        const bool readyClick = addReadyMeasure && measureIdx == 0 &&
                                channel == LaneAutoplay && val == "**";
        if (object <= 0 && !readyClick) continue;
      if (!scratchlessKeyMode && !pms) {
        if (laneNumber == 5 || laneNumber == 6 || laneNumber == 13 ||
            laneNumber == 14) {
          if (new_chart->Meta.KeyMode == 5) {
            new_chart->Meta.KeyMode = 7;
          } else if (new_chart->Meta.KeyMode == 10) {
            new_chart->Meta.KeyMode = 14;
          }
        }
        if (laneNumber >= 8) {
          if (new_chart->Meta.KeyMode == 7) {
            new_chart->Meta.KeyMode = 14;
          } else if (new_chart->Meta.KeyMode == 5) {
            new_chart->Meta.KeyMode = 10;
          }
          new_chart->Meta.IsDP = true;
        }
      }


        const auto g = Gcd(j, dataCount);
        // ReSharper disable PossibleLossOfFraction

        const auto positionNumerator = j / g;
        const auto positionDenominator = dataCount / g;
        const auto position =
            static_cast<double>(positionNumerator) /
            static_cast<double>(positionDenominator);

        if (channelCanAnchorPrepTiming) {
          measureHasPrepTimingContent = true;
          prepTimingPositions.emplace_back(positionNumerator, positionDenominator);
        }
        if (channelHasAudibleContent) {
          measureHasAudibleContent = true;
        }

        if (channel == LaneAutoplay || channel == BgaPlay || channel == LayerPlay ||
            channel == P1KeyBase || channel == P1LongKeyBase ||
            channel == P1MineKeyBase || channel == P1InvisibleKeyBase)
          insertionOrder.push_back(position);
        if ((channel == BpmChangeExtend && !BpmTable.count(object)) ||
            (channel == Stop && !StopLengthTable.count(object)) ||
            (channel == Scroll && !ScrollTable.count(object)) ||
            (channel == Speed && !SpeedTable.count(object))) continue;
        auto entry = ensureTimeline(position);
        auto timeline = entry->second.timeline;
        if (channel == LaneAutoplay || channel == P1InvisibleKeyBase) {
          if (metaOnly) {
            break;
          }
        }
        switch (channel) {
        case LaneAutoplay:
          if (!materialize) break;
          if (val == "**") {
            timeline->AddBackgroundNote(new Note{MetronomeWav});
            break;
          }
          if (ParseInt(val) != 0) {
            const int wavId = ToWaveId(new_chart, val, metaOnly);
            RegisterReferencedWaveId(new_chart, wavId);
            auto bgNote = new Note{wavId};
            timeline->AddBackgroundNote(bgNote);
          }

          break;
        case BpmChange: {
          int bpm = ParseHex(val);
          timeline->Bpm = static_cast<double>(bpm);
          controls[position].bpm = bpm;
          // std::cout << "BPM_CHANGE: " << timeline->Bpm << ", on measure " <<
          // measureIdx << std::endl; Debug.Log($"BPM_CHANGE: {timeline.Bpm}, on
          // measure {measureIdx}");
          timeline->BpmChange = true;
          break;
        }
        case BgaPlay: {
          const int bmpId = ParseInt(val);
          RegisterReferencedBmpId(new_chart, bmpId, metaOnly);
          timeline->BgaBase = bmpId;
          break;
        }
        case LayerPlay: {
          const int bmpId = ParseInt(val);
          RegisterReferencedBmpId(new_chart, bmpId, metaOnly);
          timeline->BgaLayer = bmpId;
          break;
        }
        case BpmChangeExtend: {
          const auto id = ParseInt(val);
          // std::cout << "BPM_CHANGE_EXTEND: " << id << ", on measure " <<
          // measureIdx << std::endl;
          if (!CheckResourceIdRange(id)) {
            // UE_LOG(LogTemp, Warning, TEXT("Invalid BPM id: %s"), *val);
            break;
          }
          if (const auto bpm = BpmTable.find(id); bpm != BpmTable.end()) {
            timeline->Bpm = bpm->second;
            controls[position].bpm = bpm->second;
          } else {
            break;
          }
          // Debug.Log($"BPM_CHANGE_EXTEND: {timeline.Bpm}, on measure
          // {measureIdx}, {val}");
          timeline->BpmChange = true;
          break;
        }
        case Scroll: {
          const auto id = ParseInt(val);
          if (!CheckResourceIdRange(id)) {
            // UE_LOG(LogTemp, Warning, TEXT("Invalid Scroll id: %s"), *val);
            break;
          }
          if (const auto scroll = ScrollTable.find(id); scroll != ScrollTable.end()) {
            timeline->Scroll = scroll->second;
            controls[position].scroll = scroll->second;
            timeline->ScrollChange = true;
          }
          // Debug.Log($"SCROLL: {timeline.Scroll}, on measure {measureIdx}");
          break;
        }
        case Speed: {
          const auto id = ParseInt(val);
          const auto speed = SpeedTable.find(id);
          if (speed != SpeedTable.end()) {
            timeline->Speed = speed->second;
            controls[position].speed = speed->second;
            timeline->HasSpeedObject = true;
          }
          break;
        }
        case Stop: {
          const auto id = ParseInt(val);
          if (!CheckResourceIdRange(id)) {
            // UE_LOG(LogTemp, Warning, TEXT("Invalid StopLength id: %s"),
            // *val);
            break;
          }
          if (const auto stop = StopLengthTable.find(id); stop != StopLengthTable.end()) {
            timeline->StopLength = stop->second;
            controls[position].stop = stop->second;
            entry->second.hasStop = true;
          }
          // Debug.Log($"STOP: {timeline.StopLength}, on measure {measureIdx}");
          break;
        }
        case P1KeyBase: {
          const int wav = ToWaveId(new_chart, val, !materialize);
          parsedNotes.normal(laneNumber, entry->first, wav,
                             object == Lnobj, materialize ? timeline : nullptr);
          break;
        }
        case P1InvisibleKeyBase: {
          if (!materialize) break;
          const int wav = ToWaveId(new_chart, val, false);
          timeline->SetInvisibleNote(laneNumber, new Note(wav));
          break;
        }
        case P1LongKeyBase: {
          const int wav = ToWaveId(new_chart, val, !materialize);
          parsedNotes.longNote(laneNumber, entry->first, wav,
                               materialize ? timeline : nullptr);
          break;
        }
        case P1MineKeyBase: {
          const float damage = static_cast<float>(ParseInt(val, true));
          const int wav = new_chart->WavTable.count(0) ? 0 : NoWav;
          parsedNotes.mine(laneNumber, entry->first, wav, damage,
                           materialize ? timeline : nullptr);
          break;
        }
        default:
          break;
        }
      }
    }

    if (bCancelled) return;

    // Section.makeTimeLines inserts the bar line, then sorted controls, then
    // channel objects in source order. Each new time uses its cached predecessor.
    const auto insertJavaTimeline = [&](double section) -> JavaTimelineState & {
      auto found = javaTimelines.find(section);
      if (found != javaTimelines.end()) return found->second;
      auto lower = javaTimelines.lower_bound(section);
      auto state = std::prev(lower)->second;
      state.time = state.time + static_cast<double>(state.stop) +
                   (240000000.0 * (section - state.position)) / state.bpm;
      state.position = section;
      state.stop = 0;
      return javaTimelines.emplace(section, state).first->second;
    };
    insertJavaTimeline(measureBeatPosition);
    for (const auto &[position, control] : controls) {
      const double section = measureBeatPosition + position * measure->Scale;
      auto &state = insertJavaTimeline(section);
      if (control.speed) state.speed = *control.speed;
      if (control.scroll) state.scroll = *control.scroll;
      if (control.bpm) state.bpm = *control.bpm;
      if (control.stop)
        state.stop = javaLong(240000000.0 * (*control.stop / 192.0) / state.bpm);
    }
    for (double position : insertionOrder)
      insertJavaTimeline(measureBeatPosition + position * measure->Scale);
    for (const auto &[section, entry] : timelines) {
      const auto &state = javaTimelines.at(section);
      auto *tl = entry.timeline;
      tl->Timing = javaLong(state.time);
      tl->BeatPosition = section;
      tl->Bpm = state.bpm;
      tl->Scroll = state.scroll;
      tl->Speed = state.speed;
      tl->ParsedStopDuration = state.stop;
    }
    if (measureIdx == 0 && currentBpm == 0)
      currentBpm = javaTimelines.begin()->second.bpm;
    if (measureIdx == 0 && javaTimelines.begin()->second.bpm == 0) return;
    parsedNotes.setTiming(measureBeatPosition, [&](double section) {
      return javaLong(javaTimelines.at(section).time);
    });
    auto lastPosition = 0.0;

    measure->Timing = javaLong(timePassed);

    for (auto &pair : timelines) {
      if (bCancelled) {
        break;
      }
      const auto position = (pair.first - measureBeatPosition) / measure->Scale;
      const auto timeline = pair.second.timeline;

      // Debug.Log($"measure: {measureIdx}, position: {position}, lastPosition:
      // {lastPosition} bpm: {bpm} scale: {measure.scale} interval: {240 * 1000
      // * 1000 * (position - lastPosition) * measure.scale / bpm}");
      const auto interval =
          240000000.0 * (position - lastPosition) * measure->Scale / currentBpm;
      addDuration(bpmDurations, bpmOrder, currentBpm,
                  javaLong(std::round(interval)));
      timePassed += interval;
      currentBpm = timeline->Bpm;
      // Debug.Log($"measure: {measureIdx}, position: {position}, lastPosition:
      // {lastPosition}, bpm: {currentBpm} scale: {measure.Scale} interval:
      // {interval} stop: {timeline.GetStopDuration()}");

      const auto stopDuration = javaLong(timeline->GetStopDuration());
      addDuration(bpmDurations, bpmOrder, timeline->Bpm,
                  javaLong(stopDuration));
      timePassed += stopDuration;
      if (scan != nullptr) {
        scan->HasBpmStop = scan->HasBpmStop || timeline->StopLength > 0;
        scan->HasScrollChange = scan->HasScrollChange || timeline->Scroll != 1.0;
      }
      if (materialize && pair.second.owned) {
        measure->TimeLines.push_back(pair.second.owned.release());
      }

      lastPosition = position;
    }

    if (bCancelled) return;

    if (!materialize) timelines.clear();
    if (materialize && !measure->TimeLines.empty())
      measure->TimeLines.front()->IsFirstInMeasure = true;
    const auto finalInterval =
        240000000.0 * (1 - lastPosition) * measure->Scale / currentBpm;
    addDuration(bpmDurations, bpmOrder, currentBpm,
                javaLong(std::round(finalInterval)));
    timePassed += finalInterval;
    const int measureBeats = guessedBeatsForScale(measure->Scale);
    const long long measureDuration =
        javaLong(timePassed) - measure->Timing;
    std::sort(prepTimingPositions.begin(), prepTimingPositions.end());
    prepTimingPositions.erase(
        std::unique(prepTimingPositions.begin(), prepTimingPositions.end()),
        prepTimingPositions.end());
    if (isSanePrepMeasureTiming(measureBeats, measureDuration)) {
      const bool measureHasBeatGuessSignal =
          explicitSectionRate || measureHasPrepTimingContent;
      const bool measureCanUseStartingWeight =
          measureIdx > 0 && measureHasBeatGuessSignal;
      const int measureWeight = measureCanUseStartingWeight
                                    ? startingMeasureWeight(saneSignalMeasures++)
                                    : 1;
      const long long weightedMeasureDuration =
          measureDuration * static_cast<long long>(measureWeight);
      addDuration(weightedBeatDurations, weightedBeatOrder, measureBeats,
                  weightedMeasureDuration);
      addPrepBeatBpmDuration(prepBeatBpmDurations, prepBeatBpmOrder,
                             measureBeats, measureDuration,
                             weightedMeasureDuration);
      const int timelineBeatCandidate =
          explicitSectionRate && measureBeats == 3
              ? tripleTimelineCandidate(
                    static_cast<int>(prepTimingPositions.size()))
              : 0;
      if (timelineBeatCandidate != 0 &&
          timelineBeatCandidate != measureBeats) {
        addPrepBeatBpmDuration(prepBeatBpmDurations, prepBeatBpmOrder,
                               timelineBeatCandidate, measureDuration,
                               weightedMeasureDuration);
      }
      if (measureHasAudibleContent &&
          earlyAudibleMeasures < EarlyAudibleMeasureLimit) {
        const long long earlyAudibleDuration =
            measureDuration * (EarlyAudibleWeight - 1);
        addDuration(weightedBeatDurations, weightedBeatOrder, measureBeats,
                    earlyAudibleDuration);
        addPrepBeatBpmDuration(prepBeatBpmDurations, prepBeatBpmOrder,
                               measureBeats, measureDuration,
                               earlyAudibleDuration);
        if (timelineBeatCandidate != 0 &&
            timelineBeatCandidate != measureBeats) {
          addPrepBeatBpmDuration(prepBeatBpmDurations, prepBeatBpmOrder,
                                 timelineBeatCandidate, measureDuration,
                                 earlyAudibleDuration);
        }
        ++earlyAudibleMeasures;
      }
    }
    openingTripleCandidate.observe(
        measureIdx, measureBeats, explicitSectionRate,
        measureHasPrepTimingContent,
        static_cast<int>(prepTimingPositions.size()));
    measureBeatPosition += measure->Scale;
    parsedNotes.advance(*new_chart, materialize, measureBeatPosition);

    // Future rows cannot address an earlier measure. Keep a timing
    // predecessor and any timeline rounded onto the next bar, not the chart.
    auto futureState = javaTimelines.lower_bound(measureBeatPosition);
    for (auto it = javaTimelines.begin(); it != futureState; ++it) {
      minBpm = std::min(minBpm, it->second.bpm);
      maxBpm = std::max(maxBpm, it->second.bpm);
    }
    if (futureState != javaTimelines.begin())
      javaTimelines.erase(javaTimelines.begin(), std::prev(futureState));
    globalTimelines.erase(globalTimelines.begin(),
                          globalTimelines.lower_bound(measureBeatPosition));
    if (!materialize) {
      if (!globalTimelines.empty()) {
        auto &timeline = globalTimelines.begin()->second;
        if (timeline != &carriedTimeline) carriedTimeline = *timeline;
        timeline = &carriedTimeline;
      }
      scratchTimelines.clear();
    }
    if (materialize) {
      new_chart->Measures.push_back(ownedMeasure.release());
    }
  }
#if BMS_PARSER_VERBOSE == 1
  std::cout << "Reading data field took "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - midStartTime)
                   .count()
            << "\n";
#endif
  parsedNotes.finish(*new_chart, materialize);
  for (const auto &[section, state] : javaTimelines) {
    minBpm = std::min(minBpm, state.bpm);
    maxBpm = std::max(maxBpm, state.bpm);
  }
  if (materialize) {
    new_chart->ReferencedWavTable.clear();
    for (const auto *measure : new_chart->Measures) {
      for (const auto *timeline : measure->TimeLines) {
        for (const auto *notes : {&timeline->Notes, &timeline->InvisibleNotes,
                                   &timeline->BackgroundNotes}) {
          for (const auto *note : *notes)
            if (note) RegisterReferencedWaveId(new_chart, note->Wav);
        }
      }
    }
  }
  new_chart->Meta.TotalLength = javaLong(timePassed);
  new_chart->Meta.MinBpm = minBpm;
  new_chart->Meta.MaxBpm = maxBpm;
  new_chart->Meta.MostPrevalentBpm =
      mostPrevalentValue(bpmDurations, bpmOrder, new_chart->Meta.Bpm);
  openingTripleCandidate.finalize();
  const int guessedBeats =
      guessedBeatsPerMeasure(weightedBeatDurations, weightedBeatOrder,
                             openingTripleCandidate.candidate);
  new_chart->Meta.GuessedBeatsPerMeasure = guessedBeats;
  new_chart->Meta.GuessedBeatBpm = mostPrevalentPrepBeatBpm(
      prepBeatBpmDurations, prepBeatBpmOrder, guessedBeats);
  if (bCancelled) return;
  *chart = ownedChart.release();

#if BMS_PARSER_VERBOSE == 1
  std::cout << "Total parsing time: "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - startTime)
                   .count()
            << "\n";
#endif
}

void Parser::ParseHeader(Chart *Chart, std::string_view cmd,
                         std::string_view Xx, const std::string &Value,
                         bool shiftJis) {
  // Debug.Log($"cmd: {cmd}, xx: {xx} isXXNull: {xx == null}, value: {value}");
  const auto commandIs = [&](std::string_view name) {
    return cmd.size() == name.size() && MatchHeader(cmd, name);
  };
  // BASE 62
  if (commandIs("BASE")) {
    return; // Resolved before decoding any resource IDs.
  } else if (commandIs("4K")) {
    Chart->Meta.KeyMode = 4;
    Chart->Meta.IsDP = false;
  } else if (commandIs("6K")) {
    Chart->Meta.KeyMode = 6;
    Chart->Meta.IsDP = false;
  } else if (commandIs("8K")) {
    Chart->Meta.KeyMode = 8;
    Chart->Meta.IsDP = false;
  } else if (commandIs("PLAYER")) {
    int player;
    if (parseInteger(Value, player) && player >= 1 && player < 3)
      Chart->Meta.Player = player;
  } else if (commandIs("GENRE")) {
    Chart->Meta.Genre = javaTrimmedHeaderValue(Value);
  } else if (commandIs("TITLE")) {
    Chart->Meta.Title = javaTrimmedHeaderValue(Value);
  } else if (commandIs("SUBTITLE")) {
    Chart->Meta.SubTitle = javaTrimmedHeaderValue(Value);
  } else if (commandIs("ARTIST")) {
    Chart->Meta.Artist = javaTrimmedHeaderValue(Value);
  } else if (commandIs("SUBARTIST")) {
    Chart->Meta.SubArtist = javaTrimmedHeaderValue(Value);
  } else if (commandIs("DIFFICULTY")) {
    int difficulty;
    if (parseInteger(Value, difficulty)) Chart->Meta.Difficulty = difficulty;
  } else if (commandIs("BPM")) {
    double bpm;
    if (!parseJavaDouble(Value, bpm) || !(bpm > 0)) return;
    if (Xx.empty()) {
      // chart initial bpm
      Chart->Meta.Bpm = bpm;
      // std::cout << "MainBPM: " << Chart->Meta.Bpm << std::endl;
    } else {
      // Debug.Log($"BPM: {DecodeBase36(xx)} = {double.Parse(value)}");
      int id = ParseInt(Xx);
      if (!CheckResourceIdRange(id)) {
        // UE_LOG(LogTemp, Warning, TEXT("Invalid BPM id: %s"), *Xx);
        return;
      }
      BpmTable[id] = bpm;
    }
  } else if (commandIs("STOP")) {
    if (Value.empty() || Xx.empty()) {
      return; // TODO: handle this
    }
    int id = ParseInt(Xx);
    if (!CheckResourceIdRange(id)) {
      // UE_LOG(LogTemp, Warning, TEXT("Invalid STOP id: %s"), *Xx);
      return;
    }
    double stop;
    if (parseJavaDouble(Value, stop)) StopLengthTable[id] = std::abs(stop);
  } else if (commandIs("MIDIFILE")) {
    // TODO: handle this
  } else if (commandIs("VIDEOFILE")) {
  } else if (commandIs("PLAYLEVEL")) {
    Chart->Meta.PlayLevelText = javaTrimmedHeaderValue(Value);
    double level;
    if (parseJavaDouble(Value, level)) Chart->Meta.PlayLevel = level;
  } else if (commandIs("RANK") || commandIs("DEFEXRANK")) {
    const bool extended = commandIs("DEFEXRANK");
    int rank;
    if (parseInteger(Value, rank) &&
        (extended ? rank > 0 : rank >= 0 && rank < 5)) {
      Chart->Meta.Rank = rank;
      Chart->Meta.RankType = extended ? JudgeRankType::DefExRank : JudgeRankType::BmsRank;
    }
  } else if (commandIs("TOTAL")) {
    double total;
    if (parseJavaDouble(Value, total) && total > 0) {
      Chart->Meta.Total = total;
      Chart->Meta.HasTotal = true;
    }
  } else if (commandIs("VOLWAV")) {
    int volume;
    if (parseInteger(Value, volume)) Chart->Meta.VolWav = volume;
  } else if (commandIs("STAGEFILE")) {
    Chart->Meta.StageFile = utf8_to_path_t(resourcePath(Value, shiftJis));
  } else if (commandIs("BANNER")) {
    Chart->Meta.Banner = utf8_to_path_t(resourcePath(Value, shiftJis));
  } else if (commandIs("BACKBMP")) {
    Chart->Meta.BackBmp = utf8_to_path_t(resourcePath(Value, shiftJis));
  } else if (commandIs("PREVIEW")) {
    Chart->Meta.Preview = utf8_to_path_t(resourcePath(Value, shiftJis));
  } else if (commandIs("WAV")) {
    const auto path = resourcePath(Value, shiftJis);
    if (Xx.empty()) {
      // UE_LOG(LogTemp, Warning, TEXT("WAV command requires two arguments"));
      return;
    }
    int id = ParseInt(Xx);
    if (!CheckResourceIdRange(id)) {
      // UE_LOG(LogTemp, Warning, TEXT("Invalid WAV id: %s"), *Xx);
      return;
    }
    Chart->WavTable[id] = path;
    if (Chart->ReferencedWavTable.find(id) !=
        Chart->ReferencedWavTable.end()) {
      Chart->ReferencedWavTable[id] = path;
    }
  } else if (commandIs("BMP")) {
    const auto path = resourcePath(Value, shiftJis);
    if (Xx.empty()) {
      // UE_LOG(LogTemp, Warning, TEXT("BMP command requires two arguments"));
      return;
    }
    int id = ParseInt(Xx);
    if (!CheckResourceIdRange(id)) {
      // UE_LOG(LogTemp, Warning, TEXT("Invalid BMP id: %s"), *Xx);
      return;
    }
    Chart->BmpTable[id] = path;
    if (Chart->ReferencedBmpTable.find(id) !=
        Chart->ReferencedBmpTable.end()) {
      Chart->ReferencedBmpTable[id] = path;
    }
    if (Xx == "00") {
      Chart->ReferencedBmpTable[id] = path;
      Chart->Meta.BgaPoorDefault = true;
    }
  } else if (commandIs("LNOBJ")) {
    const auto text = javaTrimmedHeaderValue(Value);
    if (UseBase62) {
      if (text.size() >= 2) {
        const int id = ParseInt(std::string_view(text).substr(0, 2));
        if (id >= 0) Lnobj = id;
      }
    } else {
      int id;
      if (parseInteger(text, id, 36)) Lnobj = id;
    }
  } else if (commandIs("LNMODE")) {
    int mode;
    if (parseInteger(Value, mode) && mode >= 0 && mode <= 3)
      Chart->Meta.LnMode = mode;
  } else if (commandIs("SCROLL")) {
    auto xx = ParseInt(Xx);
    double value;
    if (CheckResourceIdRange(xx) && parseJavaDouble(Value, value))
      ScrollTable[xx] = value;
    // std::wcout << "SCROLL: " << xx << " = " << value << std::endl;
  } else if (commandIs("SPEED")) {
    auto xx = ParseInt(Xx);
    double value;
    if (CheckResourceIdRange(xx) && parseJavaDouble(Value, value))
      SpeedTable[xx] = value;
  } else {
#if BMS_PARSER_VERBOSE == 1
    std::cout << "Unknown command: " << cmd << std::endl;
#endif
  }
}

inline unsigned long long Parser::Gcd(unsigned long long A,
                                      unsigned long long B) {
  while (true) {
    if (B == 0) {
      return A;
    }
    auto a1 = A;
    A = B;
    B = a1 % B;
  }
}

inline bool Parser::CheckResourceIdRange(int Id) const {
  return Id >= 0 && Id < (UseBase62 ? 62 * 62 : 36 * 36);
}

inline int Parser::ToWaveId(Chart *Chart, std::string_view Wav, bool metaOnly) {
  if (metaOnly) {
    return NoWav;
  }
  if (Wav.empty()) {
    return NoWav;
  }
  auto decoded = ParseInt(Wav);
  // check range
  if (!CheckResourceIdRange(decoded)) {
    // UE_LOG(LogTemp, Warning, TEXT("Invalid wav id: %s"), *Wav);
    return NoWav;
  }

  return Chart->WavTable.find(decoded) != Chart->WavTable.end() ? decoded
                                                                : NoWav;
}

inline void Parser::RegisterReferencedWaveId(Chart *Chart, int WavId) const {
  if (Chart == nullptr || WavId == NoWav || WavId == MetronomeWav) {
    return;
  }
  const auto wavIt = Chart->WavTable.find(WavId);
  if (wavIt == Chart->WavTable.end()) {
    return;
  }
  Chart->ReferencedWavTable[WavId] = wavIt->second;
}

inline void Parser::RegisterReferencedBmpId(Chart *Chart, int BmpId,
                                            bool metaOnly) const {
  if (metaOnly || Chart == nullptr || !CheckResourceIdRange(BmpId)) {
    return;
  }
  const auto bmpIt = Chart->BmpTable.find(BmpId);
  if (bmpIt == Chart->BmpTable.end()) {
    return;
  }
  Chart->ReferencedBmpTable[BmpId] = bmpIt->second;
}

inline int Parser::ParseHex(std::string_view str) {
  // Section.BPM_CHANGE decodes both digits in base 36, then applies hex weights.
  if (str.size() != 2) return -1;
  const auto digit = [](unsigned char c) {
    return c >= '0' && c <= '9' ? c - '0' :
           c >= 'A' && c <= 'Z' ? c - 'A' + 10 :
           c >= 'a' && c <= 'z' ? c - 'a' + 10 : -1;
  };
  const int high = digit(str[0]), low = digit(str[1]);
  return high < 0 || low < 0 ? -1 : high * 16 + low;
}

inline int Parser::ParseInt(std::string_view str, bool forceBase36) const {
  if (str.empty() || str.size() > 2) return -1;
  const int base = !forceBase36 && UseBase62 ? 62 : 36;
  int result = 0;
  for (unsigned char c : str) {
    int digit = c >= '0' && c <= '9' ? c - '0' :
                c >= 'A' && c <= 'Z' ? c - 'A' + 10 :
                c >= 'a' && c <= 'z' ? c - 'a' + (base == 62 ? 36 : 10) : -1;
    if (digit < 0) return -1;
    result = result * base + digit;
  }
  return result;
}
#ifdef _WIN32
std::wstring Parser::utf8_to_path_t(const std::string &input) {

  // Determine the size of the buffer needed
  int requiredSize =
      MultiByteToWideChar(65001 /* UTF8 */, 0, input.c_str(), -1, NULL, 0);

  if (requiredSize <= 0) {
    // Conversion failed, return an empty string
    return std::wstring();
  }

  // Create a string with the required size
  std::wstring result(requiredSize, '\0');

  // Perform the conversion
  MultiByteToWideChar(65001 /* UTF8 */, 0, input.c_str(), -1, &result[0],
                      requiredSize);

  // Remove the extra null terminator added by MultiByteToWideChar
  result.resize(requiredSize - 1);

  return result;
}
#else
std::string Parser::utf8_to_path_t(const std::string &input) { return input; }
#endif

Parser::~Parser() = default;
} // namespace bms_parser
