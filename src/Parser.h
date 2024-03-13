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
#include <string>
#include <map>
#include <atomic>
/**
 *
 */
namespace bms_parser
{
	class Parser
	{
	public:
		Parser();
		void SetRandomSeed(int RandomSeed);
		void Parse(std::wstring path, Chart **Chart, bool addReadyMeasure, bool metaOnly, std::atomic_bool &bCancelled);
		~Parser();
		static int NoWav;
		static int MetronomeWav;

	private:
		// bpmTable
		std::map<int, double> BpmTable;
		std::map<int, double> StopLengthTable;

		// abstract analysis for control branches
		std::pair<int, int> BpmInterval;

		int Lnobj = -1;
		int Lntype = 1;
		int Seed;
		int DecodeBase36(const std::wstring &Str);
		void ParseHeader(Chart *Chart, const std::wstring &Cmd, const std::wstring &Xx, std::wstring Value);
		bool MatchHeader(const std::wstring &str, const std::wstring &headerUpper);
		static int Gcd(int A, int B);
		static bool CheckResourceIdRange(int Id);
		int ToWaveId(Chart *Chart, const std::wstring &Wav, bool metaOnly);
	};
}
