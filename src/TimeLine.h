// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "BMSNote.h"
#include "BMSLandmineNote.h"
#include <vector>
/**
 * 
 */

class TimeLine
{
public:
	std::vector<BMSNote*> BackgroundNotes;
	std::vector<BMSNote*> InvisibleNotes;
	std::vector<BMSNote*> Notes;
	std::vector<BMSLandmineNote*> LandmineNotes;
	double Bpm = 0;
	bool BpmChange = false;
	bool BpmChangeApplied = false;
	int BgaBase = -1;
	int BgaLayer = -1;
	int BgaPoor = -1;

	double StopLength = 0;
	double Scroll = 1;

	long long Timing = 0;
	double Pos = 0;

	explicit TimeLine(int lanes, bool metaOnly);

	TimeLine* SetNote(int lane, BMSNote* note);

	TimeLine* SetInvisibleNote(int lane, BMSNote* note);

	TimeLine* SetLandmineNote(int lane, BMSLandmineNote* note);

	TimeLine* AddBackgroundNote(BMSNote* note);

	double GetStopDuration();

	~TimeLine();
};
