// Fill out your copyright notice in the Description page of Project Settings.


#include "BMSLongNote.h"

bool BMSLongNote::IsTail()
{
	return Tail == nullptr;
}

BMSLongNote::BMSLongNote(int Wav) : BMSNote(Wav)
{
	Tail = nullptr;
}

void BMSLongNote::Press(long long Time)
{
	Play(Time);
	IsHolding = true;
	Tail->IsHolding = true;
}

void BMSLongNote::Release(long long Time)
{
	Play(Time);
	IsHolding = false;
	Head->IsHolding = false;
	ReleaseTime = Time;
}

void BMSLongNote::MissPress(long long Time)
{
}

void BMSLongNote::Reset()
{
	BMSNote::Reset();
	IsHolding = false;
	ReleaseTime = 0;
}


BMSLongNote::~BMSLongNote()
{
	Head = nullptr;
	Tail = nullptr;
}
