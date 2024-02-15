// Fill out your copyright notice in the Description page of Project Settings.


#include "TimeLine.h"

TimeLine::TimeLine(int lanes, bool metaOnly)
{
	if (metaOnly)
	{
		return;
	}
	Notes.Init(nullptr, lanes);
	InvisibleNotes.Init(nullptr, lanes);
	LandmineNotes.Init(nullptr, lanes);
}

TimeLine* TimeLine::SetNote(int lane, BMSNote* note)
{
	Notes[lane] = note;
	note->Lane = lane;
	note->Timeline = this;
	return this;
}

TimeLine* TimeLine::SetInvisibleNote(int lane, BMSNote* note)
{
	InvisibleNotes[lane] = note;
	note->Lane = lane;
	note->Timeline = this;
	return this;
}

TimeLine* TimeLine::SetLandmineNote(int lane, BMSLandmineNote* note)
{
	LandmineNotes[lane] = note;
	note->Lane = lane;
	note->Timeline = this;
	return this;
}

TimeLine* TimeLine::AddBackgroundNote(BMSNote* note)
{
	BackgroundNotes.Add(note);
	note->Timeline = this;
	return this;
}

double TimeLine::GetStopDuration()
{
	return 1250000.0 * StopLength / Bpm; // 1250000 = 240 * 1000 * 1000 / 192
}

TimeLine::~TimeLine()
{
	for (const auto& note : Notes)
	{
		if (note != nullptr)
		{
			delete note;
		}
	}
	Notes.Empty();
	for (const auto& note : InvisibleNotes)
	{
		if (note != nullptr)
		{
			delete note;
		}
	}
	InvisibleNotes.Empty();
	for (const auto& note : LandmineNotes)
	{
		if (note != nullptr)
		{
			delete note;
		}
	}
	LandmineNotes.Empty();
	for (const auto& note : BackgroundNotes)
	{
		if (note != nullptr)
		{
			delete note;
		}
	}
	BackgroundNotes.Empty();
}
