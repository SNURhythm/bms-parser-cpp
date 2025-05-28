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
#include "LandmineNote.h"
#include "LongNote.h"
#include "Measure.h"
#include "Note.h"
#include "ShiftJISConverter.h"
#include "TimeLine.h"
#include <cwctype>
#include <iterator>
#include <random>
#include <thread>

#include "SHA256.h"
#include "md5.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

#ifndef BMS_PARSER_VERBOSE
#define BMS_PARSER_VERBOSE 0
#endif

namespace bms_parser {
class threadRAII {
  std::thread th;

public:
  explicit threadRAII(std::thread &&_th) { th = std::move(_th); }

  ~threadRAII() {
    if (th.joinable()) {
      th.join();
    }
  }
};

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
int Beat7[] = {0, 1, 2, 3, 4, 7, -1, 5, 6, 8, 9, 10, 11, 12, 15, -1, 13, 14};
int PopN[] = {0, 1, 2, 3, 4, -1, -1, -1, -1, -1, 5, 6, 7, 8, -1, -1, -1, -1};
} // namespace KeyAssign

constexpr int TempKey = 16;

Parser::Parser() : BpmTable{}, StopLengthTable{}, ScrollTable{} {
  std::random_device seeder;
  Seed = seeder();
}

void Parser::SetRandomSeed(unsigned int RandomSeed) { Seed = RandomSeed; }

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

  static std::regex headerRegex(R"(^#([A-Za-z]+?)(\d\d)? +?(.+)?)");

  if (bCancelled) {
    return;
  }

  auto measures =
      std::unordered_map<int, std::vector<std::pair<int, std::string>>>();

  // compute hash in separate thread
  std::thread md5Thread([&bytes, new_chart] {
#if BMS_PARSER_VERBOSE == 1
    auto startTime = std::chrono::high_resolution_clock::now();
#endif
    MD5 md5;
    md5.update(bytes.data(), bytes.size());
    md5.finalize();
    new_chart->Meta.MD5 = md5.hexdigest();
#if BMS_PARSER_VERBOSE == 1
    std::cout << "Hashing MD5 took "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::high_resolution_clock::now() - startTime)
                     .count()
              << "\n";
#endif
  });
  threadRAII md5RAII(std::move(md5Thread));
  std::thread sha256Thread([&bytes, new_chart] {
#if BMS_PARSER_VERBOSE == 1
    auto startTime = std::chrono::high_resolution_clock::now();
#endif
    new_chart->Meta.SHA256 = sha256(bytes);
#if BMS_PARSER_VERBOSE == 1
    std::cout << "Hashing SHA256 took "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::high_resolution_clock::now() - startTime)
                     .count()
              << "\n";
#endif
  });
  threadRAII sha256RAII(std::move(sha256Thread));

  // std::cout<<"file size: "<<size<<std::endl;
  // bytes to std::string
#if BMS_PARSER_VERBOSE == 1
  auto midStartTime = std::chrono::high_resolution_clock::now();
#endif
  std::string content;
  ShiftJISConverter::BytesToUTF8(bytes.data(), bytes.size(), content);
#if BMS_PARSER_VERBOSE == 1
  std::cout << "ShiftJIS-UTF8 conversion took "
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - midStartTime)
                   .count()
            << "\n";
