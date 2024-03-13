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

#include "Parser.h"
#include <iterator>
#include <random>
#include "LongNote.h"
#include "Note.h"
#include "LandmineNote.h"
#include "TimeLine.h"
#include "Measure.h"
#include "ShiftJISConverter.h"

#include <filesystem>
#include <regex>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include "md5.h"
#include "SHA256.h"
#include <cctype>
#ifndef BMS_PARSER_VERBOSE
#define BMS_PARSER_VERBOSE 0
#endif

namespace bms_parser
{
	enum Channel
	{
		LaneAutoplay = 1,
		SectionRate = 2,
		BpmChange = 3,
		BgaPlay = 4,
		PoorPlay = 6,
		LayerPlay = 7,
		BpmChangeExtend = 8,
		Stop = 9,

		P1KeyBase = 1 * 36 + 1,
		P2KeyBase = 2 * 36 + 1,
		P1InvisibleKeyBase = 3 * 36 + 1,
		P2InvisibleKeyBase = 4 * 36 + 1,
		P1LongKeyBase = 5 * 36 + 1,
		P2LongKeyBase = 6 * 36 + 1,
		P1MineKeyBase = 13 * 36 + 1,
		P2MineKeyBase = 14 * 36 + 1
	};

	namespace KeyAssign
	{
		int Beat7[] = {0, 1, 2, 3, 4, 7, -1, 5, 6, 8, 9, 10, 11, 12, 15, -1, 13, 14};
		int PopN[] = {0, 1, 2, 3, 4, -1, -1, -1, -1, -1, 5, 6, 7, 8, -1, -1, -1, -1};
	};

	constexpr int TempKey = 16;
	constexpr int Scroll = 1020;

	Parser::Parser() : BpmTable{}, StopLengthTable{}
	{
		std::random_device seeder;
		Seed = seeder();
	}

	void Parser::SetRandomSeed(int RandomSeed)
	{
		Seed = RandomSeed;
	}

	int Parser::NoWav = -1;
	int Parser::MetronomeWav = -2;
	bool Parser::MatchHeader(const std::wstring_view &str, const std::wstring_view &headerUpper)
	{
		auto size = headerUpper.length();
		if (str.length() < size)
		{
			return false;
		}
		for (size_t i = 0; i < size; ++i)
		{
			if (std::towupper(str[i]) != headerUpper[i])
			{
				return false;
			}
		}
		return true;
	}
	void Parser::Parse(std::wstring path, Chart **chart, bool addReadyMeasure, bool metaOnly, std::atomic_bool &bCancelled)
	{
		auto new_chart = new Chart();
		*chart = new_chart;
		new_chart->Meta.BmsPath = path;
		std::filesystem::path fpath = path;
		new_chart->Meta.Folder = fpath.parent_path().wstring();
		std::wregex headerRegex(L"^#([A-Za-z]+?)(\\d\\d)? +?(.+)?");

		auto measures = std::unordered_map<int, std::vector<std::pair<int, std::wstring>>>();
		std::vector<unsigned char> bytes;

		std::ifstream file(fpath, std::ios::binary);
		if (!file.is_open())
		{
			std::wcout << "Failed to open file: " << path << std::endl;
			return;
		}
#if BMS_PARSER_VERBOSE == 1
		// measure file read time
		auto startTime = std::chrono::high_resolution_clock::now();
#endif
		file.seekg(0, std::ios::end);
		auto size = file.tellg();
		file.seekg(0, std::ios::beg);
		bytes.resize(static_cast<size_t>(size));
		file.read(reinterpret_cast<char *>(bytes.data()), size);
		file.close();
#if BMS_PARSER_VERBOSE == 1
		std::cout << "File read took " << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startTime).count() << "\n";
#endif
		if (bCancelled)
		{
			return;
		}
#if BMS_PARSER_VERBOSE == 1
		startTime = std::chrono::high_resolution_clock::now();
#endif
		MD5 md5;
		md5.update(bytes.data(), bytes.size());
		md5.finalize();
		new_chart->Meta.MD5 = md5.hexdigest();
		new_chart->Meta.SHA256 = sha256(bytes);
#if BMS_PARSER_VERBOSE == 1
		std::cout << "Hashing took " << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startTime).count() << "\n";
