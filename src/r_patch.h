// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2018-2019 by Jaime "Lactozilla" Passos.
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_patch.h
/// \brief patch handling for truecolor

#include "doomdef.h"
#include "r_data.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_setup.h"
#include "v_video.h"

boolean R_CheckIfPatch(lumpnum_t lump);
void R_PatchToFlat(patch_t *patch, UINT32 *flat);

#ifndef NO_PNG_LUMPS
boolean R_IsLumpPNG(UINT8 *d, size_t s);
UINT32 *R_PNGToFlat(levelflat_t *levelflat, UINT8 *png, size_t size);
patch_t *R_PNGToPatch(UINT8 *png, size_t size);
boolean R_PNGDimensions(UINT8 *png, INT16 *width, INT16 *height, size_t size);
#endif // NO_PNG_LUMPS

void R_TextureToFlat(size_t tex, UINT32 *flat);
UINT32 *R_TruecolorMakeFlat(lumpnum_t lump);
