//------------------------------------------------------------------------
// STRUCT : Doom structures, raw on-disk layout
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

#ifndef __GLBSP_STRUCTS_H__
#define __GLBSP_STRUCTS_H__

#include "../doomtype.h"

// blockmap

typedef struct raw_blockmap_header_s
{
	INT16 x_origin, y_origin;
	INT16 x_blocks, y_blocks;
} raw_blockmap_header_t;

/* ----- The level structures ---------------------- */

typedef struct raw_vertex_s
{
	INT16 x, y;
} raw_bspvertex_t;

typedef struct raw_bsplinedef_s
{
	UINT16 start;     // from this vertex...
	UINT16 end;       // ... to this vertex
	UINT16 flags;     // linedef flags (impassible, etc)
	UINT16 type;      // linedef type (0 for none, 97 for teleporter, etc)
	INT16 tag;       // this linedef activates the sector with same tag
	UINT16 sidedef1;  // right sidedef
	UINT16 sidedef2;  // left sidedef (only if this line adjoins 2 sectors)
} raw_bsplinedef_t;

typedef struct raw_sidedef_s
{
	INT16 x_offset;  // X offset for texture
	INT16 y_offset;  // Y offset for texture

	char upper_tex[8];  // texture name for the part above
	char lower_tex[8];  // texture name for the part below
	char mid_tex[8];    // texture name for the regular part

	UINT16 sector;    // adjacent sector
} raw_bspsidedef_t;

typedef struct raw_sector_s
{
	INT16 floor_h;   // floor height
	INT16 ceil_h;    // ceiling height

	char floor_tex[8];  // floor texture
	char ceil_tex[8];   // ceiling texture

	UINT16 light;     // light level (0-255)
	UINT16 special;   // special behaviour (0 = normal, 9 = secret, ...)
	INT16 tag;       // sector activated by a linedef with same tag
} raw_bspsector_t;

/* ----- The BSP tree structures ----------------------- */

typedef struct raw_seg_s
{
	UINT16 start;     // from this vertex...
	UINT16 end;       // ... to this vertex
	UINT16 angle;     // angle (0 = east, 16384 = north, ...)
	UINT16 linedef;   // linedef that this seg goes along
	UINT16 flip;      // true if not the same direction as linedef
	UINT16 dist;      // distance from starting point
} raw_bspseg_t;

typedef struct raw_bbox_s
{
	INT16 maxy, miny;
	INT16 minx, maxx;
} raw_bspbbox_t;

typedef struct raw_node_s
{
	INT16 x, y;         // starting point
	INT16 dx, dy;       // offset to ending point
	raw_bspbbox_t b1, b2;     // bounding rectangles
	UINT16 right, left;  // children: Node or SSector (if high bit is set)
} raw_bspnode_t;

typedef struct raw_subsec_s
{
	UINT16 num;     // number of Segs in this Sub-Sector
	UINT16 first;   // first Seg
} raw_bspsubsec_t;

#endif /* __GLBSP_STRUCTS_H__ */
