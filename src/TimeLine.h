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

#include "LandmineNote.h"
#include "Note.h"
#include <vector>

/**
 *
 */
namespace bms_parser {
class TimeLine {
public:
  std::vector<Note *> BackgroundNotes;
  std::vector<Note *> InvisibleNotes;
  std::vector<Note *> Notes;
  std::vector<LandmineNote *> LandmineNotes;
  double Bpm = 0;
  bool BpmChange = false;
  bool BpmChangeApplied = false;
  int BgaBase = -1;
  int BgaLayer = -1;
  int BgaPoor = -1;

  double StopLength = 0;
  double Scroll = 1;

  // musical timing in microseconds
  long long Timing = 0;

  // beat position; measure number + position in
  // measure. 1.25 means 1 measure and 1/4 beat
  double BeatPosition = 0;

  explicit TimeLine(int lanes, bool metaOnly);

  TimeLine *SetNote(int lane, Note *note);

  TimeLine *SetInvisibleNote(int lane, Note *note);

  TimeLine *SetLandmineNote(int lane, LandmineNote *note);

  TimeLine *AddBackgroundNote(Note *note);

  [[nodiscard]] double GetStopDuration() const;

  ~TimeLine();
  bool IsFirstInMeasure = false;
};
} // namespace bms_parser
