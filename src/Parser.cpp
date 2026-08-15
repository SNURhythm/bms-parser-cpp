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
#include "EucKrConverter.h"
#include "LandmineNote.h"
#include "LongNote.h"
#include "Measure.h"
#include "Note.h"
#include "ShiftJISConverter.h"
#include "TimeLine.h"
#include <cwctype>
#include <iterator>
#include <random>

#include "SHA256.h"
#include "md5.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>

#ifndef BMS_PARSER_VERBOSE
#define BMS_PARSER_VERBOSE 0
#endif

namespace {

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

std::string bytesToString(const std::vector<unsigned char> &bytes,
                          size_t offset) {
  if (offset >= bytes.size()) {
    return "";
  }
  return std::string(reinterpret_cast<const char *>(bytes.data() + offset),
                     bytes.size() - offset);
}

bool isValidUtf8(const std::vector<unsigned char> &bytes, size_t offset) {
  size_t i = offset;
  while (i < bytes.size()) {
    const unsigned char c = bytes[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    if (c >= 0xc2 && c <= 0xdf) {
      if (i + 1 >= bytes.size() || (bytes[i + 1] & 0xc0) != 0x80) {
        return false;
      }
      i += 2;
      continue;
    }
    if (c == 0xe0) {
      if (i + 2 >= bytes.size() || bytes[i + 1] < 0xa0 ||
          bytes[i + 1] > 0xbf || (bytes[i + 2] & 0xc0) != 0x80) {
        return false;
      }
      i += 3;
      continue;
    }
    if (c >= 0xe1 && c <= 0xec) {
      if (i + 2 >= bytes.size() || (bytes[i + 1] & 0xc0) != 0x80 ||
          (bytes[i + 2] & 0xc0) != 0x80) {
        return false;
      }
      i += 3;
      continue;
    }
    if (c == 0xed) {
      if (i + 2 >= bytes.size() || bytes[i + 1] < 0x80 ||
          bytes[i + 1] > 0x9f || (bytes[i + 2] & 0xc0) != 0x80) {
        return false;
      }
      i += 3;
      continue;
    }
    if (c >= 0xee && c <= 0xef) {
      if (i + 2 >= bytes.size() || (bytes[i + 1] & 0xc0) != 0x80 ||
          (bytes[i + 2] & 0xc0) != 0x80) {
        return false;
      }
      i += 3;
      continue;
    }
    if (c == 0xf0) {
      if (i + 3 >= bytes.size() || bytes[i + 1] < 0x90 ||
          bytes[i + 1] > 0xbf || (bytes[i + 2] & 0xc0) != 0x80 ||
          (bytes[i + 3] & 0xc0) != 0x80) {
        return false;
      }
      i += 4;
      continue;
    }
    if (c >= 0xf1 && c <= 0xf3) {
      if (i + 3 >= bytes.size() || (bytes[i + 1] & 0xc0) != 0x80 ||
          (bytes[i + 2] & 0xc0) != 0x80 ||
          (bytes[i + 3] & 0xc0) != 0x80) {
        return false;
      }
      i += 4;
      continue;
    }
    if (c == 0xf4) {
      if (i + 3 >= bytes.size() || bytes[i + 1] < 0x80 ||
          bytes[i + 1] > 0x8f || (bytes[i + 2] & 0xc0) != 0x80 ||
          (bytes[i + 3] & 0xc0) != 0x80) {
        return false;
      }
      i += 4;
      continue;
    }
    return false;
  }
  return true;
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
        if (low >= 0xDC00 && low <= 0xDFFF) {
          index += 2;
          codePoint = 0x10000 + (((unit - 0xD800) << 10) | (low - 0xDC00));
        } else {
          codePoint = kReplacementCodePoint;
        }
      } else {
        codePoint = kReplacementCodePoint;
      }
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      codePoint = kReplacementCodePoint;
    }

