/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include "stdafx.h"
#include "SysPSP/Benchmark.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <algorithm>
#include <string>
#include <vector>

#include <pspkernel.h>
#include <psploadexec.h>
#include <psppower.h>
#include <pspdebug.h>

#include "Core/CPU.h"
#include "Core/ROM.h"
#include "OSHLE/ultra_os.h"
#include "SysPSP/UI/PSPMenu.h"
#include "SysPSP/Utility/PerfStats.h"
#include "System/System.h"
#include "Utility/FramerateLimiter.h"
#include "Utility/IO.h"
#include "Utility/Preferences.h"
#include "Utility/ROMFile.h"

volatile u32 gBenchmarkButtons = 0;

namespace
{
	const char * const	kSettingsFile = "benchmark.ini";
	const char * const	kResultsFile = "benchmark.txt";
	const char * const	kProgressFile = "benchmark_progress.txt";

	struct SSettings
	{
		u32							Seconds = 300;			// How long each ROM runs
		u32							ButtonSeconds = 60;		// Press Start / A during this time to get past title screens
		u32							FreezeSeconds = 30;		// Stop a ROM that shows no new frame for this long
		u32							NoPictureSeconds = 90;	// Stop a ROM that never shows a frame
		std::vector< std::string >	RomDirectories;
	};

	SSettings			gSettings;
	std::string			gEbootPath;
	volatile bool		gRunning = false;

	// The ROM being run (written by the main thread before it starts)
	u32					gRomIndex = 0;
	std::string			gRomFile;
	std::string			gRomName;

	// Measurements, updated once per N64 vertical interrupt on the emulator thread
	enum EOutcome { OUTCOME_RUNNING, OUTCOME_OK, OUTCOME_FROZE, OUTCOME_NO_PICTURE };

	struct SRun
	{
		u64		StartUs;
		u64		LastSecondUs;
		u64		LastFrameUs;
		u32		LastFrames;
		u32		FramesAtSecond;
		u32		VisAtSecond;
		bool	SeenFrame;

		u32		Seconds;					// Whole seconds since the ROM started

		// After the button phase (or everything, if the ROM stopped before then).
		// Measured in real time: with a very slow game there can be several seconds between checks.
		u64		SteadyUs;
		u32		SteadyFrames;
		u32		SteadyVis;
		u32		SteadyIdle, SteadyCpu, SteadyGfx, SteadyPercentSamples;
		u64		AllUs;
		u32		AllFrames, AllVis;

		// 10 second windows, for the slowest and fastest stretch
		u64		WindowUs;
		u32		WindowFrames;
		u32		NumWindows;
		f32		MinWindowFps;
		f32		MaxWindowFps;

		volatile EOutcome	Outcome;
		u32		StopSecond;
	};
	SRun				gRun;

	void ResetRun()
	{
		memset( &gRun, 0, sizeof( gRun ) );
		gRun.StartUs = sceKernelGetSystemTimeWide();
		gRun.LastSecondUs = gRun.StartUs;
		gRun.LastFrameUs = gRun.StartUs;
		gRun.LastFrames = gWatchdogFrames;
		gRun.FramesAtSecond = gWatchdogFrames;
		gRun.VisAtSecond = CPU_GetVerticalInterruptCount();
		gRun.MinWindowFps = 1000.0f;
		gRun.MaxWindowFps = 0.0f;
		gRun.Outcome = OUTCOME_RUNNING;
	}

	void Stop( EOutcome outcome )
	{
		gRun.Outcome = outcome;
		gRun.StopSecond = gRun.Seconds;
		gBenchmarkButtons = 0;
		CPU_Halt( "Benchmark" );
	}

