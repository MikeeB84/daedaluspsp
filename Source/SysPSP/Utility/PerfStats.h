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
	PERF_CPU = 0,		// Anything not listed below: event/interrupt handling, dispatch, OS HLE...
	PERF_CPU_INTERP,	// Interpreting N64 instructions one at a time (including trace recording)
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

// Event counters, reported per second
enum EPerfCounter
{
	PERF_COUNT_TRACE_START = 0,		// Dynarec started recording a hot trace
	PERF_COUNT_TRACE_ABORT,			// ...and had to throw it away (exception)
	PERF_COUNT_TRACE_SALVAGED,		// ...was cut short by an interrupt but compiled what it had
	PERF_COUNT_FRAGMENT,			// Fragments compiled
	PERF_COUNT_FLUSH,				// Whole fragment cache thrown away (any reason)

	NUM_PERF_COUNTERS
};

extern bool gPerfStatsEnabled;

namespace PerfStatsInternal
{
	const u32 kMaxDepth = 16;
	extern volatile u8	gStack[ kMaxDepth ];
	extern volatile u32	gDepth;
	extern u32			gCounters[ NUM_PERF_COUNTERS ];
	extern volatile u32	gInterpEntry;		// PC where the current run of interpreted code began
}

// Note where interpretation resumes (after compiled code returns, or at the start of the CPU loop)
inline void PerfStats_NoteInterpEntry( u32 pc )
{
	PerfStatsInternal::gInterpEntry = pc;
}

inline void PerfStats_Count( EPerfCounter counter )
{
	PerfStatsInternal::gCounters[ counter ]++;
}

// Events per second over the last sample
u32		PerfStats_GetCount( EPerfCounter counter );

// Dynarec trace events, collected per trace start address for dynarec.txt
enum ETraceEvent
{
	TRACE_EVENT_START,
	TRACE_EVENT_ABORT,
	TRACE_EVENT_SALVAGED,
	TRACE_EVENT_COMPILED,
};
void	PerfStats_TraceEvent( ETraceEvent event, u32 start_address, u32 pc, u32 stuff_to_do, u32 length );

// Record the instructions of an aborted trace (first time per start address)
void	PerfStats_CaptureAbortedTrace( u32 start_address, const u32 * addresses, u32 count );

// Why the whole fragment cache was thrown away
enum EFlushReason
{
	FLUSH_INVALIDATE_REQUEST,	// Game wrote over / invalidated compiled code (flush happens at next safe point)
	FLUSH_INVALIDATE_DONE,		// ...and the pending flush was carried out
	FLUSH_CACHE_FULL,			// Too many fragments
	FLUSH_HOT_MAP_FULL,			// Too many distinct branch targets being counted
	NUM_FLUSH_REASONS
};
void	PerfStats_NoteFlush( EFlushReason reason, u32 address, u32 length );

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