    appendUtf8CodePoint(codePoint, result);
  }
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
  size_t lineStart = hasUtf8Bom(bytes) ? 3 : 0;
  while (lineStart < bytes.size()) {
    size_t lineEnd = lineStart;
    while (lineEnd < bytes.size() && bytes[lineEnd] != '\n' &&
           bytes[lineEnd] != '\r') {
      ++lineEnd;
    }

    size_t pos = lineStart;
    if (pos < lineEnd && bytes[pos] == '#') {
      ++pos;
      for (std::string_view header : {"CHARSET", "ENCODING"}) {
        if (!asciiEqualsIgnoreCase(bytes, pos, lineEnd, header)) {
          continue;
        }
        size_t valueStart = pos + header.size();
        if (valueStart < lineEnd && !asciiWhitespace(bytes[valueStart])) {
          continue;
        }
        while (valueStart < lineEnd && asciiWhitespace(bytes[valueStart])) {
          ++valueStart;
        }
        return normalizeCharsetName(bytes, valueStart, lineEnd);
      }
    }

    lineStart = lineEnd;
    while (lineStart < bytes.size() &&
           (bytes[lineStart] == '\n' || bytes[lineStart] == '\r')) {
      ++lineStart;
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

bool isShiftJisLeadByte(unsigned char byte) {
  return (byte >= 0x81 && byte <= 0x9F) || (byte >= 0xE0 && byte <= 0xFC);
}

bool isShiftJisTrailByte(unsigned char byte) {
  return (byte >= 0x40 && byte <= 0x7E) || (byte >= 0x80 && byte <= 0xFC);
}

bool isEucKrLeadByte(unsigned char byte) {
  return byte >= 0xA1 && byte <= 0xFE;
}

bool isEucKrTrailByte(unsigned char byte) {
  return byte >= 0xA1 && byte <= 0xFE;
}

size_t countDbcsPairs(const std::vector<unsigned char> &bytes, size_t offset,
                      bool (*isLeadByte)(unsigned char),
                      bool (*isTrailByte)(unsigned char)) {
  size_t count = 0;
  size_t index = offset;
  while (index < bytes.size()) {
    if (isLeadByte(bytes[index]) && index + 1 < bytes.size() &&
        isTrailByte(bytes[index + 1])) {
      ++count;
      index += 2;
    } else {
      ++index;
    }
  }
  return count;
}

bool shouldDecodeNoBomAsEucKr(const std::vector<unsigned char> &bytes,
                              size_t offset) {
  const size_t eucKrPairs =
      countDbcsPairs(bytes, offset, isEucKrLeadByte, isEucKrTrailByte);
  const size_t shiftJisPairs =
      countDbcsPairs(bytes, offset, isShiftJisLeadByte, isShiftJisTrailByte);
  return eucKrPairs > shiftJisPairs;
}

void decodeBmsText(const std::vector<unsigned char> &bytes,
                   std::string &content) {
  const size_t utf8Offset = hasUtf8Bom(bytes) ? 3 : 0;
  if (hasUtf16LeBom(bytes)) {
    utf16BytesToUtf8(bytes, 2, true, content);
    return;
  }
  if (hasUtf16BeBom(bytes)) {
    utf16BytesToUtf8(bytes, 2, false, content);
    return;
  }

  const std::string charset = declaredCharset(bytes);
  if (hasUtf8Bom(bytes) || charsetIsUtf8(charset)) {
    content = bytesToString(bytes, utf8Offset);
    return;
  }
  if (charsetIsEucKr(charset)) {
    bms_parser::EucKrConverter::BytesToUTF8(
        bytes.data() + utf8Offset, bytes.size() - utf8Offset, content);
    return;
  }
  if (charsetIsShiftJis(charset)) {
    bms_parser::ShiftJISConverter::BytesToUTF8(
        bytes.data() + utf8Offset, bytes.size() - utf8Offset, content);
    return;
  }
  if (isValidUtf8(bytes, utf8Offset)) {
    content = bytesToString(bytes, utf8Offset);
    return;
  }
  if (shouldDecodeNoBomAsEucKr(bytes, utf8Offset)) {
    bms_parser::EucKrConverter::BytesToUTF8(
        bytes.data() + utf8Offset, bytes.size() - utf8Offset, content);
    return;
  }

  bms_parser::ShiftJISConverter::BytesToUTF8(
      bytes.data() + utf8Offset, bytes.size() - utf8Offset, content);
}

bool finitePositive(double value) {
  return std::isfinite(value) && value > 0.0;
}

int guessedBeatsForScale(double scale) {
  if (!finitePositive(scale)) {
    return 4;
  }
  const int beats = static_cast<int>(std::lround(scale * 4.0));
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
  durations[key] += durationMicros;
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

  Scroll = 1020
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

Parser::Parser() : BpmTable{}, StopLengthTable{}, ScrollTable{} {
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
    if (std::towupper(str[i]) != headerUpper[i]) {
      return false;
    }
  }
  return true;
}

void Parser::Parse(const std::filesystem::path &fpath, Chart **chart,
                   bool addReadyMeasure, bool metaOnly,
                   std::atomic_bool &bCancelled) {
#if BMS_PARSER_VERBOSE == 1
  auto startTime = std::chrono::high_resolution_clock::now();
#endif
  std::vector<unsigned char> bytes;
  std::ifstream file(fpath, std::ios::binary);
  if (!file.is_open()) {
    std::cout << "Failed to open file: " << fpath << std::endl;
    return;
  }
#if BMS_PARSER_VERBOSE == 1
  // measure file read time
  auto midStartTime = std::chrono::high_resolution_clock::now();
#endif
  file.seekg(0, std::ios::end);
  auto size = file.tellg();
  file.seekg(0, std::ios::beg);
  bytes.resize(static_cast<size_t>(size));
  file.read(reinterpret_cast<char *>(bytes.data()), size);
  file.close();
#if BMS_PARSER_VERBOSE == 1
  std::cout << "File read took "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - midStartTime)
                   .count()
            << "\n";
#endif
  Parse(bytes, chart, addReadyMeasure, metaOnly, bCancelled);
  auto new_chart = *chart;
  if (new_chart != nullptr) {
    new_chart->Meta.BmsPath = fpath;

    new_chart->Meta.Folder = fpath.parent_path();
  }
#if BMS_PARSER_VERBOSE == 1
  auto endTime = std::chrono::high_resolution_clock::now();
  std::cout << "Total parsing+reading took "
            << std::chrono::duration_cast<std::chrono::microseconds>(endTime -
                                                                     startTime)
                   .count()
            << "\n";
#endif
}

void Parser::Parse(const std::vector<unsigned char> &bytes, Chart **chart,
                   bool addReadyMeasure, bool metaOnly,
                   std::atomic_bool &bCancelled) {
#if BMS_PARSER_VERBOSE == 1
  auto startTime = std::chrono::high_resolution_clock::now();
#endif
  auto new_chart = new Chart();
  *chart = new_chart;
  new_chart->Meta.RandomSeed = Seed;
  new_chart->Meta.RandomPrng = RandomPrng;

  static std::regex headerRegex(R"(^#([A-Za-z]+?)(\d\d)? +?(.+)?)");

  if (bCancelled) {
    return;
  }

  auto measures =
      std::unordered_map<int, std::vector<std::pair<int, std::string>>>();

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
  decodeBmsText(bytes, content);
#if BMS_PARSER_VERBOSE == 1
  std::cout << "BMS text decoding took "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - midStartTime)
                   .count()
            << "\n";
#endif
  // std::wcout<<content<<std::endl;
  struct ConditionalFrame {
    bool parentSkipped = false;
    bool branchMatched = false;
    bool currentSkipped = false;
    size_t randomDepth = 0;
  };
  struct RandomFrame {
    bool active = false;
  };
  std::vector<int> RandomStack;
  std::vector<RandomFrame> RandomFrames;
  std::vector<ConditionalFrame> ConditionalStack;
  auto isSkipping = [&]() {
    for (const auto &frame : ConditionalStack) {
      if (frame.currentSkipped) {
        return true;
      }
    }
    for (const auto &frame : RandomFrames) {
      if (!frame.active) {
        return true;
      }
    }
    return false;
  };
  auto popRandomFrame = [&]() {
    if (RandomFrames.empty()) {
      return;
    }
    const bool wasActive = RandomFrames.back().active;
    RandomFrames.pop_back();
    if (wasActive && !RandomStack.empty()) {
      RandomStack.pop_back();
    }
  };
  auto isInsideConditionalBranchOfRandomDepth = [&](size_t randomDepth) {
    for (auto it = ConditionalStack.rbegin(); it != ConditionalStack.rend();
         ++it) {
      if (it->randomDepth == randomDepth) {
        return true;
      }
      if (it->randomDepth < randomDepth) {
        return false;
      }
    }
    return false;
  };
  auto closeUnbranchedRandomFrames = [&]() {
    while (!RandomFrames.empty() &&
           !isInsideConditionalBranchOfRandomDepth(RandomFrames.size())) {
      popRandomFrame();
    }
  };
  // init prng with seed
  std::mt19937_64 Prng(Seed);

  std::string line;
  std::istringstream stream(content);
#if BMS_PARSER_VERBOSE == 1
  midStartTime = std::chrono::high_resolution_clock::now();
#endif
  auto lastMeasure = -1;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (bCancelled) {
      return;
    }
    // std::cout << line << std::endl;
    if (line.size() <= 1 || line[0] != L'#')
      continue;
    if (bCancelled) {
      return;
    }

    if (MatchHeader(line, "#IF")) // #IF n
    {
      const bool parentSkipped = isSkipping();
      if (RandomStack.empty() && !parentSkipped) {
        // UE_LOG(LogTemp, Warning, TEXT("RandomStack is empty!"));
        continue;
      }
      const int CurrentRandom = parentSkipped ? 0 : RandomStack.back();
      const int n =
          static_cast<int>(std::strtol(line.substr(4).c_str(), nullptr, 10));
      const bool matched = !parentSkipped && CurrentRandom == n;
      ConditionalStack.push_back({parentSkipped, matched,
                                  parentSkipped || !matched,
                                  RandomFrames.size()});
      continue;
    }
    if (MatchHeader(line, "#ELSEIF")) {
      if (ConditionalStack.empty()) {
        // UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
        continue;
      }
      auto &frame = ConditionalStack.back();
      const int n =
          static_cast<int>(std::strtol(line.substr(8).c_str(), nullptr, 10));
      if (frame.parentSkipped || frame.branchMatched || RandomStack.empty()) {
        frame.currentSkipped = true;
        continue;
      }
      const int CurrentRandom = RandomStack.back();
      const bool matched = CurrentRandom == n;
      frame.branchMatched = matched;
      frame.currentSkipped = !matched;
      continue;
    }
    if (MatchHeader(line, "#ELSE")) {
      if (ConditionalStack.empty()) {
        // UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
        continue;
      }
      auto &frame = ConditionalStack.back();
      if (frame.parentSkipped) {
        frame.currentSkipped = true;
      } else {
        frame.currentSkipped = frame.branchMatched;
        frame.branchMatched = true;
      }
      continue;
    }
    if (MatchHeader(line, "#ENDIF") || MatchHeader(line, "#END IF")) {
      if (ConditionalStack.empty()) {
        // UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
        continue;
      }
      ConditionalStack.pop_back();
      continue;
    }
    if (MatchHeader(line, "#RANDOM") ||
        MatchHeader(line, "#RONDAM")) // #RANDOM n
    {
      closeUnbranchedRandomFrames();
      if (isSkipping()) {
        RandomFrames.push_back({false});
        continue;
      }
      const int n =
          static_cast<int>(std::strtol(line.substr(7).c_str(), nullptr, 10));
      if (n <= 0) {
        continue;
      }
      const size_t randomIndex = new_chart->Meta.RandomValues.size();
      int selectedRandom;
      if (randomIndex < RandomValues.size()) {
        selectedRandom = RandomValues[randomIndex];
      } else {
        std::uniform_int_distribution<int> dist(1, n);
        selectedRandom = dist(Prng);
      }
      new_chart->Meta.RandomValues.push_back(selectedRandom);
      RandomStack.push_back(selectedRandom);
      RandomFrames.push_back({true});
      continue;
    }
    if (MatchHeader(line, "#ENDRANDOM")) {
      if (RandomFrames.empty()) {
        // UE_LOG(LogTemp, Warning, TEXT("RandomStack is empty!"));
        continue;
      }
      popRandomFrame();
      continue;
    }
    if (isSkipping()) {
      continue;
    }
    if (MatchHeader(line, "#4K")) {
      ParseHeader(new_chart, "4K", "", "");
      continue;
    }
    if (MatchHeader(line, "#6K")) {
      ParseHeader(new_chart, "6K", "", "");
      continue;
    }
    if (MatchHeader(line, "#8K")) {
      ParseHeader(new_chart, "8K", "", "");
      continue;
    }

    if (line.length() >= 7 && std::isdigit(static_cast<unsigned char>(line[1])) &&
        std::isdigit(static_cast<unsigned char>(line[2])) &&
        std::isdigit(static_cast<unsigned char>(line[3])) &&
        line[6] == ':') {
      const int measure =
          static_cast<int>(std::strtol(line.substr(1, 3).c_str(), nullptr, 10));
      lastMeasure = std::max(lastMeasure, measure);
      const std::string ch = line.substr(4, 2);
      const int channel = ParseInt(ch);
      const std::string value = line.substr(7);
      if (measures.find(measure) == measures.end()) {
        measures[measure] = std::vector<std::pair<int, std::string>>();
      }
      measures[measure].emplace_back(channel, value);
    } else {
      if (MatchHeader(line, "#WAV")) {
        if (metaOnly) {
          continue;
        }
        if (line.length() < 7) {
          continue;
        }
        const auto xx = line.substr(4, 2);
        const auto value = line.substr(7);
        ParseHeader(new_chart, "WAV", xx, value);
      } else if (MatchHeader(line, "#BMP")) {
        if (metaOnly) {
          continue;
        }
        if (line.length() < 7) {
          continue;
        }
        const auto xx = line.substr(4, 2);
        const auto value = line.substr(7);
        ParseHeader(new_chart, "BMP", xx, value);
      } else if (MatchHeader(line, "#STOP")) {
        if (line.length() < 8) {
          continue;
        }
        const auto xx = line.substr(5, 2);
        const auto value = line.substr(8);
        ParseHeader(new_chart, "STOP", xx, value);
      } else if (MatchHeader(line, "#BPM")) {
        if (line.substr(4).rfind(' ', 0) == 0) {
          const auto value = line.substr(5);
          ParseHeader(new_chart, "BPM", "", value);
        } else {
          if (line.length() < 7) {
            continue;
          }
          const auto xx = line.substr(4, 2);
          const auto value = line.substr(7);
          ParseHeader(new_chart, "BPM", xx, value);
        }
      } else if (MatchHeader(line, "#SCROLL")) {
        if (line.length() < 10) {
          continue;
        }
        const auto xx = line.substr(7, 2);
        const auto value = line.substr(10);
        ParseHeader(new_chart, "SCROLL", xx, value);
      } else {
        std::smatch matcher;

        if (std::regex_search(line, matcher, headerRegex)) {
          std::string xx = matcher[2].str();
          std::string value = matcher[3].str();
          if (value.empty()) {
            value = xx;
            xx = "";
          }
          ParseHeader(new_chart, matcher[1].str(), xx, value);
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
  if (addReadyMeasure) {
    measures[0] = std::vector<std::pair<int, std::string>>();
    measures[0].emplace_back(LaneAutoplay, "********");
  }

  double timePassed = 0;
  int totalNotes = 0;
  int totalLongNotes = 0;
  int totalScratchNotes = 0;
  int totalBackSpinNotes = 0;
  int totalLandmineNotes = 0;
  auto currentBpm = new_chart->Meta.Bpm;
  auto currentScroll = 1.0;
  auto minBpm = new_chart->Meta.Bpm;
  auto maxBpm = new_chart->Meta.Bpm;
  auto lastNote = std::vector<Note *>();
  lastNote.resize(TempKey, nullptr);
  auto lnStart = std::vector<LongNote *>();
  lnStart.resize(TempKey, nullptr);
  const auto channelLongNoteType = LongNoteTypeFromLnMode(new_chart->Meta.LnMode);
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
  for (auto measureIdx = 0; measureIdx <= lastMeasure; ++measureIdx) {
    if (bCancelled) {
      return;
    }
    if (measures.find(measureIdx) == measures.end()) {
      measures[measureIdx] = std::vector<std::pair<int, std::string>>();
    }

    // gcd (int, int)
    auto measure = new Measure();
    bool explicitSectionRate = false;
    bool measureHasPrepTimingContent = false;
    bool measureHasAudibleContent = false;
    auto prepTimingPositions =
        std::set<std::pair<unsigned long long, unsigned long long>>();
    std::set<double> playableTimelinePositions;

    // NOTE: this should be an ordered map
    auto timelines = std::map<double, TimeLine *>();
    double bgaPoorTimingExtent = 0.0;

    for (auto &pair : measures[measureIdx]) {
      if (bCancelled) {
        break;
      }
      auto channel = pair.first;
      auto &data = pair.second;
      if (channel == SectionRate) {
        measure->Scale = std::strtod(data.c_str(), nullptr);
        explicitSectionRate = true;
        continue;
      }

      const bool scratchlessKeyMode =
          new_chart->Meta.IsScratchlessKeyMode();
      const auto *keyAssign =
          scratchlessKeyMode ? KeyAssign::Scratchless(new_chart->Meta.KeyMode)
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
      const bool isScratch =
          !scratchlessKeyMode && (laneNumber == 7 || laneNumber == 15);
      if (!scratchlessKeyMode) {
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

      const auto dataCount = data.length() / 2;
      if (channel == PoorPlay) {
        bool hasActiveCell = false;
        for (size_t j = 0; j < dataCount; ++j) {
          if (bCancelled) {
            break;
          }
          if (data.substr(j * 2, 2) != "00") {
            hasActiveCell = true;
            break;
          }
        }
        if (bCancelled) {
          break;
        }
        if (!hasActiveCell) {
          continue;
        }
        BgaPoorSequence sequence;
        sequence.Frames.reserve(dataCount);
        for (size_t j = 0; j < dataCount; ++j) {
          if (bCancelled) {
            break;
          }
          const std::string value = data.substr(j * 2, 2);
          if (value != "00") {
            const auto g = Gcd(j, dataCount);
            const auto positionNumerator = j / g;
            const auto positionDenominator = dataCount / g;
            const auto position =
                static_cast<double>(positionNumerator) /
                static_cast<double>(positionDenominator);
            bgaPoorTimingExtent = std::max(bgaPoorTimingExtent, position);
          } else {
            sequence.Frames.push_back(BgaSequenceBlank);
            continue;
          }
          const int bmpId = ParseInt(value);
          if (CheckResourceIdRange(bmpId) &&
              new_chart->BmpTable.find(bmpId) != new_chart->BmpTable.end()) {
            RegisterReferencedBmpId(new_chart, bmpId, metaOnly);
            sequence.Frames.push_back(bmpId);
          } else {
            sequence.Frames.push_back(BgaSequenceBlank);
          }
        }
        if (bCancelled) {
          break;
        }
        if (timelines.find(0.0) == timelines.end()) {
          timelines[0.0] = new TimeLine(TempKey, metaOnly);
        }
        timelines[0.0]->BgaPoor = std::move(sequence);
        continue;
      }
      const bool channelCanAnchorPrepTiming =
          channel == LaneAutoplay || channel == BpmChange ||
          channel == BpmChangeExtend || channel == Stop || channel == Scroll ||
          channel == P1KeyBase || channel == P1InvisibleKeyBase ||
          channel == P1LongKeyBase || channel == P1MineKeyBase;
      const bool channelHasAudibleContent =
          channel == LaneAutoplay || channel == P1KeyBase ||
          channel == P1InvisibleKeyBase || channel == P1LongKeyBase ||
          channel == P1MineKeyBase;
      for (size_t j = 0; j < dataCount; ++j) {
        if (bCancelled) {
          break;
        }
        std::string val = data.substr(j * 2, 2);
        if (val == "00") {
          if (timelines.empty() && j == 0) {
            auto timeline = new TimeLine(TempKey, metaOnly);
            timelines[0] = timeline; // add ghost timeline
          }

          continue;
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
          prepTimingPositions.emplace(positionNumerator, positionDenominator);
        }
        if (channelHasAudibleContent) {
          measureHasAudibleContent = true;
        }

        if (timelines.find(position) == timelines.end()) {
          timelines[position] = new TimeLine(TempKey, metaOnly);
        }

        auto timeline = timelines[position];
        if (channel == LaneAutoplay || channel == P1InvisibleKeyBase) {
          if (metaOnly) {
            break;
          }
        }
        switch (channel) {
        case LaneAutoplay:
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
          if (BpmTable.find(id) != BpmTable.end()) {
            timeline->Bpm = BpmTable[id];
          } else {
            timeline->Bpm = 0;
            // std::cout<<"Undefined BPM: "<<id<<std::endl;
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
          timeline->ScrollChange = true;
          if (ScrollTable.find(id) != ScrollTable.end()) {
            timeline->Scroll = ScrollTable[id];
          } else {
            timeline->Scroll = 1;
          }
          // Debug.Log($"SCROLL: {timeline.Scroll}, on measure {measureIdx}");
          break;
        }
        case Stop: {
          const auto id = ParseInt(val);
          if (!CheckResourceIdRange(id)) {
            // UE_LOG(LogTemp, Warning, TEXT("Invalid StopLength id: %s"),
            // *val);
            break;
          }
          if (StopLengthTable.find(id) != StopLengthTable.end()) {
            timeline->StopLength = StopLengthTable[id];
          } else {
            timeline->StopLength = 0;
          }
          // Debug.Log($"STOP: {timeline.StopLength}, on measure {measureIdx}");
          break;
        }
        case P1KeyBase: {
          playableTimelinePositions.insert(position);
          const auto ch = ParseInt(val);
          if (ch == Lnobj && lastNote[laneNumber] != nullptr) {
            if (isScratch) {
              ++totalBackSpinNotes;
            } else {
              ++totalLongNotes;
            }

            auto last = lastNote[laneNumber];
            lastNote[laneNumber] = nullptr;
            if (metaOnly) {
              break;
            }

            auto lastTimeline = last->Timeline;
            auto ln = new LongNote{last->Wav, channelLongNoteType};
            delete last;
            ln->Tail = new LongNote{NoWav, ln->Type};
            ln->Tail->Head = ln;
            lastTimeline->SetNote(laneNumber, ln);
            timeline->SetNote(laneNumber, ln->Tail);
          } else {
            const int wavId = ToWaveId(new_chart, val, metaOnly);
            RegisterReferencedWaveId(new_chart, wavId);
            auto note = new Note{wavId};
            lastNote[laneNumber] = note;
            ++totalNotes;
            if (isScratch) {
              ++totalScratchNotes;
            }
            if (metaOnly) {
              delete note; // this is intended
              break;
            }
            timeline->SetNote(laneNumber, note);
          }
        } break;
        case P1InvisibleKeyBase: {
          const int wavId = ToWaveId(new_chart, val, metaOnly);
          RegisterReferencedWaveId(new_chart, wavId);
          auto invNote = new Note{wavId};
          timeline->SetInvisibleNote(laneNumber, invNote);
          break;
        }

        case P1LongKeyBase: {
          // Beatoraja's BMS decoder always materializes 5x/6x channels as
          // lane notes. Its effective LN judgement mode is independent of
          // whether these channel objects exist.
          playableTimelinePositions.insert(position);
          if (lnStart[laneNumber] == nullptr) {
            ++totalNotes;
            if (isScratch) {
              ++totalBackSpinNotes;
            } else {
              ++totalLongNotes;
            }

            const int wavId = ToWaveId(new_chart, val, metaOnly);
            RegisterReferencedWaveId(new_chart, wavId);
            auto ln = new LongNote{wavId, channelLongNoteType};
            lnStart[laneNumber] = ln;

            if (metaOnly) {
              delete ln; // this is intended
              break;
            }

            timeline->SetNote(laneNumber, ln);
          } else {
            if (!metaOnly) {
              auto tail = new LongNote{NoWav, lnStart[laneNumber]->Type};
              tail->Head = lnStart[laneNumber];
              lnStart[laneNumber]->Tail = tail;
              timeline->SetNote(laneNumber, tail);
            }
            lnStart[laneNumber] = nullptr;
          }
          break;
        }
        case P1MineKeyBase: {
          playableTimelinePositions.insert(position);
          // landmine
          ++totalLandmineNotes;
          if (metaOnly) {
            break;
          }
          const auto damage = static_cast<float>(ParseInt(val, true)) / 2.0f;
          timeline->SetNote(laneNumber, new LandmineNote{damage});
          break;
        }
        default:
          break;
        }
      }
    }

    if (bCancelled) {
      for (const auto &[position, timeline] : timelines) {
        (void)position;
        delete timeline;
      }
      delete measure;
      delete new_chart;
      *chart = nullptr;
      return;
    }

    new_chart->Meta.TotalNotes = totalNotes;
    new_chart->Meta.TotalLongNotes = totalLongNotes;
    new_chart->Meta.TotalScratchNotes = totalScratchNotes;
    new_chart->Meta.TotalBackSpinNotes = totalBackSpinNotes;
    new_chart->Meta.TotalLandmineNotes = totalLandmineNotes;

    auto lastPosition = 0.0;

    measure->Timing = static_cast<long long>(timePassed);

    for (auto &pair : timelines) {
      if (bCancelled) {
        break;
      }
      const auto position = pair.first;
      const auto timeline = pair.second;

      // Debug.Log($"measure: {measureIdx}, position: {position}, lastPosition:
      // {lastPosition} bpm: {bpm} scale: {measure.scale} interval: {240 * 1000
      // * 1000 * (position - lastPosition) * measure.scale / bpm}");
      const auto interval =
          240000000.0 * (position - lastPosition) * measure->Scale / currentBpm;
      addDuration(bpmDurations, bpmOrder, currentBpm,
                  static_cast<long long>(std::llround(interval)));
      timePassed += interval;
      timeline->Timing = static_cast<long long>(timePassed);
      timeline->BeatPosition = measureBeatPosition + position * measure->Scale;
      if (timeline->BpmChange) {
        currentBpm = timeline->Bpm;
        minBpm = std::min(minBpm, timeline->Bpm);
        maxBpm = std::max(maxBpm, timeline->Bpm);
      } else {
        timeline->Bpm = currentBpm;
      }

      if (timeline->ScrollChange) {
        currentScroll = timeline->Scroll;
      } else {
        timeline->Scroll = currentScroll;
      }

      // Debug.Log($"measure: {measureIdx}, position: {position}, lastPosition:
      // {lastPosition}, bpm: {currentBpm} scale: {measure.Scale} interval:
      // {interval} stop: {timeline.GetStopDuration()}");

      const auto stopDuration = timeline->GetStopDuration();
      addDuration(bpmDurations, bpmOrder, timeline->Bpm,
                  static_cast<long long>(std::llround(stopDuration)));
      timePassed += stopDuration;
      if (playableTimelinePositions.find(position) !=
          playableTimelinePositions.end()) {
        new_chart->Meta.PlayLength = timeline->Timing;
      }
      if (!metaOnly) {
        measure->TimeLines.push_back(timeline);
      }

      lastPosition = position;
    }

    if (bgaPoorTimingExtent > lastPosition) {
      const auto interval = 240000000.0 *
                            (bgaPoorTimingExtent - lastPosition) *
                            measure->Scale / currentBpm;
      addDuration(bpmDurations, bpmOrder, currentBpm,
                  static_cast<long long>(std::llround(interval)));
      timePassed += interval;
      lastPosition = bgaPoorTimingExtent;
    }

    if (metaOnly) {
      for (auto &timeline : timelines) {
        delete timeline.second;
      }
      timelines.clear();
    }

    if (!metaOnly && measure->TimeLines.empty()) {
      auto timeline = new TimeLine(TempKey, metaOnly);
      timeline->Timing = static_cast<long long>(timePassed);
      timeline->BeatPosition = measureBeatPosition;
      timeline->Bpm = currentBpm;
      timeline->Scroll = currentScroll;
      measure->TimeLines.push_back(timeline);
    }
    if (!metaOnly) {
      measure->TimeLines[0]->IsFirstInMeasure = true;
    }
    const auto finalInterval =
        240000000.0 * (1 - lastPosition) * measure->Scale / currentBpm;
    addDuration(bpmDurations, bpmOrder, currentBpm,
                static_cast<long long>(std::llround(finalInterval)));
    timePassed += finalInterval;
    const int measureBeats = guessedBeatsForScale(measure->Scale);
    const long long measureDuration =
        static_cast<long long>(timePassed) - measure->Timing;
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
    if (!metaOnly) {
      new_chart->Measures.push_back(measure);
    } else {
      delete measure;
    }
  }
#if BMS_PARSER_VERBOSE == 1
  std::cout << "Reading data field took "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - midStartTime)
                   .count()
            << "\n";
#endif
  new_chart->Meta.TotalLength = static_cast<long long>(timePassed);
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
  if (new_chart->Meta.Difficulty == 0) {
    std::string FullTitle;
    FullTitle.reserve(new_chart->Meta.Title.length() +
                      new_chart->Meta.SubTitle.length());
    std::transform(new_chart->Meta.Title.begin(), new_chart->Meta.Title.end(),
                   std::back_inserter(FullTitle), ::towlower);
    std::transform(new_chart->Meta.SubTitle.begin(),
                   new_chart->Meta.SubTitle.end(),
                   std::back_inserter(FullTitle), ::towlower);
    if (FullTitle.find("easy") != std::string::npos) {
      new_chart->Meta.Difficulty = 1;
    } else if (FullTitle.find("normal") != std::string::npos) {
      new_chart->Meta.Difficulty = 2;
    } else if (FullTitle.find("hyper") != std::string::npos) {
      new_chart->Meta.Difficulty = 3;
    } else if (FullTitle.find("another") != std::string::npos) {
      new_chart->Meta.Difficulty = 4;
    } else if (FullTitle.find("insane") != std::string::npos) {
      new_chart->Meta.Difficulty = 5;
    } else {
      if (totalNotes < 250) {
        new_chart->Meta.Difficulty = 1;
      } else if (totalNotes < 600) {
        new_chart->Meta.Difficulty = 2;
      } else if (totalNotes < 1000) {
        new_chart->Meta.Difficulty = 3;
      } else if (totalNotes < 2000) {
        new_chart->Meta.Difficulty = 4;
      } else {
        new_chart->Meta.Difficulty = 5;
      }
    }
  }

#if BMS_PARSER_VERBOSE == 1
  std::cout << "Total parsing time: "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - startTime)
                   .count()
            << "\n";
#endif
}

void Parser::ParseHeader(Chart *Chart, std::string_view cmd,
                         std::string_view Xx, const std::string &Value) {
  // Debug.Log($"cmd: {cmd}, xx: {xx} isXXNull: {xx == null}, value: {value}");
  // BASE 62
  if (MatchHeader(cmd, "BASE")) {
    if (Value.empty()) {
      return; // TODO: handle this
    }
    auto base = static_cast<int>(std::strtol(Value.c_str(), nullptr, 10));
    std::wcout << "BASE: " << base << std::endl;
    if (base != 36 && base != 62) {
      return; // TODO: handle this
    }
    this->UseBase62 = base == 62;
  } else if (MatchHeader(cmd, "4K")) {
    Chart->Meta.KeyMode = 4;
    Chart->Meta.IsDP = false;
  } else if (MatchHeader(cmd, "6K")) {
    Chart->Meta.KeyMode = 6;
    Chart->Meta.IsDP = false;
  } else if (MatchHeader(cmd, "8K")) {
    Chart->Meta.KeyMode = 8;
    Chart->Meta.IsDP = false;
  } else if (MatchHeader(cmd, "PLAYER")) {
    Chart->Meta.Player =
        static_cast<int>(std::strtol(Value.c_str(), nullptr, 10));
  } else if (MatchHeader(cmd, "GENRE")) {
    Chart->Meta.Genre = Value;
  } else if (MatchHeader(cmd, "TITLE")) {
    Chart->Meta.Title = Value;
  } else if (MatchHeader(cmd, "SUBTITLE")) {
    Chart->Meta.SubTitle = Value;
  } else if (MatchHeader(cmd, "ARTIST")) {
    Chart->Meta.Artist = Value;
  } else if (MatchHeader(cmd, "SUBARTIST")) {
    Chart->Meta.SubArtist = Value;
  } else if (MatchHeader(cmd, "DIFFICULTY")) {
    Chart->Meta.Difficulty =
        static_cast<int>(std::strtol(Value.c_str(), nullptr, 10));
  } else if (MatchHeader(cmd, "BPM")) {
    if (Value.empty()) {
      return; // TODO: handle this
    }
    if (Xx.empty()) {
      // chart initial bpm
      Chart->Meta.Bpm = std::strtod(Value.c_str(), nullptr);
      // std::cout << "MainBPM: " << Chart->Meta.Bpm << std::endl;
    } else {
      // Debug.Log($"BPM: {DecodeBase36(xx)} = {double.Parse(value)}");
      int id = ParseInt(Xx);
      if (!CheckResourceIdRange(id)) {
        // UE_LOG(LogTemp, Warning, TEXT("Invalid BPM id: %s"), *Xx);
        return;
      }
      BpmTable[id] = std::strtod(Value.c_str(), nullptr);
    }
  } else if (MatchHeader(cmd, "STOP")) {
    if (Value.empty() || Xx.empty()) {
      return; // TODO: handle this
    }
    int id = ParseInt(Xx);
    if (!CheckResourceIdRange(id)) {
      // UE_LOG(LogTemp, Warning, TEXT("Invalid STOP id: %s"), *Xx);
      return;
    }
    StopLengthTable[id] = std::strtod(Value.c_str(), nullptr);
  } else if (MatchHeader(cmd, "MIDIFILE")) {
    // TODO: handle this
  } else if (MatchHeader(cmd, "VIDEOFILE")) {
  } else if (MatchHeader(cmd, "PLAYLEVEL")) {
    Chart->Meta.PlayLevelText = javaTrimmedHeaderValue(Value);
    Chart->Meta.PlayLevel =
        std::strtod(Value.c_str(), nullptr); // TODO: handle error
  } else if (MatchHeader(cmd, "RANK")) {
    Chart->Meta.Rank =
        static_cast<int>(std::strtol(Value.c_str(), nullptr, 10));
  } else if (MatchHeader(cmd, "TOTAL")) {
    auto total = std::strtod(Value.c_str(), nullptr);
    if (total > 0) {
      Chart->Meta.Total = total;
      Chart->Meta.HasTotal = true;
    }
  } else if (MatchHeader(cmd, "VOLWAV")) {
  } else if (MatchHeader(cmd, "STAGEFILE")) {
    Chart->Meta.StageFile = utf8_to_path_t(Value);
  } else if (MatchHeader(cmd, "BANNER")) {
    Chart->Meta.Banner = utf8_to_path_t(Value);
  } else if (MatchHeader(cmd, "BACKBMP")) {
    Chart->Meta.BackBmp = utf8_to_path_t(Value);
  } else if (MatchHeader(cmd, "PREVIEW")) {
    Chart->Meta.Preview = utf8_to_path_t(Value);
  } else if (MatchHeader(cmd, "WAV")) {
    if (Xx.empty() || Value.empty()) {
      // UE_LOG(LogTemp, Warning, TEXT("WAV command requires two arguments"));
      return;
    }
    int id = ParseInt(Xx);
    if (!CheckResourceIdRange(id)) {
      // UE_LOG(LogTemp, Warning, TEXT("Invalid WAV id: %s"), *Xx);
      return;
    }
    Chart->WavTable[id] = Value;
    if (Chart->ReferencedWavTable.find(id) !=
        Chart->ReferencedWavTable.end()) {
      Chart->ReferencedWavTable[id] = Value;
    }
  } else if (MatchHeader(cmd, "BMP")) {
    if (Xx.empty() || Value.empty()) {
      // UE_LOG(LogTemp, Warning, TEXT("BMP command requires two arguments"));
      return;
    }
    int id = ParseInt(Xx);
    if (!CheckResourceIdRange(id)) {
      // UE_LOG(LogTemp, Warning, TEXT("Invalid BMP id: %s"), *Xx);
      return;
    }
    Chart->BmpTable[id] = Value;
    if (Chart->ReferencedBmpTable.find(id) !=
        Chart->ReferencedBmpTable.end()) {
      Chart->ReferencedBmpTable[id] = Value;
    }
    if (Xx == "00") {
      Chart->ReferencedBmpTable[id] = Value;
      Chart->Meta.BgaPoorDefault = true;
    }
  } else if (MatchHeader(cmd, "LNOBJ")) {
    Lnobj = ParseInt(Value);
  } else if (MatchHeader(cmd, "LNMODE")) {
    Chart->Meta.LnMode =
        static_cast<int>(std::strtol(Value.c_str(), nullptr, 10));
  } else if (MatchHeader(cmd, "SCROLL")) {
    auto xx = ParseInt(Xx);
    auto value = std::strtod(Value.c_str(), nullptr);
    ScrollTable[xx] = value;
    // std::wcout << "SCROLL: " << xx << " = " << value << std::endl;
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

inline int Parser::ParseHex(std::string_view Str) {
  auto result = 0;
  for (size_t i = 0; i < Str.length(); ++i) {
    auto c = Str[i];
    if (c >= '0' && c <= '9') {
      result = result * 16 + c - '0';
    } else if (c >= 'A' && c <= 'F') {
      result = result * 16 + c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
      result = result * 16 + c - 'a' + 10;
    }
  }
  return result;
}

inline int Parser::ParseInt(std::string_view Str, bool forceBase36) const {
  if (forceBase36 || !UseBase62) {
    auto result = static_cast<int>(std::strtol(Str.data(), nullptr, 36));
    // std::wcout << "ParseInt36: " << Str << " = " << result << std::endl;
    return result;
  }

  auto result = 0;
  for (size_t i = 0; i < Str.length(); ++i) {
    auto c = Str[i];
    if (c >= '0' && c <= '9') {
      result = result * 62 + c - '0';
    } else if (c >= 'A' && c <= 'Z') {
      result = result * 62 + c - 'A' + 10;
    } else if (c >= 'a' && c <= 'z') {
      result = result * 62 + c - 'a' + 36;
    } else
      return -1;
  }
  // std::wcout << "ParseInt62: " << Str << " = " << result << std::endl;
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
