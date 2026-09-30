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
volatile u32 gPerfFragmentEntry = 0;

namespace PerfStatsInternal
{
	volatile u8		gStack[ kMaxDepth ];
	volatile u32	gDepth = 0;
	u32				gCounters[ NUM_PERF_COUNTERS ] = {};
	volatile u32	gInterpEntry = 0;
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
	const u32		kNumPcSlots = 1024;
	struct SPcSample { u32 Pc; u32 Count; };
	SPcSample		gInterpPcs[ kNumPcSlots ];
	SPcSample		gInterpEntries[ kNumPcSlots ];
	volatile u32	gInterpPcsDropped = 0;
	SPcSample		gFragmentSamples[ kNumPcSlots ];

	// Instructions of compiled fragments, stored as runs of consecutive addresses
	const u32		kNumFragmentInfos = 4096;
	const u32		kNumFragmentRuns = 16384;
	struct SFragmentInfo { u32 Entry; u16 FirstRun; u16 NumRuns; u16 NumOps; u16 OutputBytes; };
	struct SFragmentRun { u32 Start; u32 Length; };
	SFragmentInfo	gFragmentInfos[ kNumFragmentInfos ];
	SFragmentRun	gFragmentRuns[ kNumFragmentRuns ];
	u32				gNumFragmentInfos = 0;
	u32				gNumFragmentRuns = 0;

	SFragmentInfo * FindFragmentInfo( u32 entry, bool create )
	{
		u32 idx = ((entry >> 2) * 2654435761u) >> 20;		// 12 bits
		for( u32 i = 0; i < kNumFragmentInfos; ++i )
		{
			SFragmentInfo & info = gFragmentInfos[ (idx + i) & (kNumFragmentInfos - 1) ];
			if( info.Entry == entry ) return &info;
			if( info.Entry == 0 )
			{
				if( !create || gNumFragmentInfos >= kNumFragmentInfos * 3 / 4 ) return nullptr;
				gNumFragmentInfos++;
				info.Entry = entry;
				return &info;
			}
		}
		return nullptr;
	}

	bool RecordPc( SPcSample * table, u32 pc )
	{
		u32 idx = (pc >> 2) & (kNumPcSlots - 1);
		for( u32 i = 0; i < 16; ++i )
		{
			SPcSample & slot = table[ (idx + i) & (kNumPcSlots - 1) ];
			if( slot.Count == 0 || slot.Pc == pc )
			{
				slot.Pc = pc;
				slot.Count++;
				return true;
			}
		}
		return false;
	}

	void RecordInterpPc( u32 pc )
	{
		if( !RecordPc( gInterpPcs, pc ) )
		{
			gInterpPcsDropped = gInterpPcsDropped + 1;
		}
		RecordPc( gInterpEntries, gInterpEntry );
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
		else if( category == PERF_CPU_DYNAREC )
		{
			RecordPc( gFragmentSamples, gPerfFragmentEntry );
		}
		return kAlarmPeriodMicroseconds;
	}

	// Trace statistics per start address
	const u32		kNumTraceSlots = 1024;
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

	// Instruction listings of aborted traces
	const u32		kNumCapturedTraces = 8;
	const u32		kMaxCapturedOps = 48;
	struct SCapturedTrace
	{
		u32		StartAddress;
		u32		Count;
		u32		Addresses[ kMaxCapturedOps ];
	};
	SCapturedTrace	gCapturedTraces[ kNumCapturedTraces ];
	u32				gNumCapturedTraces = 0;

