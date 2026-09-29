/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include "Base/Types.h"
#include "SysPSP/Utility/PerfStats.h"

#include <pspthreadman.h>

bool gPerfStatsEnabled = false;

namespace
{
	const u32		kMaxDepth = 8;
	const u32		kSampleMicroseconds = 1000000;

	EPerfCategory	gStack[ kMaxDepth ];
	u32				gDepth = 0;
	u32				gLastStamp = 0;
	u32				gSampleStart = 0;
	u32				gAccum[ NUM_PERF_CATEGORIES ] = {};
	u32				gPercent[ NUM_PERF_CATEGORIES ] = {};

	inline EPerfCategory Current()
	{
		if( gDepth == 0 ) return PERF_CPU;
		return gStack[ (gDepth <= kMaxDepth ? gDepth : kMaxDepth) - 1 ];
	}

	// Charge the time since the last stamp to the category currently running
	inline void Charge()
	{
		u32 now = sceKernelGetSystemTimeLow();
		gAccum[ Current() ] += now - gLastStamp;
		gLastStamp = now;
	}
}

void PerfStats_Enter( EPerfCategory category )
{
	Charge();
	if( gDepth < kMaxDepth )
	{
		gStack[ gDepth ] = category;
	}
	++gDepth;
}

void PerfStats_Exit()
{
	Charge();
	if( gDepth > 0 )
	{
		--gDepth;
	}
}

bool PerfStats_Update()
{
	Charge();

	u32 now = gLastStamp;
	u32 elapsed = now - gSampleStart;
	if( gSampleStart == 0 || elapsed > kSampleMicroseconds * 4 )
	{
		// First call, or stats were switched off for a while: start a fresh sample
		for( u32 i = 0; i < NUM_PERF_CATEGORIES; ++i ) gAccum[ i ] = 0;
		gSampleStart = now;
		return false;
	}

	if( elapsed < kSampleMicroseconds )
	{
		return false;
	}

	for( u32 i = 0; i < NUM_PERF_CATEGORIES; ++i )
	{
		gPercent[ i ] = (u32)(((u64)gAccum[ i ] * 100 + elapsed / 2) / elapsed);
		gAccum[ i ] = 0;
	}
	gSampleStart = now;
	return true;
}

u32 PerfStats_GetPercent( EPerfCategory category )
{
	return gPercent[ category ];
}