	// Runs on the emulator thread once per N64 vertical interrupt
	void BenchmarkVbl( void * )
	{
		if( gRun.Outcome != OUTCOME_RUNNING )
		{
			return;
		}

		const u64 now = sceKernelGetSystemTimeWide();
		const u64 elapsed_ms = ( now - gRun.StartUs ) / 1000;

		// Get past title screens: tap Start, then A, every 4 seconds during the button phase
		u32 buttons = 0;
		if( elapsed_ms < u64( gSettings.ButtonSeconds ) * 1000 )
		{
			const u32 phase = u32( elapsed_ms % 4000 );
			if( phase < 200 )							buttons = START_BUTTON;
			else if( phase >= 2000 && phase < 2200 )	buttons = A_BUTTON;
		}
		gBenchmarkButtons = buttons;

		const u32 frames = gWatchdogFrames;
		if( frames != gRun.LastFrames )
		{
			gRun.LastFrames = frames;
			gRun.LastFrameUs = now;
			gRun.SeenFrame = true;
		}

		// About once a second
		if( now - gRun.LastSecondUs >= 1000000 )
		{
			const u64 delta_us = now - gRun.LastSecondUs;
			gRun.LastSecondUs = now;
			gRun.Seconds = u32( ( now - gRun.StartUs ) / 1000000 );
			scePowerTick( PSP_POWER_TICK_ALL );		// Don't let the PSP go to sleep or dim the screen

			const u32 vis = CPU_GetVerticalInterruptCount();
			const u32 delta_frames = frames - gRun.FramesAtSecond;
			const u32 delta_vis = vis - gRun.VisAtSecond;
			gRun.FramesAtSecond = frames;
			gRun.VisAtSecond = vis;

			gRun.AllUs += delta_us;
			gRun.AllFrames += delta_frames;
			gRun.AllVis += delta_vis;

			if( gRun.Seconds > gSettings.ButtonSeconds )
			{
				gRun.SteadyUs += delta_us;
				gRun.SteadyFrames += delta_frames;
				gRun.SteadyVis += delta_vis;
				gRun.SteadyIdle += PerfStats_GetPercent( PERF_LIMITER );
				gRun.SteadyCpu += PerfStats_GetCpuPercent();
				gRun.SteadyGfx += PerfStats_GetGfxPercent();
				gRun.SteadyPercentSamples++;

				gRun.WindowUs += delta_us;
				gRun.WindowFrames += delta_frames;
				if( gRun.WindowUs >= 10 * 1000000 )
				{
					const f32 fps = gRun.WindowFrames * 1000000.0f / f32( gRun.WindowUs );
					gRun.MinWindowFps = std::min( gRun.MinWindowFps, fps );
					gRun.MaxWindowFps = std::max( gRun.MaxWindowFps, fps );
					gRun.NumWindows++;
					gRun.WindowUs = 0;
					gRun.WindowFrames = 0;
				}
			}

			if( gRun.Seconds >= gSettings.Seconds )
			{
				Stop( OUTCOME_OK );
				return;
			}
		}

		if( !gRun.SeenFrame )
		{
			if( now - gRun.StartUs >= u64( gSettings.NoPictureSeconds ) * 1000000 )
			{
				Stop( OUTCOME_NO_PICTURE );
			}
		}
		else if( now - gRun.LastFrameUs >= u64( gSettings.FreezeSeconds ) * 1000000 )
		{
			Stop( OUTCOME_FROZE );
		}
	}

	//*************************************************************************
	// Files
	//*************************************************************************
	void ReadSettings()
	{
		FILE * fh = fopen( kSettingsFile, "r" );
		if( fh == nullptr )
		{
			return;
		}
		char line[ 512 ];
		while( fgets( line, sizeof( line ), fh ) != nullptr )
		{
			char * end = line + strlen( line );
			while( end > line && ( end[ -1 ] == '\n' || end[ -1 ] == '\r' || end[ -1 ] == ' ' ) ) *--end = '\0';
			if( line[ 0 ] == '#' || line[ 0 ] == ';' ) continue;
			char * eq = strchr( line, '=' );
			if( eq == nullptr ) continue;
			*eq = '\0';
			const char * key = line;
			const char * value = eq + 1;
			if( strcasecmp( key, "Seconds" ) == 0 )					gSettings.Seconds = std::max( 10, atoi( value ) );
			else if( strcasecmp( key, "ButtonSeconds" ) == 0 )		gSettings.ButtonSeconds = std::max( 0, atoi( value ) );
			else if( strcasecmp( key, "FreezeSeconds" ) == 0 )		gSettings.FreezeSeconds = std::max( 5, atoi( value ) );
			else if( strcasecmp( key, "NoPictureSeconds" ) == 0 )	gSettings.NoPictureSeconds = std::max( 10, atoi( value ) );
			else if( strcasecmp( key, "Roms" ) == 0 && value[ 0 ] != '\0' )
			{
				std::string dir( value );
				if( dir[ dir.size() - 1 ] != '/' ) dir += '/';
				gSettings.RomDirectories.push_back( dir );
			}
		}
		fclose( fh );

		if( gSettings.RomDirectories.empty() )
		{
			for( u32 i = 0; i < ARRAYSIZE( gRomsDirectories ); ++i )
			{
				gSettings.RomDirectories.push_back( gRomsDirectories[ i ] );
			}
		}
	}

