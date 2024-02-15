// Fill out your copyright notice in the Description page of Project Settings.


#include "BMSNote.h"

BMSNote::BMSNote(int wav)
{
	Wav = wav;
}

void BMSNote::Play(long long Time)
{
	IsPlayed = true;
	PlayedTime = Time;
}

void BMSNote::Press(long long Time)
{
	Play(Time);
}

void BMSNote::Reset()
{
	IsPlayed = false;
	IsDead = false;
	PlayedTime = 0;
}

BMSNote::~BMSNote()
{
	Timeline = nullptr;
}
