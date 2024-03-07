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

#include "Note.h"
namespace bms_parser
{
	Note::Note(int wav)
	{
		Wav = wav;
	}

	void Note::Play(long long Time)
	{
		IsPlayed = true;
		PlayedTime = Time;
	}

	void Note::Press(long long Time)
	{
		Play(Time);
	}

	void Note::Reset()
	{
		IsPlayed = false;
		IsDead = false;
		PlayedTime = 0;
	}

	Note::~Note()
	{
		Timeline = nullptr;
	}
}
