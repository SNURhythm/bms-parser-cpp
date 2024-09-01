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

namespace bms_parser {
class TimeLine;

class Note {
public:
  int Lane = 0;
  int Wav = 0;

  bool IsPlayed = false;
  bool IsDead = false;
  long long PlayedTime = 0;
  TimeLine *Timeline = nullptr;

  // private Note nextNote;

  explicit Note(int Wav);

  void Play(long long Time);

  virtual void Press(long long Time);

  virtual void Reset();
  virtual ~Note();

  virtual bool IsLongNote() { return false; }
  virtual bool IsLandmineNote() { return false; }
};
} // namespace bms_parser
