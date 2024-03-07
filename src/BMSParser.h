// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Chart.h"
#include <string>
#include <map>
#include <atomic>
/**
 * 
 */
class BMSParser
{
public:
	BMSParser();
	void SetRandomSeed(int RandomSeed);
	void Parse(std::wstring path, BMSChart** Chart, bool addReadyMeasure, bool metaOnly, std::atomic_bool& bCancelled);
	~BMSParser();
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
	int DecodeBase36(const std::wstring& Str);
	void ParseHeader(BMSChart* Chart, const std::wstring& Cmd, const std::wstring& Xx, std::wstring Value);

	static int Gcd(int A, int B);
	static bool CheckResourceIdRange(int Id);
	int ToWaveId(BMSChart* Chart, const std::wstring& Wav, bool metaOnly);
};
