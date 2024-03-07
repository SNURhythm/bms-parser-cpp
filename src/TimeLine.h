// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Note.h"
#include "LandmineNote.h"
#include <vector>
/**
 *
 */
namespace bms_parser
{
	class TimeLine
	{
	public:
		std::vector<Note *> BackgroundNotes;
		std::vector<Note *> InvisibleNotes;
		std::vector<Note *> Notes;
		std::vector<LandmineNote *> LandmineNotes;
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

		TimeLine *SetNote(int lane, Note *note);

		TimeLine *SetInvisibleNote(int lane, Note *note);

		TimeLine *SetLandmineNote(int lane, LandmineNote *note);

		TimeLine *AddBackgroundNote(Note *note);

		double GetStopDuration();

		~TimeLine();
	};
}