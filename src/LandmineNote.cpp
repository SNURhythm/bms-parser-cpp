// Fill out your copyright notice in the Description page of Project Settings.

#include "LandmineNote.h"
namespace bms_parser
{
	LandmineNote::LandmineNote(float Damage) : Note(0)
	{
		this->Damage = Damage;
	}

	LandmineNote::~LandmineNote()
	{
	}
}
