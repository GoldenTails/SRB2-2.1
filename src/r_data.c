// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_data.c
/// \brief Preparation of data for rendering,generation of lookups, caching, retrieval by name

#include "doomdef.h"
#include "g_game.h"
#include "i_video.h"
#include "r_local.h"
#include "r_sky.h"
#include "st_stuff.h"
#include "p_local.h"
#include "m_misc.h"
#include "r_data.h"
#include "r_patch.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_setup.h" // levelflats
#include "v_video.h" // pLocalPalette
#include "dehacked.h"

#if defined (_WIN32) || defined (_WIN32_WCE)
#include <malloc.h> // alloca(sizeof)
#endif

#if defined(_MSC_VER)
#pragma pack(1)
#endif

// Not sure if this is necessary, but it was in w_wad.c, so I'm putting it here too -Shadow Hog
#ifdef _WIN32_WCE
#define AVOID_ERRNO
#else
#include <errno.h>
#endif

#if defined(_MSC_VER)
#pragma pack()
#endif

// Store lists of lumps for F_START/F_END etc.
typedef struct
{
	UINT16 wadfile;
	UINT16 firstlump;
	size_t numlumps;
} lumplist_t;

//
// Graphics.
// SRB2 graphics for walls and sprites
// is stored in vertical runs of opaque pixels (posts).
// A column is composed of zero or more posts,
// a patch or sprite is composed of zero or more columns.
//

size_t numspritelumps, max_spritelumps;

// textures
INT32 numtextures = 0; // total number of textures found,
// size of following tables

texture_t **textures = NULL;
textureflat_t *texflats = NULL;
static UINT32 **texturecolumnofs; // column offset lookup table for each texture
static UINT8 **texturecache; // graphics data for each generated full-size texture

INT32 *texturewidth;
fixed_t *textureheight; // needed for texture pegging
INT32 *texturetranslation;

// needed for pre rendering
sprcache_t *spritecachedinfo;

lighttable_t *colormaps;
lighttable32_t *truecolormaps;
size_t colormap_size;

// for debugging/info purposes
static size_t flatmemory, spritememory, texturememory;

// Painfully simple texture id caching to make maps load faster. :3
static struct {
	char name[9];
	INT32 id;
} *tidcache = NULL;
static INT32 tidcachelen = 0;

//
// TEXTURE_T CACHING
// When a texture is first needed, it counts the number of composite columns
//  required in the texture and allocates space for a column directory and
//  any new columns.
// The directory will simply point inside other patches if there is only one
//  patch in a given column, but any columns with multiple patches will have
//  new column_ts generated.
//

//
// R_DrawColumnInCache
// Clip and draw a column from a patch into a cached post.
//
static inline void R_DrawColumnInCache(column_t *patch, UINT32 *cache, texpatchoptions_t *options, INT32 originy, INT32 cacheheight, boolean truecolor)
{
	INT32 count, position;
	UINT8 *source;
	INT32 topdelta, prevdelta = -1;

	while (patch->topdelta != 0xff)
	{
		topdelta = patch->topdelta;
		if (topdelta <= prevdelta)
			topdelta += prevdelta;
		prevdelta = topdelta;
		source = (UINT8 *)patch + 3;
		count = patch->length;
		position = originy + topdelta;

		if (position < 0)
		{
			count += position;
			position = 0;
		}

		if (position + count > cacheheight)
			count = cacheheight - position;

		if (count > 0)
		{
			UINT32 *dest = (cache + position);
			if (options->flipy)
				dest += count;
			while (count)
			{
				if (truecolor)
				{
					UINT32 *s = (UINT32 *)source;
					UINT32 px = *s, bg = *dest;
					if (!bg)
					{
						int pxa = (px&0xFF000000)>>24;
						pxa = (((float)pxa/255.0f)*((float)options->alpha/255.0f))*255.0f;
						if (pxa < 0)
							pxa = 0;
						if (pxa > 255)
							pxa = 255;
						px = (((UINT8)pxa)<<24)|(px&0x00FFFFFF);
					}
					else
						px = V_BlendTrueColor(bg, px, options->alpha);
					if (options->hasblend)
					{
#ifdef TRUECOLOR_USETINT
						if (options->tint)
						{
							RGBA_t rgba;
							rgba.rgba = px;
							px = V_TintTrueColor(rgba, options->blend.rgba, 255);
						}
						else
#endif
							px = V_BlendTrueColor(px, options->blend.rgba, options->blend.s.alpha);
					}
					*dest = px;
					source += 4;
				}
				else
				{
					UINT8 pixel = *source;
					if (pixel != TRANSPARENTPIXEL)
					{
						UINT32 px = V_GetTrueColor(pixel), bg = *dest;
						if (!bg)
							px = (options->alpha<<24)|(px&0x00FFFFFF);
						else
							px = V_BlendTrueColor(bg, px, options->alpha);
						if (options->hasblend)
						{
#ifdef TRUECOLOR_USETINT
							if (options->tint)
							{
								RGBA_t rgba;
								rgba.rgba = px;
								px = V_TintTrueColor(rgba, options->blend.rgba, 255);
							}
							else
#endif
								px = V_BlendTrueColor(px, options->blend.rgba, options->blend.s.alpha);
						}
						*dest = px;
					}
					source++;
				}
				if (options->flipy)
					dest--;
				else
					dest++;
				count--;
			}
			//M_Memcpy(cache + position, source, count);
		}

		{
			size_t len = patch->length;
			if (truecolor)
				len *= 4;
			patch = (column_t *)((UINT8 *)patch + len + 4);
		}
	}
}

//
// R_GenerateTexture
//
// Allocate space for full size texture, either single patch or 'composite'
// Build the full textures from patches.
// The texture caching system is a little more hungry of memory, but has
// been simplified for the sake of highcolor, dynamic ligthing, & speed.
//
// This is not optimised, but it's supposed to be executed only once
// per level, when enough memory is available.
//
static UINT8 *R_GenerateTexture(size_t texnum)
{
	UINT8 *block;
	UINT8 *blocktex;
	texture_t *texture;
	texpatch_t *patch;
	patch_t *realpatch;
	int x, x1, x2, i;
	size_t blocksize;
	column_t *patchcol;
	UINT32 *colofs;

	UINT16 wadnum;
	lumpnum_t lumpnum;
	size_t lumplength;

	I_Assert(texnum <= (size_t)numtextures);
	texture = textures[texnum];
	I_Assert(texture != NULL);

	// allocate texture column offset lookup
	texture->holes = false;
	blocksize = (texture->width * 4) + (texture->width * texture->height);
	texturememory += blocksize;
	block = Z_Calloc((blocksize+1) * sizeof(block), PU_STATIC, &texturecache[texnum]);

	// Transparency hack
	// (changed from TRANSPARENTPIXEL to 0x00)
	memset(block, 0x00, (blocksize+1) * sizeof(block));

	// columns lookup table
	colofs = (UINT32 *)(void *)block;
	texturecolumnofs[texnum] = colofs;

	// texture data after the lookup table
	blocktex = block + (texture->width*4);

	// Composite the columns together.
	for (i = 0, patch = texture->patches; i < texture->patchcount; i++, patch++)
	{
		boolean ispng;
		wadnum = patch->wad;
		lumpnum = patch->lump;
		lumplength = W_LumpLengthPwad(wadnum, lumpnum);
		realpatch = W_CacheLumpNumPwad(wadnum, lumpnum, PU_CACHE);
		ispng = R_IsLumpPNG((UINT8 *)realpatch, lumplength);
		if (ispng)
			realpatch = R_PNGToPatch((UINT8 *)realpatch, lumplength);

		x1 = patch->originx;
		x2 = x1 + SHORT(realpatch->width);

		if (x1 < 0)
			x = 0;
		else
			x = x1;

		if (x2 > texture->width)
			x2 = texture->width;

		for (; x < x2; x++)
		{
			int px = x;
			patchcol = (column_t *)((UINT8 *)realpatch + LONG(realpatch->columnofs[x-x1]));

			// generate column offset lookup
			if (patch->options.flipx)
				px = (x2-px)-1;
			colofs[px] = LONG((x * texture->height) + (texture->width*4)) * sizeof(UINT32);
			R_DrawColumnInCache(patchcol, (UINT32 *)(block + LONG(colofs[px])), &patch->options, patch->originy, texture->height, ispng);
		}
	}

	// Now that the texture has been built in column cache, it is purgable from zone memory.
	Z_ChangeTag(block, PU_CACHE);
	return blocktex;
}