#endif
  // std::wcout<<content<<std::endl;
  std::vector<int> RandomStack;
  std::vector<bool> SkipStack;
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
      if (RandomStack.empty()) {
        // UE_LOG(LogTemp, Warning, TEXT("RandomStack is empty!"));
        continue;
      }
      const int CurrentRandom = RandomStack.back();
      const int n =
          static_cast<int>(std::strtol(line.substr(4).c_str(), nullptr, 10));
      SkipStack.push_back(CurrentRandom != n);
      continue;
    }
    if (MatchHeader(line, "#ELSE")) {
      if (SkipStack.empty()) {
        // UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
        continue;
      }
      const bool CurrentSkip = SkipStack.back();
      SkipStack.pop_back();
      SkipStack.push_back(!CurrentSkip);
      continue;
    }
    if (MatchHeader(line, "#ELSEIF")) {
      if (SkipStack.empty()) {
        // UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
        continue;
      }
      const bool CurrentSkip = SkipStack.back();
      SkipStack.pop_back();
      const int CurrentRandom = RandomStack.back();
      const int n =
          static_cast<int>(std::strtol(line.substr(8).c_str(), nullptr, 10));
      SkipStack.push_back(CurrentSkip && CurrentRandom != n);
      continue;
    }
    if (MatchHeader(line, "#ENDIF") || MatchHeader(line, "#END IF")) {
      if (SkipStack.empty()) {
        // UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
        continue;
      }
      SkipStack.pop_back();
      continue;
    }
    if (!SkipStack.empty() && SkipStack.back()) {
      continue;
    }
    if (MatchHeader(line, "#RANDOM") ||
        MatchHeader(line, "#RONDAM")) // #RANDOM n
    {
      const int n =
          static_cast<int>(std::strtol(line.substr(7).c_str(), nullptr, 10));
      std::uniform_int_distribution<int> dist(1, n);
      RandomStack.push_back(dist(Prng));
      continue;
    }
    if (MatchHeader(line, "#ENDRANDOM")) {
      if (RandomStack.empty()) {
        // UE_LOG(LogTemp, Warning, TEXT("RandomStack is empty!"));
        continue;
      }
      RandomStack.pop_back();
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
  auto minBpm = new_chart->Meta.Bpm;
  auto maxBpm = new_chart->Meta.Bpm;
  auto lastNote = std::vector<Note *>();
  lastNote.resize(TempKey, nullptr);
  auto lnStart = std::vector<LongNote *>();
  lnStart.resize(TempKey, nullptr);
#if BMS_PARSER_VERBOSE == 1
  midStartTime = std::chrono::high_resolution_clock::now();
#endif
  double measureBeatPosition = 0;
  for (auto measureIdx = 0; measureIdx <= lastMeasure; ++measureIdx) {
    if (bCancelled) {
      return;
    }
    if (measures.find(measureIdx) == measures.end()) {
      measures[measureIdx] = std::vector<std::pair<int, std::string>>();
    }

    // gcd (int, int)
    auto measure = new Measure();

    // NOTE: this should be an ordered map
    auto timelines = std::map<double, TimeLine *>();

    for (auto &pair : measures[measureIdx]) {
      if (bCancelled) {
        break;
      }
      auto channel = pair.first;
      auto &data = pair.second;
      if (channel == SectionRate) {
        measure->Scale = std::strtod(data.c_str(), nullptr);
        continue;
      }

      auto laneNumber = 0; // NOTE: This is intentionally set to 0, not -1!
      if (channel >= P1KeyBase && channel < P1KeyBase + 9) {
        laneNumber = KeyAssign::Beat7[channel - P1KeyBase];
        channel = P1KeyBase;
      } else if (channel >= P2KeyBase && channel < P2KeyBase + 9) {
        laneNumber = KeyAssign::Beat7[channel - P2KeyBase + 9];
        channel = P1KeyBase;
      } else if (channel >= P1InvisibleKeyBase &&
                 channel < P1InvisibleKeyBase + 9) {
        laneNumber = KeyAssign::Beat7[channel - P1InvisibleKeyBase];
        channel = P1InvisibleKeyBase;
      } else if (channel >= P2InvisibleKeyBase &&
                 channel < P2InvisibleKeyBase + 9) {
        laneNumber = KeyAssign::Beat7[channel - P2InvisibleKeyBase + 9];
        channel = P1InvisibleKeyBase;
      } else if (channel >= P1LongKeyBase && channel < P1LongKeyBase + 9) {
        laneNumber = KeyAssign::Beat7[channel - P1LongKeyBase];
        channel = P1LongKeyBase;
      } else if (channel >= P2LongKeyBase && channel < P2LongKeyBase + 9) {
        laneNumber = KeyAssign::Beat7[channel - P2LongKeyBase + 9];
        channel = P1LongKeyBase;
      } else if (channel >= P1MineKeyBase && channel < P1MineKeyBase + 9) {
        laneNumber = KeyAssign::Beat7[channel - P1MineKeyBase];
        channel = P1MineKeyBase;
      } else if (channel >= P2MineKeyBase && channel < P2MineKeyBase + 9) {
        laneNumber = KeyAssign::Beat7[channel - P2MineKeyBase + 9];
        channel = P1MineKeyBase;
      }

      if (laneNumber == -1) {
        continue;
      }
      const bool isScratch = laneNumber == 7 || laneNumber == 15;
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

      const auto dataCount = data.length() / 2;
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

        const auto position =
            static_cast<double>(j / g) /
            static_cast<double>(dataCount / g); // NOLINT(*-integer-division)

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
            auto bgNote = new Note{ToWaveId(new_chart, val, metaOnly)};
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
        case BgaPlay:
          timeline->BgaBase = ParseInt(val);
          break;
        case PoorPlay:
          timeline->BgaPoor = ParseInt(val);
          break;
        case LayerPlay:
          timeline->BgaLayer = ParseInt(val);
          break;
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
            auto ln = new LongNote{last->Wav};
            delete last;
            ln->Tail = new LongNote{NoWav};
            ln->Tail->Head = ln;
            lastTimeline->SetNote(laneNumber, ln);
            timeline->SetNote(laneNumber, ln->Tail);
          } else {
            auto note = new Note{ToWaveId(new_chart, val, metaOnly)};
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
          auto invNote = new Note{ToWaveId(new_chart, val, metaOnly)};
          timeline->SetInvisibleNote(laneNumber, invNote);
          break;
        }

        case P1LongKeyBase:
          if (Lntype == 1) {
            if (lnStart[laneNumber] == nullptr) {
              ++totalNotes;
              if (isScratch) {
                ++totalBackSpinNotes;
              } else {
                ++totalLongNotes;
              }

              auto ln = new LongNote{ToWaveId(new_chart, val, metaOnly)};
              lnStart[laneNumber] = ln;

              if (metaOnly) {
                delete ln; // this is intended
                break;
              }

              timeline->SetNote(laneNumber, ln);
            } else {
              if (!metaOnly) {
                auto tail = new LongNote{NoWav};
                tail->Head = lnStart[laneNumber];
                lnStart[laneNumber]->Tail = tail;
                timeline->SetNote(laneNumber, tail);
              }
              lnStart[laneNumber] = nullptr;
            }
          }

          break;
        case P1MineKeyBase: {
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

      // Debug.Log($"measure: {measureIdx}, position: {position}, lastPosition:
      // {lastPosition}, bpm: {currentBpm} scale: {measure.Scale} interval:
      // {interval} stop: {timeline.GetStopDuration()}");

      timePassed += timeline->GetStopDuration();
      if (!metaOnly) {
        measure->TimeLines.push_back(timeline);
      }

      lastPosition = position;
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
      measure->TimeLines.push_back(timeline);
    }
    if (!metaOnly) {
      measure->TimeLines[0]->IsFirstInMeasure = true;
    }
    new_chart->Meta.PlayLength = static_cast<long long>(timePassed);
    timePassed +=
        240000000.0 * (1 - lastPosition) * measure->Scale / currentBpm;
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
    Chart->Meta.PlayLevel =
        std::strtod(Value.c_str(), nullptr); // TODO: handle error
  } else if (MatchHeader(cmd, "RANK")) {
    Chart->Meta.Rank =
        static_cast<int>(std::strtol(Value.c_str(), nullptr, 10));
  } else if (MatchHeader(cmd, "TOTAL")) {
    auto total = std::strtod(Value.c_str(), nullptr);
    if (total > 0) {
      Chart->Meta.Total = total;
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
    if (Xx == "00") {
      Chart->Meta.BgaPoorDefault = true;
    }
  } else if (MatchHeader(cmd, "LNOBJ")) {
    Lnobj = ParseInt(Value);
  } else if (MatchHeader(cmd, "LNTYPE")) {
    Lntype = static_cast<int>(std::strtol(Value.c_str(), nullptr, 10));
  } else if (MatchHeader(cmd, "LNMODE")) {
    Chart->Meta.LnMode =
        static_cast<int>(std::strtol(Value.c_str(), nullptr, 10));
  } else if (MatchHeader(cmd, "SCROLL")) {
    auto xx = ParseInt(Xx);
    auto value = std::strtod(Value.c_str(), nullptr);
    ScrollTable[xx] = value;
    // std::wcout << "SCROLL: " << xx << " = " << value << std::endl;
  } else {
    std::cout << "Unknown command: " << cmd << std::endl;
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
