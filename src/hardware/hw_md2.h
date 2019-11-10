// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1998-2000 by DooM Legacy Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//-----------------------------------------------------------------------------
/// \file
/// \brief MD2 Handling
///	Inspired from md2.h by Mete Ciragan (mete@swissquake.ch)

#ifndef _HW_MD2_H_
#define _HW_MD2_H_

#include "hw_glob.h"
#include "../r_model.h"

#if defined(_MSC_VER)
#pragma pack()
#endif

typedef struct
{
	char         filename[32];
	float        scale;
	float        xoffset;
	float        yoffset;
	float        angleoffset;
	float        axisrotate[3];
	model_t      *model;
	void         *grpatch;
	void         *blendgrpatch;
	modelflags_t modelflags;
	boolean      internal;
	UINT32       model_lumpnum;
	UINT32       texture_lumpnum;
	UINT32       blendtexture_lumpnum;
	boolean      notfound;
	INT32        skin;
} md2_t;

extern md2_t md2_models[NUMSPRITES];
extern md2_t md2_playermodels[MAXSKINS];

void HWR_InitMD2(void);
boolean HWR_DrawMD2(gr_vissprite_t *spr);

void HWR_AddPlayerMD2(INT32 skin);
void HWR_AddSpriteMD2(size_t spritenum);

void HWR_AddInternalPlayerMD2(UINT32 lumpnum, size_t skinnum, float scale, float xoffset, float yoffset);
void HWR_AddInternalSpriteMD2(UINT32 lumpnum);

#endif // _HW_MD2_H_