#endif
		// std::cout<<"file size: "<<size<<std::endl;
		// bytes to std::wstring
#if BMS_PARSER_VERBOSE == 1
		startTime = std::chrono::high_resolution_clock::now();
#endif
		std::wstring content;
		ShiftJISConverter::BytesToUTF8(bytes.data(), bytes.size(), content);
#if BMS_PARSER_VERBOSE == 1
		std::cout << "ShiftJIS-UTF8 conversion took " << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startTime).count() << "\n";
#endif
		// std::wcout<<content<<std::endl;
		std::vector<int> RandomStack;
		std::vector<bool> SkipStack;
		// init prng with seed
		std::mt19937_64 Prng(Seed);

		std::wstring line;
		std::wistringstream stream(content);
#if BMS_PARSER_VERBOSE == 1
		startTime = std::chrono::high_resolution_clock::now();
#endif
		auto lastMeasure = -1;
		while (std::getline(stream, line))
		{
			if (bCancelled)
			{
				return;
			}
			// std::cout << line << std::endl;
			if (line.size() <= 1 || line[0] != L'#')
				continue;
			if (bCancelled)
			{
				return;
			}


			if (MatchHeader(line, L"#IF")) // #IF n
			{
				if (RandomStack.empty())
				{
					// UE_LOG(LogTemp, Warning, TEXT("RandomStack is empty!"));
					continue;
				}
				int CurrentRandom = RandomStack.back();
				int n = static_cast<int>(std::wcstol(line.substr(4).c_str(), nullptr, 10));
				SkipStack.push_back(CurrentRandom != n);
				continue;
			}
			if (MatchHeader(line, L"#ELSE"))
			{
				if (SkipStack.empty())
				{
					// UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
					continue;
				}
				bool CurrentSkip = SkipStack.back();
				SkipStack.pop_back();
				SkipStack.push_back(!CurrentSkip);
				continue;
			}
			if (MatchHeader(line, L"#ELSEIF"))
			{
				if (SkipStack.empty())
				{
					// UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
					continue;
				}
				bool CurrentSkip = SkipStack.back();
				SkipStack.pop_back();
				int CurrentRandom = RandomStack.back();
				int n = static_cast<int>(std::wcstol(line.substr(8).c_str(), nullptr, 10));
				SkipStack.push_back(CurrentSkip && CurrentRandom != n);
				continue;
			}
			if (MatchHeader(line, L"#ENDIF") || MatchHeader(line, L"#END IF"))
			{
				if (SkipStack.empty())
				{
					// UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
					continue;
				}
				SkipStack.pop_back();
				continue;
			}
			if (!SkipStack.empty() && SkipStack.back())
			{
				continue;
			}
			if (MatchHeader(line, L"#RANDOM") || MatchHeader(line, L"#RONDAM")) // #RANDOM n
			{
				int n = static_cast<int>(std::wcstol(line.substr(7).c_str(), nullptr, 10));
				std::uniform_int_distribution<int> dist(1, n);
				RandomStack.push_back(dist(Prng));
				continue;
			}
			if (MatchHeader(line, L"#ENDRANDOM"))
			{
				if (RandomStack.empty())
				{
					// UE_LOG(LogTemp, Warning, TEXT("RandomStack is empty!"));
					continue;
				}
				RandomStack.pop_back();
				continue;
			}

			if (line.length() >= 7 && std::isdigit(line[1]) && std::isdigit(line[2]) && std::isdigit(line[3]) && line[6] == ':')
			{
				int measure = static_cast<int>(std::wcstol(line.substr(1, 3).c_str(), nullptr, 10));
				lastMeasure = std::max(lastMeasure, measure);
				std::wstring_view ch = line.substr(4, 2);
				int channel;
				std::wstring value;
				channel = DecodeBase36(ch);
				value = line.substr(7);
				if (measures.find(measure) == measures.end())
				{
					measures[measure] = std::vector<std::pair<int, std::wstring>>();
				}
				measures[measure].push_back(std::pair<int, std::wstring>(channel, value));
			}
			else
			{
				if (MatchHeader(line, L"#WAV"))
				{
					if (metaOnly)
					{
						continue;
					}
					if (line.length() < 7)
					{
						continue;
					}
					auto xx = line.substr(4, 2);
					auto value = line.substr(7);
					ParseHeader(new_chart, L"WAV", xx, value);
				}
				else if (MatchHeader(line, L"#BMP"))
				{
					if (metaOnly)
					{
						continue;
					}
					if (line.length() < 7)
					{
						continue;
					}
					auto xx = line.substr(4, 2);
					auto value = line.substr(7);
					ParseHeader(new_chart, L"BMP", xx, value);
				}
				else if (MatchHeader(line, L"#STOP"))
				{
					if (line.length() < 8)
					{
						continue;
					}
					auto xx = line.substr(5, 2);
					auto value = line.substr(8);
					ParseHeader(new_chart, L"STOP", xx, value);
				}
				else if (MatchHeader(line, L"#BPM"))
				{
					if (line.substr(4).rfind(L" ", 0) == 0)
					{
						auto value = line.substr(5);
						ParseHeader(new_chart, L"BPM", L"", value);
					}
					else
					{
						if (line.length() < 7)
						{
							continue;
						}
						auto xx = line.substr(4, 2);
						auto value = line.substr(7);
						ParseHeader(new_chart, L"BPM", xx, value);
					}
				}
				else
				{
					std::wsmatch matcher;

					if (std::regex_search(line, matcher, headerRegex))
					{
						std::wstring cmd = matcher[1].str();
						std::wstring cmdUpper;
						std::transform(cmd.begin(), cmd.end(), std::back_inserter(cmdUpper), ::toupper);
						std::wstring xx = matcher[2].str();
						std::wstring value = matcher[3].str();
						if (value.empty())
						{
							value = xx;
							xx = L"";
						}
						ParseHeader(new_chart, cmdUpper, xx, value);
					}
				}
			}
		}