	bool LessFilename( const std::string & a, const std::string & b )
	{
		const char * na = IO::Path::FindFileName( a.c_str() );
		const char * nb = IO::Path::FindFileName( b.c_str() );
		int c = strcasecmp( na, nb );
		return c != 0 ? c < 0 : a < b;
	}

	std::vector< std::string > FindRoms()
	{
		std::vector< std::string > roms;
		for( const std::string & dir : gSettings.RomDirectories )
		{
			IO::FindHandleT		find_handle;
			IO::FindDataT		find_data;
			if( IO::FindFileOpen( dir.c_str(), &find_handle, find_data ) )
			{
				do
				{
					if( IsRomfilename( find_data.Name ) )
					{
						roms.push_back( dir + find_data.Name );
					}
				}
				while( IO::FindFileNext( find_handle, find_data ) );
				IO::FindFileClose( find_handle );
			}
		}
		std::sort( roms.begin(), roms.end(), LessFilename );
		return roms;
	}

	struct SProgress
	{
		u32			Next = 0;			// Index of the next ROM to run
		s32			Running = -1;		// ROM that was running (if the PSP crashed or was turned off during it)
		std::string	RunningFile;
		std::string	RunningName;
	};

	SProgress ReadProgress()
	{
		SProgress progress;
		FILE * fh = fopen( kProgressFile, "r" );
		if( fh == nullptr )
		{
			return progress;
		}
		char line[ 512 ];
		while( fgets( line, sizeof( line ), fh ) != nullptr )
		{
			char * end = line + strlen( line );
			while( end > line && ( end[ -1 ] == '\n' || end[ -1 ] == '\r' ) ) *--end = '\0';
			if( strncmp( line, "next=", 5 ) == 0 )			progress.Next = atoi( line + 5 );
			else if( strncmp( line, "running=", 8 ) == 0 )	progress.Running = atoi( line + 8 );
			else if( strncmp( line, "file=", 5 ) == 0 )		progress.RunningFile = line + 5;
			else if( strncmp( line, "name=", 5 ) == 0 )		progress.RunningName = line + 5;
		}
		fclose( fh );
		return progress;
	}

	void WriteProgress( u32 next, s32 running, const std::string & file, const std::string & name )
	{
		FILE * fh = fopen( kProgressFile, "w" );
		if( fh == nullptr )
		{
			return;
		}
		fprintf( fh, "next=%u\nrunning=%d\nfile=%s\nname=%s\n", (unsigned)next, (int)running, file.c_str(), name.c_str() );
		fclose( fh );
	}

	void AppendResult( const char * line )
	{
		FILE * fh = fopen( kResultsFile, "a" );
		if( fh == nullptr )
		{
			return;
		}
		fputs( line, fh );
		fclose( fh );
	}

	void WriteResultsHeader( u32 num_roms )
	{
		char line[ 512 ];
		snprintf( line, sizeof( line ),
			"# DaedalusX64 benchmark: %u ROMs, %u s each, Start/A pressed for the first %u s\n"
			"# Figures are measured after the first %u s (or over the whole run if it stopped earlier).\n"
			"# fps = average frames per second; slowest / fastest = 10 second stretches; speed = N64 speed (100%% = full speed);\n"
			"# idle = spare PSP time; cpu / gfx = share of PSP time. Details per second are in perf.txt.\n"
			"# result       | fps  | slowest | fastest | speed | idle | cpu | gfx | time  | game | file\n",
			(unsigned)num_roms, (unsigned)gSettings.Seconds, (unsigned)gSettings.ButtonSeconds, (unsigned)gSettings.ButtonSeconds );
		AppendResult( line );
	}

