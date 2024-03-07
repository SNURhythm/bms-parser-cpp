// Fill out your copyright notice in the Description page of Project Settings.

#include "LongNote.h"
namespace bms_parser
{
	bool LongNote::IsTail()
	{
		return Tail == nullptr;
	}

	LongNote::LongNote(int Wav) : Note(Wav)
	{
		Tail = nullptr;
	}

	void LongNote::Press(long long Time)
	{
		Play(Time);
		IsHolding = true;
		Tail->IsHolding = true;
	}

	void LongNote::Release(long long Time)
	{
		Play(Time);
		IsHolding = false;
		Head->IsHolding = false;
		ReleaseTime = Time;
	}

	void LongNote::MissPress(long long Time)
	{
	}

	void LongNote::Reset()
	{
		Note::Reset();
		IsHolding = false;
		ReleaseTime = 0;
	}

	LongNote::~LongNote()
	{
		Head = nullptr;
		Tail = nullptr;
	}
}