//
// R_GetTextureNum
//
// Returns the actual texture id that we should use.
// This can either be texnum, the current frame for texnum's anim (if animated),
// or 0 if not valid.
//
INT32 R_GetTextureNum(INT32 texnum)
{
	if (texnum < 0 || texnum >= numtextures)
		return 0;
	return texturetranslation[texnum];
}

//
// R_CheckTextureCache
//
// Use this if you need to make sure the texture is cached before R_GetColumn calls
// e.g.: midtextures and FOF walls
//
void R_CheckTextureCache(INT32 tex)
{
	if (!texturecache[tex])
		R_GenerateTexture(tex);
}

//
// R_GetColumn
//
UINT8 *R_GetColumn(fixed_t tex, INT32 col)
{
	UINT8 *data;
	INT32 width = texturewidth[tex];

	if (width & (width - 1))
		col = (UINT32)col % width;
	else
		col &= (width - 1);

	data = texturecache[tex];
	if (!data)
		data = R_GenerateTexture(tex);

	return data + LONG(texturecolumnofs[tex][col]);
}

UINT8 *R_GetFlat(lumpnum_t flatlumpnum)
{
	return W_CacheLumpNum(flatlumpnum, PU_CACHE);
}

//
// Empty the texture cache (used for load wad at runtime)
//
void R_FlushTextureCache(void)
{
	INT32 i;

	if (numtextures)
		for (i = 0; i < numtextures; i++)
			Z_Free(texturecache[i]);
}

// Need these prototypes for later; defining them here instead of r_data.h so they're "private"
int R_CountTexturesInTEXTURESLump(UINT16 wadNum, UINT16 lumpNum);
void R_ParseTEXTURESLump(UINT16 wadNum, UINT16 lumpNum, INT32 *index);

//
// R_LoadTextures
// Initializes the texture list with the textures from the world map.
//
#define TX_START "TX_START"
#define TX_END "TX_END"
void R_LoadTextures(void)
{
	INT32 i, w;
	UINT16 j;
	UINT16 texstart, texend, texturesLumpPos;
	patch_t *patchlump;
	texpatch_t *patch;
	texture_t *texture;

	// Free previous memory before numtextures change.
	if (numtextures)
	{
		for (i = 0; i < numtextures; i++)
		{
			Z_Free(textures[i]);
			Z_Free(texturecache[i]);
		}
		Z_Free(texturetranslation);
		Z_Free(textures);
		Z_Free(texflats);
	}

	// Load patches and textures.

	// Get the number of textures to check.
	// NOTE: Make SURE the system does not process
	// the markers.
	// This system will allocate memory for all duplicate/patched textures even if it never uses them,
	// but the alternative is to spend a ton of time checking and re-checking all previous entries just to skip any potentially patched textures.
	for (w = 0, numtextures = 0; w < numwadfiles; w++)
	{
		if (wadfiles[w]->type == RET_PK3)
		{
			texstart = W_CheckNumForFolderStartPK3("textures/", (UINT16)w, 0);
			texend = W_CheckNumForFolderEndPK3("textures/", (UINT16)w, texstart);
		}
		else
		{
			texstart = W_CheckNumForNamePwad(TX_START, (UINT16)w, 0) + 1;
			texend = W_CheckNumForNamePwad(TX_END, (UINT16)w, 0);
		}

		texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", (UINT16)w, 0);
		while (texturesLumpPos != INT16_MAX)
		{
			numtextures += R_CountTexturesInTEXTURESLump((UINT16)w, (UINT16)texturesLumpPos);
			texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", (UINT16)w, texturesLumpPos + 1);
		}

		// Add all the textures between TX_START and TX_END
		if (texstart != INT16_MAX && texend != INT16_MAX)
		{
			numtextures += (UINT32)(texend - texstart);
		}

		// If no textures found by this point, bomb out
		if (!numtextures && w == (numwadfiles - 1))
		{
			I_Error("No textures detected in any WADs!\n");
		}
	}

	// Allocate memory and initialize to 0 for all the textures we are initialising.
	// There are actually 5 buffers allocated in one for convenience.
	textures = Z_Calloc((numtextures * sizeof(void *)) * 5, PU_STATIC, NULL);
	texflats = Z_Calloc((numtextures * sizeof(*texflats)), PU_STATIC, NULL);

	// Allocate texture column offset table.
	texturecolumnofs = (void *)((UINT8 *)textures + (numtextures * sizeof(void *)));
	// Allocate texture referencing cache.
	texturecache     = (void *)((UINT8 *)textures + ((numtextures * sizeof(void *)) * 2));
	// Allocate texture width table.
	texturewidth     = (void *)((UINT8 *)textures + ((numtextures * sizeof(void *)) * 3));
	// Allocate texture height table.
	textureheight    = (void *)((UINT8 *)textures + ((numtextures * sizeof(void *)) * 4));
	// Create translation table for global animation.
	texturetranslation = Z_Malloc((numtextures + 1) * sizeof(*texturetranslation), PU_STATIC, NULL);

	for (i = 0; i < numtextures; i++)
		texturetranslation[i] = i;

	for (i = 0, w = 0; w < numwadfiles; w++)
	{
		// Get the lump numbers for the markers in the WAD, if they exist.
		if (wadfiles[w]->type == RET_PK3)
		{
			texstart = W_CheckNumForFolderStartPK3("textures/", (UINT16)w, 0);
			texend = W_CheckNumForFolderEndPK3("textures/", (UINT16)w, texstart);
			texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", (UINT16)w, 0);
			while (texturesLumpPos != INT16_MAX)
			{
				R_ParseTEXTURESLump(w, texturesLumpPos, &i);
				texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", (UINT16)w, texturesLumpPos + 1);
			}
		}
		else
		{
			texstart = W_CheckNumForNamePwad(TX_START, (UINT16)w, 0) + 1;
			texend = W_CheckNumForNamePwad(TX_END, (UINT16)w, 0);
			texturesLumpPos = W_CheckNumForNamePwad("TEXTURES", (UINT16)w, 0);
			if (texturesLumpPos != INT16_MAX)
				R_ParseTEXTURESLump(w, texturesLumpPos, &i);
		}

		if (texstart == INT16_MAX || texend == INT16_MAX)
			continue;

		// Work through each lump between the markers in the WAD.
		for (j = 0; j < (texend - texstart); i++, j++)
		{
			UINT16 wadnum = (UINT16)w;
			lumpnum_t lumpnum = texstart + j;
			size_t lumplength = W_LumpLengthPwad(wadnum, lumpnum);
			patchlump = W_CacheLumpNumPwad(wadnum, lumpnum, PU_CACHE);

			// Then, check the lump directly to see if it's a texture SOC,
			// and if it is, load it using dehacked instead.
			if (strstr((const char *)patchlump, "TEXTURE"))
			{
				CONS_Alert(CONS_WARNING, "%s is a Texture SOC.\n", W_CheckNameForNumPwad((UINT16)w,texstart+j));
				Z_Unlock(patchlump);
				DEH_LoadDehackedLumpPwad((UINT16)w, texstart + j);
			}
			else
			{
				//CONS_Printf("\n\"%s\" is a single patch, dimensions %d x %d",W_CheckNameForNumPwad((UINT16)w,texstart+j),patchlump->width, patchlump->height);
				texture = textures[i] = Z_Calloc(sizeof(texture_t) + sizeof(texpatch_t), PU_STATIC, NULL);

				// Set texture properties.
				M_Memcpy(texture->name, W_CheckNameForNumPwad(wadnum, lumpnum), sizeof(texture->name));
				if (R_IsLumpPNG((UINT8 *)patchlump, lumplength))
				{
					INT16 width, height;
					R_PNGDimensions((UINT8 *)patchlump, &width, &height, lumplength);
					texture->width = width;
					texture->height = height;
				}
				else
				{
					texture->width = SHORT(patchlump->width);
					texture->height = SHORT(patchlump->height);
				}
				texture->patchcount = 1;
				texture->holes = false;

				// Allocate information for the texture's patches.
				patch = &texture->patches[0];

				patch->originx = patch->originy = 0;
				patch->options.flipx = false;
				patch->options.flipy = false;
				patch->options.hasblend = false;
				patch->options.alpha = 255;
				patch->wad = (UINT16)w;
				patch->lump = texstart + j;

				Z_Unlock(patchlump);

				texturewidth[i] = texture->width;
				textureheight[i] = texture->height << FRACBITS;
			}
		}
	}
}