	// Fragment cache flushes
	u32				gFlushCounts[ NUM_FLUSH_REASONS ] = {};
	const u32		kNumFlushLog = 16;
	struct SFlushEvent { u32 Reason; u32 Address; u32 Length; };
	SFlushEvent		gFlushLog[ kNumFlushLog ];
	u32				gNumFlushLog = 0;

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

void PerfStats_CaptureAbortedTrace( u32 start_address, const u32 * addresses, u32 count )
{
	if( !gPerfStatsEnabled )
	{
		return;
	}
	for( u32 i = 0; i < gNumCapturedTraces; ++i )
	{
		if( gCapturedTraces[ i ].StartAddress == start_address )
		{
			return;
		}
	}
	if( gNumCapturedTraces >= kNumCapturedTraces )
	{
		return;
	}
	SCapturedTrace & capture = gCapturedTraces[ gNumCapturedTraces++ ];
	capture.StartAddress = start_address;
	capture.Count = count < kMaxCapturedOps ? count : kMaxCapturedOps;
	memcpy( capture.Addresses, addresses, capture.Count * sizeof( u32 ) );
}

void PerfStats_NoteFragment( u32 entry_address, const u32 * addresses, u32 count, u32 output_bytes )
{
	if( count == 0 ) return;

	// Count the runs first so a fragment is either stored whole or not at all
	u32 num_runs = 1;
	for( u32 i = 1; i < count; ++i )
	{
		if( addresses[ i ] != addresses[ i - 1 ] + 4 ) num_runs++;
	}
	if( gNumFragmentRuns + num_runs > kNumFragmentRuns || gNumFragmentInfos >= kNumFragmentInfos * 3 / 4 )
	{
		// Out of space: start again (fragments recompiled later are recorded afresh)
		memset( gFragmentInfos, 0, sizeof( gFragmentInfos ) );
		gNumFragmentInfos = 0;
		gNumFragmentRuns = 0;
		if( num_runs > kNumFragmentRuns ) return;
	}

	SFragmentInfo * info = FindFragmentInfo( entry_address, true );
	if( info == nullptr ) return;

	info->FirstRun = (u16)gNumFragmentRuns;
	info->NumRuns = (u16)num_runs;
	info->NumOps = (u16)std::min< u32 >( count, 0xFFFF );
	info->OutputBytes = (u16)std::min< u32 >( output_bytes, 0xFFFF );

	SFragmentRun * run = &gFragmentRuns[ gNumFragmentRuns ];
	run->Start = addresses[ 0 ];
	run->Length = 1;
	for( u32 i = 1; i < count; ++i )
	{
		if( addresses[ i ] == addresses[ i - 1 ] + 4 )
		{
			run->Length++;
		}
		else
		{
			++run;
			run->Start = addresses[ i ];
			run->Length = 1;
		}
	}
	gNumFragmentRuns += num_runs;
}

void PerfStats_NoteFlush( EFlushReason reason, u32 address, u32 length )
{
	if( reason != FLUSH_INVALIDATE_REQUEST )
	{
		PerfStats_Count( PERF_COUNT_FLUSH );
	}
	if( !gPerfStatsEnabled )
	{
		return;
	}
	gFlushCounts[ reason ]++;
	if( gNumFlushLog < kNumFlushLog )
	{
		gFlushLog[ gNumFlushLog++ ] = SFlushEvent{ reason, address, length };
	}
}

// Implemented in Core/Dynamo.cpp: hit count and fragment state for a PC
extern void Dynamo_DescribePc( u32 pc, u32 * hot_count, bool * has_fragment );

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
		static SPcSample pcs[ kNumPcSlots ];
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
		fprintf( fh, "  samples  [hits so far / compiled here?]  (hits and compiled only mean something at an entry address)\n" );
		for( u32 i = 0; i < num_pcs && i < 32; ++i )
		{
			u32 hot_count = 0;
			bool has_fragment = false;
			Dynamo_DescribePc( pcs[ i ].Pc, &hot_count, &has_fragment );

			char prefix[ 48 ];
			snprintf( prefix, sizeof( prefix ), "  %5u  [%4u %s]  ", (unsigned)pcs[ i ].Count, (unsigned)hot_count, has_fragment ? "yes" : " no" );
			WriteInstruction( fh, pcs[ i ].Pc, prefix );
		}

