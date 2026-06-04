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

#include "Modifier.h"
#include "LongNote.h"
#include "TimeLine.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>

namespace bms_parser {
namespace {

constexpr int SranThresholdMillis = 40;

class JavaRandom {
public:
  explicit JavaRandom(long long seed) { SetSeed(seed); }

  void SetSeed(long long seed) {
    Seed = (static_cast<unsigned long long>(seed) ^ Multiplier) & Mask;
  }

  int NextInt(int bound) {
    if (bound <= 0) {
      return 0;
    }
    if ((bound & -bound) == bound) {
      return static_cast<int>((bound * static_cast<long long>(Next(31))) >> 31);
    }

    int bits;
    int value;
    do {
      bits = Next(31);
      value = bits % bound;
    } while (bits - value + (bound - 1) < 0);
    return value;
  }

private:
  static constexpr unsigned long long Multiplier = 0x5DEECE66DULL;
  static constexpr unsigned long long Addend = 0xBULL;
  static constexpr unsigned long long Mask = (1ULL << 48ULL) - 1ULL;

  unsigned long long Seed = 0;

  int Next(int bits) {
    Seed = (Seed * Multiplier + Addend) & Mask;
    return static_cast<int>(Seed >> (48 - bits));
  }
};

long long CreateSeed() {
  std::random_device seeder;
  return static_cast<long long>(seeder()) & 0xFFFFFFLL;
}

int ThresholdMillisFromBpm(int thresholdBpm) {
  if (thresholdBpm > 0) {
    return static_cast<int>(std::ceil(15000.0 / thresholdBpm));
  }
  if (thresholdBpm == 0) {
    return 0;
  }
  return 100;
}

bool IsScratchLane(const ChartMeta &meta, int lane) {
  const auto scratchLanes = meta.GetScratchLaneIndices();
  return std::find(scratchLanes.begin(), scratchLanes.end(), lane) !=
         scratchLanes.end();
}

std::vector<TimeLine *> GetAllTimeLines(Chart &chart) {
  std::vector<TimeLine *> timelines;
  for (auto *measure : chart.Measures) {
    if (measure == nullptr) {
      continue;
    }
    timelines.insert(timelines.end(), measure->TimeLines.begin(),
                     measure->TimeLines.end());
  }
  return timelines;
}

bool IsLongTail(const Note *note) {
  const auto *ln = dynamic_cast<const LongNote *>(note);
  return ln != nullptr && ln->IsTail();
}

bool IsLongHead(const Note *note) {
  const auto *ln = dynamic_cast<const LongNote *>(note);
  return ln != nullptr && !ln->IsTail();
}

void AssignNote(TimeLine &timeline, int lane, Note *note) {
  timeline.Notes[lane] = note;
  if (note != nullptr) {
    note->Lane = lane;
    note->Timeline = &timeline;
  }
}

void AssignHiddenNote(TimeLine &timeline, int lane, Note *note) {
  timeline.InvisibleNotes[lane] = note;
  if (note != nullptr) {
    note->Lane = lane;
    note->Timeline = &timeline;
  }
}

void AssignLandmineNote(TimeLine &timeline, int lane, LandmineNote *note) {
  timeline.LandmineNotes[lane] = note;
  if (note != nullptr) {
    note->Lane = lane;
    note->Timeline = &timeline;
  }
}

bool HasAnyLaneNote(const TimeLine &timeline) {
  return std::any_of(timeline.Notes.begin(), timeline.Notes.end(),
                     [](const Note *note) { return note != nullptr; }) ||
         std::any_of(timeline.InvisibleNotes.begin(),
                     timeline.InvisibleNotes.end(),
                     [](const Note *note) { return note != nullptr; }) ||
         std::any_of(timeline.LandmineNotes.begin(),
                     timeline.LandmineNotes.end(),
                     [](const LandmineNote *note) { return note != nullptr; });
}

std::string NormalizeOption(std::string_view option) {
  std::string normalized;
  normalized.reserve(option.length());
  for (char c : option) {
    if (c == '_' || c == ' ') {
      normalized.push_back('-');
    } else {
      normalized.push_back(
          static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
  }
  return normalized;
}

class IdentityModifier final : public BaseModifier {
public:
  explicit IdentityModifier(long long seed = -1, int player = 0)
      : BaseModifier(seed, player) {}

  void Modify(Chart &chart) override { RecalculateNoteCounts(chart); }

  [[nodiscard]] const char *Name() const override { return "NORMAL"; }
};

class TimelineNoteRandomizer {
public:
  TimelineNoteRandomizer(const std::vector<int> &lanes, long long seed,
                         int thresholdMillis, bool allScratch, bool spiral,
                         const std::vector<int> &scratchLanes, int player,
                         bool doublePlay)
      : ModifyLanes(lanes), Random(seed), ThresholdMillis(thresholdMillis),
        AllScratch(allScratch), Spiral(spiral), ScratchLanes(scratchLanes),
        Player(player), DoublePlay(doublePlay) {
    ChangeableLanes = ModifyLanes;
    AssignableLanes = ModifyLanes;
    for (int lane : ModifyLanes) {
      LastNoteTime[lane] = -10000;
    }

    if (Spiral && ModifyLanes.size() > 1) {
      SpiralIncrement =
          Random.NextInt(static_cast<int>(ModifyLanes.size()) - 1) + 1;
    }
  }

  void Permutate(TimeLine &timeline) {
    auto permutationMap = Randomize(timeline);
    for (const auto &entry : LongNoteActive) {
      PutMapping(permutationMap, entry.first, entry.second);
    }

    std::vector<Note *> notes(timeline.Notes.size(), nullptr);
    std::vector<Note *> hiddenNotes(timeline.InvisibleNotes.size(), nullptr);
    std::vector<LandmineNote *> landmineNotes(timeline.LandmineNotes.size(),
                                              nullptr);
    for (int lane : ModifyLanes) {
      if (lane < static_cast<int>(notes.size())) {
        notes[lane] = timeline.Notes[lane];
        AssignNote(timeline, lane, nullptr);
      }
      if (lane < static_cast<int>(hiddenNotes.size())) {
        hiddenNotes[lane] = timeline.InvisibleNotes[lane];
        AssignHiddenNote(timeline, lane, nullptr);
      }
      if (lane < static_cast<int>(landmineNotes.size())) {
        landmineNotes[lane] = timeline.LandmineNotes[lane];
        AssignLandmineNote(timeline, lane, nullptr);
      }
    }

    for (const auto &entry : permutationMap) {
      const int sourceLane = entry.first;
      const int targetLane = entry.second;
      if (sourceLane < 0 || targetLane < 0 ||
          sourceLane >= static_cast<int>(notes.size()) ||
          targetLane >= static_cast<int>(notes.size())) {
        continue;
      }

      Note *note = notes[sourceLane];
      if (auto *longNote = dynamic_cast<LongNote *>(note)) {
        if (longNote->IsTail() &&
            LongNoteActive.find(sourceLane) != LongNoteActive.end() &&
            longNote->Timeline == &timeline) {
          LongNoteActive.erase(sourceLane);
          ChangeableLanes.push_back(sourceLane);
          AssignableLanes.push_back(targetLane);
        } else if (!longNote->IsTail()) {
          LongNoteActive[sourceLane] = targetLane;
          EraseValue(ChangeableLanes, sourceLane);
          EraseValue(AssignableLanes, targetLane);
        }
      }

      AssignNote(timeline, targetLane, note);
      AssignHiddenNote(timeline, targetLane, hiddenNotes[sourceLane]);
      AssignLandmineNote(timeline, targetLane, landmineNotes[sourceLane]);
    }
  }

private:
  std::vector<int> ModifyLanes;
  JavaRandom Random;
  int ThresholdMillis;
  bool AllScratch;
  bool Spiral;
  std::vector<int> ScratchLanes;
  int Player;
  bool DoublePlay;
  std::unordered_map<int, int> LongNoteActive;
  std::vector<int> ChangeableLanes;
  std::vector<int> AssignableLanes;
  std::unordered_map<int, int> LastNoteTime;
  int ScratchIndex = 0;
  int SpiralIncrement = 0;
  int SpiralHead = 0;

  using Mapping = std::vector<std::pair<int, int>>;

  static void PutMapping(Mapping &mapping, int sourceLane, int targetLane) {
    for (auto &entry : mapping) {
      if (entry.first == sourceLane) {
        entry.second = targetLane;
        return;
      }
    }
    mapping.emplace_back(sourceLane, targetLane);
  }

  static void EraseValue(std::vector<int> &values, int value) {
    values.erase(std::remove(values.begin(), values.end(), value),
                 values.end());
  }

  [[nodiscard]] bool HasPlayableNote(const TimeLine &timeline, int lane) const {
    return lane >= 0 && lane < static_cast<int>(timeline.Notes.size()) &&
           timeline.Notes[lane] != nullptr;
  }

  [[nodiscard]] int TimelineMillis(const TimeLine &timeline) const {
    return static_cast<int>(timeline.Timing / 1000);
  }

  Mapping Randomize(TimeLine &timeline) {
    if (Spiral) {
      return SpiralShuffle();
    }
    if (AllScratch) {
      return AllScratchShuffle(timeline);
    }
    auto changeableLane = ChangeableLanes;
    auto assignableLane = AssignableLanes;
    auto randomMap = TimeBasedShuffle(timeline, changeableLane, assignableLane);
    UpdateNoteTime(timeline, randomMap);
    return randomMap;
  }

  Mapping SpiralShuffle() {
    Mapping rotateMap;
    if (ModifyLanes.empty()) {
      return rotateMap;
    }
    if (ChangeableLanes.size() == ModifyLanes.size()) {
      SpiralHead =
          (SpiralHead + SpiralIncrement) % static_cast<int>(ModifyLanes.size());
      for (size_t i = 0; i < ModifyLanes.size(); ++i) {
        rotateMap.emplace_back(
            ModifyLanes[i], ModifyLanes[(i + static_cast<size_t>(SpiralHead)) %
                                        ModifyLanes.size()]);
      }
    } else {
      for (size_t i = 0; i < ModifyLanes.size(); ++i) {
        if (std::find(ChangeableLanes.begin(), ChangeableLanes.end(),
                      ModifyLanes[i]) != ChangeableLanes.end()) {
          rotateMap.emplace_back(
              ModifyLanes[i],
              ModifyLanes[(i + static_cast<size_t>(SpiralHead)) %
                          ModifyLanes.size()]);
        }
      }
    }
    return rotateMap;
  }

  Mapping AllScratchShuffle(TimeLine &timeline) {
    Mapping randomMap;
    auto changeableLane = ChangeableLanes;
    auto assignableLane = AssignableLanes;

    if (!ScratchLanes.empty()) {
      const int scratchLane = ScratchLanes[ScratchIndex];
      const bool scratchAssignable =
          std::find(assignableLane.begin(), assignableLane.end(),
                    scratchLane) != assignableLane.end();
      if (scratchAssignable &&
          TimelineMillis(timeline) - LastNoteTime[scratchLane] >
              SranThresholdMillis) {
        int noteLane = -1;
        for (int lane : changeableLane) {
          if (HasPlayableNote(timeline, lane)) {
            noteLane = lane;
            break;
          }
        }
        if (noteLane != -1) {
          randomMap.emplace_back(noteLane, scratchLane);
          EraseValue(changeableLane, noteLane);
          EraseValue(assignableLane, scratchLane);
          ScratchIndex =
              (ScratchIndex + 1) % static_cast<int>(ScratchLanes.size());
        }
      }
    }

    auto restMap = TimeBasedShuffle(timeline, changeableLane, assignableLane);
    randomMap.insert(randomMap.end(), restMap.begin(), restMap.end());
    UpdateNoteTime(timeline, randomMap);
    return randomMap;
  }

  Mapping TimeBasedShuffle(const TimeLine &timeline,
                           std::vector<int> &changeableLane,
                           std::vector<int> &assignableLane) {
    Mapping randomMap;
    std::vector<int> noteLane;
    std::vector<int> emptyLane;
    std::vector<int> primaryLane;
    std::vector<int> inferiorLane;

    for (int lane : changeableLane) {
      if (HasPlayableNote(timeline, lane)) {
        noteLane.push_back(lane);
      } else {
        emptyLane.push_back(lane);
      }
    }
    for (int lane : assignableLane) {
      if (TimelineMillis(timeline) - LastNoteTime[lane] > ThresholdMillis) {
        primaryLane.push_back(lane);
      } else {
        inferiorLane.push_back(lane);
      }
    }

    while (!noteLane.empty() && !primaryLane.empty()) {
      const int index = SelectLane(primaryLane);
      randomMap.emplace_back(noteLane.front(), primaryLane[index]);
      noteLane.erase(noteLane.begin());
      primaryLane.erase(primaryLane.begin() + index);
    }

    while (!noteLane.empty() && !inferiorLane.empty()) {
      int minTime = std::numeric_limits<int>::max();
      for (int lane : inferiorLane) {
        minTime = std::min(minTime, LastNoteTime[lane]);
      }
      std::vector<int> minLane;
      for (int lane : inferiorLane) {
        if (LastNoteTime[lane] == minTime) {
          minLane.push_back(lane);
        }
      }
      const int selectedLane =
          minLane[Random.NextInt(static_cast<int>(minLane.size()))];
      randomMap.emplace_back(noteLane.front(), selectedLane);
      noteLane.erase(noteLane.begin());
      EraseValue(inferiorLane, selectedLane);
    }

    primaryLane.insert(primaryLane.end(), inferiorLane.begin(),
                       inferiorLane.end());
    while (!emptyLane.empty() && !primaryLane.empty()) {
      const int index = Random.NextInt(static_cast<int>(primaryLane.size()));
      randomMap.emplace_back(emptyLane.front(), primaryLane[index]);
      emptyLane.erase(emptyLane.begin());
      primaryLane.erase(primaryLane.begin() + index);
    }

    return randomMap;
  }

  int SelectLane(const std::vector<int> &lanes) {
    if (AllScratch && DoublePlay) {
      int selectedIndex = 0;
      for (int i = 1; i < static_cast<int>(lanes.size()); ++i) {
        if ((Player == 0 && lanes[i] < lanes[selectedIndex]) ||
            (Player != 0 && lanes[i] > lanes[selectedIndex])) {
          selectedIndex = i;
        }
      }
      return selectedIndex;
    }
    return Random.NextInt(static_cast<int>(lanes.size()));
  }

  void UpdateNoteTime(const TimeLine &timeline, const Mapping &randomMap) {
    for (const auto &entry : randomMap) {
      if (HasPlayableNote(timeline, entry.first)) {
        LastNoteTime[entry.second] = TimelineMillis(timeline);
      }
    }
  }
};

std::vector<int> ScratchLanesForPlayer(const ChartMeta &meta, int player) {
  auto scratchLanes = meta.GetScratchLaneIndices();
  if (!meta.IsDP || scratchLanes.size() <= 1) {
    return scratchLanes;
  }
  if (player < 0 || player >= static_cast<int>(scratchLanes.size())) {
    return {};
  }
  return {scratchLanes[static_cast<size_t>(player)]};
}

std::vector<int> MakeRandomLaneMap(const std::vector<int> &keys,
                                   size_t laneCount, long long seed) {
  JavaRandom random(seed);
  auto remaining = keys;
  std::vector<int> result(laneCount);
  for (size_t i = 0; i < laneCount; ++i) {
    result[i] = static_cast<int>(i);
  }
  for (int key : keys) {
    const int index = random.NextInt(static_cast<int>(remaining.size()));
    result[static_cast<size_t>(key)] = remaining[static_cast<size_t>(index)];
    remaining.erase(remaining.begin() + index);
  }
  return result;
}

} // namespace

BaseModifier::BaseModifier(long long seed, int player)
    : Seed(seed >= 0 ? seed : CreateSeed()), Player(player) {}

BaseModifier::~BaseModifier() = default;

void BaseModifier::SetSeed(long long seed) {
  if (seed >= 0) {
    Seed = seed;
  }
}

long long BaseModifier::GetSeed() const { return Seed; }

void BaseModifier::SetPlayer(int player) { Player = player; }

int BaseModifier::GetPlayer() const { return Player; }

std::vector<int> BaseModifier::GetModifyLanes(const ChartMeta &meta,
                                              bool includeScratch) const {
  auto keyLanes = meta.GetKeyLaneIndices();
  std::vector<int> result;
  if (meta.IsDP) {
    const int keysPerPlayer = meta.KeyMode / 2;
    const int player = Player < 0 ? 0 : Player;
    if (player >= meta.GetScratchLaneCount()) {
      return {};
    }
    const int begin = keysPerPlayer * player;
    const int end = begin + keysPerPlayer;
    if (begin >= 0 && end <= static_cast<int>(keyLanes.size())) {
      result.insert(result.end(), keyLanes.begin() + begin,
                    keyLanes.begin() + end);
    }
    if (includeScratch) {
      auto scratchLanes = meta.GetScratchLaneIndices();
      if (player < static_cast<int>(scratchLanes.size())) {
        result.push_back(scratchLanes[static_cast<size_t>(player)]);
      }
    }
    return result;
  }

  result = std::move(keyLanes);
  if (includeScratch) {
    auto scratchLanes = meta.GetScratchLaneIndices();
    result.insert(result.end(), scratchLanes.begin(), scratchLanes.end());
  }
  return result;
}

void BaseModifier::RecalculateNoteCounts(Chart &chart) {
  int totalNotes = 0;
  int totalLongNotes = 0;
  int totalScratchNotes = 0;
  int totalBackSpinNotes = 0;
  int totalLandmineNotes = 0;

  for (const auto *timeline : GetAllTimeLines(chart)) {
    for (size_t lane = 0; lane < timeline->Notes.size(); ++lane) {
      const Note *note = timeline->Notes[lane];
      if (note == nullptr || IsLongTail(note)) {
        continue;
      }
      ++totalNotes;
      if (IsLongHead(note)) {
        if (IsScratchLane(chart.Meta, static_cast<int>(lane))) {
          ++totalBackSpinNotes;
        } else {
          ++totalLongNotes;
        }
      } else if (IsScratchLane(chart.Meta, static_cast<int>(lane))) {
        ++totalScratchNotes;
      }
    }
    for (const auto *landmine : timeline->LandmineNotes) {
      if (landmine != nullptr) {
        ++totalLandmineNotes;
      }
    }
  }

  chart.Meta.TotalNotes = totalNotes;
  chart.Meta.TotalLongNotes = totalLongNotes;
  chart.Meta.TotalScratchNotes = totalScratchNotes;
  chart.Meta.TotalBackSpinNotes = totalBackSpinNotes;
  chart.Meta.TotalLandmineNotes = totalLandmineNotes;
}

const char *ToString(PlayOptionModifier option) {
  switch (option) {
  case PlayOptionModifier::Normal:
    return "NORMAL";
  case PlayOptionModifier::Mirror:
    return "MIRROR";
  case PlayOptionModifier::Random:
    return "RANDOM";
  case PlayOptionModifier::RRandom:
    return "R-RANDOM";
  case PlayOptionModifier::SRandom:
    return "S-RANDOM";
  case PlayOptionModifier::Spiral:
    return "SPIRAL";
  case PlayOptionModifier::HRandom:
    return "H-RANDOM";
  case PlayOptionModifier::AllScratch:
    return "ALL-SCR";
  case PlayOptionModifier::RandomEx:
    return "RANDOM-EX";
  case PlayOptionModifier::SRandomEx:
    return "S-RANDOM-EX";
  }
  return "NORMAL";
}

std::unique_ptr<BaseModifier>
CreatePlayOptionModifier(PlayOptionModifier option, long long seed, int player,
                         int hranThresholdBpm) {
  switch (option) {
  case PlayOptionModifier::Normal:
    return std::make_unique<IdentityModifier>(seed, player);
  case PlayOptionModifier::Mirror:
    return std::make_unique<MirrorModifier>(player);
  case PlayOptionModifier::Random:
    return std::make_unique<RandomModifier>(seed, player);
  case PlayOptionModifier::RRandom:
    return std::make_unique<RRandomModifier>(seed, player);
  case PlayOptionModifier::SRandom:
    return std::make_unique<SRandomModifier>(seed, player);
  case PlayOptionModifier::Spiral:
    return std::make_unique<SpiralModifier>(seed, player);
  case PlayOptionModifier::HRandom:
    return std::make_unique<HRandomModifier>(seed, player, hranThresholdBpm);
  case PlayOptionModifier::AllScratch:
    return std::make_unique<AllScratchModifier>(seed, player, hranThresholdBpm);
  case PlayOptionModifier::RandomEx:
    return std::make_unique<RandomExModifier>(seed, player);
  case PlayOptionModifier::SRandomEx:
    return std::make_unique<SRandomExModifier>(seed, player);
  }
  return std::make_unique<IdentityModifier>(seed, player);
}

std::unique_ptr<BaseModifier> CreatePlayOptionModifier(std::string_view option,
                                                       long long seed,
                                                       int player,
                                                       int hranThresholdBpm) {
  const auto normalized = NormalizeOption(option);
  if (normalized == "NORMAL" || normalized == "OFF" || normalized.empty()) {
    return CreatePlayOptionModifier(PlayOptionModifier::Normal, seed, player,
                                    hranThresholdBpm);
  }
  if (normalized == "MIRROR") {
    return CreatePlayOptionModifier(PlayOptionModifier::Mirror, seed, player,
                                    hranThresholdBpm);
  }
  if (normalized == "RANDOM") {
    return CreatePlayOptionModifier(PlayOptionModifier::Random, seed, player,
                                    hranThresholdBpm);
  }
  if (normalized == "R-RANDOM" || normalized == "ROTATE") {
    return CreatePlayOptionModifier(PlayOptionModifier::RRandom, seed, player,
                                    hranThresholdBpm);
  }
  if (normalized == "S-RANDOM") {
    return CreatePlayOptionModifier(PlayOptionModifier::SRandom, seed, player,
                                    hranThresholdBpm);
  }
  if (normalized == "SPIRAL") {
    return CreatePlayOptionModifier(PlayOptionModifier::Spiral, seed, player,
                                    hranThresholdBpm);
  }
  if (normalized == "H-RANDOM") {
    return CreatePlayOptionModifier(PlayOptionModifier::HRandom, seed, player,
                                    hranThresholdBpm);
  }
  if (normalized == "ALL-SCR") {
    return CreatePlayOptionModifier(PlayOptionModifier::AllScratch, seed,
                                    player, hranThresholdBpm);
  }
  if (normalized == "RANDOM-EX") {
    return CreatePlayOptionModifier(PlayOptionModifier::RandomEx, seed, player,
                                    hranThresholdBpm);
  }
  if (normalized == "S-RANDOM-EX") {
    return CreatePlayOptionModifier(PlayOptionModifier::SRandomEx, seed, player,
                                    hranThresholdBpm);
  }
  return nullptr;
}

LaneShuffleModifier::LaneShuffleModifier(bool includeScratch, long long seed,
                                         int player)
    : BaseModifier(seed, player), IncludeScratch(includeScratch) {}

bool LaneShuffleModifier::IncludesScratch() const { return IncludeScratch; }

void LaneShuffleModifier::Modify(Chart &chart) {
  const auto keys = GetModifyLanes(chart.Meta, IncludeScratch);
  if (keys.empty()) {
    return;
  }

  size_t laneCount = 0;
  for (const auto *timeline : GetAllTimeLines(chart)) {
    laneCount = std::max(laneCount, timeline->Notes.size());
  }
  const auto laneMap = MakeLaneMap(chart, keys, laneCount);
  if (laneMap.empty()) {
    return;
  }

  for (auto *timeline : GetAllTimeLines(chart)) {
    std::vector<Note *> notes = timeline->Notes;
    std::vector<Note *> hiddenNotes = timeline->InvisibleNotes;
    std::vector<LandmineNote *> landmineNotes = timeline->LandmineNotes;

    for (size_t lane = 0; lane < laneMap.size() && lane < notes.size();
         ++lane) {
      const int sourceLane = laneMap[lane];
      if (sourceLane < 0 || sourceLane >= static_cast<int>(notes.size())) {
        continue;
      }
      AssignNote(*timeline, static_cast<int>(lane), notes[sourceLane]);
      AssignHiddenNote(*timeline, static_cast<int>(lane),
                       hiddenNotes[sourceLane]);
      AssignLandmineNote(*timeline, static_cast<int>(lane),
                         landmineNotes[sourceLane]);
    }
  }
  RecalculateNoteCounts(chart);
}

MirrorModifier::MirrorModifier(int player)
    : LaneShuffleModifier(false, -1, player) {}

MirrorModifier::MirrorModifier(long long seed, int player)
    : LaneShuffleModifier(false, seed, player) {}

const char *MirrorModifier::Name() const { return "MIRROR"; }

std::vector<int> MirrorModifier::MakeLaneMap(const Chart &chart,
                                             const std::vector<int> &keys,
                                             size_t laneCount) {
  (void)chart;
  std::vector<int> result(laneCount);
  for (size_t i = 0; i < laneCount; ++i) {
    result[i] = static_cast<int>(i);
  }
  for (size_t lane = 0; lane < keys.size(); ++lane) {
    result[static_cast<size_t>(keys[lane])] = keys[keys.size() - 1 - lane];
  }
  return result;
}

RandomModifier::RandomModifier(long long seed, int player)
    : LaneShuffleModifier(false, seed, player) {}

const char *RandomModifier::Name() const { return "RANDOM"; }

std::vector<int> RandomModifier::MakeLaneMap(const Chart &chart,
                                             const std::vector<int> &keys,
                                             size_t laneCount) {
  (void)chart;
  return MakeRandomLaneMap(keys, laneCount, GetSeed());
}

RRandomModifier::RRandomModifier(long long seed, int player)
    : LaneShuffleModifier(false, seed, player) {}

const char *RRandomModifier::Name() const { return "R-RANDOM"; }

std::vector<int> RRandomModifier::MakeLaneMap(const Chart &chart,
                                              const std::vector<int> &keys,
                                              size_t laneCount) {
  (void)chart;
  JavaRandom random(GetSeed());
  std::vector<int> result(laneCount);
  for (size_t i = 0; i < laneCount; ++i) {
    result[i] = static_cast<int>(i);
  }
  if (keys.size() <= 1) {
    return result;
  }

  const bool inc = random.NextInt(2) == 1;
  const int start =
      random.NextInt(static_cast<int>(keys.size()) - 1) + (inc ? 1 : 0);
  int rotatedLane = start;
  for (int key : keys) {
    result[static_cast<size_t>(key)] = keys[static_cast<size_t>(rotatedLane)];
    rotatedLane = inc ? (rotatedLane + 1) % static_cast<int>(keys.size())
                      : (rotatedLane + static_cast<int>(keys.size()) - 1) %
                            static_cast<int>(keys.size());
  }
  return result;
}

RandomExModifier::RandomExModifier(long long seed, int player)
    : LaneShuffleModifier(true, seed, player) {}

const char *RandomExModifier::Name() const { return "RANDOM-EX"; }

std::vector<int> RandomExModifier::MakeLaneMap(const Chart &chart,
                                               const std::vector<int> &keys,
                                               size_t laneCount) {
  (void)chart;
  return MakeRandomLaneMap(keys, laneCount, GetSeed());
}

NoteShuffleModifier::NoteShuffleModifier(bool includeScratch,
                                         int keyRepeatThresholdMillis,
                                         bool allScratch, bool spiral,
                                         long long seed, int player)
    : BaseModifier(seed, player), IncludeScratch(includeScratch),
      KeyRepeatThresholdMillis(keyRepeatThresholdMillis),
      AllScratch(allScratch), Spiral(spiral) {}

void NoteShuffleModifier::Modify(Chart &chart) {
  const auto lanes = GetModifyLanes(chart.Meta, IncludeScratch);
  if (lanes.empty()) {
    return;
  }

  auto scratchLanes = AllScratch
                          ? ScratchLanesForPlayer(chart.Meta, GetPlayer())
                          : std::vector<int>{};
  TimelineNoteRandomizer randomizer(lanes, GetSeed(), KeyRepeatThresholdMillis,
                                    AllScratch, Spiral, scratchLanes,
                                    GetPlayer(), chart.Meta.IsDP);
  for (auto *timeline : GetAllTimeLines(chart)) {
    if (HasAnyLaneNote(*timeline)) {
      randomizer.Permutate(*timeline);
    }
  }
  RecalculateNoteCounts(chart);
}

SRandomModifier::SRandomModifier(long long seed, int player)
    : NoteShuffleModifier(false, SranThresholdMillis, false, false, seed,
                          player) {}

const char *SRandomModifier::Name() const { return "S-RANDOM"; }

SpiralModifier::SpiralModifier(long long seed, int player)
    : NoteShuffleModifier(false, SranThresholdMillis, false, true, seed,
                          player) {}

const char *SpiralModifier::Name() const { return "SPIRAL"; }

HRandomModifier::HRandomModifier(long long seed, int player, int thresholdBpm)
    : NoteShuffleModifier(false, ThresholdMillisFromBpm(thresholdBpm), false,
                          false, seed, player) {}

HRandomModifier HRandomModifier::FromThresholdMillis(long long seed, int player,
                                                     int thresholdMillis) {
  return HRandomModifier(seed, player,
                         thresholdMillis > 0 ? static_cast<int>(std::ceil(
                                                   15000.0 / thresholdMillis))
                                             : 0);
}

const char *HRandomModifier::Name() const { return "H-RANDOM"; }

AllScratchModifier::AllScratchModifier(long long seed, int player,
                                       int thresholdBpm)
    : NoteShuffleModifier(true, ThresholdMillisFromBpm(thresholdBpm), true,
                          false, seed, player) {}

AllScratchModifier
AllScratchModifier::FromThresholdMillis(long long seed, int player,
                                        int thresholdMillis) {
  return AllScratchModifier(
      seed, player,
      thresholdMillis > 0
          ? static_cast<int>(std::ceil(15000.0 / thresholdMillis))
          : 0);
}

const char *AllScratchModifier::Name() const { return "ALL-SCR"; }

SRandomExModifier::SRandomExModifier(long long seed, int player)
    : NoteShuffleModifier(true, SranThresholdMillis, false, false, seed,
                          player) {}

const char *SRandomExModifier::Name() const { return "S-RANDOM-EX"; }

} // namespace bms_parser
