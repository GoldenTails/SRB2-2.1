// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  screen.c
/// \brief Handles multiple resolutions, 8bpp/16bpp(highcolor) modes

#include "doomdef.h"
#include "doomstat.h"
#include "screen.h"
#include "console.h"
#include "am_map.h"
#include "i_system.h"
#include "i_video.h"
#include "r_local.h"
#include "r_sky.h"
#include "m_argv.h"
#include "m_misc.h"
#include "v_video.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "z_zone.h"
#include "d_main.h"
#include "d_clisrv.h"
#include "f_finale.h"


#if defined (USEASM) && !defined (NORUSEASM)//&& (!defined (_MSC_VER) || (_MSC_VER <= 1200))
#define RUSEASM //MSC.NET can't patch itself
#endif

// ===============================
//   C drawer routines for 32bpp
// ===============================

void (*colfunc)(void); // current column drawer

void (*basecolfunc)(void); // standard column
void (*fuzzcolfunc)(void); // translucency
void (*shadowcolfunc)(void); // ?
void (*fogcolfunc)(void); // fog column
void (*blendcolfunc)(void); // color blending
void (*transcolfunc)(void); // colormapped
void (*transtransfunc)(void); // translucent colormapped
void (*twosmultipatchfunc)(void); // for cols with transparent pixels
void (*twosmultipatchtransfunc)(void); // for cols with transparent pixels AND translucency

void (*spanfunc)(void); // current span drawer
void (*basespanfunc)(void); // standard span

void (*splatfunc)(void); // span drawer w/ transparent pixels
void (*transspanfunc)(void); // translucent span
void (*transsplatfunc)(void); // translucent splat (unused)
void (*fogspanfunc)(void); // fog span
void (*blendspanfunc)(void); // color blending

// Those are wall column renderers
// that ignore colormaps and use 32bpp patches.
void (*basecolfunc_ex)(void);
void (*fuzzcolfunc_ex)(void);

#ifndef NOWATER
void (*waterspanfunc)(void); // water
#endif

// tilted span drawers
#ifdef ESLOPE
void (*tiltedspanfunc)(void); // tilted span
void (*tiltedsplatfunc)(void); // tilted splat
void (*tiltedtransspanfunc)(void); // tilted translucent span
#endif

boolean translucency;

// ------------------
// global video state
// ------------------
viddef_t vid;
INT32 setmodeneeded; //video mode change needed if > 0 (the mode number to set + 1)

static CV_PossibleValue_t scr_depth_cons_t[] = {{8, "8 bits"}, {16, "16 bits"}, {24, "24 bits"}, {32, "32 bits"}, {0, NULL}};