// copypasted from R_PrecacheLevel
void R_ReloadTexturesAndFlatsInLevel(void)
{
	char *texturepresent;
	size_t j;

	//
	// Precache textures.
	//
	// no need to precache all software textures in 3D mode
	// (note they are still used with the reference software view)
	texturepresent = calloc(numtextures, sizeof (*texturepresent));
	if (texturepresent == NULL) I_Error("%s: Out of memory looking up textures", "R_PrecacheLevel");

	for (j = 0; j < numsides; j++)
	{
		// huh, a potential bug here????
		if (sides[j].toptexture >= 0 && sides[j].toptexture < numtextures)
			texturepresent[sides[j].toptexture] = 1;
		if (sides[j].midtexture >= 0 && sides[j].midtexture < numtextures)
			texturepresent[sides[j].midtexture] = 1;
		if (sides[j].bottomtexture >= 0 && sides[j].bottomtexture < numtextures)
			texturepresent[sides[j].bottomtexture] = 1;
	}

	// Sky texture is always present.
	// Note that F_SKY1 is the name used to indicate a sky floor/ceiling as a flat,
	// while the sky texture is stored like a wall texture, with a skynum dependent name.
	texturepresent[skytexture] = 1;

	for (j = 0; j < (unsigned)numtextures; j++)
	{
		if (!texturepresent[j])
			continue;

		if (texturecache[j])
		{
			Z_Free(texturecache[j]);
			R_GenerateTexture(j);
		}
		// pre-caching individual patches that compose textures became obsolete,
		// since we cache entire composite textures
	}
	free(texturepresent);

	for (j = 0; j < numlevelflats; j++)
		levelflats[j].reload_flat = true;
}

static texpatch_t *R_ParsePatch(boolean actuallyLoadPatch)
{
	char *texturesToken;
	size_t texturesTokenLength;
	char *endPos;
	char *patchName = NULL;
	INT16 patchXPos;
	INT16 patchYPos;
	texpatch_t *resultPatch = NULL;
	lumpnum_t patchLumpNum;

	// Patch identifier
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch name should be");
	}
	texturesTokenLength = strlen(texturesToken);
	if (texturesTokenLength>8)
	{
		I_Error("Error parsing TEXTURES lump: Patch name \"%s\" exceeds 8 characters",texturesToken);
	}
	else
	{
		if (patchName != NULL)
		{
			Z_Free(patchName);
		}
		patchName = (char *)Z_Malloc((texturesTokenLength+1)*sizeof(char),PU_STATIC,NULL);
		M_Memcpy(patchName,texturesToken,texturesTokenLength*sizeof(char));
		patchName[texturesTokenLength] = '\0';
	}

	// Comma 1
	Z_Free(texturesToken);
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where comma after \"%s\"'s patch name should be",patchName);
	if (strcmp(texturesToken,",")!=0)
		I_Error("Error parsing TEXTURES lump: Expected \",\" after %s's patch name, got \"%s\"",patchName,texturesToken);

	// XPos
	Z_Free(texturesToken);
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch \"%s\"'s x coordinate should be",patchName);
	endPos = NULL;
#ifndef AVOID_ERRNO
	errno = 0;
#endif
	patchXPos = strtol(texturesToken,&endPos,10);
	if (endPos == texturesToken // Empty string
		|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
		|| errno == ERANGE // Number out-of-range
#endif
		)
	{
		I_Error("Error parsing TEXTURES lump: Expected an integer for patch \"%s\"'s x coordinate, got \"%s\"",patchName,texturesToken);
	}

	// Comma 2
	Z_Free(texturesToken);
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where comma after patch \"%s\"'s x coordinate should be",patchName);
	if (strcmp(texturesToken,",")!=0)
		I_Error("Error parsing TEXTURES lump: Expected \",\" after patch \"%s\"'s x coordinate, got \"%s\"",patchName,texturesToken);

	// YPos
	Z_Free(texturesToken);
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch \"%s\"'s y coordinate should be",patchName);
	endPos = NULL;
#ifndef AVOID_ERRNO
	errno = 0;
