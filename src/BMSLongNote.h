// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "BMSNote.h"
/**
 * 
 */

class BMSLongNote : public BMSNote
{
public:
	BMSLongNote();
	virtual ~BMSLongNote() override;
	BMSLongNote* Tail;
	BMSLongNote* Head;
	bool IsHolding = false;
	bool IsTail();
	long long ReleaseTime;

	BMSLongNote(int Wav);

	virtual void Press(long long Time) override;

	void Release(long long Time);

	void MissPress(long long Time);

	virtual void Reset() override;

	virtual bool IsLongNote() override { return true; }
};
