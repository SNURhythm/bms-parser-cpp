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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bms_parser {

enum class PlayOptionModifier {
  Normal,
  Mirror,
  Random,
  RRandom,
  SRandom,
  Spiral,
  HRandom,
  AllScratch,
  RandomEx,
  SRandomEx,
};

class BaseModifier {
public:
  explicit BaseModifier(long long seed = -1, int player = 0);
  virtual ~BaseModifier();

  virtual void Modify(Chart &chart) = 0;
  [[nodiscard]] virtual const char *Name() const = 0;

  void SetSeed(long long seed);
  [[nodiscard]] long long GetSeed() const;

  void SetPlayer(int player);
  [[nodiscard]] int GetPlayer() const;

  static void RecalculateNoteCounts(Chart &chart);

protected:
  [[nodiscard]] std::vector<int> GetModifyLanes(const ChartMeta &meta,
                                                bool includeScratch) const;

private:
  long long Seed;
  int Player;
};

[[nodiscard]] const char *ToString(PlayOptionModifier option);
[[nodiscard]] std::unique_ptr<BaseModifier>
CreatePlayOptionModifier(PlayOptionModifier option, long long seed = -1,
                         int player = 0, int hranThresholdBpm = 120);
[[nodiscard]] std::unique_ptr<BaseModifier>
CreatePlayOptionModifier(std::string_view option, long long seed = -1,
                         int player = 0, int hranThresholdBpm = 120);

class LaneShuffleModifier : public BaseModifier {
public:
  void Modify(Chart &chart) override;

protected:
  LaneShuffleModifier(bool includeScratch, long long seed = -1, int player = 0);

  [[nodiscard]] bool IncludesScratch() const;
  [[nodiscard]] virtual std::vector<int>
  MakeLaneMap(const Chart &chart, const std::vector<int> &keys,
              size_t laneCount) = 0;

private:
  bool IncludeScratch;
};

class MirrorModifier final : public LaneShuffleModifier {
public:
  explicit MirrorModifier(int player = 0);
  MirrorModifier(long long seed, int player);

  [[nodiscard]] const char *Name() const override;

private:
  [[nodiscard]] std::vector<int> MakeLaneMap(const Chart &chart,
                                             const std::vector<int> &keys,
                                             size_t laneCount) override;
};

class RandomModifier final : public LaneShuffleModifier {
public:
  explicit RandomModifier(long long seed = -1, int player = 0);

  [[nodiscard]] const char *Name() const override;

private:
  [[nodiscard]] std::vector<int> MakeLaneMap(const Chart &chart,
                                             const std::vector<int> &keys,
                                             size_t laneCount) override;
};

class RRandomModifier final : public LaneShuffleModifier {
public:
  explicit RRandomModifier(long long seed = -1, int player = 0);

  [[nodiscard]] const char *Name() const override;

private:
  [[nodiscard]] std::vector<int> MakeLaneMap(const Chart &chart,
                                             const std::vector<int> &keys,
                                             size_t laneCount) override;
};

class RandomExModifier final : public LaneShuffleModifier {
public:
  explicit RandomExModifier(long long seed = -1, int player = 0);

  [[nodiscard]] const char *Name() const override;

private:
  [[nodiscard]] std::vector<int> MakeLaneMap(const Chart &chart,
                                             const std::vector<int> &keys,
                                             size_t laneCount) override;
};

class NoteShuffleModifier : public BaseModifier {
public:
  void Modify(Chart &chart) override;

protected:
  NoteShuffleModifier(bool includeScratch, int keyRepeatThresholdMillis,
                      bool allScratch, bool spiral, long long seed = -1,
                      int player = 0);

private:
  bool IncludeScratch;
  int KeyRepeatThresholdMillis;
  bool AllScratch;
  bool Spiral;
};

class SRandomModifier final : public NoteShuffleModifier {
public:
  explicit SRandomModifier(long long seed = -1, int player = 0);

  [[nodiscard]] const char *Name() const override;
};

class SpiralModifier final : public NoteShuffleModifier {
public:
  explicit SpiralModifier(long long seed = -1, int player = 0);

  [[nodiscard]] const char *Name() const override;
};

class HRandomModifier final : public NoteShuffleModifier {
public:
  explicit HRandomModifier(long long seed = -1, int player = 0,
                           int thresholdBpm = 120);
  static HRandomModifier FromThresholdMillis(long long seed, int player,
                                             int thresholdMillis);

  [[nodiscard]] const char *Name() const override;
};

class AllScratchModifier final : public NoteShuffleModifier {
public:
  explicit AllScratchModifier(long long seed = -1, int player = 0,
                              int thresholdBpm = 120);
  static AllScratchModifier FromThresholdMillis(long long seed, int player,
                                                int thresholdMillis);

  [[nodiscard]] const char *Name() const override;
};

class SRandomExModifier final : public NoteShuffleModifier {
public:
  explicit SRandomExModifier(long long seed = -1, int player = 0);

  [[nodiscard]] const char *Name() const override;
};

} // namespace bms_parser
