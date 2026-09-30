/*
Copyright (C) 2007 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/


#include "Base/Types.h"

#include "Debug/DBGConsole.h"

#include "HLEGraphics/BaseRenderer.h"
#include "HLEGraphics/TextureCache.h"
#include "HLEGraphics/DLParser.h"
#include "HLEGraphics/DisplayListDebugger.h"

#include "Graphics/GraphicsContext.h"
#include "HLEGraphics/GraphicsPlugin.h"

#include "Utility/Profiler.h"
#include "Utility/FramerateLimiter.h"
#include "Interface/Preferences.h"
#include "System/Timing/Timing.h"

#include <pspdebug.h>

#include "Core/Memory.h"
#include "SysPSP/Utility/PerfStats.h"


extern void battery_warning();
extern void HandleEndOfFrame();

extern bool gFrameskipActive;

u32		gSoundSync =  44100;
u32		gVISyncRate = 1500;
bool	gTakeScreenshot = false;
bool	gTakeScreenshotSS = false;

EFrameskipValue		gFrameskipValue = FV_DISABLED;

namespace
{
	//u32					gVblCount = 0;
	u32					gFlipCount = 0;
	//float				gCurrentVblrate = 0.0f;
	float				gCurrentFramerate = 0.0f;
	u64					gLastFramerateCalcTime = 0;
	u64					gTicksPerSecond = 0;

#ifdef DAEDALUS_FRAMERATE_ANALYSIS
	u32					gTotalFrames = 0;
	u64					gFirstFrameTime = 0;
	FILE *				gFramerateFile = nullptr;
#endif

static void	UpdateFramerate()
{
#ifdef DAEDALUS_FRAMERATE_ANALYSIS
	gTotalFrames++;
#endif
	gFlipCount++;

	u64			now = 0;
	NTiming::GetPreciseTime( &now );

	if(gLastFramerateCalcTime == 0)
	{
		u64		freq = 0;
		gLastFramerateCalcTime = now;

		NTiming::GetPreciseFrequency( &freq );
		gTicksPerSecond = freq;
	}

#ifdef DAEDALUS_FRAMERATE_ANALYSIS
	if( gFramerateFile == nullptr )
	{
		gFirstFrameTime = now;
		gFramerateFile = fopen( "framerate.csv", "w" );
	}
	fprintf( gFramerateFile, "%d,%f\n", gTotalFrames, f32(now - gFirstFrameTime) / f32(gTicksPerSecond) );
#endif

	// If 1 second has elapsed since last recalculation, do it now
	u64		ticks_since_recalc( now - gLastFramerateCalcTime );
	if(ticks_since_recalc > gTicksPerSecond)
	{
		//gCurrentVblrate = float( gVblCount * gTicksPerSecond ) / float( ticks_since_recalc );
		gCurrentFramerate = float( gFlipCount * gTicksPerSecond ) / float( ticks_since_recalc );

		//gVblCount = 0;
		gFlipCount = 0;
		gLastFramerateCalcTime = now;

#ifdef DAEDALUS_FRAMERATE_ANALYSIS
		if( gFramerateFile != nullptr )
		{
			fflush( gFramerateFile );
		}
#endif
	}

}
}

class CGraphicsPluginImpl : public CGraphicsPlugin
{
	public:
	virtual	~CGraphicsPluginImpl();

		virtual 	bool		Initialise();
		virtual bool		StartEmulation()		{ return true; }
		virtual void		ViStatusChanged()		{}
		virtual void		ViWidthChanged()		{}
		virtual void		ProcessDList();

		virtual void		UpdateScreen();

		virtual void		RomClosed();
};

CGraphicsPluginImpl::~CGraphicsPluginImpl()
{
}


CGraphicsPlugin::~CGraphicsPlugin()
{
}


bool CGraphicsPluginImpl::Initialise()
{
	if(!CreateRenderer())
	{
		return false;
	}

	if(!CTextureCache::Create())
	{
		return false;
	}

	Watchdog_Start();

	if (!DLParser_Initialise())
	{
		return false;
	}

	return true;
}

void CGraphicsPluginImpl::ProcessDList()
{
#ifdef DAEDALUS_DEBUG_DISPLAYLIST
	if (!DLDebugger_Process())
	{
		DLParser_Process();
	}
#else
	DLParser_Process();
#endif
}

#ifdef DAEDALUS_DEBUG_DISPLAYLIST
extern u32 gNumInstructionsExecuted;
extern u32 gNumDListsCulled;
extern u32 gNumRectsClipped;
#endif

void CGraphicsPluginImpl::UpdateScreen()
{
	//gVblCount++;

	static u32		last_origin = 0;
	u32 current_origin = Memory_VI_GetRegister(VI_ORIGIN_REG);
	static bool Old_FrameskipActive = false;
	static bool Older_FrameskipActive =false;

	if( current_origin != last_origin )
	{
		Watchdog_NoteFrame();
		//printf( "Flip (%08x, %08x)\n", current_origin, last_origin );
		PerfStats_SetEnabled( gGlobalPreferences.DisplayFramerate );
		if( gGlobalPreferences.DisplayFramerate )
		{
			UpdateFramerate();
			if( PerfStats_Update() )
			{
				const u32 tv_hz = FramerateLimiter_GetTvFrequencyHz();
				PerfStats_LogSample( gCurrentFramerate, u32( FramerateLimiter_GetSync() * f32( tv_hz ) ), tv_hz );
			}
		}

		const f32 Fsync = FramerateLimiter_GetSync();

		//Calc sync rates for audio and game speed //Corn
		const f32 inv_Fsync = 1.0f / Fsync;
		gSoundSync = (u32)(44100.0f * inv_Fsync);
		gVISyncRate = (u32)(1500.0f * inv_Fsync);
		if( gVISyncRate > 4000 ) gVISyncRate = 4000;
		else if ( gVISyncRate < 1500 ) gVISyncRate = 1500;

		if(!gFrameskipActive)
		{
			if( gGlobalPreferences.DisplayFramerate )
			{
				pspDebugScreenSetTextColor( 0xffffffff );
				pspDebugScreenSetBackColor(0);
				pspDebugScreenSetXY(0, 0);

#ifdef DAEDALUS_DEBUG_DISPLAYLIST
				pspDebugScreenPrintf( "Dlist[%d] Cull[%d] | Tris[%d] Cull[%d] | Rect[%d] Clip[%d] ", gNumInstructionsExecuted, gNumDListsCulled, gRenderer->GetNumTrisRendered(), gRenderer->GetNumTrisClipped(), gRenderer->GetNumRect(), gNumRectsClipped);
#else
				pspDebugScreenPrintf( "FPS[%#.1f] VB[%d/%d] Sync[%#.1f%%]   ", gCurrentFramerate, u32( Fsync * f32( FramerateLimiter_GetTvFrequencyHz() ) ), FramerateLimiter_GetTvFrequencyHz(), Fsync * 100.0f );
				pspDebugScreenSetXY(0, 1);
				pspDebugScreenPrintf( "CPU %2d%% GFX %2d%% AUD %2d%% GE %2d%% IDLE %2d%%   ",
					(int)PerfStats_GetCpuPercent(), (int)PerfStats_GetGfxPercent(), (int)PerfStats_GetPercent( PERF_AUDIO ),
					(int)PerfStats_GetPercent( PERF_GE_WAIT ), (int)PerfStats_GetPercent( PERF_LIMITER ) );
				pspDebugScreenSetXY(0, 2);
				pspDebugScreenPrintf( "CPU: INT %2d%% DYN %2d%% JIT %2d%% OTH %2d%% TR %d/%d/%d/%d   ",
					(int)PerfStats_GetPercent( PERF_CPU_INTERP ), (int)PerfStats_GetPercent( PERF_CPU_DYNAREC ),
					(int)PerfStats_GetPercent( PERF_CPU_COMPILE ), (int)PerfStats_GetPercent( PERF_CPU ),
					(int)PerfStats_GetCount( PERF_COUNT_TRACE_START ), (int)PerfStats_GetCount( PERF_COUNT_TRACE_ABORT ),
					(int)PerfStats_GetCount( PERF_COUNT_TRACE_SALVAGED ), (int)PerfStats_GetCount( PERF_COUNT_FRAGMENT ) );
				pspDebugScreenSetXY(0, 3);
				pspDebugScreenPrintf( "GFX: DL %2d%% VTX %2d%% TEX %2d%% DRAW %2d%%   ",
					(int)PerfStats_GetPercent( PERF_GFX ), (int)PerfStats_GetPercent( PERF_GFX_VTX ),
					(int)PerfStats_GetPercent( PERF_GFX_TEX ), (int)PerfStats_GetPercent( PERF_GFX_DRAW ) );
#endif
			}
			if( gGlobalPreferences.BatteryWarning )
			{
				battery_warning();
			}
			if(gTakeScreenshot)
			{
				CGraphicsContext::Get()->DumpNextScreen();
				gTakeScreenshot = false;
			}

			CGraphicsContext::Get()->UpdateFrame( false );
			HandleEndOfFrame();
		}

		static u32 current_frame = 0;
		current_frame++;


		Older_FrameskipActive = Old_FrameskipActive;
		Old_FrameskipActive = gFrameskipActive;

		switch(gFrameskipValue)
		{
		case FV_DISABLED:
			gFrameskipActive = false;
			break;
		case FV_AUTO1:
			if(!Old_FrameskipActive && (Fsync < 0.965f)) gFrameskipActive = true;
			else gFrameskipActive = false;
			break;
		case FV_AUTO2:
			if((!Old_FrameskipActive | !Older_FrameskipActive) && (Fsync < 0.965f)) gFrameskipActive = true;
			else gFrameskipActive = false;
			break;
		default:
			gFrameskipActive = (current_frame % (gFrameskipValue - 1)) != 0;
			break;
		}

		last_origin = current_origin;
	}
}

void CGraphicsPluginImpl::RomClosed()
{
	#ifdef DAEDALUS_DEBUG_CONSOLE
	DBGConsole_Msg(0, "Finalising PSPGraphics");
	#endif
	Watchdog_Stop();
	PerfStats_Flush();
	DLParser_Finalise();
	CTextureCache::Destroy();
	DestroyRenderer();
}

class std::unique_ptr<CGraphicsPlugin>	CreateGraphicsPlugin()
{
	DBGConsole_Msg( 0, "Initialising PSP Graphics Plugin" );
	auto plugin = std::make_unique<CGraphicsPluginImpl>();
	if (!plugin->Initialise())
	{
		plugin = nullptr;
	}

	return plugin;
}