#endif
	patchYPos = strtol(texturesToken,&endPos,10);
	if (endPos == texturesToken // Empty string
		|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
		|| errno == ERANGE // Number out-of-range
#endif
		)
	{
		I_Error("Error parsing TEXTURES lump: Expected an integer for patch \"%s\"'s y coordinate, got \"%s\"",patchName,texturesToken);
	}
	Z_Free(texturesToken);

	if (actuallyLoadPatch)
	{
		// Check lump exists
		patchLumpNum = W_GetNumForName(patchName);
		// If so, allocate memory for texpatch_t and fill 'er up
		resultPatch = (texpatch_t *)Z_Malloc(sizeof(texpatch_t),PU_STATIC,NULL);
		resultPatch->originx = patchXPos;
		resultPatch->originy = patchYPos;
		resultPatch->options.flipx = false;
		resultPatch->options.flipy = false;
		resultPatch->options.hasblend = false;
		resultPatch->options.alpha = 255;
		resultPatch->lump = patchLumpNum & 65535;
		resultPatch->wad = patchLumpNum>>16;
	}

	// https://zdoom.org/wiki/TEXTURES
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where open curly brace for patch \"%s\" could be",patchName);
	if (strcmp(texturesToken,"{")==0)
	{
		Z_Free(texturesToken);
		texturesToken = M_GetToken(NULL);
		if (texturesToken == NULL)
			I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch options for patch \"%s\" should be",patchName);
		while (strcmp(texturesToken,"}")!=0)
		{
			if (stricmp(texturesToken, "Alpha")==0)
			{
				Z_Free(texturesToken);
				texturesToken = M_GetToken(NULL);
				if (texturesToken == NULL)
					I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch \"%s\"'s translucency value should be",patchName);
				endPos = NULL;
#ifndef AVOID_ERRNO
				errno = 0;
#endif
				if (actuallyLoadPatch)
				{
					resultPatch->options.alpha = (UINT8)(llrintf(strtof(texturesToken,&endPos)*255.0f));
					if (endPos == texturesToken // Empty string
						|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
						|| errno == ERANGE // Number out-of-range
#endif
						)
					{
						I_Error("Error parsing TEXTURES lump: Expected a float for patch \"%s\"'s translucency value, got \"%s\"",patchName,texturesToken);
					}
				}
				Z_Free(texturesToken);
			}
			else if (stricmp(texturesToken, "Blend")==0)
			{
				// Only supports
				// Blend <string color>[,<float alpha>]
				// ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				// this because I'm lazy.
				char *blendString = NULL;
				UINT8 blendAmount = 255;
				texturesToken = M_GetToken(NULL);
				if (texturesToken == NULL)
					I_Error("Error parsing TEXTURES lump: Unexpected end of file where blend string should be in patch \"%s\"",patchName);
				texturesTokenLength = strlen(texturesToken);
				if (texturesTokenLength != 7)
					I_Error("Error parsing TEXTURES lump: Blend string \"%s\" is not exactly 7 characters",texturesToken);
				else
				{
					blendString = (char *)Z_Malloc((texturesTokenLength+1)*sizeof(char),PU_STATIC,NULL);
					M_Memcpy(blendString,texturesToken,texturesTokenLength*sizeof(char));
					blendString[texturesTokenLength] = '\0';
				}

				// Comma 1
				Z_Free(texturesToken);
				texturesToken = M_GetToken(NULL);
				if (texturesToken == NULL)
					I_Error("Error parsing TEXTURES lump: Unexpected end of file where comma after \"%s\"'s blend string should be",patchName);

				// Blend amount
				if (strcmp(texturesToken,",")==0)
				{
					Z_Free(texturesToken);
					texturesToken = M_GetToken(NULL);
					if (texturesToken == NULL)
						I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch \"%s\"'s blend amount should be",patchName);
					endPos = NULL;
#ifndef AVOID_ERRNO
					errno = 0;
#endif
					blendAmount = (UINT8)(llrintf(strtof(texturesToken,&endPos)*255.0f));
					if (actuallyLoadPatch)
					{
						resultPatch->options.tint = false;
						if (endPos == texturesToken // Empty string
							|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
							|| errno == ERANGE // Number out-of-range
#endif
							)
						{
							I_Error("Error parsing TEXTURES lump: Expected a float for patch \"%s\"'s blend amount, got \"%s\"",patchName,texturesToken);
						}
					}
					Z_Free(texturesToken);
				}
				else
				{
					// Apparently omitting the alpha value
					// makes the patch have some kind of tint.
					M_UndoGetToken();
					if (actuallyLoadPatch)
						resultPatch->options.tint = true;
				}

#define HEX2INT(x) (UINT32)(x >= '0' && x <= '9' ? x - '0' : x >= 'a' && x <= 'f' ? x - 'a' + 10 : x >= 'A' && x <= 'F' ? x - 'A' + 10 : 0)
				if (actuallyLoadPatch)
				{
					if (blendString[0] == '#')
					{
						resultPatch->options.blend.s.red = ((HEX2INT(blendString[1]) * 16) + HEX2INT(blendString[2]));
						resultPatch->options.blend.s.green = ((HEX2INT(blendString[3]) * 16) + HEX2INT(blendString[4]));
						resultPatch->options.blend.s.blue = ((HEX2INT(blendString[5]) * 16) + HEX2INT(blendString[6]));
						resultPatch->options.blend.s.alpha = blendAmount;
						resultPatch->options.hasblend = true;
					}
					else
						I_Error("Error parsing TEXTURES lump: Expected # on the beginning of patch \"%s\"'s blend string, got \"%c\"",patchName,blendString[0]);
				}
#undef HEX2INT
			}
			else if (stricmp(texturesToken, "Style")==0)
			{
				// SRB2 shouldn't really care about this but we have to parse it anyway...
				Z_Free(texturesToken);
				texturesToken = M_GetToken(NULL);
				if (texturesToken == NULL)
					I_Error("Error parsing TEXTURES lump: Unexpected end of file (patch \"%s\")",patchName);
				Z_Free(texturesToken);
			}
			else if (stricmp(texturesToken, "FlipX")==0)
			{
				if (actuallyLoadPatch)
					resultPatch->options.flipx = true;
				Z_Free(texturesToken);
			}
			else if (stricmp(texturesToken, "FlipY")==0)
			{
				if (actuallyLoadPatch)
					resultPatch->options.flipy = true;
				Z_Free(texturesToken);
			}
			else
				I_Error("Error parsing TEXTURES lump: Unexpected token in \"%s\" patch \"%s\"",texturesToken,patchName);

			texturesToken = M_GetToken(NULL);
			if (texturesToken == NULL)
				I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch options or right curly brace for patch \"%s\" should be",patchName);
		}
	}
	else
		M_UndoGetToken();

	// Clean up a little after ourselves
	Z_Free(patchName);
	Z_Free(texturesToken);

	return resultPatch;
}

static texture_t *R_ParseTexture(boolean actuallyLoadTexture)
{
	char *texturesToken;
	size_t texturesTokenLength;
	char *endPos;
	INT32 newTextureWidth;
	INT32 newTextureHeight;
	texture_t *resultTexture = NULL;
	texpatch_t *newPatch;
	char newTextureName[9]; // no longer dynamically allocated

	// Texture name
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where texture name should be");
	}
	texturesTokenLength = strlen(texturesToken);
	if (texturesTokenLength>8)
	{
		I_Error("Error parsing TEXTURES lump: Texture name \"%s\" exceeds 8 characters",texturesToken);
	}
	else
	{
		memset(&newTextureName, 0, 9);
		M_Memcpy(newTextureName, texturesToken, texturesTokenLength);
		// ^^ we've confirmed that the token is <= 8 characters so it will never overflow a 9 byte char buffer
		strupr(newTextureName); // Just do this now so we don't have to worry about it
	}
	Z_Free(texturesToken);

	// Comma 1
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where comma after texture \"%s\"'s name should be",newTextureName);
	}
	else if (strcmp(texturesToken,",")!=0)
	{
		I_Error("Error parsing TEXTURES lump: Expected \",\" after texture \"%s\"'s name, got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	// Width
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where texture \"%s\"'s width should be",newTextureName);
	}
	endPos = NULL;
#ifndef AVOID_ERRNO
	errno = 0;
#endif
	newTextureWidth = strtol(texturesToken,&endPos,10);
	if (endPos == texturesToken // Empty string
		|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
		|| errno == ERANGE // Number out-of-range
#endif
		|| newTextureWidth < 0) // Number is not positive
	{
		I_Error("Error parsing TEXTURES lump: Expected a positive integer for texture \"%s\"'s width, got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	// Comma 2
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where comma after texture \"%s\"'s width should be",newTextureName);
	}
	if (strcmp(texturesToken,",")!=0)
	{
		I_Error("Error parsing TEXTURES lump: Expected \",\" after texture \"%s\"'s width, got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	// Height
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where texture \"%s\"'s height should be",newTextureName);
	}
	endPos = NULL;
#ifndef AVOID_ERRNO
	errno = 0;
