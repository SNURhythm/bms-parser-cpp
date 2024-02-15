// Fill out your copyright notice in the Description page of Project Settings.


#include "BMSParser.h"
#include <random>
#include "BMSLongNote.h"
#include "BMSNote.h"
#include "BMSLandmineNote.h"
#include "TimeLine.h"
#include "Measure.h"
#include "ShiftJISConverter.h"
#include "SHA256.h"
#include <filesystem>
#include <regex>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include "md5.h"
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

BMSParser::BMSParser(): BpmTable{}, StopLengthTable{}
{
	std::random_device seeder;
	Seed = seeder();
}

void BMSParser::SetRandomSeed(int RandomSeed)
{
	Seed = RandomSeed;
}

int BMSParser::NoWav = -1;
int BMSParser::MetronomeWav = -2;

void BMSParser::Parse(std::string path, BMSChart** chart, bool addReadyMeasure, bool metaOnly, std::atomic_bool& bCancelled)
{
	auto Chart = new BMSChart();
	*chart = Chart;
	Chart->Meta.BmsPath = path;
	std::filesystem::path p = path;
	Chart->Meta.Folder = p.parent_path().string();
	std::regex headerRegex("^#([A-Za-z]+?)(\\d\\d)? +?(.+)?");

	// implement the same thing as BMSParser.cs
	auto measures = std::map<int, std::vector<std::pair<int, std::string>>>();
	std::vector<uint8_t> bytes;
	try
	{
		bytes = std::vector<uint8_t>(std::istreambuf_iterator<char>(std::ifstream(path, std::ios::binary)), std::istreambuf_iterator<char>());
	}
	catch (std::exception& e)
	{
		// UE_LOG(LogTemp, Warning, TEXT("Failed to read file: %s"), *FString(e.what()));
		return;
	}
	if (bCancelled)
	{
		return;
	}
	MD5 md5;
	md5.update(bytes.data(), bytes.size());
	Chart->Meta.MD5 = md5.hexdigest();
	Chart->Meta.SHA256 = sha256(bytes);
	// bytes to std::string
	std::string content;
	ShiftJISConverter::BytesToUTF8(content, bytes.data(), bytes.size());
	std::vector<int> RandomStack;
	std::vector<bool> SkipStack;
	// init prng with seed
	std::mt19937_64 Prng(Seed);


	auto lines = std::vector<std::string>();
	std::string line;
	std::istringstream stream(content);
	while (std::getline(stream, line))
	{
		if (bCancelled)
		{
			return;
		}
		lines.push_back(line);
	}
	auto lastMeasure = -1;

	for (auto& line : lines)
	{
		if (bCancelled)
		{
			return;
		}
		// should start with #
		if (line.rfind("#", 0) != 0)
		{
			continue;
		}
		std::string upperLine;
		std::transform(line.begin(), line.end(), upperLine.begin(), ::toupper);
		if (upperLine.rfind("#IF", 0) == 0) // #IF n
		{
			if (RandomStack.empty())
			{
				// UE_LOG(LogTemp, Warning, TEXT("RandomStack is empty!"));
				continue;
			}
			int CurrentRandom = RandomStack.back();
			int n = std::stoi(line.substr(4));
			SkipStack.push_back(CurrentRandom != n);
			continue;
		}
		if (upperLine.rfind("#ELSE", 0) == 0)
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
		if (upperLine.rfind("#ELSEIF", 0) == 0)
		{
			if (SkipStack.empty())
			{
				// UE_LOG(LogTemp, Warning, TEXT("SkipStack is empty!"));
				continue;
			}
			bool CurrentSkip = SkipStack.back();
			SkipStack.pop_back();
			int CurrentRandom = RandomStack.back();
			int n = std::stoi(line.substr(8));
			SkipStack.push_back(CurrentSkip && CurrentRandom != n);
			continue;
		}
		if (upperLine.rfind("#ENDIF", 0) == 0 || upperLine.rfind("#END IF", 0) == 0)
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
		if (upperLine.rfind("#RANDOM", 0) == 0 || upperLine.rfind("#RONDAM", 0) == 0) // #RANDOM n
		{
			int n = std::stoi(line.substr(7));
			std::uniform_int_distribution<int> dist(1, n);
			RandomStack.push_back(dist(Prng));
			continue;
		}
		if (upperLine.rfind("#ENDRANDOM", 0) == 0)
		{
			if (RandomStack.empty())
			{
				// UE_LOG(LogTemp, Warning, TEXT("RandomStack is empty!"));
				continue;
			}
			RandomStack.pop_back();
			continue;
		}
		if (line.size() >= 7 && isdigit(line[1]) && isdigit(line[2]) && isdigit(line[3]) && line[6]
			== ':')
		{
			auto measure = std::stoi(line.substr(1, 3));
			lastMeasure = std::max(lastMeasure, measure);
			std::string ch = line.substr(4, 2);
			int channel;
			std::string value;
			channel = DecodeBase36(ch);
			value = line.substr(7);
			if (measures.find(measure) == measures.end())
			{
				measures[measure] = std::vector<std::pair<int, std::string>>();
			}
			measures[measure].push_back(std::pair<int, std::string>(channel, value));
		}
		else
		{
			if (upperLine.rfind("#WAV", 0) == 0 || upperLine.rfind("#BMP", 0) == 0)
			{
				if (line.length() < 7)
				{
					continue;
				}
				auto xx = line.substr(4, 2);
				auto value = line.substr(7);
				std::string cmd = upperLine.substr(1, 3);
				ParseHeader(Chart, cmd, xx, value);
			}
			else if (upperLine.rfind("#STOP", 0) == 0)
			{
				if (line.length() < 8)
				{
					continue;
				}
				auto xx = line.substr(5, 2);
				auto value = line.substr(8);
				std::string cmd = "STOP";
				ParseHeader(Chart, cmd, xx, value);
			}
			else if (upperLine.rfind("#BPM", 0) == 0)
			{
				if (line.substr(4).rfind(" ", 0) == 0)
				{
					auto value = line.substr(5);
					std::string cmd = "BPM";
					std::string xx = "";
					ParseHeader(Chart, cmd, xx, value);
				}
				else
				{
					if (line.length() < 7)
					{
						continue;
					}
					auto xx = line.substr(4, 2);
					auto value = line.substr(7);
					std::string cmd = "BPM";
					ParseHeader(Chart, cmd, xx, value);
				}
			}
			else
			{
				std::string copy = line;
				std::smatch matcher;

				if (std::regex_search(copy, matcher, headerRegex))
				{
					std::string cmd = matcher[1].str();
					std::string xx = matcher[2].str();
					std::string value = matcher[3].str();
					if (value.empty())
					{
						value = xx;
						xx = "";
					}
					ParseHeader(Chart, cmd, xx, value);
				}
			}
		}
	}

	if (addReadyMeasure)
	{
		measures[0] = std::vector<std::pair<int, std::string>>();
		measures[0].push_back(std::pair<int, std::string>(LaneAutoplay, "********"));
	}

	double timePassed = 0;
	int totalNotes = 0;
	int totalLongNotes = 0;
	int totalScratchNotes = 0;
	int totalBackSpinNotes = 0;
	int totalLandmineNotes = 0;
	auto currentBpm = Chart->Meta.Bpm;
	auto minBpm = Chart->Meta.Bpm;
	auto maxBpm = Chart->Meta.Bpm;
	auto lastNote = std::vector<BMSNote*>();
	lastNote.resize(TempKey, nullptr);
	auto lnStart = std::vector<BMSLongNote*>();
	lnStart.resize(TempKey, nullptr);

	for (auto i = 0; i <= lastMeasure; ++i)
	{
		if (bCancelled)
		{
			return;
		}
		if (measures.find(i) == measures.end())
		{
			measures[i] = std::vector<std::pair<int, std::string>>();
		}

		// gcd (int, int)
		auto measure = new Measure();
		auto timelines = std::map<double, TimeLine*>();

		for (auto& pair : measures[i])
		{
			if (bCancelled)
			{
				break;
			}
			auto channel = pair.first;
			auto& data = pair.second;
			if (channel == SectionRate)
			{
				measure->Scale = std::stod(data);
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
				if (Chart->Meta.KeyMode == 5)
				{
					Chart->Meta.KeyMode = 7;
				}
				else if (Chart->Meta.KeyMode == 10)
				{
					Chart->Meta.KeyMode = 14;
				}
			}
			if (laneNumber >= 8)
			{
				if (Chart->Meta.KeyMode == 7)
				{
					Chart->Meta.KeyMode = 14;
				}
				else if (Chart->Meta.KeyMode == 5)
				{
					Chart->Meta.KeyMode = 10;
				}
				Chart->Meta.IsDP = true;
			}

			auto dataCount = data.length() / 2;
			for (auto j = 0; j < dataCount; ++j)
			{
				if (bCancelled)
				{
					break;
				}
				auto val = data.substr(j * 2, 2);
				if (val == "00")
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
					if (val == "**")
					{
						timeline->AddBackgroundNote(new BMSNote{MetronomeWav});
						break;
					}
					if (DecodeBase36(val) != 0)
					{
						auto bgNote = new BMSNote{ToWaveId(Chart, val)};
						timeline->AddBackgroundNote(bgNote);
					}

					break;
				case BpmChange:
				{
					std::stringstream ss;
					ss << std::hex << val;
					double bpm;
					ss >> bpm;
				// parse hex number
					timeline->Bpm = bpm;
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
							auto ln = new BMSLongNote{last->Wav};
							delete last;
							ln->Tail = new BMSLongNote{NoWav};
							ln->Tail->Head = ln;
							lastTimeline->SetNote(
								laneNumber, ln
							);
							timeline->SetNote(
								laneNumber, ln->Tail
							);
						}
						else
						{
							auto note = new BMSNote{ToWaveId(Chart, val)};
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
								laneNumber, note
							);
						}
					}
					break;
				case P1InvisibleKeyBase:
					{
						if (metaOnly)
						{
							break;
						}
						auto invNote = new BMSNote{ToWaveId(Chart, val)};
						timeline->SetInvisibleNote(
							laneNumber, invNote
						);
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

							auto ln = new BMSLongNote{ToWaveId(Chart, val)};
							lnStart[laneNumber] = ln;

							if (metaOnly)
							{
								delete ln; // this is intended
								break;
							}

							timeline->SetNote(
								laneNumber, ln
							);
						}
						else
						{
							if (!metaOnly)
							{
								auto tail = new BMSLongNote{NoWav};
								tail->Head = lnStart[laneNumber];
								lnStart[laneNumber]->Tail = tail;
								timeline->SetNote(
									laneNumber, tail
								);
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
						laneNumber, new BMSLandmineNote{damage}
					);
					break;
				}
			}
		}

		Chart->Meta.TotalNotes = totalNotes;
		Chart->Meta.TotalLongNotes = totalLongNotes;
		Chart->Meta.TotalScratchNotes = totalScratchNotes;
		Chart->Meta.TotalBackSpinNotes = totalBackSpinNotes;

		auto lastPosition = 0.0;

		measure->Timing = static_cast<long long>(timePassed);

		for (auto& pair : timelines)
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
			Chart->Meta.PlayLength = static_cast<long long>(timePassed);

			lastPosition = position;
		}

		if (metaOnly)
		{
			for (auto& timeline : timelines)
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
			Chart->Measures.push_back(measure);
		}
		else
		{
			delete measure;
		}
	}

	Chart->Meta.TotalLength = static_cast<long long>(timePassed);
	Chart->Meta.MinBpm = minBpm;
	Chart->Meta.MaxBpm = maxBpm;
	if (Chart->Meta.Difficulty == 0)
	{
		std::string temp = Chart->Meta.Title + Chart->Meta.SubTitle;
		std::string FullTitle;
		std::transform(temp.begin(), temp.end(), FullTitle.begin(), ::tolower);
		if (FullTitle.find("easy") != std::string::npos)
		{
			Chart->Meta.Difficulty = 1;
		}
		else if (FullTitle.find("normal") != std::string::npos)
		{
			Chart->Meta.Difficulty = 2;
		}
		else if (FullTitle.find("hyper") != std::string::npos)
		{
			Chart->Meta.Difficulty = 3;
		}
		else if (FullTitle.find("another") != std::string::npos)
		{
			Chart->Meta.Difficulty = 4;
		}
		else if (FullTitle.find("insane") != std::string::npos)
		{
			Chart->Meta.Difficulty = 5;
		}
		else
		{
			if (totalNotes < 250)
			{
				Chart->Meta.Difficulty = 1;
			}
			else if (totalNotes < 600)
			{
				Chart->Meta.Difficulty = 2;
			}
			else if (totalNotes < 1000)
			{
				Chart->Meta.Difficulty = 3;
			}
			else if (totalNotes < 2000)
			{
				Chart->Meta.Difficulty = 4;
			}
			else
			{
				Chart->Meta.Difficulty = 5;
			}
		}
	}
}

