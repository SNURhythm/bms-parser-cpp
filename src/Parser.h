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

#pragma once

#include "Chart.h"
#include <atomic>
#include <filesystem>
#include <map>
#include <string>

/**
 *
 */
namespace bms_parser {
class Parser {
public:
  Parser();
  void SetRandomSeed(unsigned int RandomSeed);

  void Parse(const std::filesystem::path &path, Chart **Chart,
             bool addReadyMeasure, bool metaOnly, std::atomic_bool &bCancelled);
  ~Parser();
  void Parse(const std::vector<unsigned char> &bytes, Chart **chart,
             bool addReadyMeasure, bool metaOnly, std::atomic_bool &bCancelled);
  static int NoWav;
  static int MetronomeWav;

private:
  // bpmTable
  std::unordered_map<int, double> BpmTable;
  std::unordered_map<int, double> StopLengthTable;
  std::unordered_map<int, double> ScrollTable;

  bool UseBase62 = false;
  int Lnobj = -1;
  int Lntype = 1;
  unsigned int Seed;
  static inline int ParseHex(std::string_view Str);
  inline int ParseInt(std::string_view Str, bool forceBase32 = false) const;
  void ParseHeader(Chart *Chart, std::string_view cmd, std::string_view Xx,
                   const std::string &Value);
  static inline bool MatchHeader(const std::string_view &str,
                                 const std::string_view &headerUpper);
  static inline unsigned long long Gcd(unsigned long long A,
                                       unsigned long long B);
  inline bool CheckResourceIdRange(int Id) const;
  inline int ToWaveId(Chart *Chart, std::string_view Wav, bool metaOnly);
};
} // namespace bms_parser