#endif
	newTextureHeight = strtol(texturesToken,&endPos,10);
	if (endPos == texturesToken // Empty string
		|| *endPos != '\0' // Not end of string
#ifndef AVOID_ERRNO
		|| errno == ERANGE // Number out-of-range
#endif
		|| newTextureHeight < 0) // Number is not positive
	{
		I_Error("Error parsing TEXTURES lump: Expected a positive integer for texture \"%s\"'s height, got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	// Left Curly Brace
	texturesToken = M_GetToken(NULL);
	if (texturesToken == NULL)
	{
		I_Error("Error parsing TEXTURES lump: Unexpected end of file where open curly brace for texture \"%s\" should be",newTextureName);
	}
	if (strcmp(texturesToken,"{")==0)
	{
		if (actuallyLoadTexture)
		{
			// Allocate memory for a zero-patch texture. Obviously, we'll be adding patches momentarily.
			resultTexture = (texture_t *)Z_Calloc(sizeof(texture_t),PU_STATIC,NULL);
			M_Memcpy(resultTexture->name, newTextureName, 8);
			resultTexture->width = newTextureWidth;
			resultTexture->height = newTextureHeight;
		}
		Z_Free(texturesToken);
		texturesToken = M_GetToken(NULL);
		if (texturesToken == NULL)
		{
			I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch definition for texture \"%s\" should be",newTextureName);
		}
		while (strcmp(texturesToken,"}")!=0)
		{
			if (stricmp(texturesToken, "PATCH")==0)
			{
				Z_Free(texturesToken);
				if (resultTexture)
				{
					// Get that new patch
					newPatch = R_ParsePatch(true);
					// Make room for the new patch
					resultTexture = Z_Realloc(resultTexture, sizeof(texture_t) + (resultTexture->patchcount+1)*sizeof(texpatch_t), PU_STATIC, NULL);
					// Populate the uninitialized values in the new patch entry of our array
					M_Memcpy(&resultTexture->patches[resultTexture->patchcount], newPatch, sizeof(texpatch_t));
					// Account for the new number of patches in the texture
					resultTexture->patchcount++;
					// Then free up the memory assigned to R_ParsePatch, as it's unneeded now
					Z_Free(newPatch);
				}
				else
				{
					R_ParsePatch(false);
				}
			}
			else
			{
				I_Error("Error parsing TEXTURES lump: Expected \"PATCH\" in texture \"%s\", got \"%s\"",newTextureName,texturesToken);
			}

			texturesToken = M_GetToken(NULL);
			if (texturesToken == NULL)
			{
				I_Error("Error parsing TEXTURES lump: Unexpected end of file where patch declaration or right curly brace for texture \"%s\" should be",newTextureName);
			}
		}
		if (resultTexture && resultTexture->patchcount == 0)
		{
			I_Error("Error parsing TEXTURES lump: Texture \"%s\" must have at least one patch",newTextureName);
		}
	}
	else
	{
		I_Error("Error parsing TEXTURES lump: Expected \"{\" for texture \"%s\", got \"%s\"",newTextureName,texturesToken);
	}
	Z_Free(texturesToken);

	if (actuallyLoadTexture) return resultTexture;
	else return NULL;
}

// Parses the TEXTURES lump... but just to count the number of textures.
int R_CountTexturesInTEXTURESLump(UINT16 wadNum, UINT16 lumpNum)
{
	char *texturesLump;
	size_t texturesLumpLength;
	char *texturesText;
	UINT32 numTexturesInLump = 0;
	char *texturesToken;

	// Since lumps AREN'T \0-terminated like I'd assumed they should be, I'll
	// need to make a space of memory where I can ensure that it will terminate
	// correctly. Start by loading the relevant data from the WAD.
	texturesLump = (char *)W_CacheLumpNumPwad(wadNum, lumpNum, PU_STATIC);
	// If that didn't exist, we have nothing to do here.
	if (texturesLump == NULL) return 0;
	// If we're still here, then it DOES exist; figure out how long it is, and allot memory accordingly.
	texturesLumpLength = W_LumpLengthPwad(wadNum, lumpNum);
	texturesText = (char *)Z_Malloc((texturesLumpLength+1)*sizeof(char),PU_STATIC,NULL);
	// Now move the contents of the lump into this new location.
	memmove(texturesText,texturesLump,texturesLumpLength);
	// Make damn well sure the last character in our new memory location is \0.
	texturesText[texturesLumpLength] = '\0';
	// Finally, free up the memory from the first data load, because we really
	// don't need it.
	Z_Free(texturesLump);

	texturesToken = M_GetToken(texturesText);
	while (texturesToken != NULL)
	{
		if (stricmp(texturesToken, "WALLTEXTURE") == 0 || stricmp(texturesToken, "TEXTURE") == 0)
		{
			numTexturesInLump++;
			Z_Free(texturesToken);
			R_ParseTexture(false);
		}
		else
		{
			I_Error("Error parsing TEXTURES lump: Expected \"WALLTEXTURE\" or \"TEXTURE\", got \"%s\"",texturesToken);
		}
		texturesToken = M_GetToken(NULL);
	}
	Z_Free(texturesToken);
	Z_Free((void *)texturesText);

	return numTexturesInLump;
}

// Parses the TEXTURES lump... for real, this time.
void R_ParseTEXTURESLump(UINT16 wadNum, UINT16 lumpNum, INT32 *texindex)
{
	char *texturesLump;
	size_t texturesLumpLength;
	char *texturesText;
	char *texturesToken;
	texture_t *newTexture;

	I_Assert(texindex != NULL);

	// Since lumps AREN'T \0-terminated like I'd assumed they should be, I'll
	// need to make a space of memory where I can ensure that it will terminate
	// correctly. Start by loading the relevant data from the WAD.
	texturesLump = (char *)W_CacheLumpNumPwad(wadNum, lumpNum, PU_STATIC);
	// If that didn't exist, we have nothing to do here.
	if (texturesLump == NULL) return;
	// If we're still here, then it DOES exist; figure out how long it is, and allot memory accordingly.
	texturesLumpLength = W_LumpLengthPwad(wadNum, lumpNum);
	texturesText = (char *)Z_Malloc((texturesLumpLength+1)*sizeof(char),PU_STATIC,NULL);
	// Now move the contents of the lump into this new location.
	memmove(texturesText,texturesLump,texturesLumpLength);
	// Make damn well sure the last character in our new memory location is \0.
	texturesText[texturesLumpLength] = '\0';
	// Finally, free up the memory from the first data load, because we really
	// don't need it.
	Z_Free(texturesLump);

	texturesToken = M_GetToken(texturesText);
	while (texturesToken != NULL)
	{
		if (stricmp(texturesToken, "WALLTEXTURE") == 0 || stricmp(texturesToken, "TEXTURE") == 0)
		{
			Z_Free(texturesToken);
			// Get the new texture
			newTexture = R_ParseTexture(true);
			// Store the new texture
			textures[*texindex] = newTexture;
			texturewidth[*texindex] = newTexture->width;
			textureheight[*texindex] = newTexture->height << FRACBITS;
			// Increment i back in R_LoadTextures()
			(*texindex)++;
		}
		else
		{
			I_Error("Error parsing TEXTURES lump: Expected \"WALLTEXTURE\" or \"TEXTURE\", got \"%s\"",texturesToken);
		}
		texturesToken = M_GetToken(NULL);
	}
	Z_Free(texturesToken);
	Z_Free((void *)texturesText);
}

static inline lumpnum_t R_CheckNumForNameList(const char *name, lumplist_t *list, size_t listsize)
{
	size_t i;
	UINT16 lump;

	for (i = listsize - 1; i < INT16_MAX; i--)
	{
		lump = W_CheckNumForNamePwad(name, list[i].wadfile, list[i].firstlump);
		if (lump == INT16_MAX || lump > (list[i].firstlump + list[i].numlumps))
			continue;
		else
			return (list[i].wadfile<<16)+lump;
	}
	return LUMPERROR;
}

static lumplist_t *colormaplumps = NULL; ///\todo free leak
static size_t numcolormaplumps = 0;