	const char * FileNameOf( const std::string & path )
	{
		return IO::Path::FindFileName( path.c_str() );
	}

	void WriteResult( const char * result, const std::string & name, const std::string & file )
	{
		const bool steady = gRun.SteadyUs > 0;
		const f32 seconds = f32( steady ? gRun.SteadyUs : gRun.AllUs ) / 1000000.0f;
		const u32 frames = steady ? gRun.SteadyFrames : gRun.AllFrames;
		const u32 vis = steady ? gRun.SteadyVis : gRun.AllVis;
		const u32 tv_hz = FramerateLimiter_GetTvFrequencyHz() ? FramerateLimiter_GetTvFrequencyHz() : 60;
		const u32 run_seconds = gRun.StartUs ? u32( ( sceKernelGetSystemTimeWide() - gRun.StartUs ) / 1000000 ) : 0;

		char fps[ 16 ] = "-", slowest[ 16 ] = "-", fastest[ 16 ] = "-", speed[ 16 ] = "-";
		char idle[ 16 ] = "-", cpu[ 16 ] = "-", gfx[ 16 ] = "-";
		if( seconds > 0.5f )
		{
			snprintf( fps, sizeof( fps ), "%.1f", frames / seconds );
			snprintf( speed, sizeof( speed ), "%u%%", (unsigned)( vis * 100.0f / ( seconds * tv_hz ) + 0.5f ) );
		}
		if( gRun.NumWindows > 0 )
		{
			snprintf( slowest, sizeof( slowest ), "%.1f", gRun.MinWindowFps );
			snprintf( fastest, sizeof( fastest ), "%.1f", gRun.MaxWindowFps );
		}
		if( gRun.SteadyPercentSamples > 0 )
		{
			snprintf( idle, sizeof( idle ), "%u%%", (unsigned)( gRun.SteadyIdle / gRun.SteadyPercentSamples ) );
			snprintf( cpu, sizeof( cpu ), "%u%%", (unsigned)( gRun.SteadyCpu / gRun.SteadyPercentSamples ) );
			snprintf( gfx, sizeof( gfx ), "%u%%", (unsigned)( gRun.SteadyGfx / gRun.SteadyPercentSamples ) );
		}

		char line[ 768 ];
		snprintf( line, sizeof( line ), "%-14s | %-4s | %-7s | %-7s | %-5s | %-4s | %-3s | %-3s | %4us | %s | %s\n",
			result, fps, slowest, fastest, speed, idle, cpu, gfx, (unsigned)run_seconds,
			name.empty() ? "?" : name.c_str(), FileNameOf( file ) );
		AppendResult( line );
	}

	void Relaunch()
	{
		gRunning = false;
		if( !gEbootPath.empty() )
		{
			struct SceKernelLoadExecParam param;
			memset( &param, 0, sizeof( param ) );
			param.size = sizeof( param );
			param.args = gEbootPath.size() + 1;
			param.argp = (void *)gEbootPath.c_str();
			sceKernelLoadExec( gEbootPath.c_str(), &param );
		}
		// Not allowed (or failed): back to the XMB. Starting the benchmark again carries on.
		sceKernelExitGame();
	}

	// A ROM that hangs the emulator itself (no N64 vertical interrupts any more) can't be
	// stopped from the emulator thread: record it and restart.
	int StuckWatchdog( SceSize, void * )
	{
		while( gRunning )
		{
			sceKernelDelayThread( 5 * 1000 * 1000 );
			if( !gRunning || gRun.StartUs == 0 )
			{
				continue;
			}
			const u64 now = sceKernelGetSystemTimeWide();
			const u64 limit_us = u64( gSettings.Seconds + gSettings.NoPictureSeconds + 60 ) * 1000000;
			if( gRun.Outcome == OUTCOME_RUNNING && now - gRun.StartUs > limit_us )
			{
				WriteResult( "STUCK", gRomName, gRomFile );
				WriteProgress( gRomIndex + 1, -1, "", "" );
				Relaunch();
			}
		}
		return 0;
	}

	void ShowMessage( const char * message )
	{
		pspDebugScreenInit();
		pspDebugScreenSetXY( 0, 2 );
		pspDebugScreenPrintf( "%s", message );
		sceKernelDelayThread( 10 * 1000 * 1000 );
	}
}