#if BMS_PARSER_VERBOSE == 1
		std::cout << "Parsing headers took " << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startTime).count() << "\n";
#endif
		if (bCancelled)
		{
			return;
		}
		if (addReadyMeasure)
		{
			measures[0] = std::vector<std::pair<int, std::wstring>>();
			measures[0].push_back(std::pair<int, std::wstring>(LaneAutoplay, L"********"));
		}

		double timePassed = 0;
		int totalNotes = 0;
		int totalLongNotes = 0;
		int totalScratchNotes = 0;
		int totalBackSpinNotes = 0;
		int totalLandmineNotes = 0;
		auto currentBpm = new_chart->Meta.Bpm;
		auto minBpm = new_chart->Meta.Bpm;
		auto maxBpm = new_chart->Meta.Bpm;
		auto lastNote = std::vector<Note *>();
		lastNote.resize(TempKey, nullptr);
		auto lnStart = std::vector<LongNote *>();
		lnStart.resize(TempKey, nullptr);
#if BMS_PARSER_VERBOSE == 1
		startTime = std::chrono::high_resolution_clock::now();
#endif
		for (auto i = 0; i <= lastMeasure; ++i)
		{
			if (bCancelled)
			{
				return;
			}
			if (measures.find(i) == measures.end())
			{
				measures[i] = std::vector<std::pair<int, std::wstring>>();
			}

			// gcd (int, int)
			auto measure = new Measure();

			// NOTE: this should be an ordered map
			auto timelines = std::map<double, TimeLine *>();

			for (auto &pair : measures[i])
			{
				if (bCancelled)
				{
					break;
				}
				auto channel = pair.first;
				auto &data = pair.second;
				if (channel == SectionRate)
				{
					measure->Scale = std::wcstod(data.c_str(), nullptr);
					continue;
				}

				auto laneNumber = 0; // NOTE: This is intentionally set to 0, not -1!
				if (channel >= P1KeyBase && channel < P1KeyBase + 9)
				{
					laneNumber = KeyAssign::Beat7[channel - P1KeyBase];
					channel = P1KeyBase;
				}
				else if (channel >= P2KeyBase && channel < P2KeyBase + 9)
				{
					laneNumber = KeyAssign::Beat7[channel - P2KeyBase + 9];
					channel = P1KeyBase;
				}
				else if (channel >= P1InvisibleKeyBase && channel < P1InvisibleKeyBase + 9)
				{
					laneNumber = KeyAssign::Beat7[channel - P1InvisibleKeyBase];
					channel = P1InvisibleKeyBase;
				}
				else if (channel >= P2InvisibleKeyBase && channel < P2InvisibleKeyBase + 9)
				{
					laneNumber = KeyAssign::Beat7[channel - P2InvisibleKeyBase + 9];
					channel = P1InvisibleKeyBase;
				}
				else if (channel >= P1LongKeyBase && channel < P1LongKeyBase + 9)
				{
					laneNumber = KeyAssign::Beat7[channel - P1LongKeyBase];
					channel = P1LongKeyBase;
				}
				else if (channel >= P2LongKeyBase && channel < P2LongKeyBase + 9)
				{
					laneNumber = KeyAssign::Beat7[channel - P2LongKeyBase + 9];
					channel = P1LongKeyBase;
				}
				else if (channel >= P1MineKeyBase && channel < P1MineKeyBase + 9)
				{
					laneNumber = KeyAssign::Beat7[channel - P1MineKeyBase];
					channel = P1MineKeyBase;
				}
				else if (channel >= P2MineKeyBase && channel < P2MineKeyBase + 9)
				{
					laneNumber = KeyAssign::Beat7[channel - P2MineKeyBase + 9];
					channel = P1MineKeyBase;
				}

				if (laneNumber == -1)
				{
					continue;
				}
				auto isScratch = laneNumber == 7 || laneNumber == 15;
				if (laneNumber == 5 || laneNumber == 6 || laneNumber == 13 || laneNumber == 14)
				{
					if (new_chart->Meta.KeyMode == 5)
					{
						new_chart->Meta.KeyMode = 7;
					}
					else if (new_chart->Meta.KeyMode == 10)
					{
						new_chart->Meta.KeyMode = 14;
					}
				}
				if (laneNumber >= 8)
				{
					if (new_chart->Meta.KeyMode == 7)
					{
						new_chart->Meta.KeyMode = 14;
					}
					else if (new_chart->Meta.KeyMode == 5)
					{
						new_chart->Meta.KeyMode = 10;
					}
					new_chart->Meta.IsDP = true;
				}

				auto dataCount = data.length() / 2;
				for (auto j = 0; j < dataCount; ++j)
				{
					if (bCancelled)
					{
						break;
					}
					auto val = data.substr(j * 2, 2);
					if (val == L"00")
					{
						if (timelines.size() == 0 && j == 0)
						{
							auto timeline = new TimeLine(TempKey, metaOnly);
							timelines[0] = timeline; // add ghost timeline
						}

						continue;
					}

					auto g = Gcd(j, dataCount);
					// ReSharper disable PossibleLossOfFraction
					auto position = static_cast<double>(j / g) / (dataCount / g);

					if (timelines.find(position) == timelines.end())
					{
						auto timeline = new TimeLine(TempKey, metaOnly);
						timelines[position] = timeline;
					}

					auto timeline = timelines[position];
					if (channel == LaneAutoplay || channel == P1InvisibleKeyBase)
					{
						if (metaOnly)
						{
							break;
						}
					}
					switch (channel)
					{
					case LaneAutoplay:
						if (metaOnly)
						{
							break;
						}
						if (val == L"**")
						{
							timeline->AddBackgroundNote(new Note{MetronomeWav});
							break;
						}
						if (DecodeBase36(val) != 0)
						{
							auto bgNote = new Note{ToWaveId(new_chart, val, metaOnly)};
							timeline->AddBackgroundNote(bgNote);
						}

						break;
					case BpmChange:
					{
						std::wstringstream ss;
						ss << std::hex << val;
						int bpm;
						ss >> bpm;
						// parse hex number
						timeline->Bpm = bpm;
						// std::cout << "BPM_CHANGE: " << timeline->Bpm << ", on measure " << i << std::endl;
						// Debug.Log($"BPM_CHANGE: {timeline.Bpm}, on measure {i}");
						timeline->BpmChange = true;
						break;
					}
					case BgaPlay:
						timeline->BgaBase = DecodeBase36(val);
						break;
					case PoorPlay:
						timeline->BgaPoor = DecodeBase36(val);
						break;
					case LayerPlay:
						timeline->BgaLayer = DecodeBase36(val);
						break;
					case BpmChangeExtend:
					{
						auto id = DecodeBase36(val);
						if (!CheckResourceIdRange(id))
						{
							// UE_LOG(LogTemp, Warning, TEXT("Invalid BPM id: %s"), *val);
							break;
						}
						if (BpmTable.find(id) != BpmTable.end())
						{
							timeline->Bpm = BpmTable[id];
						}
						else
						{
							timeline->Bpm = 0;
						}
						// Debug.Log($"BPM_CHANGE_EXTEND: {timeline.Bpm}, on measure {i}, {val}");
						timeline->BpmChange = true;
						break;
					}
					case Stop:
					{
						auto id = DecodeBase36(val);
						if (!CheckResourceIdRange(id))
						{
							// UE_LOG(LogTemp, Warning, TEXT("Invalid StopLength id: %s"), *val);
							break;
						}
						if (StopLengthTable.find(id) != StopLengthTable.end())
						{
							timeline->StopLength = StopLengthTable[id];
						}
						else
						{
							timeline->StopLength = 0;
						}
						// Debug.Log($"STOP: {timeline.StopLength}, on measure {i}");
						break;
					}
					case P1KeyBase:
					{
						auto ch = DecodeBase36(val);
						if (ch == Lnobj && lastNote[laneNumber] != nullptr)
						{
							if (isScratch)
							{
								++totalBackSpinNotes;
							}
							else
							{
								++totalLongNotes;
							}

							auto last = lastNote[laneNumber];
							lastNote[laneNumber] = nullptr;
							if (metaOnly)
							{
								break;
							}

							auto lastTimeline = last->Timeline;
							auto ln = new LongNote{last->Wav};
							delete last;
							ln->Tail = new LongNote{NoWav};
							ln->Tail->Head = ln;
							lastTimeline->SetNote(
								laneNumber, ln);
							timeline->SetNote(
								laneNumber, ln->Tail);
						}
						else
						{
							auto note = new Note{ToWaveId(new_chart, val, metaOnly)};
							lastNote[laneNumber] = note;
							++totalNotes;
							if (isScratch)
							{
								++totalScratchNotes;
							}
							if (metaOnly)
							{
								delete note; // this is intended
								break;
							}
							timeline->SetNote(
								laneNumber, note);
						}
					}
					break;
					case P1InvisibleKeyBase:
					{
						if (metaOnly)
						{
							break;
						}
						auto invNote = new Note{ToWaveId(new_chart, val, metaOnly)};
						timeline->SetInvisibleNote(
							laneNumber, invNote);
						break;
					}

					case P1LongKeyBase:
						if (Lntype == 1)
						{
							if (lnStart[laneNumber] == nullptr)
							{
								++totalNotes;
								if (isScratch)
								{
									++totalBackSpinNotes;
								}
								else
								{
									++totalLongNotes;
								}

								auto ln = new LongNote{ToWaveId(new_chart, val, metaOnly)};
								lnStart[laneNumber] = ln;

								if (metaOnly)
								{
									delete ln; // this is intended
									break;
								}

								timeline->SetNote(
									laneNumber, ln);
							}
							else
							{
								if (!metaOnly)
								{
									auto tail = new LongNote{NoWav};
									tail->Head = lnStart[laneNumber];
									lnStart[laneNumber]->Tail = tail;
									timeline->SetNote(
										laneNumber, tail);
								}
								lnStart[laneNumber] = nullptr;
							}
						}

						break;
					case P1MineKeyBase:
						// landmine
						++totalLandmineNotes;
						if (metaOnly)
						{
							break;
						}
						auto damage = DecodeBase36(val) / 2.0f;
						timeline->SetNote(
							laneNumber, new LandmineNote{damage});
						break;
					}
				}
			}

			new_chart->Meta.TotalNotes = totalNotes;
			new_chart->Meta.TotalLongNotes = totalLongNotes;
			new_chart->Meta.TotalScratchNotes = totalScratchNotes;
			new_chart->Meta.TotalBackSpinNotes = totalBackSpinNotes;

			auto lastPosition = 0.0;

			measure->Timing = static_cast<long long>(timePassed);

			for (auto &pair : timelines)
			{
				if (bCancelled)
				{
					break;
				}
				auto position = pair.first;
				auto timeline = pair.second;

				// Debug.Log($"measure: {i}, position: {position}, lastPosition: {lastPosition} bpm: {bpm} scale: {measure.scale} interval: {240 * 1000 * 1000 * (position - lastPosition) * measure.scale / bpm}");
				auto interval = 240000000.0 * (position - lastPosition) * measure->Scale / currentBpm;
				timePassed += interval;
				timeline->Timing = static_cast<long long>(timePassed);
				if (timeline->BpmChange)
				{
					currentBpm = timeline->Bpm;
					minBpm = std::min(minBpm, timeline->Bpm);
					maxBpm = std::max(maxBpm, timeline->Bpm);
				}
				else
				{
					timeline->Bpm = currentBpm;
				}

				// Debug.Log($"measure: {i}, position: {position}, lastPosition: {lastPosition}, bpm: {currentBpm} scale: {measure.Scale} interval: {interval} stop: {timeline.GetStopDuration()}");

				timePassed += timeline->GetStopDuration();
				if (!metaOnly)
				{
					measure->TimeLines.push_back(timeline);
				}
				new_chart->Meta.PlayLength = static_cast<long long>(timePassed);

				lastPosition = position;
			}

			if (metaOnly)
			{
				for (auto &timeline : timelines)
				{
					delete timeline.second;
				}
				timelines.clear();
			}

			if (!metaOnly && measure->TimeLines.size() == 0)
			{
				auto timeline = new TimeLine(TempKey, metaOnly);
				timeline->Timing = static_cast<long long>(timePassed);
				timeline->Bpm = currentBpm;
				measure->TimeLines.push_back(timeline);
			}
			timePassed += 240000000.0 * (1 - lastPosition) * measure->Scale / currentBpm;
			if (!metaOnly)
			{
				new_chart->Measures.push_back(measure);
			}
			else
			{
				delete measure;
			}
		}
