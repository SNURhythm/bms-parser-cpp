// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Note.h"
/**
 *
 */
namespace bms_parser
{
	class LongNote : public Note
	{
	public:
		LongNote();
		virtual ~LongNote() override;
		LongNote *Tail;
		LongNote *Head;
		bool IsHolding = false;
		bool IsTail();
		long long ReleaseTime;

		LongNote(int Wav);

		virtual void Press(long long Time) override;

		void Release(long long Time);

		void MissPress(long long Time);

		virtual void Reset() override;

		virtual bool IsLongNote() override { return true; }
	};
}