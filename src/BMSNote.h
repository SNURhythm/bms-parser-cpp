// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

/**
 * 
 */
class TimeLine;

class BMSNote
{
public:
	int Lane = 0;
	int Wav = 0;

	bool IsPlayed = false;
	bool IsDead = false;
	long long PlayedTime = 0;
	TimeLine* Timeline;

	// private Note nextNote;

	BMSNote(int Wav);

	void Play(long long Time);

	virtual void Press(long long Time);

	virtual void Reset();
	virtual ~BMSNote();

	virtual bool IsLongNote() { return false; }
	virtual bool IsLandmineNote() { return false; }
};
