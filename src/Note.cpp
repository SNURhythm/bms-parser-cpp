// Fill out your copyright notice in the Description page of Project Settings.

#include "Note.h"
namespace bms_parser
{
	Note::Note(int wav)
	{
		Wav = wav;
	}

	void Note::Play(long long Time)
	{
		IsPlayed = true;
		PlayedTime = Time;
	}

	void Note::Press(long long Time)
	{
		Play(Time);
	}

	void Note::Reset()
	{
		IsPlayed = false;
		IsDead = false;
		PlayedTime = 0;
	}

	Note::~Note()
	{
		Timeline = nullptr;
	}
}