#if BMS_PARSER_VERBOSE == 1
		std::cout << "Reading data field took " << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - startTime).count() << "\n";
#endif
		new_chart->Meta.TotalLength = static_cast<long long>(timePassed);
		new_chart->Meta.MinBpm = minBpm;
		new_chart->Meta.MaxBpm = maxBpm;
		if (new_chart->Meta.Difficulty == 0)
		{
			std::wstring temp = new_chart->Meta.Title + new_chart->Meta.SubTitle;
			std::wstring FullTitle;
			std::transform(temp.begin(), temp.end(), std::back_inserter(FullTitle), ::tolower);
			if (FullTitle.find(L"easy") != std::wstring::npos)
			{
				new_chart->Meta.Difficulty = 1;
			}
			else if (FullTitle.find(L"normal") != std::wstring::npos)
			{
				new_chart->Meta.Difficulty = 2;
			}
			else if (FullTitle.find(L"hyper") != std::wstring::npos)
			{
				new_chart->Meta.Difficulty = 3;
			}
			else if (FullTitle.find(L"another") != std::wstring::npos)
			{
				new_chart->Meta.Difficulty = 4;
			}
			else if (FullTitle.find(L"insane") != std::wstring::npos)
			{
				new_chart->Meta.Difficulty = 5;
			}
			else
			{
				if (totalNotes < 250)
				{
					new_chart->Meta.Difficulty = 1;
				}
				else if (totalNotes < 600)
				{
					new_chart->Meta.Difficulty = 2;
				}
				else if (totalNotes < 1000)
				{
					new_chart->Meta.Difficulty = 3;
				}
				else if (totalNotes < 2000)
				{
					new_chart->Meta.Difficulty = 4;
				}
				else
				{
					new_chart->Meta.Difficulty = 5;
				}
			}
		}
	}

	void Parser::ParseHeader(Chart *Chart, std::wstring_view CmdUpper, std::wstring_view Xx, std::wstring Value)
	{
		// Debug.Log($"cmd: {cmd}, xx: {xx} isXXNull: {xx == null}, value: {value}");
		if (CmdUpper == L"PLAYER")
		{
			Chart->Meta.Player = static_cast<int>(std::wcstol(Value.c_str(), nullptr, 10));
		}
		else if (CmdUpper == L"GENRE")
		{
			Chart->Meta.Genre = Value;
		}
		else if (CmdUpper == L"TITLE")
		{
			Chart->Meta.Title = Value;
		}
		else if (CmdUpper == L"SUBTITLE")
		{
			Chart->Meta.SubTitle = Value;
		}
		else if (CmdUpper == L"ARTIST")
		{
			Chart->Meta.Artist = Value;
		}
		else if (CmdUpper == L"SUBARTIST")
		{
			Chart->Meta.SubArtist = Value;
		}
		else if (CmdUpper == L"DIFFICULTY")
		{
			Chart->Meta.Difficulty = static_cast<int>(std::wcstol(Value.c_str(), nullptr, 10));
		}
		else if (CmdUpper == L"BPM")
		{
			if (Value.empty())
			{
				return; // TODO: handle this
			}
			if (Xx.empty())
			{
				// chart initial bpm
				Chart->Meta.Bpm = std::wcstod(Value.c_str(), nullptr);
				// std::cout << "MainBPM: " << Chart->Meta.Bpm << std::endl;
			}
			else
			{
				// Debug.Log($"BPM: {DecodeBase36(xx)} = {double.Parse(value)}");
				int id = DecodeBase36(Xx);
				if (!CheckResourceIdRange(id))
				{
					// UE_LOG(LogTemp, Warning, TEXT("Invalid BPM id: %s"), *Xx);
					return;
				}
				BpmTable[id] = std::wcstod(Value.c_str(), nullptr);
			}
		}
		else if (CmdUpper == L"STOP")
		{
			if (Value.empty() || Xx.empty() || Xx.length() == 0)
			{
				return; // TODO: handle this
			}
			int id = DecodeBase36(Xx);
			if (!CheckResourceIdRange(id))
			{
				// UE_LOG(LogTemp, Warning, TEXT("Invalid STOP id: %s"), *Xx);
				return;
			}
			StopLengthTable[id] = std::wcstod(Value.c_str(), nullptr);
		}
		else if (CmdUpper == L"MIDIFILE")
		{
		}
		else if (CmdUpper == L"VIDEOFILE")
		{
		}
		else if (CmdUpper == L"PLAYLEVEL")
		{
			Chart->Meta.PlayLevel = std::wcstod(Value.c_str(), nullptr); // TODO: handle error
		}
		else if (CmdUpper == L"RANK")
		{
			Chart->Meta.Rank = static_cast<int>(std::wcstol(Value.c_str(), nullptr, 10));
		}
		else if (CmdUpper == L"TOTAL")
		{
			auto total = std::wcstod(Value.c_str(), nullptr);
			if (total > 0)
			{
				Chart->Meta.Total = total;
			}
		}
		else if (CmdUpper == L"VOLWAV")
		{
		}
		else if (CmdUpper == L"STAGEFILE")
		{
			Chart->Meta.StageFile = Value;
		}
		else if (CmdUpper == L"BANNER")
		{
			Chart->Meta.Banner = Value;
		}
		else if (CmdUpper == L"BACKBMP")
		{
			Chart->Meta.BackBmp = Value;
		}
		else if (CmdUpper == L"PREVIEW")
		{
			Chart->Meta.Preview = Value;
		}
		else if (CmdUpper == L"WAV")
		{
			if (Xx.empty() || Value.empty())
			{
				// UE_LOG(LogTemp, Warning, TEXT("WAV command requires two arguments"));
				return;
			}
			int id = DecodeBase36(Xx);
			if (!CheckResourceIdRange(id))
			{
				// UE_LOG(LogTemp, Warning, TEXT("Invalid WAV id: %s"), *Xx);
				return;
			}
			Chart->WavTable[id] = Value;
		}
		else if (CmdUpper == L"BMP")
		{
			if (Xx.empty() || Value.empty())
			{
				// UE_LOG(LogTemp, Warning, TEXT("BMP command requires two arguments"));
				return;
			}
			int id = DecodeBase36(Xx);
			if (!CheckResourceIdRange(id))
			{
				// UE_LOG(LogTemp, Warning, TEXT("Invalid BMP id: %s"), *Xx);
				return;
			}
			Chart->BmpTable[id] = Value;
			if (Xx == L"00")
			{
				Chart->Meta.BgaPoorDefault = true;
			}
		}
		else if (CmdUpper == L"RANDOM")
		{
		}
		else if (CmdUpper == L"IF")
		{
		}
		else if (CmdUpper == L"ENDIF")
		{
		}
		else if (CmdUpper == L"LNOBJ")
		{
			Lnobj = DecodeBase36(Value);
		}
		else if (CmdUpper == L"LNTYPE")
		{
			Lntype = static_cast<int>(std::wcstol(Value.c_str(), nullptr, 10));
		}
		else if (CmdUpper == L"LNMODE")
		{
			Chart->Meta.LnMode = static_cast<int>(std::wcstol(Value.c_str(), nullptr, 10));
		}
		else
		{
			std::wcout << "Unknown command: " << CmdUpper << std::endl;
		}
	}

	int Parser::Gcd(int A, int B)
	{
		while (true)
		{
			if (B == 0)
			{
				return A;
			}
			auto a1 = A;
			A = B;
			B = a1 % B;
		}
	}

	bool Parser::CheckResourceIdRange(int Id)
	{
		return Id >= 0 && Id < 36 * 36;
	}

	int Parser::ToWaveId(Chart *Chart, std::wstring_view Wav, bool metaOnly)
	{
		if (metaOnly)
		{
			return NoWav;
		}
		if (Wav.empty())
		{
			return NoWav;
		}
		auto decoded = DecodeBase36(Wav);
		// check range
		if (!CheckResourceIdRange(decoded))
		{
			// UE_LOG(LogTemp, Warning, TEXT("Invalid wav id: %s"), *Wav);
			return NoWav;
		}

		return Chart->WavTable.find(decoded) != Chart->WavTable.end() ? decoded : NoWav;
	}

	int Parser::DecodeBase36(std::wstring_view Str)
	{
		int result = 0;
		// std::wstring StrUpper;
		// std::transform(Str.begin(), Str.end(), std::back_inserter(StrUpper), ::toupper);
		for (auto c : Str)
		{
			result *= 36;
			if (std::isdigit(c))
			{
				result += c - '0';
			}
			else if (isalpha(c))
			{
				result += c - (isupper(c) ? 'A' : 'a') + 10;
			}
			else
			{
				return -1;
			}
		}
		return result;
	}

	Parser::~Parser()
	{
	}
}
