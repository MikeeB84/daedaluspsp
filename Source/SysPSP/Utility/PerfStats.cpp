/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include "Base/Types.h"
#include "SysPSP/Utility/PerfStats.h"
#include "Core/ROM.h"
#include "Core/CPU.h"
#include "Core/Memory.h"
#include "Debug/PrintOpCode.h"

#include <algorithm>

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

	// Interpreter hotspots: which N64 PC was being interpreted when sampled
	const u32		kNumPcSlots = 256;
	struct SPcSample { u32 Pc; u32 Count; };
	SPcSample		gInterpPcs[ kNumPcSlots ];
	volatile u32	gInterpPcsDropped = 0;

	void RecordInterpPc( u32 pc )
	{
		u32 idx = (pc >> 2) & (kNumPcSlots - 1);
		for( u32 i = 0; i < 8; ++i )
		{
			SPcSample & slot = gInterpPcs[ (idx + i) & (kNumPcSlots - 1) ];
			if( slot.Count == 0 || slot.Pc == pc )
			{
				slot.Pc = pc;
				slot.Count++;
				return;
			}
		}
		gInterpPcsDropped = gInterpPcsDropped + 1;
	}

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
		if( category == PERF_CPU_INTERP )
		{
			RecordInterpPc( gCPUState.CurrentPC );
		}
		return kAlarmPeriodMicroseconds;
	}

	// Trace statistics per start address
	const u32		kNumTraceSlots = 128;
	struct STraceStats
	{
		u32		StartAddress;
		u32		Starts;
		u32		Aborts;
		u32		Salvaged;
		u32		Compiled;
		u32		AbortReasons;		// OR of StuffToDo flags seen on abort
		u32		LastAbortPc;
		u32		LastAbortLength;
	};
	STraceStats		gTraceStats[ kNumTraceSlots ];
	u32				gNumTraceStats = 0;
	u32				gTraceStatsDropped = 0;

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

void PerfStats_TraceEvent( ETraceEvent event, u32 start_address, u32 pc, u32 stuff_to_do, u32 length )
{
	if( !gPerfStatsEnabled )
	{
		return;
	}

	STraceStats * stats = nullptr;
	for( u32 i = 0; i < gNumTraceStats; ++i )
	{
		if( gTraceStats[ i ].StartAddress == start_address )
		{
			stats = &gTraceStats[ i ];
			break;
		}
	}
	if( stats == nullptr )
	{
		if( gNumTraceStats >= kNumTraceSlots )
		{
			gTraceStatsDropped++;
			return;
		}
		stats = &gTraceStats[ gNumTraceStats++ ];
		memset( stats, 0, sizeof( STraceStats ) );
		stats->StartAddress = start_address;
	}

	switch( event )
	{
	case TRACE_EVENT_START:		stats->Starts++; break;
	case TRACE_EVENT_SALVAGED:	stats->Salvaged++; break;
	case TRACE_EVENT_COMPILED:	stats->Compiled++; break;
	case TRACE_EVENT_ABORT:
		stats->Aborts++;
		stats->AbortReasons |= stuff_to_do;
		stats->LastAbortPc = pc;
		stats->LastAbortLength = length;
		break;
	}
}

namespace
{
	// Read an instruction for the report without side effects (RDRAM only)
	bool PeekInstruction( u32 address, OpCode * op )
	{
		if( (address & 0x3) != 0 ) return false;
		u32 segment = address & 0xE0000000;
		if( segment != 0x80000000 && segment != 0xA0000000 ) return false;
		u32 offset = address & 0x1FFFFFFF;
		if( offset + 4 > gRamSize ) return false;
		op->_u32 = *(const u32 *)( g_pu8RamBase + offset );
		return true;
	}

	void WriteInstruction( FILE * fh, u32 address, const char * prefix )
	{
		OpCode op;
		char buf[ 128 ];
		if( PeekInstruction( address, &op ) )
		{
			SprintOpCodeInfo( buf, address, op );
			fprintf( fh, "%s%08x: %08x  %s\n", prefix, (unsigned)address, (unsigned)op._u32, buf );
		}
		else
		{
			fprintf( fh, "%s%08x: (mapped or not RDRAM)\n", prefix, (unsigned)address );
		}
	}

	void DescribeReasons( u32 reasons, char * out, u32 size )
	{
		snprintf( out, size, "%s%s%s%s",
			(reasons & CPU_CHECK_EXCEPTIONS) ? "exception " : "",
			(reasons & CPU_CHECK_INTERRUPTS) ? "interrupt " : "",
			(reasons & CPU_STOP_RUNNING) ? "stop " : "",
			(reasons & ~(CPU_CHECK_EXCEPTIONS|CPU_CHECK_INTERRUPTS|CPU_STOP_RUNNING|CPU_CHANGE_CORE)) ? "other" : "" );
	}

	u32 gReportIndex = 0;