bool Benchmark_IsRequested()
{
	return IO::File::Exists( kSettingsFile );
}

bool Benchmark_IsRunning()
{
	return gRunning;
}

void Benchmark_OnCrash()
{
	if( !gRunning )
	{
		return;
	}
	WriteResult( "CRASHED", gRomName, gRomFile );
	WriteProgress( gRomIndex + 1, -1, "", "" );
	Relaunch();
}

void Benchmark_Run( const char * eboot_path )
{
	gEbootPath = eboot_path ? eboot_path : "";
	ReadSettings();

	const std::vector< std::string > roms = FindRoms();
	SProgress progress = ReadProgress();

	if( progress.Next == 0 && progress.Running < 0 )
	{
		WriteResultsHeader( roms.size() );
	}

	// The PSP was turned off or crashed without the crash screen while a ROM was running
	if( progress.Running >= 0 )
	{
		memset( &gRun, 0, sizeof( gRun ) );
		WriteResult( "CRASHED/OFF", progress.RunningName, progress.RunningFile );
		progress.Next = progress.Running + 1;
		WriteProgress( progress.Next, -1, "", "" );
	}

	if( progress.Next >= roms.size() )
	{
		char message[ 256 ];
		snprintf( message, sizeof( message ),
			"Benchmark finished: all %u ROMs have been run.\n\n"
			"Results are in benchmark.txt and perf.txt.\n"
			"Delete benchmark_progress.txt to run it again.", (unsigned)roms.size() );
		ShowMessage( message );
		sceKernelExitGame();
		return;
	}

	gRunning = true;
	SceUID watchdog = sceKernelCreateThread( "BenchmarkWatchdog", StuckWatchdog, 0x11, 0x4000, PSP_THREAD_ATTR_USER, nullptr );
	if( watchdog >= 0 )
	{
		sceKernelStartThread( watchdog, 0, nullptr );
	}

	PerfStats_SetDynarecReport( false );		// One report every 10 seconds for hours of play is far too much

	for( u32 i = progress.Next; i < roms.size(); ++i )
	{
		gRomIndex = i;
		gRomFile = roms[ i ];
		gRomName.clear();
		gRun.StartUs = 0;
		WriteProgress( i, i, gRomFile, "" );

		gGlobalPreferences.DisplayFramerate = 3;		// FPS + Timing
		if( !System_Open( gRomFile.c_str() ) )
		{
			memset( &gRun, 0, sizeof( gRun ) );
			WriteResult( "LOAD FAILED", "", gRomFile );
			WriteProgress( i + 1, -1, "", "" );
			continue;
		}

		gRomName = g_ROM.settings.GameName.c_str();
		if( gRomName.empty() || gRomName == "?" )
		{
			gRomName = "(not in roms.ini)";
		}
		WriteProgress( i, i, gRomFile, gRomName );
		PerfStats_NewLogSection( FileNameOf( gRomFile ) );

		ResetRun();
		CPU_RegisterVblCallback( BenchmarkVbl, nullptr );
		CPU_Run();
		CPU_UnregisterVblCallback( BenchmarkVbl, nullptr );
		gBenchmarkButtons = 0;
		System_Close();

		const char * result = "OK";
		char froze[ 32 ];
		switch( gRun.Outcome )
		{
		case OUTCOME_FROZE:
			snprintf( froze, sizeof( froze ), "FROZE at %us", (unsigned)gRun.StopSecond );
			result = froze;
			break;
		case OUTCOME_NO_PICTURE:	result = "NO PICTURE"; break;
		case OUTCOME_OK:			result = "OK"; break;
		default:					result = "STOPPED"; break;		// Left with the pause menu
		}
		WriteResult( result, gRomName, gRomFile );
		gRun.StartUs = 0;
		WriteProgress( i + 1, -1, "", "" );
	}

	gRunning = false;
	PerfStats_Flush();

	char message[ 256 ];
	snprintf( message, sizeof( message ),
		"Benchmark finished: all %u ROMs have been run.\n\n"
		"Results are in benchmark.txt and perf.txt.", (unsigned)roms.size() );
	ShowMessage( message );
	sceKernelExitGame();
}
