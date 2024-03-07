// Fill out your copyright notice in the Description page of Project Settings.

#include "Measure.h"

namespace bms_parser
{
	Measure::~Measure()
	{
		for (const auto &Timeline : TimeLines)
		{
			delete Timeline;
		}
		TimeLines.clear();
	}
}
