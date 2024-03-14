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
		void Parse(std::wstring_view path, Chart **Chart, bool addReadyMeasure, bool metaOnly, std::atomic_bool &bCancelled);
		~Parser();
		void Parse(const std::vector<unsigned char>& bytes, Chart **chart, bool addReadyMeasure, bool metaOnly, std::atomic_bool &bCancelled);
		static int NoWav;
		static int MetronomeWav;

	private:
		// bpmTable
		std::unordered_map<int, double> BpmTable;
		std::unordered_map<int, double> StopLengthTable;

		int Lnobj = -1;
		int Lntype = 1;
		int Seed;
		int DecodeBase36(std::wstring_view Str);
		void ParseHeader(Chart *Chart, std::wstring_view cmd, std::wstring_view Xx, const std::wstring &Value);
		bool MatchHeader(const std::wstring_view &str, const std::wstring_view &headerUpper);
		static int Gcd(int A, int B);
		static bool CheckResourceIdRange(int Id);
		int ToWaveId(Chart *Chart, std::wstring_view Wav, bool metaOnly);
	};
}
