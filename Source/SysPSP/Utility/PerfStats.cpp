/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include "Base/Types.h"
#include "SysPSP/Utility/PerfStats.h"
#include "Core/ROM.h"

#include <pspthreadman.h>
#include <cstdio>
#include <cstring>
#include <string>

bool gPerfStatsEnabled = false;

namespace PerfStatsInternal
{
	volatile u8		gStack[ kMaxDepth ];
	volatile u32	gDepth = 0;
	u32				gCounters[ NUM_PERF_COUNTERS ] = {};
}

using namespace PerfStatsInternal;

namespace
{
	const u32		kAlarmPeriodMicroseconds = 500;		// ~2000 samples per second
	const u32		kSampleMicroseconds = 1000000;

	volatile u32	gSampleCount[ NUM_PERF_CATEGORIES ] = {};
	SceUID			gAlarm = -1;
	u32				gSampleStart = 0;
	u32				gPercent[ NUM_PERF_CATEGORIES ] = {};
	u32				gCountsPerSecond[ NUM_PERF_COUNTERS ] = {};

	// Runs in interrupt context: only read the stack and bump a counter
	SceUInt PerfAlarmHandler( void * )
	{
		u32 depth = gDepth;
		u32 category = PERF_CPU;
		if( depth > 0 )
		{
			category = gStack[ (depth <= kMaxDepth ? depth : kMaxDepth) - 1 ];
		}
		if( category < NUM_PERF_CATEGORIES )
		{
			gSampleCount[ category ] = gSampleCount[ category ] + 1;
		}
		return kAlarmPeriodMicroseconds;
	}

	void ResetCounts()
	{
		for( u32 i = 0; i < NUM_PERF_CATEGORIES; ++i ) gSampleCount[ i ] = 0;
	}
}

void PerfStats_SetEnabled( bool enabled )
{
	if( enabled == gPerfStatsEnabled )
	{
		return;
	}

	gPerfStatsEnabled = enabled;
	if( enabled )
	{
		ResetCounts();
		for( u32 i = 0; i < NUM_PERF_COUNTERS; ++i ) gCounters[ i ] = 0;
		gSampleStart = sceKernelGetSystemTimeLow();
		gAlarm = sceKernelSetAlarm( kAlarmPeriodMicroseconds, PerfAlarmHandler, nullptr );
	}
	else
	{
		if( gAlarm >= 0 )
		{
			sceKernelCancelAlarm( gAlarm );
			gAlarm = -1;
		}
		// Scopes opened while enabled still close through their own flag, so
		// the stack is balanced. Reset anyway in case of an early exit path.
		gDepth = 0;
	}
}

bool PerfStats_Update()
{
	if( !gPerfStatsEnabled )
	{
		return false;
	}

	u32 now = sceKernelGetSystemTimeLow();
	u32 elapsed = now - gSampleStart;
	if( elapsed < kSampleMicroseconds )
	{
		return false;
	}
	gSampleStart = now;

	for( u32 i = 0; i < NUM_PERF_COUNTERS; ++i )
	{
		gCountsPerSecond[ i ] = (u32)(((u64)gCounters[ i ] * kSampleMicroseconds + elapsed / 2) / elapsed);
		gCounters[ i ] = 0;
	}

	u32 counts[ NUM_PERF_CATEGORIES ];
	u32 total = 0;
	for( u32 i = 0; i < NUM_PERF_CATEGORIES; ++i )
	{
		counts[ i ] = gSampleCount[ i ];
		gSampleCount[ i ] = 0;
		total += counts[ i ];
	}

	for( u32 i = 0; i < NUM_PERF_CATEGORIES; ++i )
	{
		gPercent[ i ] = total ? (counts[ i ] * 100 + total / 2) / total : 0;
	}
	return true;
}

u32 PerfStats_GetPercent( EPerfCategory category )
{
	return gPercent[ category ];
}

u32 PerfStats_GetCount( EPerfCounter counter )
{
	return gCountsPerSecond[ counter ];
}

u32 PerfStats_GetCpuPercent()
{
	return gPercent[ PERF_CPU ] + gPercent[ PERF_CPU_INTERP ] + gPercent[ PERF_CPU_DYNAREC ] + gPercent[ PERF_CPU_COMPILE ];
}

u32 PerfStats_GetGfxPercent()
{
	return gPercent[ PERF_GFX ] + gPercent[ PERF_GFX_VTX ] + gPercent[ PERF_GFX_TEX ] + gPercent[ PERF_GFX_DRAW ];
}

namespace
{
	const u32		kLogBufferSize = 4096;
	const u32		kFlushEverySamples = 10;

	char			gLogBuffer[ kLogBufferSize ];
	u32				gLogLength = 0;
	u32				gSamplesSinceFlush = 0;
	std::string		gLoggedGame;
	bool			gLoggedHeader = false;

	void Append( const char * line )
	{
		u32 len = strlen( line );
		if( gLogLength + len >= kLogBufferSize )
		{
			PerfStats_Flush();
		}
		if( len < kLogBufferSize )
		{
			memcpy( gLogBuffer + gLogLength, line, len );
			gLogLength += len;
		}
	}
}

void PerfStats_LogSample( f32 fps, u32 vbls_per_second, u32 tv_hz )
{
	char line[ 200 ];

	if( !gLoggedHeader || gLoggedGame != g_ROM.settings.GameName )
	{
		gLoggedHeader = true;
		gLoggedGame = g_ROM.settings.GameName;
		snprintf( line, sizeof( line ), "# %s\n# fps vb/hz | cpu: int dyn jit other | gfx: dl vtx tex draw | aud ge idle | per second: traces aborted compiled\n", gLoggedGame.c_str() );
		Append( line );
	}

	snprintf( line, sizeof( line ), "%.1f %u/%u | %u %u %u %u | %u %u %u %u | %u %u %u | %u %u %u\n",
		fps, (unsigned)vbls_per_second, (unsigned)tv_hz,
		(unsigned)gPercent[ PERF_CPU_INTERP ], (unsigned)gPercent[ PERF_CPU_DYNAREC ], (unsigned)gPercent[ PERF_CPU_COMPILE ], (unsigned)gPercent[ PERF_CPU ],
		(unsigned)gPercent[ PERF_GFX ], (unsigned)gPercent[ PERF_GFX_VTX ], (unsigned)gPercent[ PERF_GFX_TEX ], (unsigned)gPercent[ PERF_GFX_DRAW ],
		(unsigned)gPercent[ PERF_AUDIO ], (unsigned)gPercent[ PERF_GE_WAIT ], (unsigned)gPercent[ PERF_LIMITER ],
		(unsigned)gCountsPerSecond[ PERF_COUNT_TRACE_START ], (unsigned)gCountsPerSecond[ PERF_COUNT_TRACE_ABORT ],
		(unsigned)gCountsPerSecond[ PERF_COUNT_FRAGMENT ] );
	Append( line );

	if( ++gSamplesSinceFlush >= kFlushEverySamples )
	{
		PerfStats_Flush();
	}
}

void PerfStats_Flush()
{
	gSamplesSinceFlush = 0;
	if( gLogLength == 0 )
	{
		return;
	}

	FILE * fh = fopen( "perf.txt", "a" );
	if( fh != nullptr )
	{
		fwrite( gLogBuffer, 1, gLogLength, fh );
		fclose( fh );
	}
	gLogLength = 0;
}