//added : 03-02-98: default screen mode, as loaded/saved in config
#ifdef WII
consvar_t cv_scr_width = {"scr_width", "640", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_scr_height = {"scr_height", "480", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_scr_depth = {"scr_depth", "16 bits", CV_SAVE, scr_depth_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
#else
consvar_t cv_scr_width = {"scr_width", "1280", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_scr_height = {"scr_height", "800", CV_SAVE, CV_Unsigned, NULL, 0, NULL, NULL, 0, 0, NULL};
consvar_t cv_scr_depth = {"scr_depth", "16 bits", CV_SAVE, scr_depth_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};
#endif
consvar_t cv_renderview = {"renderview", "On", 0, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

static void SCR_ChangeFullscreen (void);

consvar_t cv_fullscreen = {"fullscreen", "Yes", CV_SAVE|CV_CALL, CV_YesNo, SCR_ChangeFullscreen, 0, NULL, NULL, 0, 0, NULL};

// =========================================================================

boolean R_ASM = true;
boolean R_486 = false;
boolean R_586 = false;
boolean R_MMX = false;
boolean R_SSE = false;
boolean R_3DNow = false;
boolean R_MMXExt = false;
boolean R_SSE2 = false;

void SCR_SetMode(void)
{
	if (dedicated)
		return;

	if (!setmodeneeded || WipeInAction)
		return; // should never happen and don't change it during a wipe, BAD!

	VID_SetMode(--setmodeneeded);

	V_SetPalette(0);

	//
	//  setup the right draw routines
	//

	// This is used by anything that isn't a wall column.
	colfunc = basecolfunc = R_DrawColumn_32;
	fuzzcolfunc = R_DrawTranslucentColumn_32;
	transcolfunc = R_DrawTranslatedColumn_32;
	transtransfunc = R_DrawTranslatedTranslucentColumn_32;

	// This is, though.
	basecolfunc_ex = R_DrawColumn_Ex32;
	fuzzcolfunc_ex = R_DrawTranslucentColumn_Ex32;
	shadowcolfunc = R_DrawColumnShadowed_32;
	twosmultipatchfunc = basecolfunc_ex;
	twosmultipatchtransfunc = fuzzcolfunc_ex;

	spanfunc = basespanfunc = R_DrawSpan_32;
	splatfunc = R_DrawSplat_32;
	transspanfunc = R_DrawTranslucentSpan_32;
	transsplatfunc = R_DrawTranslucentSplat_32;
#ifndef NOWATER
	waterspanfunc = R_DrawTranslucentWaterSpan_32;
#endif
#ifdef ESLOPE
	tiltedspanfunc = R_DrawTiltedSpan_32;
	tiltedsplatfunc = R_DrawTiltedSplat_32;
	tiltedtransspanfunc = R_DrawTiltedTranslucentSpan_32;
#endif

	fogcolfunc = R_DrawFogColumn_32;
	blendcolfunc = R_DrawBlendColumn_32;

	fogspanfunc = R_DrawFogSpan_32;
	blendspanfunc = R_DrawBlendSpan_32;

	setmodeneeded = 0;
}

// do some initial settings for the game loading screen
//
void SCR_Startup(void)
{
	const CPUInfoFlags *RCpuInfo = I_CPUInfo();
	if (!M_CheckParm("-NOCPUID") && RCpuInfo)
	{
#if defined (__i386__) || defined (_M_IX86) || defined (__WATCOMC__)
		R_486 = true;
#endif
		if (RCpuInfo->RDTSC)
			R_586 = true;
		if (RCpuInfo->MMX)
			R_MMX = true;
		if (RCpuInfo->AMD3DNow)
			R_3DNow = true;
		if (RCpuInfo->MMXExt)
			R_MMXExt = true;
		if (RCpuInfo->SSE)
			R_SSE = true;
		if (RCpuInfo->SSE2)
			R_SSE2 = true;
		CONS_Printf("CPU Info: 486: %i, 586: %i, MMX: %i, 3DNow: %i, MMXExt: %i, SSE2: %i\n", R_486, R_586, R_MMX, R_3DNow, R_MMXExt, R_SSE2);
	}

	if (M_CheckParm("-noASM"))
		R_ASM = false;
	if (M_CheckParm("-486"))
		R_486 = true;
	if (M_CheckParm("-586"))
		R_586 = true;
	if (M_CheckParm("-MMX"))
		R_MMX = true;
	if (M_CheckParm("-3DNow"))
		R_3DNow = true;
	if (M_CheckParm("-MMXExt"))
		R_MMXExt = true;

	if (M_CheckParm("-SSE"))
		R_SSE = true;
	if (M_CheckParm("-noSSE"))
		R_SSE = false;

	if (M_CheckParm("-SSE2"))
		R_SSE2 = true;

	M_SetupMemcpy();

	if (dedicated)
	{
		V_Init();
		V_SetPalette(0);
		return;
	}

	vid.modenum = 0;

	vid.dupx = vid.width / BASEVIDWIDTH;
	vid.dupy = vid.height / BASEVIDHEIGHT;
	vid.dupx = vid.dupy = (vid.dupx < vid.dupy ? vid.dupx : vid.dupy);
	vid.fdupx = FixedDiv(vid.width*FRACUNIT, BASEVIDWIDTH*FRACUNIT);
	vid.fdupy = FixedDiv(vid.height*FRACUNIT, BASEVIDHEIGHT*FRACUNIT);

#ifdef HWRENDER
	if (rendermode != render_opengl && rendermode != render_none) // This was just placing it incorrectly at non aspect correct resolutions in opengl
#endif
		vid.fdupx = vid.fdupy = (vid.fdupx < vid.fdupy ? vid.fdupx : vid.fdupy);

	vid.meddupx = (UINT8)(vid.dupx >> 1) + 1;
	vid.meddupy = (UINT8)(vid.dupy >> 1) + 1;
#ifdef HWRENDER
	vid.fmeddupx = vid.meddupx*FRACUNIT;
	vid.fmeddupy = vid.meddupy*FRACUNIT;
#endif

	vid.smalldupx = (UINT8)(vid.dupx / 3) + 1;
	vid.smalldupy = (UINT8)(vid.dupy / 3) + 1;
#ifdef HWRENDER
	vid.fsmalldupx = vid.smalldupx*FRACUNIT;
	vid.fsmalldupy = vid.smalldupy*FRACUNIT;
#endif

	vid.baseratio = FRACUNIT;

	V_Init();
	CV_RegisterVar(&cv_ticrate);
	CV_RegisterVar(&cv_constextsize);

	V_SetPalette(0);
}

// Called at new frame, if the video mode has changed
//
void SCR_Recalc(void)
{
	if (dedicated)
		return;

	// scale 1,2,3 times in x and y the patches for the menus and overlays...
	// calculated once and for all, used by routines in v_video.c
	vid.dupx = vid.width / BASEVIDWIDTH;
	vid.dupy = vid.height / BASEVIDHEIGHT;
	vid.dupx = vid.dupy = (vid.dupx < vid.dupy ? vid.dupx : vid.dupy);
	vid.fdupx = FixedDiv(vid.width*FRACUNIT, BASEVIDWIDTH*FRACUNIT);
	vid.fdupy = FixedDiv(vid.height*FRACUNIT, BASEVIDHEIGHT*FRACUNIT);

#ifdef HWRENDER
	//if (rendermode != render_opengl && rendermode != render_none) // This was just placing it incorrectly at non aspect correct resolutions in opengl
	// 13/11/18:
	// The above is no longer necessary, since we want OpenGL to be just like software now
	// -- Monster Iestyn
#endif
		vid.fdupx = vid.fdupy = (vid.fdupx < vid.fdupy ? vid.fdupx : vid.fdupy);

	//vid.baseratio = FixedDiv(vid.height << FRACBITS, BASEVIDHEIGHT << FRACBITS);
	vid.baseratio = FRACUNIT;

	vid.meddupx = (UINT8)(vid.dupx >> 1) + 1;
	vid.meddupy = (UINT8)(vid.dupy >> 1) + 1;
#ifdef HWRENDER
	vid.fmeddupx = vid.meddupx*FRACUNIT;
	vid.fmeddupy = vid.meddupy*FRACUNIT;
#endif

	vid.smalldupx = (UINT8)(vid.dupx / 3) + 1;
	vid.smalldupy = (UINT8)(vid.dupy / 3) + 1;
#ifdef HWRENDER
	vid.fsmalldupx = vid.smalldupx*FRACUNIT;
	vid.fsmalldupy = vid.smalldupy*FRACUNIT;
#endif

	// toggle off automap because some screensize-dependent values will
	// be calculated next time the automap is activated.
	if (automapactive)
		AM_Stop();

	// set the screen[x] ptrs on the new vidbuffers
	V_Init();

	// scr_viewsize doesn't change, neither detailLevel, but the pixels
	// per screenblock is different now, since we've changed resolution.
	R_SetViewSize(); //just set setsizeneeded true now ..

	// vid.recalc lasts only for the next refresh...
	con_recalc = true;
	am_recalc = true;
}

// Check for screen cmd-line parms: to force a resolution.
//
// Set the video mode to set at the 1st display loop (setmodeneeded)
//

void SCR_CheckDefaultMode(void)
{
	INT32 scr_forcex, scr_forcey; // resolution asked from the cmd-line

	if (dedicated)
		return;

	// 0 means not set at the cmd-line
	scr_forcex = scr_forcey = 0;

	if (M_CheckParm("-width") && M_IsNextParm())
		scr_forcex = atoi(M_GetNextParm());

	if (M_CheckParm("-height") && M_IsNextParm())
		scr_forcey = atoi(M_GetNextParm());

	if (scr_forcex && scr_forcey)
	{
		CONS_Printf(M_GetText("Using resolution: %d x %d\n"), scr_forcex, scr_forcey);
		// returns -1 if not found, thus will be 0 (no mode change) if not found
		setmodeneeded = VID_GetModeForSize(scr_forcex, scr_forcey) + 1;
	}
	else
	{
		CONS_Printf(M_GetText("Default resolution: %d x %d (%d bits)\n"), cv_scr_width.value,
			cv_scr_height.value, cv_scr_depth.value);
		// see note above
		setmodeneeded = VID_GetModeForSize(cv_scr_width.value, cv_scr_height.value) + 1;
	}

	// no video mode changes will crash the game
	if (!setmodeneeded)
		setmodeneeded = 1;
}

// sets the modenum as the new default video mode to be saved in the config file
void SCR_SetDefaultMode(void)
{
	// remember the default screen size
	CV_SetValue(&cv_scr_width, vid.width);
	CV_SetValue(&cv_scr_height, vid.height);
	CV_SetValue(&cv_scr_depth, vid.bpp*8);
}

// Change fullscreen on/off according to cv_fullscreen
void SCR_ChangeFullscreen(void)
{
#ifdef DIRECTFULLSCREEN
	// allow_fullscreen is set by VID_PrepareModeList
	// it is used to prevent switching to fullscreen during startup
	if (!allow_fullscreen)
		return;

	if (graphics_started)
	{
		VID_PrepareModeList();
		setmodeneeded = VID_GetModeForSize(vid.width, vid.height) + 1;
	}
	return;
#endif
}

boolean SCR_IsAspectCorrect(INT32 width, INT32 height)
{
	return
	 (  width % BASEVIDWIDTH == 0
	 && height % BASEVIDHEIGHT == 0
	 && width / BASEVIDWIDTH == height / BASEVIDHEIGHT
	 );
}

// XMOD FPS display
// moved out of os-specific code for consistency
static boolean fpsgraph[TICRATE];
static tic_t lasttic;

void SCR_DisplayTicRate(void)
{
	tic_t i;
	tic_t ontic = I_GetTime();
	tic_t totaltics = 0;
	INT32 ticcntcolor = 0;

	for (i = lasttic + 1; i < TICRATE+lasttic && i < ontic; ++i)
		fpsgraph[i % TICRATE] = false;

	fpsgraph[ontic % TICRATE] = true;

	for (i = 0;i < TICRATE;++i)
		if (fpsgraph[i])
			++totaltics;

	if (totaltics <= TICRATE/2) ticcntcolor = V_REDMAP;
	else if (totaltics == TICRATE) ticcntcolor = V_GREENMAP;

	V_DrawString(vid.width-(24*vid.dupx), vid.height-(16*vid.dupy),
		V_YELLOWMAP|V_NOSCALESTART, "FPS");
	V_DrawString(vid.width-(40*vid.dupx), vid.height-( 8*vid.dupy),
		ticcntcolor|V_NOSCALESTART, va("%02d/%02u", totaltics, TICRATE));

	lasttic = ontic;
}
