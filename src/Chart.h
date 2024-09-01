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

#include "Measure.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace bms_parser {
class ChartMeta {
public:
  std::string SHA256;
  std::string MD5;
  std::filesystem::path BmsPath;
  std::filesystem::path Folder;
  std::string Artist;
  std::string SubArtist;
  double Bpm = 0;
  std::string Genre;
  std::string Title;
  std::string SubTitle;
  int Rank = 3;
  double Total = 100;
  long long PlayLength = 0; // Timing of the last playable note, in microseconds
  long long TotalLength = 0;
  // Timing of the last timeline(including background note, bga change note,
  // invisible note, ...), in microseconds
  std::filesystem::path Banner;
  std::filesystem::path StageFile;
  std::filesystem::path BackBmp;
  std::filesystem::path Preview;
  bool BgaPoorDefault = false;
  int Difficulty = 0;
  double PlayLevel = 3;
  double MinBpm = 0;
  double MaxBpm = 0;
  int Player = 1;
  int KeyMode = 5;
  bool IsDP = false;
  int TotalNotes = 0;
  int TotalLongNotes = 0;
  int TotalScratchNotes = 0;
  int TotalBackSpinNotes = 0;
  int TotalLandmineNotes = 0;
  int LnMode = 0; // 0: user decides, 1: LN, 2: CN, 3: HCN

  [[nodiscard]] int GetKeyLaneCount() const { return KeyMode; }
  [[nodiscard]] int GetScratchLaneCount() const { return IsDP ? 2 : 1; }
  [[nodiscard]] int GetTotalLaneCount() const { return KeyMode + GetScratchLaneCount(); }

  [[nodiscard]] std::vector<int> GetKeyLaneIndices() const {
    switch (KeyMode) {
    case 5:
      return {0, 1, 2, 3, 4};
    case 7:
      return {0, 1, 2, 3, 4, 5, 6};
    case 10:
      return {0, 1, 2, 3, 4, 8, 9, 10, 11, 12};
    case 14:
      return {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14};
    default:
      return {};
    }
  }

  [[nodiscard]] std::vector<int> GetScratchLaneIndices() const {
    if (IsDP) {
      return {7, 15};
    }
    return {7};
  }

  [[nodiscard]] std::vector<int> GetTotalLaneIndices() const {
    std::vector<int> Result;
    Result.insert(Result.end(), GetKeyLaneIndices().begin(),
                  GetKeyLaneIndices().end());
    Result.insert(Result.end(), GetScratchLaneIndices().begin(),
                  GetScratchLaneIndices().end());
    return Result;
  }
};

class Chart {
public:
  Chart();
  ~Chart();
  ChartMeta Meta;
  std::vector<Measure *> Measures;
  std::unordered_map<int, std::string> WavTable;
  std::unordered_map<int, std::string> BmpTable;
};
} // namespace bms_parser