static void R_InitExtraColormaps(void)
{
	lumpnum_t startnum, endnum;
	UINT16 cfile, clump;
	static size_t maxcolormaplumps = 16;

	for (cfile = clump = 0; cfile < numwadfiles; cfile++, clump = 0)
	{
		startnum = W_CheckNumForNamePwad("C_START", cfile, clump);
		if (startnum == INT16_MAX)
			continue;

		endnum = W_CheckNumForNamePwad("C_END", cfile, clump);

		if (endnum == INT16_MAX)
			I_Error("R_InitExtraColormaps: C_START without C_END\n");

		// This shouldn't be possible when you use the Pwad function, silly
		//if (WADFILENUM(startnum) != WADFILENUM(endnum))
			//I_Error("R_InitExtraColormaps: C_START and C_END in different wad files!\n");

		if (numcolormaplumps >= maxcolormaplumps)
			maxcolormaplumps *= 2;
		colormaplumps = Z_Realloc(colormaplumps,
			sizeof (*colormaplumps) * maxcolormaplumps, PU_STATIC, NULL);
		colormaplumps[numcolormaplumps].wadfile = cfile;
		colormaplumps[numcolormaplumps].firstlump = startnum+1;
		colormaplumps[numcolormaplumps].numlumps = endnum - (startnum + 1);
		numcolormaplumps++;
	}
	CONS_Printf(M_GetText("Number of Extra Colormaps: %s\n"), sizeu1(numcolormaplumps));
}

// Search for flat name.
lumpnum_t R_GetFlatNumForName(const char *name)
{
	INT32 i;
	lumpnum_t lump;
	lumpnum_t start;
	lumpnum_t end;

	// Scan wad files backwards so patched flats take preference.
	for (i = numwadfiles - 1; i >= 0; i--)
	{
		switch (wadfiles[i]->type)
		{
		case RET_WAD:
			if ((start = W_CheckNumForNamePwad("F_START", (UINT16)i, 0)) == INT16_MAX)
			{
				if ((start = W_CheckNumForNamePwad("FF_START", (UINT16)i, 0)) == INT16_MAX)
					continue;
				else if ((end = W_CheckNumForNamePwad("FF_END", (UINT16)i, start)) == INT16_MAX)
					continue;
			}
			else
				if ((end = W_CheckNumForNamePwad("F_END", (UINT16)i, start)) == INT16_MAX)
					continue;
			break;
		case RET_PK3:
			if ((start = W_CheckNumForFolderStartPK3("Flats/", i, 0)) == INT16_MAX)
				continue;
			if ((end = W_CheckNumForFolderEndPK3("Flats/", i, start)) == INT16_MAX)
				continue;
			break;
		default:
			continue;
		}

		// Now find lump with specified name in that range.
		lump = W_CheckNumForNamePwad(name, (UINT16)i, start);
		if (lump < end)
		{
			lump += (i<<16); // found it, in our constraints
			break;
		}
		lump = LUMPERROR;
	}

	if (lump == LUMPERROR)
	{
		if (strcmp(name, SKYFLATNAME))
			CONS_Debug(DBG_SETUP, "R_GetFlatNumForName: Could not find flat %.8s\n", name);
		lump = W_CheckNumForName("REDFLR");
	}

	return lump;
}

//
// R_InitSpriteLumps
// Finds the width and hoffset of all sprites in the wad, so the sprite does not need to be
// cached completely, just for having the header info ready during rendering.
//

//
// allocate sprite lookup tables
//
static void R_InitSpriteLumps(void)
{
	numspritelumps = 0;
	max_spritelumps = 8192;

	Z_Malloc(max_spritelumps*sizeof(*spritecachedinfo), PU_STATIC, &spritecachedinfo);
}

void R_InitColormapsTrueColor(UINT8 palindex)
{
	int color, row, alpha = 255;
	UINT32 fadecolor = V_GetTrueColor(palindex);

	if (truecolormaps)
		Z_Free(truecolormaps);
	truecolormaps = Z_MallocAlign(colormap_size*4, PU_STATIC, NULL, 32);

	for (row = 0; row < 34; row++)
	{
		for (color = 0; color < 256; color++)
			truecolormaps[color + (row * 256)] = V_BlendTrueColor(fadecolor,V_GetTrueColor(color),alpha);
		alpha -= 8;
	}
}

//
// R_InitColormaps
//
static void R_InitColormaps(void)
{
	lumpnum_t lump;

	// Load in the light tables
	lump = W_GetNumForName("COLORMAP");
	colormap_size = W_LumpLength(lump);
	colormaps = Z_MallocAlign(colormap_size, PU_STATIC, NULL, 8);
	W_ReadLump(lump, colormaps);

	// Init Boom colormaps.
	R_ClearColormaps();
	R_InitExtraColormaps();
	R_InitColormapsTrueColor(colormaps[(31*256)+31]);
}

void R_ReInitColormaps(UINT16 num)
{
	char colormap[9] = "COLORMAP";
	lumpnum_t lump;

	if (num > 0 && num <= 10000)
		snprintf(colormap, 8, "CLM%04u", num-1);

	// Load in the light tables
	lump = W_GetNumForName(colormap);
	if (lump == LUMPERROR)
		lump = W_GetNumForName("COLORMAP");
	W_ReadLump(lump, colormaps);

	// Init Boom colormaps.
	R_ClearColormaps();
	R_InitColormapsTrueColor(colormaps[(31*256)+31]);
}

static lumpnum_t foundcolormaps[MAXCOLORMAPS];

//
// R_ClearColormaps
//
// Clears out extra colormaps between levels.
//
void R_ClearColormaps(void)
{
	size_t i;

	num_extra_colormaps = 0;

	for (i = 0; i < MAXCOLORMAPS; i++)
		foundcolormaps[i] = LUMPERROR;

	memset(extra_colormaps, 0, sizeof (extra_colormaps));
}

INT32 R_ColormapNumForName(char *name)
{
	lumpnum_t lump, i;

	if (num_extra_colormaps == MAXCOLORMAPS)
		I_Error("R_ColormapNumForName: Too many colormaps! the limit is %d\n", MAXCOLORMAPS);

	lump = R_CheckNumForNameList(name, colormaplumps, numcolormaplumps);
	if (lump == LUMPERROR)
		I_Error("R_ColormapNumForName: Cannot find colormap lump %.8s\n", name);

	for (i = 0; i < num_extra_colormaps; i++)
		if (lump == foundcolormaps[i])
			return i;

	foundcolormaps[num_extra_colormaps] = lump;

	// aligned on 8 bit for asm code
	extra_colormaps[num_extra_colormaps].colormap = Z_MallocAlign(W_LumpLength(lump), PU_LEVEL, NULL, 16);
	W_ReadLump(lump, extra_colormaps[num_extra_colormaps].colormap);

	// We set all params of the colormap to normal because there
	// is no real way to tell how GL should handle a colormap lump anyway..
	extra_colormaps[num_extra_colormaps].maskcolor = 0xffff;
	extra_colormaps[num_extra_colormaps].fadecolor = 0x0;
	extra_colormaps[num_extra_colormaps].maskamt = 0x0;
	extra_colormaps[num_extra_colormaps].fadestart = 0;
	extra_colormaps[num_extra_colormaps].fadeend = 31;
	extra_colormaps[num_extra_colormaps].fog = 0;

	num_extra_colormaps++;
	return (INT32)num_extra_colormaps - 1;
}

//
// R_CreateColormap
//
// This is a more GL friendly way of doing colormaps: Specify colormap
// data in a special linedef's texture areas and use that to generate
// custom colormaps at runtime. NOTE: For GL mode, we only need to color
// data and not the colormap data.
//
static double deltas[256][3], map[256][3];
static int RoundUp(double number);

