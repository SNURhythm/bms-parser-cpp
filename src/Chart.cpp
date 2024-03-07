// Fill out your copyright notice in the Description page of Project Settings.

#include "Chart.h"
namespace bms_parser
{
	Chart::Chart()
	{
	}

	Chart::~Chart()
	{
		for (const auto &measure : Measures)
		{
			delete measure;
		}

		Measures.clear();
	}
}