void BMSParser::ParseHeader(BMSChart* Chart, const std::string& Cmd, const std::string& Xx, std::string Value)
{
	// Debug.Log($"cmd: {cmd}, xx: {xx} isXXNull: {xx == null}, value: {value}");
	std::string CmdUpper;
	std::transform(Cmd.begin(), Cmd.end(), CmdUpper.begin(), ::toupper);
	if (CmdUpper == "PLAYER")
	{
		Chart->Meta.Player = std::stoi(Value);
	}
	else if (CmdUpper == "GENRE")
	{
		Chart->Meta.Genre = Value;
	}
	else if (CmdUpper == "TITLE")
	{
		Chart->Meta.Title = Value;
	}
	else if (CmdUpper == "SUBTITLE")
	{
		Chart->Meta.SubTitle = Value;
	}
	else if (CmdUpper == "ARTIST")
	{
		Chart->Meta.Artist = Value;
	}
	else if (CmdUpper == "SUBARTIST")
	{
		Chart->Meta.SubArtist = Value;
	}
	else if (CmdUpper == "DIFFICULTY")
	{
		Chart->Meta.Difficulty = std::stoi(Value);
	}
	else if (CmdUpper == "BPM")
	{
		if (Value.empty())
		{
			return; // TODO: handle this
		}
		if (Xx.empty())
		{
			// chart initial bpm
			Chart->Meta.Bpm = std::stod(Value);
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
			BpmTable[id] = std::stod(Value);
		}
	}
	else if (CmdUpper == "STOP")
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
		StopLengthTable[id] = std::stod(Value);
	}
	else if (CmdUpper == "MIDIFILE")
	{
	}
	else if (CmdUpper == "VIDEOFILE")
	{
	}
	else if (CmdUpper == "PLAYLEVEL")
	{
		Chart->Meta.PlayLevel = std::stod(Value); // TODO: handle error
	}
	else if (CmdUpper == "RANK")
	{
		Chart->Meta.Rank = std::stoi(Value);
	}
	else if (CmdUpper == "TOTAL")
	{
		auto total = std::stod(Value);
		if (total > 0)
		{
			Chart->Meta.Total = total;
		}
	}
	else if (CmdUpper == "VOLWAV")
	{
	}
	else if (CmdUpper == "STAGEFILE")
	{
		Chart->Meta.StageFile = Value;
	}
	else if (CmdUpper == "BANNER")
	{
		Chart->Meta.Banner = Value;
	}
	else if (CmdUpper == "BACKBMP")
	{
		Chart->Meta.BackBmp = Value;
	}
	else if (CmdUpper == "PREVIEW")
	{
		Chart->Meta.Preview = Value;
	}
	else if (CmdUpper == "WAV")
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
	else if (CmdUpper == "BMP")
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
		if (Xx == "00")
		{
			Chart->Meta.BgaPoorDefault = true;
		}
	}
	else if (CmdUpper == "RANDOM")
	{
	}
	else if (CmdUpper == "IF")
	{
	}
	else if (CmdUpper == "ENDIF")
	{
	}
	else if (CmdUpper == "LNOBJ")
	{
		Lnobj = DecodeBase36(Value);
	}
	else if (CmdUpper == "LNTYPE")
	{
		Lntype = std::stoi(Value);
	}
	else if (CmdUpper == "LNMODE")
	{
		Chart->Meta.LnMode = std::stoi(Value);
	}
	else
	{
		// UE_LOG(LogTemp, Warning, TEXT("Unknown command: %s"), *CmdUpper);
	}
}

int BMSParser::Gcd(int A, int B)
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

bool BMSParser::CheckResourceIdRange(int Id)
{
	return Id >= 0 && Id < 36 * 36;
}

int BMSParser::ToWaveId(BMSChart* Chart, const std::string& Wav)
{
	auto decoded = DecodeBase36(Wav);
	// check range
	if (!CheckResourceIdRange(decoded))
	{
		// UE_LOG(LogTemp, Warning, TEXT("Invalid wav id: %s"), *Wav);
		return NoWav;
	}

	return Chart->WavTable.find(decoded) != Chart->WavTable.end() ? decoded : NoWav;
}

int BMSParser::DecodeBase36(const std::string& Str)
{
	int result = 0;
	std::string StrUpper;
	std::transform(Str.begin(), Str.end(), StrUpper.begin(), ::toupper);
	for (auto c : StrUpper)
	{
		result *= 36;
		if (isdigit(c))
		{
			result += c - '0';
		}
		else if (isalpha(c))
		{
			result += c - 'A' + 10;
		}
		else
		{
			return -1;
		}
	}
	return result;
}

BMSParser::~BMSParser()
{
}
