// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "TimeLine.h"
/**
 *
 */
namespace bms_parser
{
	class Measure
	{
	public:
		double Scale = 1;
		long long Timing = 0;
		double Pos = 0;
		std::vector<TimeLine *> TimeLines;
		~Measure();
	};
}
