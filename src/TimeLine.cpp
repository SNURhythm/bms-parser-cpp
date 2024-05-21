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
  InvisibleNotes[lane] = note;
  note->Lane = lane;
  note->Timeline = this;
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

double TimeLine::GetStopDuration() {
  return 1250000.0 * StopLength / Bpm; // 1250000 = 240 * 1000 * 1000 / 192
}

TimeLine::~TimeLine() {
  for (const auto &note : Notes) {
    if (note != nullptr) {
      delete note;
    }
  }
  Notes.clear();
  for (const auto &note : InvisibleNotes) {
    if (note != nullptr) {
      delete note;
    }
  }
  InvisibleNotes.clear();
  for (const auto &note : LandmineNotes) {
    if (note != nullptr) {
      delete note;
    }
  }
  LandmineNotes.clear();
  for (const auto &note : BackgroundNotes) {
    if (note != nullptr) {
      delete note;
    }
  }
  BackgroundNotes.clear();
}
} // namespace bms_parser