	void WriteDynarecReport()
	{
		FILE * fh = fopen( "dynarec.txt", "a" );
		if( fh == nullptr )
		{
			return;
		}

		fprintf( fh, "==== %s - report %u (last ~10 seconds)\n", g_ROM.settings.GameName.c_str(), (unsigned)gReportIndex++ );

		// Interpreter hotspots
		SPcSample pcs[ kNumPcSlots ];
		u32 num_pcs = 0;
		u32 total = 0;
		for( u32 i = 0; i < kNumPcSlots; ++i )
		{
			if( gInterpPcs[ i ].Count )
			{
				pcs[ num_pcs++ ] = gInterpPcs[ i ];
				total += gInterpPcs[ i ].Count;
			}
			gInterpPcs[ i ].Count = 0;
		}
		std::sort( pcs, pcs + num_pcs, []( const SPcSample & a, const SPcSample & b ) { return a.Count > b.Count; } );
		fprintf( fh, "Interpreter samples: %u (dropped %u)\n", (unsigned)total, (unsigned)gInterpPcsDropped );
		gInterpPcsDropped = 0;
		for( u32 i = 0; i < num_pcs && i < 24; ++i )
		{
			char prefix[ 32 ];
			snprintf( prefix, sizeof( prefix ), "  %5u  ", (unsigned)pcs[ i ].Count );
			WriteInstruction( fh, pcs[ i ].Pc, prefix );
		}

		// Traces
		// Problem traces (aborted or cut short) first, then the busiest
		std::sort( gTraceStats, gTraceStats + gNumTraceStats, []( const STraceStats & a, const STraceStats & b )
		{
			u32 pa = a.Aborts + a.Salvaged, pb = b.Aborts + b.Salvaged;
			return pa != pb ? pa > pb : a.Starts > b.Starts;
		} );
		fprintf( fh, "Traces (start address: started aborted salvaged compiled):\n" );
		for( u32 i = 0; i < gNumTraceStats && i < 32; ++i )
		{
			const STraceStats & t = gTraceStats[ i ];
			fprintf( fh, "  %08x: %u %u %u %u", (unsigned)t.StartAddress, (unsigned)t.Starts, (unsigned)t.Aborts, (unsigned)t.Salvaged, (unsigned)t.Compiled );
			if( t.Aborts )
			{
				char reasons[ 64 ];
				DescribeReasons( t.AbortReasons, reasons, sizeof( reasons ) );
				fprintf( fh, "  aborted by: %sat %08x after %u ops\n", reasons, (unsigned)t.LastAbortPc, (unsigned)t.LastAbortLength );
				WriteInstruction( fh, t.LastAbortPc - 4, "      " );
				WriteInstruction( fh, t.LastAbortPc, "   -> " );
			}
			else
			{
				fprintf( fh, "\n" );
			}
		}
		if( gTraceStatsDropped )
		{
			fprintf( fh, "  (%u events for other addresses not recorded)\n", (unsigned)gTraceStatsDropped );
		}
		gNumTraceStats = 0;
		gTraceStatsDropped = 0;

		fprintf( fh, "\n" );
		fclose( fh );
	}
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
		snprintf( line, sizeof( line ), "# %s\n# fps vb/hz | cpu: int dyn jit other | gfx: dl vtx tex draw | aud ge idle | per second: traces aborted salvaged compiled\n", gLoggedGame.c_str() );
		Append( line );
	}

	snprintf( line, sizeof( line ), "%.1f %u/%u | %u %u %u %u | %u %u %u %u | %u %u %u | %u %u %u %u\n",
		fps, (unsigned)vbls_per_second, (unsigned)tv_hz,
		(unsigned)gPercent[ PERF_CPU_INTERP ], (unsigned)gPercent[ PERF_CPU_DYNAREC ], (unsigned)gPercent[ PERF_CPU_COMPILE ], (unsigned)gPercent[ PERF_CPU ],
		(unsigned)gPercent[ PERF_GFX ], (unsigned)gPercent[ PERF_GFX_VTX ], (unsigned)gPercent[ PERF_GFX_TEX ], (unsigned)gPercent[ PERF_GFX_DRAW ],
		(unsigned)gPercent[ PERF_AUDIO ], (unsigned)gPercent[ PERF_GE_WAIT ], (unsigned)gPercent[ PERF_LIMITER ],
		(unsigned)gCountsPerSecond[ PERF_COUNT_TRACE_START ], (unsigned)gCountsPerSecond[ PERF_COUNT_TRACE_ABORT ],
		(unsigned)gCountsPerSecond[ PERF_COUNT_TRACE_SALVAGED ], (unsigned)gCountsPerSecond[ PERF_COUNT_FRAGMENT ] );
	Append( line );

	if( ++gSamplesSinceFlush >= kFlushEverySamples )
	{
		PerfStats_Flush();
		WriteDynarecReport();
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
