// Fill out your copyright notice in the Description page of Project Settings.


#include "Chart.h"

BMSChart::BMSChart()
{
}

BMSChart::~BMSChart()
{
	for (const auto& measure : Measures)
	{
		delete measure;
	}

	Measures.clear();
}