		// Where runs of interpreted code began
		{
			static SPcSample entries[ kNumPcSlots ];
			u32 num_entries = 0;
			for( u32 i = 0; i < kNumPcSlots; ++i )
			{
				if( gInterpEntries[ i ].Count )
				{
					entries[ num_entries++ ] = gInterpEntries[ i ];
				}
				gInterpEntries[ i ].Count = 0;
			}
			std::sort( entries, entries + num_entries, []( const SPcSample & a, const SPcSample & b ) { return a.Count > b.Count; } );
			fprintf( fh, "Interpreted runs started at (samples [hits so far / compiled here?]):\n" );
			for( u32 i = 0; i < num_entries && i < 12; ++i )
			{
				u32 hot_count = 0;
				bool has_fragment = false;
				Dynamo_DescribePc( entries[ i ].Pc, &hot_count, &has_fragment );

				char prefix[ 48 ];
				snprintf( prefix, sizeof( prefix ), "  %5u  [%4u %s]  ", (unsigned)entries[ i ].Count, (unsigned)hot_count, has_fragment ? "yes" : " no" );
				WriteInstruction( fh, entries[ i ].Pc, prefix );
				if( i < 4 )
				{
					for( u32 j = 1; j < 6; ++j )
					{
						WriteInstruction( fh, entries[ i ].Pc + j * 4, "                       " );
					}
				}
			}
		}

		// Compiled code hotspots, by fragment
		{
			static SPcSample frags[ kNumPcSlots ];
			u32 num_frags = 0;
			u32 frag_total = 0;
			for( u32 i = 0; i < kNumPcSlots; ++i )
			{
				if( gFragmentSamples[ i ].Count )
				{
					frags[ num_frags++ ] = gFragmentSamples[ i ];
					frag_total += gFragmentSamples[ i ].Count;
				}
				gFragmentSamples[ i ].Count = 0;
			}
			std::sort( frags, frags + num_frags, []( const SPcSample & a, const SPcSample & b ) { return a.Count > b.Count; } );
			fprintf( fh, "Compiled code samples: %u in %u fragments\n", (unsigned)frag_total, (unsigned)num_frags );
			fprintf( fh, "  samples  fragment  N64 ops  PSP bytes  bytes/op\n" );
			for( u32 i = 0; i < num_frags && i < 24; ++i )
			{
				const SFragmentInfo * info = FindFragmentInfo( frags[ i ].Pc, false );
				if( info != nullptr && info->NumOps > 0 )
				{
					fprintf( fh, "  %5u    %08x  %5u    %6u     %5.1f\n", (unsigned)frags[ i ].Count, (unsigned)frags[ i ].Pc,
						(unsigned)info->NumOps, (unsigned)info->OutputBytes, (float)info->OutputBytes / info->NumOps );
				}
				else
				{
					fprintf( fh, "  %5u    %08x  (compiled before stats were enabled, or an OS function)\n", (unsigned)frags[ i ].Count, (unsigned)frags[ i ].Pc );
				}
			}
			for( u32 i = 0; i < num_frags && i < 8; ++i )
			{
				const SFragmentInfo * info = FindFragmentInfo( frags[ i ].Pc, false );
				if( info == nullptr || info->NumOps == 0 ) continue;
				fprintf( fh, "Fragment %08x (%u samples, %u ops):\n", (unsigned)frags[ i ].Pc, (unsigned)frags[ i ].Count, (unsigned)info->NumOps );
				u32 printed = 0;
				for( u32 r = 0; r < info->NumRuns && printed < 64; ++r )
				{
					const SFragmentRun & run = gFragmentRuns[ info->FirstRun + r ];
					for( u32 j = 0; j < run.Length && printed < 64; ++j, ++printed )
					{
						WriteInstruction( fh, run.Start + j * 4, "      " );
					}
				}
				if( printed < info->NumOps ) fprintf( fh, "      ... %u more\n", (unsigned)(info->NumOps - printed) );
			}
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
		if( gNumTraceStats > 32 )
		{
			fprintf( fh, "  (%u more addresses)\n", (unsigned)(gNumTraceStats - 32) );
		}
		if( gTraceStatsDropped )
		{
			fprintf( fh, "  (%u events for other addresses not recorded)\n", (unsigned)gTraceStatsDropped );
		}
		gNumTraceStats = 0;
		gTraceStatsDropped = 0;

		// Aborted trace listings
		for( u32 i = 0; i < gNumCapturedTraces; ++i )
		{
			const SCapturedTrace & capture = gCapturedTraces[ i ];
			fprintf( fh, "Aborted trace %08x (%u ops):\n", (unsigned)capture.StartAddress, (unsigned)capture.Count );
			for( u32 j = 0; j < capture.Count; ++j )
			{
				WriteInstruction( fh, capture.Addresses[ j ], "      " );
			}
		}
		gNumCapturedTraces = 0;

		// Flushes
		fprintf( fh, "Fragment cache flushes: invalidate requests %u, done %u, cache full %u, branch-target table full %u\n",
			(unsigned)gFlushCounts[ FLUSH_INVALIDATE_REQUEST ], (unsigned)gFlushCounts[ FLUSH_INVALIDATE_DONE ],
			(unsigned)gFlushCounts[ FLUSH_CACHE_FULL ], (unsigned)gFlushCounts[ FLUSH_HOT_MAP_FULL ] );
		static const char * const kReasonNames[ NUM_FLUSH_REASONS ] = { "invalidate request", "invalidate done", "cache full", "branch-target table full" };
		for( u32 i = 0; i < gNumFlushLog; ++i )
		{
			fprintf( fh, "  %s", kReasonNames[ gFlushLog[ i ].Reason ] );
			if( gFlushLog[ i ].Reason == FLUSH_INVALIDATE_REQUEST )
			{
				fprintf( fh, " at %08x, %u bytes", (unsigned)gFlushLog[ i ].Address, (unsigned)gFlushLog[ i ].Length );
			}
			fprintf( fh, "\n" );
		}
		for( u32 i = 0; i < NUM_FLUSH_REASONS; ++i ) gFlushCounts[ i ] = 0;
		gNumFlushLog = 0;

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
		snprintf( line, sizeof( line ), "# %s\n# fps vb/hz | cpu: int dyn jit other | gfx: dl vtx tex draw | aud ge idle | per second: traces aborted salvaged compiled flushes\n", gLoggedGame.c_str() );
		Append( line );
	}

