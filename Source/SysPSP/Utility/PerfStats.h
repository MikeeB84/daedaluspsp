/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#ifndef SYSPSP_UTILITY_PERFSTATS_H_
#define SYSPSP_UTILITY_PERFSTATS_H_

#include "Base/Types.h"

// Lightweight frame time breakdown shown under the FPS counter (and logged to
// perf.txt) when "Display Framerate" is enabled.
//
// Code marks what it is doing with DAEDALUS_PERF_SCOPE, which only pushes and
// pops a category on a small stack. A timer interrupt samples the top of that
// stack about 2000 times a second, so the percentages are statistical but the
// cost of a scope is a few instructions. Scopes nest: the innermost one wins.
enum EPerfCategory
{
	PERF_CPU = 0,		// Interpreter, trace recording and anything not listed below
	PERF_CPU_DYNAREC,	// Running dynarec compiled code (fragments)
	PERF_CPU_COMPILE,	// Compiling fragments / flushing the fragment cache
	PERF_GFX,			// Display list parsing and everything in HLE graphics not listed below
	PERF_GFX_VTX,		// Vertex transform, lighting and clipping
	PERF_GFX_TEX,		// Texture lookup, hashing and conversion
	PERF_GFX_DRAW,		// Blend/combiner setup and GE command submission
	PERF_AUDIO,			// Audio list processing / dispatch to the Media Engine
	PERF_GE_WAIT,		// Waiting for the PSP GPU to finish the frame
	PERF_LIMITER,		// Sleeping in the framerate limiter (spare time)

	NUM_PERF_CATEGORIES
};

extern bool gPerfStatsEnabled;

namespace PerfStatsInternal
{
	const u32 kMaxDepth = 16;
	extern volatile u8	gStack[ kMaxDepth ];
	extern volatile u32	gDepth;
}

inline void PerfStats_Enter( EPerfCategory category )
{
	using namespace PerfStatsInternal;
	u32 depth = gDepth;
	if( depth < kMaxDepth )
	{
		gStack[ depth ] = (u8)category;
	}
	gDepth = depth + 1;
}

inline void PerfStats_Exit()
{
	using namespace PerfStatsInternal;
	if( gDepth > 0 )
	{
		gDepth = gDepth - 1;
	}
}

// Turn sampling on or off. Call once per displayed frame.
void	PerfStats_SetEnabled( bool enabled );

// Returns true when a new one second sample is ready.
bool	PerfStats_Update();

// Percentage of samples spent in each category over the last second.
u32		PerfStats_GetPercent( EPerfCategory category );
u32		PerfStats_GetCpuPercent();		// All CPU categories
u32		PerfStats_GetGfxPercent();		// All graphics categories

// Append the last sample to perf.txt (buffered, written every 10 samples)
void	PerfStats_LogSample( f32 fps, u32 vbls_per_second, u32 tv_hz );
void	PerfStats_Flush();

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
