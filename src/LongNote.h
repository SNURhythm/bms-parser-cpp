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

#include "Note.h"
/**
 *
 */
namespace bms_parser {
class LongNote : public Note {
public:
  ~LongNote() override;
  LongNote *Tail;
  LongNote *Head = nullptr;
  bool IsHolding = false;
  [[nodiscard]] bool IsTail() const;
  long long ReleaseTime{};

  explicit LongNote(int Wav);

  void Press(long long Time) override;

  void Release(long long Time);

  void MissPress(long long Time);

  void Reset() override;

  bool IsLongNote() override { return true; }
};
} // namespace bms_parser