	snprintf( line, sizeof( line ), "%.1f %u/%u | %u %u %u %u | %u %u %u %u | %u %u %u | %u %u %u %u %u\n",
		fps, (unsigned)vbls_per_second, (unsigned)tv_hz,
		(unsigned)gPercent[ PERF_CPU_INTERP ], (unsigned)gPercent[ PERF_CPU_DYNAREC ], (unsigned)gPercent[ PERF_CPU_COMPILE ], (unsigned)gPercent[ PERF_CPU ],
		(unsigned)gPercent[ PERF_GFX ], (unsigned)gPercent[ PERF_GFX_VTX ], (unsigned)gPercent[ PERF_GFX_TEX ], (unsigned)gPercent[ PERF_GFX_DRAW ],
		(unsigned)gPercent[ PERF_AUDIO ], (unsigned)gPercent[ PERF_GE_WAIT ], (unsigned)gPercent[ PERF_LIMITER ],
		(unsigned)gCountsPerSecond[ PERF_COUNT_TRACE_START ], (unsigned)gCountsPerSecond[ PERF_COUNT_TRACE_ABORT ],
		(unsigned)gCountsPerSecond[ PERF_COUNT_TRACE_SALVAGED ], (unsigned)gCountsPerSecond[ PERF_COUNT_FRAGMENT ],
		(unsigned)gCountsPerSecond[ PERF_COUNT_FLUSH ] );
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

//*****************************************************************************
// Hang watchdog
//*****************************************************************************
#include <pspge.h>
#include "Core/CPU.h"

volatile u32 gWatchdogFrames = 0;

extern volatile u32 gDLLastCmd0;
extern volatile u32 gDLLastCmd1;
extern volatile u32 gDLLastPC;
extern volatile s32 gDLLastDepth;
extern char gUcodeDescription[];

namespace
{
	const u32		kWatchdogPollMicroseconds = 500 * 1000;
	const u32		kWatchdogStallPolls = 8;			// Report after ~4 seconds without a new frame

	SceUID			gWatchdogThread = -1;
	volatile bool	gWatchdogRunning = false;

	const char * CategoryName( u32 category )
	{
		static const char * const kNames[] = { "CPU other (events, interrupts, OS HLE)", "CPU interpreter", "CPU compiled code",
			"CPU compiling", "Graphics (display list)", "Graphics vertex", "Graphics texture", "Graphics draw",
			"Audio", "Waiting for the PSP GPU (GE)", "Frame limiter" };
		return category < NUM_PERF_CATEGORIES ? kNames[ category ] : "?";
	}

