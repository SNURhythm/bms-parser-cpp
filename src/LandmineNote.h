// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Note.h"
/**
 *
 */
namespace bms_parser
{
	class LandmineNote : public Note
	{
	public:
		float Damage;
		LandmineNote(float Damage);
		virtual ~LandmineNote() override;

		virtual bool IsLandmineNote() override { return true; }
	};
}