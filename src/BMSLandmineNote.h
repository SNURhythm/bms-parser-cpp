// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "BMSNote.h"
/**
 * 
 */
class BMSLandmineNote : public BMSNote
{
public:
	float Damage;
	BMSLandmineNote(float Damage);
	virtual ~BMSLandmineNote() override;

	virtual bool IsLandmineNote() override { return true; }
};