	void WriteHangReport( u32 seconds, u32 vi_at_stall, u32 frames )
	{
		FILE * fh = fopen( "hang.txt", "a" );
		if( fh == nullptr )
		{
			return;
		}

		fprintf( fh, "==== %s: no new frame for %u seconds\n", g_ROM.settings.GameName.c_str(), (unsigned)seconds );
		fprintf( fh, "Frames presented: %u. N64 vertical interrupts: %u at the stall, %u now (%s)\n",
			(unsigned)frames, (unsigned)vi_at_stall, (unsigned)CPU_GetVerticalInterruptCount(),
			CPU_GetVerticalInterruptCount() != vi_at_stall ? "N64 emulation is still running" : "N64 emulation is stuck" );

		if( gPerfStatsEnabled )
		{
			u32 depth = gDepth;
			fprintf( fh, "Emulator activity (innermost last):" );
			if( depth == 0 ) fprintf( fh, " %s", CategoryName( PERF_CPU ) );
			for( u32 i = 0; i < depth && i < kMaxDepth; ++i )
			{
				fprintf( fh, "%s %s", i ? " >" : "", CategoryName( gStack[ i ] ) );
			}
			fprintf( fh, "\n" );
			fprintf( fh, "Last compiled fragment entered (N64): %08x\n", (unsigned)gPerfFragmentEntry );
		}
		else
		{
			fprintf( fh, "Emulator activity: not tracked (set Display Framerate to FPS + Timing)\n" );
		}

		fprintf( fh, "N64 PC: %08x\n", (unsigned)gCPUState.CurrentPC );
		fprintf( fh, "Graphics microcode: %s\n", gUcodeDescription );
		fprintf( fh, "Last display list command: %08x %08x at %08x (depth %d)\n",
			(unsigned)gDLLastCmd0, (unsigned)gDLLastCmd1, (unsigned)gDLLastPC, (int)gDLLastDepth );
		// Peek at the PSP GPU without waiting: 0 = done, 1 = queued, 2 = drawing, 3 = stall reached, <0 = error
		fprintf( fh, "PSP GE draw state: %d\n\n", sceGeDrawSync( 1 ) );
		fclose( fh );
	}

	int WatchdogThread( SceSize, void * )
	{
		u32 last_frames = gWatchdogFrames;
		u32 stalled_polls = 0;
		u32 vi_at_stall = 0;
		bool reported = false;

		while( gWatchdogRunning )
		{
			sceKernelDelayThread( kWatchdogPollMicroseconds );

			u32 frames = gWatchdogFrames;
			if( frames != last_frames || !CPU_IsRunning() )		// Progress, or paused in the menu
			{
				last_frames = frames;
				stalled_polls = 0;
				reported = false;
				continue;
			}

			if( stalled_polls == 0 )
			{
				vi_at_stall = CPU_GetVerticalInterruptCount();
			}
			stalled_polls++;

			if( stalled_polls >= kWatchdogStallPolls && !reported )
			{
				WriteHangReport( stalled_polls * kWatchdogPollMicroseconds / 1000000, vi_at_stall, frames );
				reported = true;
			}
		}
		return 0;
	}
}

void Watchdog_Start()
{
	if( gWatchdogThread >= 0 )
	{
		return;
	}
	gWatchdogRunning = true;
	// High priority so it still runs while the emulator thread is busy; it sleeps almost all the time
	gWatchdogThread = sceKernelCreateThread( "Watchdog", WatchdogThread, 0x10, 0x8000, PSP_THREAD_ATTR_USER, nullptr );
	if( gWatchdogThread >= 0 )
	{
		sceKernelStartThread( gWatchdogThread, 0, nullptr );
	}
}

void Watchdog_Stop()
{
	if( gWatchdogThread < 0 )
	{
		return;
	}
	gWatchdogRunning = false;
	SceUInt timeout = 2 * 1000 * 1000;
	sceKernelWaitThreadEnd( gWatchdogThread, &timeout );
	sceKernelDeleteThread( gWatchdogThread );
	gWatchdogThread = -1;
}
