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

#include "TimeLine.h"
#include <cmath>
#include <limits>
namespace bms_parser {
TimeLine::TimeLine(int lanes, bool metaOnly) {
  if (metaOnly) {
    return;
  }
  Notes.resize(lanes, nullptr);
  InvisibleNotes.resize(lanes, nullptr);
  LandmineNotes.resize(lanes, nullptr);
}

TimeLine *TimeLine::SetNote(int lane, Note *note) {
  Notes[lane] = note;
  note->Lane = lane;
  note->Timeline = this;
  return this;
}

TimeLine *TimeLine::SetInvisibleNote(int lane, Note *note) {
  if (InvisibleNotes[lane] != note) delete InvisibleNotes[lane];
  InvisibleNotes[lane] = note;
  if (note) {
    note->Lane = lane;
    note->Timeline = this;
  }
  return this;
}

TimeLine *TimeLine::SetLandmineNote(int lane, LandmineNote *note) {
  LandmineNotes[lane] = note;
  note->Lane = lane;
  note->Timeline = this;
  return this;
}

TimeLine *TimeLine::AddBackgroundNote(Note *note) {
  BackgroundNotes.push_back(note);
  note->Timeline = this;
  return this;
}

double TimeLine::GetStopDuration() const {
  if (ParsedStopDuration) return static_cast<double>(*ParsedStopDuration);
  // Section.java divides the definition by 192 before converting to long.
  const double duration = 240000000.0 * (StopLength / 192.0) / Bpm;
  if (std::isnan(duration)) return 0;
  if (duration >= static_cast<double>(std::numeric_limits<long long>::max()))
    return static_cast<double>(std::numeric_limits<long long>::max());
  if (duration <= static_cast<double>(std::numeric_limits<long long>::min()))
    return static_cast<double>(std::numeric_limits<long long>::min());
  return std::trunc(duration);
}

TimeLine::~TimeLine() {
  for (const auto &note : Notes) {
    delete note;
  }
  Notes.clear();
  for (const auto &note : InvisibleNotes) {
    delete note;
  }
  InvisibleNotes.clear();
  for (const auto &note : LandmineNotes) {
    delete note;
  }
  LandmineNotes.clear();
  for (const auto &note : BackgroundNotes) {
    delete note;
  }
  BackgroundNotes.clear();
}
} // namespace bms_parser
