/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef SYSPSP_UTILITY_PERFSTATS_H_
#define SYSPSP_UTILITY_PERFSTATS_H_

#include "Base/Types.h"

// Lightweight frame-time breakdown shown under the FPS counter when
// "Display Framerate" is enabled. Time is attributed exclusively: entering a
// category pauses the one below it, so all categories add up to 100%.
// Anything not inside a scope counts as CPU (R4300 emulation, dynarec, etc).
enum EPerfCategory
{
	PERF_CPU = 0,		// Everything not covered below
	PERF_GFX,			// Display list processing (HLE graphics on the main CPU)
	PERF_AUDIO,			// Audio list processing / dispatch to the Media Engine
	PERF_GE_WAIT,		// Waiting for the PSP GPU to finish the frame
	PERF_LIMITER,		// Sleeping in the framerate limiter (spare time)

	NUM_PERF_CATEGORIES
};

extern bool gPerfStatsEnabled;

void	PerfStats_Enter( EPerfCategory category );
void	PerfStats_Exit();

// Call once per displayed frame. Returns true when a new one second sample is ready.
bool	PerfStats_Update();

// Percentage of wall time spent in each category over the last sample.
u32		PerfStats_GetPercent( EPerfCategory category );

class CPerfScope
{
public:
	explicit CPerfScope( EPerfCategory category )
		:	mActive( gPerfStatsEnabled )
	{
		if( mActive ) PerfStats_Enter( category );
	}
	~CPerfScope()
	{
		if( mActive ) PerfStats_Exit();
	}

private:
	bool	mActive;
};

#define DAEDALUS_PERF_SCOPE( category )		CPerfScope _perf_scope( category )

#endif // SYSPSP_UTILITY_PERFSTATS_H_
