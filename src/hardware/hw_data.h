// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2019 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief defines structures and exports for the standard GPU driver

#ifndef _HWR_DATA_
#define _HWR_DATA_

#if defined (_WIN32) && !defined (__CYGWIN__) && !defined (_XBOX)
//#define WIN32_LEAN_AND_MEAN
#define RPC_NO_WINDOWS_H
#include <windows.h>
#endif

#include "../doomdef.h"
//THIS MUST DISAPPEAR!!!
#include "hw_glide.h"
#include "../screen.h"


// ==========================================================================
//                                                               TEXTURE INFO
// ==========================================================================

// grInfo.data holds the address of the graphics data cached in heap memory
//                NULL if the texture is not in Doom heap cache.
struct GLMipmap_s
{
	GrTexInfo 				grInfo;
	FxU32 					flags;
	UINT16 					width, height;
	UINT32 					downloaded;		// tex_downloaded

	struct 	GLMipmap_s 		*nextmipmap;
	struct 	GLMipmap_s 		*nextcolormap;
	const 	UINT8 			*colormap;
};
typedef struct GLMipmap_s GLMipmap_t;

//
// Doom texture info, as cached for hardware rendering
//
struct GLTexture_s
{
	GLMipmap_t		mipmap;
	float			scaleX;             //used for scaling textures on walls
	float			scaleY;
};
typedef struct GLTexture_s GLTexture_t;

// a cached patch as converted to hardware format, holding the original patch_t
// header so that the existing code can retrieve ->width, ->height as usual
// This is returned by W_CachePatchNum()/W_CachePatchName(), when rendermode
// is 'render_opengl'. Else it returns the normal patch_t data.
struct GLPatch_s
{
	// the 4 first fields come right away from the original patch_t
	INT16               width;
	INT16               height;
	INT16               leftoffset;     // pixels to the left of origin
	INT16               topoffset;      // pixels below the origin
	//
	float               max_s,max_t;
	UINT16              wadnum;      // the software patch lump num for when the hardware patch
	UINT16              lumpnum;     // was flushed, and we need to re-create it
	GLMipmap_t          mipmap;
};
typedef struct GLPatch_s GLPatch_t;

#endif //_HWR_DATA_