INT32 R_CreateColormap(char *p1, char *p2, char *p3)
{
	double cmaskr, cmaskg, cmaskb, cdestr = 0, cdestg = 0, cdestb = 0;
	double maskamt = 0, othermask = 0;
	int mask, fog = 0;
	size_t mapnum = num_extra_colormaps;
	size_t i;
	UINT32 cr = 0, cg = 0, cb = 0, maskcolor, fadecolor;
	UINT32 fadestart = 0, fadeend = 31, fadedist = 31;
	UINT32 rgb_color = 0xFF000000;

#define HEX2INT(x) (UINT32)(x >= '0' && x <= '9' ? x - '0' : x >= 'a' && x <= 'f' ? x - 'a' + 10 : x >= 'A' && x <= 'F' ? x - 'A' + 10 : 0)
	if (p1[0] == '#')
	{
		cr = ((HEX2INT(p1[1]) * 16) + HEX2INT(p1[2]));
		cmaskr = cr;
		cg = ((HEX2INT(p1[3]) * 16) + HEX2INT(p1[4]));
		cmaskg = cg;
		cb = ((HEX2INT(p1[5]) * 16) + HEX2INT(p1[6]));
		cmaskb = cb;
		// Create a rough approximation of the color (a 16 bit color)
		maskcolor = ((cb) >> 3) + (((cg) >> 2) << 5) + (((cr) >> 3) << 11);
		if (p1[7] >= 'a' && p1[7] <= 'z')
			mask = (p1[7] - 'a');
		else if (p1[7] >= 'A' && p1[7] <= 'Z')
			mask = (p1[7] - 'A');
		else
			mask = 24;

		maskamt = (double)(mask/24.0l);
		rgb_color = llrint(maskamt*256.0f);
		if (rgb_color > 256)
			rgb_color = 255;
		rgb_color <<= 24;

		othermask = 1 - maskamt;
		maskamt /= 0xff;
		cmaskr *= maskamt;
		cmaskg *= maskamt;
		cmaskb *= maskamt;
	}
	else
	{
		cmaskr = cmaskg = cmaskb = 0xff;
		maskamt = 0;
		maskcolor = ((0xff) >> 3) + (((0xff) >> 2) << 5) + (((0xff) >> 3) << 11);
	}

#define NUMFROMCHAR(c) (c >= '0' && c <= '9' ? c - '0' : 0)
	if (p2[0] == '#')
	{
		// Get parameters like fadestart, fadeend, and the fogflag
		fadestart = NUMFROMCHAR(p2[3]) + (NUMFROMCHAR(p2[2]) * 10);
		fadeend = NUMFROMCHAR(p2[5]) + (NUMFROMCHAR(p2[4]) * 10);
		if (fadestart > 30)
			fadestart = 0;
		if (fadeend > 31 || fadeend < 1)
			fadeend = 31;
		fadedist = fadeend - fadestart;
		fog = NUMFROMCHAR(p2[1]);
	}
#undef NUMFROMCHAR

	if (p3[0] == '#')
	{
		cdestr = cr = ((HEX2INT(p3[1]) * 16) + HEX2INT(p3[2]));
		cdestg = cg = ((HEX2INT(p3[3]) * 16) + HEX2INT(p3[4]));
		cdestb = cb = ((HEX2INT(p3[5]) * 16) + HEX2INT(p3[6]));
		fadecolor = (((cb) >> 3) + (((cg) >> 2) << 5) + (((cr) >> 3) << 11));
	}
	else
		cdestr = cdestg = cdestb = fadecolor = 0;
#undef HEX2INT

	for (i = 0; i < num_extra_colormaps; i++)
	{
		if (foundcolormaps[i] != LUMPERROR)
			continue;
		if (maskcolor == extra_colormaps[i].maskcolor
			&& fadecolor == extra_colormaps[i].fadecolor
			&& fabs(maskamt - extra_colormaps[i].maskamt) < DBL_EPSILON
			&& fadestart == extra_colormaps[i].fadestart
			&& fadeend == extra_colormaps[i].fadeend
			&& fog == extra_colormaps[i].fog)
		{
			return (INT32)i;
		}
	}

	if (num_extra_colormaps == MAXCOLORMAPS)
		I_Error("R_CreateColormap: Too many colormaps! the limit is %d\n", MAXCOLORMAPS);

	num_extra_colormaps++;

	foundcolormaps[mapnum] = LUMPERROR;

	// aligned on 8 bit for asm code
	extra_colormaps[mapnum].colormap = NULL;
	extra_colormaps[mapnum].maskcolor = (UINT16)maskcolor;
	extra_colormaps[mapnum].fadecolor = (UINT16)fadecolor;
	extra_colormaps[mapnum].maskamt = maskamt;
	extra_colormaps[mapnum].fadestart = (UINT16)fadestart;
	extra_colormaps[mapnum].fadeend = (UINT16)fadeend;
	extra_colormaps[mapnum].fog = fog;
	extra_colormaps[mapnum].rgba = rgb_color|((cb<<16)|(cg<<8)|cr);
	extra_colormaps[mapnum].fadergba = rgb_color|((llrint(cdestb)<<16)|(llrint(cdestg)<<8)|llrint(cdestr));

	strcpy(extra_colormaps[mapnum].hex1, p1);
	strcpy(extra_colormaps[mapnum].hex2, p2);
	strcpy(extra_colormaps[mapnum].hex3, p3);

	// This code creates the colormap array used by software renderer
	if (rendermode == render_soft)
	{
		double r, g, b, cbrightness;
		int p;
		char *colormap_p;
		UINT32 *colormap_p32;

		// Initialise the map and delta arrays
		// map[i] stores an RGB color (as double) for index i,
		//  which is then converted to SRB2's palette later
		// deltas[i] stores a corresponding fade delta between the RGB color and the final fade color;
		//  map[i]'s values are decremented by after each use
		for (i = 0; i < 256; i++)
		{
			RGBA_t rgba = (st_palette > 0) ? V_GetColorPal(i,st_palette) : V_GetColor(i);
			r = rgba.s.red;
			g = rgba.s.green;
			b = rgba.s.blue;
			cbrightness = sqrt((r*r) + (g*g) + (b*b));

			map[i][0] = (cbrightness * cmaskr) + (r * othermask);
			if (map[i][0] > 255.0l)
				map[i][0] = 255.0l;
			deltas[i][0] = (map[i][0] - cdestr) / (double)fadedist;

			map[i][1] = (cbrightness * cmaskg) + (g * othermask);
			if (map[i][1] > 255.0l)
				map[i][1] = 255.0l;
			deltas[i][1] = (map[i][1] - cdestg) / (double)fadedist;

			map[i][2] = (cbrightness * cmaskb) + (b * othermask);
			if (map[i][2] > 255.0l)
				map[i][2] = 255.0l;
			deltas[i][2] = (map[i][2] - cdestb) / (double)fadedist;
		}

		// Now allocate memory for the actual colormap array itself!
		colormap_p = Z_MallocAlign((256 * 34) + 10, PU_LEVEL, NULL, 8);
		extra_colormaps[mapnum].colormap = (UINT8 *)colormap_p;
		colormap_p32 = Z_MallocAlign(((256 * 34) + 10) * 4, PU_LEVEL, NULL, 32);
		extra_colormaps[mapnum].truecolormap = (UINT32 *)colormap_p32;

		// Calculate the palette index for each palette index, for each light level
		// (as well as the two unused colormap lines we inherited from Doom)
		for (p = 0; p < 34; p++)
		{
			for (i = 0; i < 256; i++)
			{
				rgb_color = 0xFF000000;

				*colormap_p = NearestColor((UINT8)RoundUp(map[i][0]), (UINT8)RoundUp(map[i][1]), (UINT8)RoundUp(map[i][2]));
				colormap_p++;
				rgb_color |= (RoundUp(map[i][2]) & 255) << 16;
				rgb_color |= (RoundUp(map[i][1]) & 255) << 8;
				rgb_color |= (RoundUp(map[i][0]) & 255);
				*colormap_p32 = rgb_color;
				colormap_p32++;

				if ((UINT32)p < fadestart)
					continue;
#define ABS2(x) ((x) < 0 ? -(x) : (x))
				if (ABS2(map[i][0] - cdestr) > ABS2(deltas[i][0]))
					map[i][0] -= deltas[i][0];
				else
					map[i][0] = cdestr;

				if (ABS2(map[i][1] - cdestg) > ABS2(deltas[i][1]))
					map[i][1] -= deltas[i][1];
				else
					map[i][1] = cdestg;

				if (ABS2(map[i][2] - cdestb) > ABS2(deltas[i][1]))
					map[i][2] -= deltas[i][2];
				else
					map[i][2] = cdestb;
#undef ABS2
			}
		}
	}

	return (INT32)mapnum;
}

