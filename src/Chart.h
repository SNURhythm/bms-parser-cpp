// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Measure.h"
#include <string>
#include <vector>
#include <map>
/**
 * 
 */


class BMSChartMeta
{
public:
	std::string SHA256;
	std::string MD5;
	std::wstring BmsPath;
	std::wstring Folder;
	std::wstring Artist = L"";
	std::wstring SubArtist = L"";
	double Bpm;
	std::wstring Genre = L"";
	std::wstring Title = L"";
	std::wstring SubTitle = L"";
	int Rank = 3;
	double Total = 100;
	long long PlayLength = 0; // Timing of the last playable note, in microseconds
	long long TotalLength = 0;
	// Timing of the last timeline(including background note, bga change note, invisible note, ...), in microseconds
	std::wstring Banner;
	std::wstring StageFile;
	std::wstring BackBmp;
	std::wstring Preview;
	bool BgaPoorDefault = false;
	int Difficulty = 0;
	double PlayLevel = 3;
	double MinBpm;
	double MaxBpm;
	int Player = 1;
	int KeyMode = 5;
	bool IsDP = false;
	int TotalNotes;
	int TotalLongNotes;
	int TotalScratchNotes;
	int TotalBackSpinNotes;
	int LnMode = 0; // 0: user decides, 1: LN, 2: CN, 3: HCN

	int GetKeyLaneCount() const { return KeyMode; }
	int GetScratchLaneCount() const { return IsDP ? 2 : 1; }
	int GetTotalLaneCount() const { return KeyMode + GetScratchLaneCount(); }

	std::vector<int> GetKeyLaneIndices() const
	{
		switch (KeyMode)
		{
		case 5:
			return {0, 1, 2, 3, 4};
		case 7:
			return {0, 1, 2, 3, 4, 5, 6};
		case 10:
			return {0, 1, 2, 3, 4, 8, 9, 10, 11, 12};
		case 14:
			return {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14};
		default:
			return {};
		}
	}

	std::vector<int> GetScratchLaneIndices() const
	{
		if (IsDP)
		{
			return {7, 15};
		}
		return {7};
	}

	std::vector<int> GetTotalLaneIndices() const
	{
		std::vector<int> Result;
		Result.insert(Result.end(), GetKeyLaneIndices().begin(), GetKeyLaneIndices().end());
		Result.insert(Result.end(), GetScratchLaneIndices().begin(), GetScratchLaneIndices().end());
		return Result;
	}
};


class BMSChart
{
public:
	BMSChart();
	~BMSChart();
	BMSChartMeta Meta;
	std::vector<Measure*> Measures;
	std::map<int, std::wstring> WavTable;
	std::map<int, std::wstring> BmpTable;
};
