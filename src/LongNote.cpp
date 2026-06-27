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

#include "LongNote.h"
namespace bms_parser {
LongNoteType LongNoteTypeFromLnMode(int LnMode) {
  switch (LnMode) {
  case 1:
    return LongNoteType::LongNote;
  case 2:
    return LongNoteType::ChargeNote;
  case 3:
    return LongNoteType::HellChargeNote;
  default:
    return LongNoteType::Undefined;
  }
}

LongNoteType ResolveLongNoteType(LongNoteType Type, int LnMode) {
  if (Type != LongNoteType::Undefined) {
    return Type;
  }
  return LongNoteTypeFromLnMode(LnMode);
}

bool LongNote::IsTail() const { return Tail == nullptr; }

LongNote::LongNote(int Wav, LongNoteType Type) : Note(Wav), Type(Type) {
  Tail = nullptr;
}

LongNoteType LongNote::GetType() const { return Type; }

void LongNote::SetType(LongNoteType Type) { this->Type = Type; }

void LongNote::Press(long long Time) {
  Play(Time);
  IsHolding = true;
  Tail->IsHolding = true;
}

void LongNote::Release(long long Time) {
  Play(Time);
  IsHolding = false;
  Head->IsHolding = false;
  ReleaseTime = Time;
}

void LongNote::MissPress(long long Time) {}

void LongNote::Reset() {
  Note::Reset();
  IsHolding = false;
  ReleaseTime = 0;
}

LongNote::~LongNote() {
  Head = nullptr;
  Tail = nullptr;
}
} // namespace bms_parser