// Thanks to quake2 source!
// utils3/qdata/images.c
UINT8 NearestColor(UINT8 r, UINT8 g, UINT8 b)
{
	int dr, dg, db;
	int distortion, bestdistortion = 256 * 256 * 4, bestcolor = 0, i;

	for (i = 0; i < 256; i++)
	{
		dr = r - pLocalPalette[i].s.red;
		dg = g - pLocalPalette[i].s.green;
		db = b - pLocalPalette[i].s.blue;
		distortion = dr*dr + dg*dg + db*db;
		if (distortion < bestdistortion)
		{
			if (!distortion)
				return (UINT8)i;

			bestdistortion = distortion;
			bestcolor = i;
		}
	}

	return (UINT8)bestcolor;
}

// Rounds off floating numbers and checks for 0 - 255 bounds
static int RoundUp(double number)
{
	if (number > 255.0l)
		return 255;
	if (number < 0.0l)
		return 0;

	if ((int)number <= (int)(number - 0.5f))
		return (int)number + 1;

	return (int)number;
}

const char *R_ColormapNameForNum(INT32 num)
{
	if (num == -1)
		return "NONE";

	if (num < 0 || num > MAXCOLORMAPS)
		I_Error("R_ColormapNameForNum: num %d is invalid!\n", num);

	if (foundcolormaps[num] == LUMPERROR)
		return "INLEVEL";

	return W_CheckNameForNum(foundcolormaps[num]);
}

//
// R_InitData
//
// Locates all the lumps that will be used by all views
// Must be called after W_Init.
//
void R_InitData(void)
{
	CONS_Printf("R_LoadTextures()...\n");
	R_LoadTextures();

	CONS_Printf("P_InitPicAnims()...\n");
	P_InitPicAnims();

	CONS_Printf("R_InitSprites()...\n");
	R_InitSpriteLumps();
	R_InitSprites();

	CONS_Printf("R_InitColormaps()...\n");
	R_InitColormaps();
}

void R_ClearTextureNumCache(boolean btell)
{
	if (tidcache)
		Z_Free(tidcache);
	tidcache = NULL;
	if (btell)
		CONS_Debug(DBG_SETUP, "Fun Fact: There are %d textures used in this map.\n", tidcachelen);
	tidcachelen = 0;
}

//
// R_CheckTextureNumForName
//
// Check whether texture is available. Filter out NoTexture indicator.
//
INT32 R_CheckTextureNumForName(const char *name)
{
	INT32 i;

	// "NoTexture" marker.
	if (name[0] == '-')
		return 0;

	for (i = 0; i < tidcachelen; i++)
		if (!strncasecmp(tidcache[i].name, name, 8))
			return tidcache[i].id;

	// Need to parse the list backwards, so textures loaded more recently are used in lieu of ones loaded earlier
	//for (i = 0; i < numtextures; i++) <- old
	for (i = (numtextures - 1); i >= 0; i--) // <- new
		if (!strncasecmp(textures[i]->name, name, 8))
		{
			tidcachelen++;
			Z_Realloc(tidcache, tidcachelen * sizeof(*tidcache), PU_STATIC, &tidcache);
			strncpy(tidcache[tidcachelen-1].name, name, 8);
			tidcache[tidcachelen-1].name[8] = '\0';
#ifndef ZDEBUG
			CONS_Debug(DBG_SETUP, "texture #%s: %s\n", sizeu1(tidcachelen), tidcache[tidcachelen-1].name);
#endif
			tidcache[tidcachelen-1].id = i;
			return i;
		}

	return -1;
}

//
// R_TextureNumForName
//
// Calls R_CheckTextureNumForName, aborts with error message.
//
INT32 R_TextureNumForName(const char *name)
{
	const INT32 i = R_CheckTextureNumForName(name);

	if (i == -1)
	{
		static INT32 redwall = -2;
		CONS_Debug(DBG_SETUP, "WARNING: R_TextureNumForName: %.8s not found\n", name);
		if (redwall == -2)
			redwall = R_CheckTextureNumForName("REDWALL");
		if (redwall != -1)
			return redwall;
		return 1;
	}
	return i;
}

//
// R_PrecacheLevel
//
// Preloads all relevant graphics for the level.
//
void R_PrecacheLevel(void)
{
	char *texturepresent, *spritepresent;
	size_t i, j, k;
	lumpnum_t lump;

	thinker_t *th;
	spriteframe_t *sf;

	if (demoplayback)
		return;

	// do not flush the memory, Z_Malloc twice with same user will cause error in Z_CheckHeap()
	if (rendermode != render_soft)
		return;

	// Precache flats.
	flatmemory = P_PrecacheLevelFlats();

	//
	// Precache textures.
	//
	// no need to precache all software textures in 3D mode
	// (note they are still used with the reference software view)
	texturepresent = calloc(numtextures, sizeof (*texturepresent));
	if (texturepresent == NULL) I_Error("%s: Out of memory looking up textures", "R_PrecacheLevel");

	for (j = 0; j < numsides; j++)
	{
		// huh, a potential bug here????
		if (sides[j].toptexture >= 0 && sides[j].toptexture < numtextures)
			texturepresent[sides[j].toptexture] = 1;
		if (sides[j].midtexture >= 0 && sides[j].midtexture < numtextures)
			texturepresent[sides[j].midtexture] = 1;
		if (sides[j].bottomtexture >= 0 && sides[j].bottomtexture < numtextures)
			texturepresent[sides[j].bottomtexture] = 1;
	}

	// Sky texture is always present.
	// Note that F_SKY1 is the name used to indicate a sky floor/ceiling as a flat,
	// while the sky texture is stored like a wall texture, with a skynum dependent name.
	texturepresent[skytexture] = 1;

	texturememory = 0;
	for (j = 0; j < (unsigned)numtextures; j++)
	{
		if (!texturepresent[j])
			continue;

		if (!texturecache[j])
			R_GenerateTexture(j);
		// pre-caching individual patches that compose textures became obsolete,
		// since we cache entire composite textures
	}
	free(texturepresent);

	//
	// Precache sprites.
	//
	spritepresent = calloc(numsprites, sizeof (*spritepresent));
	if (spritepresent == NULL) I_Error("%s: Out of memory looking up sprites", "R_PrecacheLevel");

	for (th = thinkercap.next; th != &thinkercap; th = th->next)
		if (th->function.acp1 == (actionf_p1)P_MobjThinker)
			spritepresent[((mobj_t *)th)->sprite] = 1;

	spritememory = 0;
	for (i = 0; i < numsprites; i++)
	{
		if (!spritepresent[i])
			continue;

		for (j = 0; j < sprites[i].numframes; j++)
		{
			sf = &sprites[i].spriteframes[j];
			for (k = 0; k < 8; k++)
			{
				// see R_InitSprites for more about lumppat,lumpid
				lump = sf->lumppat[k];
				if (devparm)
					spritememory += W_LumpLength(lump);
				W_CachePatchNum(lump, PU_CACHE);
			}
		}
	}
	free(spritepresent);

	// FIXME: this is no longer correct with OpenGL render mode
	CONS_Debug(DBG_SETUP, "Precache level done:\n"
			"flatmemory:    %s k\n"
			"texturememory: %s k\n"
			"spritememory:  %s k\n", sizeu1(flatmemory>>10), sizeu2(texturememory>>10), sizeu3(spritememory>>10));
}
