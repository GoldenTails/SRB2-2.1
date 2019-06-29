//------------------------------------------------------------------------
// MAIN : Main program for glBSP
//------------------------------------------------------------------------
//
//  GL-Friendly Node Builder (C) 2000-2007 Andrew Apted
//
//  Based on 'BSP 2.3' by Colin Reed, Lee Killough and others.
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <assert.h>

#include "bsp_main.h"
#include "bsp_blockmap.h"
#include "bsp_level.h"
#include "bsp_node.h"
#include "bsp_seg.h"
#include "bsp_structs.h"
#include "bsp_util.h"

#include "../doomtype.h"
#include "../i_system.h"
#include "../g_game.h"

boolean GLBSP_HandleLevel(void)
{
	superblock_t *seg_list;
	bspnode_t *root_node;
	bspsubsec_t *root_sub;
	boolean ret;

	LoadLevel();

	// initialize blockmap
	InitBlockmap();

	// create initial segs
	seg_list = CreateSegs();

	// recursively create nodes
	ret = BuildNodes(seg_list, &root_node, &root_sub, 0);
	FreeSuper(seg_list);

	if (ret)
	{
		ClockwiseBspTree();
		SaveLevel(root_node);
	}
	else
		I_Error("GLBSP error: BuildNodes failed");

	FreeLevel();
	FreeQuickAllocCuts();
	FreeQuickAllocSupers();

	return true;
}
