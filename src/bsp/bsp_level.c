//------------------------------------------------------------------------
// LEVEL : Level structure read/write functions.
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
//
//  ZDBSP format support based on code (C) 2002,2003 Randy Heit
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

#include "bsp_analyze.h"
#include "bsp_blockmap.h"
#include "bsp_level.h"
#include "bsp_node.h"
#include "bsp_seg.h"
#include "bsp_structs.h"
#include "bsp_util.h"

#include "../doomdata.h"
#include "../doomdef.h"
#include "../doomtype.h"
#include "../byteptr.h"

#include "../i_system.h"
#include "../p_setup.h"
#include "../z_zone.h"

#define ALLOC_BLKNUM  1024

// per-level variables

#define LEVELARRAY(TYPE, BASEVAR, NUMVAR)  \
    TYPE ** BASEVAR = NULL;  \
    int NUMVAR = 0;

LEVELARRAY(bspvertex_t,  lev_vertices,   num_vertices)
LEVELARRAY(bsplinedef_t, lev_linedefs,   num_linedefs)
LEVELARRAY(bspsidedef_t, lev_sidedefs,   num_sidedefs)
LEVELARRAY(bspsector_t,  lev_sectors,    num_sectors)

static LEVELARRAY(bspseg_t,     segs,       num_segs)
static LEVELARRAY(bspsubsec_t,  subsecs,    num_subsecs)
static LEVELARRAY(bspnode_t,    nodes,      num_nodes)
static LEVELARRAY(wall_tip_t,wall_tips,  num_wall_tips)

int num_normal_vert = 0;
int num_complete_seg = 0;

/* ----- allocation routines ---------------------------- */

#define ALLIGATOR(TYPE, BASEVAR, NUMVAR)  \
{  \
  if ((NUMVAR % ALLOC_BLKNUM) == 0)  \
  {  \
    BASEVAR = UtilRealloc(BASEVAR, (NUMVAR + ALLOC_BLKNUM) *   \
        sizeof(TYPE *));  \
  }  \
  BASEVAR[NUMVAR] = (TYPE *) UtilCalloc(sizeof(TYPE));  \
  NUMVAR += 1;  \
  return BASEVAR[NUMVAR - 1];  \
}

bspvertex_t *NewVertex(void)
  ALLIGATOR(bspvertex_t, lev_vertices, num_vertices)

bsplinedef_t *NewLinedef(void)
  ALLIGATOR(bsplinedef_t, lev_linedefs, num_linedefs)

bspsidedef_t *NewSidedef(void)
  ALLIGATOR(bspsidedef_t, lev_sidedefs, num_sidedefs)

bspsector_t *NewSector(void)
  ALLIGATOR(bspsector_t, lev_sectors, num_sectors)

bspseg_t *NewSeg(void)
  ALLIGATOR(bspseg_t, segs, num_segs)

bspsubsec_t *NewSubsec(void)
  ALLIGATOR(bspsubsec_t, subsecs, num_subsecs)

bspnode_t *NewNode(void)
  ALLIGATOR(bspnode_t, nodes, num_nodes)

wall_tip_t *NewWallTip(void)
  ALLIGATOR(wall_tip_t, wall_tips, num_wall_tips)


/* ----- free routines ---------------------------- */

#define FREEMASON(TYPE, BASEVAR, NUMVAR)  \
{  \
  int i;  \
  for (i=0; i < NUMVAR; i++)  \
    UtilFree(BASEVAR[i]);  \
  if (BASEVAR)  \
    UtilFree(BASEVAR);  \
  BASEVAR = NULL; NUMVAR = 0;  \
}

void FreeVertices(void)
  FREEMASON(bspvertex_t, lev_vertices, num_vertices)

void FreeLinedefs(void)
  FREEMASON(bsplinedef_t, lev_linedefs, num_linedefs)

void FreeSidedefs(void)
  FREEMASON(bspsidedef_t, lev_sidedefs, num_sidedefs)

void FreeSectors(void)
  FREEMASON(bspsector_t, lev_sectors, num_sectors)

void FreeSegs(void)
  FREEMASON(bspseg_t, segs, num_segs)

void FreeSubsecs(void)
  FREEMASON(bspsubsec_t, subsecs, num_subsecs)

void FreeNodes(void)
  FREEMASON(bspnode_t, nodes, num_nodes)

void FreeWallTips(void)
  FREEMASON(wall_tip_t, wall_tips, num_wall_tips)


/* ----- lookup routines ------------------------------ */

#define LOOKERUPPER(BASEVAR, NUMVAR, NAMESTR)  \
{  \
  if (index < 0 || index >= NUMVAR)  \
    I_Error("GLBSP error: No such %s number #%d (max %d)", NAMESTR, index, NUMVAR);  \
    \
  return BASEVAR[index];  \
}

bspvertex_t *LookupVertex(int index)
  LOOKERUPPER(lev_vertices, num_vertices, "vertex")

bsplinedef_t *LookupLinedef(int index)
  LOOKERUPPER(lev_linedefs, num_linedefs, "linedef")

bspsidedef_t *LookupSidedef(int index)
  LOOKERUPPER(lev_sidedefs, num_sidedefs, "sidedef")

bspsector_t *LookupSector(int index)
  LOOKERUPPER(lev_sectors, num_sectors, "sector")

bspseg_t *LookupSeg(int index)
  LOOKERUPPER(segs, num_segs, "seg")

bspsubsec_t *LookupSubsec(int index)
  LOOKERUPPER(subsecs, num_subsecs, "subsector")

bspnode_t *LookupNode(int index)
  LOOKERUPPER(nodes, num_nodes, "node")

//
// GetVertices
//
void GetVertices(void)
{
	int i, count=-1;
	raw_bspvertex_t *raw;

	count = rawvertexes_size / sizeof(raw_bspvertex_t);
	if (!count)
		I_Error("GLBSP error: Couldn't find any Vertices");

	raw = (raw_bspvertex_t *)rawvertexes;

	for (i=0; i < count; i++, raw++)
	{
		bspvertex_t *vert = NewVertex();

		vert->x = (float_g) (INT16)raw->x;
		vert->y = (float_g) (INT16)raw->y;

		vert->index = i;
	}

	num_normal_vert = num_vertices;
	num_complete_seg = 0;
}

//
// GetSectors
//
void GetSectors(void)
{
	int i, count=-1;
	raw_bspsector_t *raw;

	count = rawsectors_size / sizeof(raw_bspsector_t);
	if (!count)
		I_Error("GLBSP error: Couldn't find any Sectors");

	raw = (raw_bspsector_t *)rawsectors;

	for (i=0; i < count; i++, raw++)
	{
		bspsector_t *sector = NewSector();

		sector->floor_h = (INT16)raw->floor_h;
		sector->ceil_h  = (INT16)raw->ceil_h;

		memcpy(sector->floor_tex, raw->floor_tex, sizeof(sector->floor_tex));
		memcpy(sector->ceil_tex,  raw->ceil_tex,  sizeof(sector->ceil_tex));

		sector->light = (INT16)raw->light;
		sector->special = (UINT16)raw->special;
		sector->tag = (INT16)raw->tag;

		/* sector indices never change */
		sector->index = i;

		sector->warned_facing = -1;
	}
}

//
// GetSidedefs
//
void GetSidedefs(void)
{
	int i, count=-1;
	raw_bspsidedef_t *raw;

	count = rawsides_size / sizeof(raw_bspsidedef_t);
	if (!count)
		I_Error("GLBSP error: Couldn't find any Sidedefs");

	raw = (raw_bspsidedef_t *)rawsides;
	for (i=0; i < count; i++, raw++)
	{
		bspsidedef_t *side = NewSidedef();

		side->sector = ((INT16)raw->sector == -1) ? NULL : LookupSector((UINT16)raw->sector);

		if (side->sector)
			side->sector->ref_count++;

		side->x_offset = (INT16)raw->x_offset;
		side->y_offset = (INT16)raw->y_offset;

		memcpy(side->upper_tex, raw->upper_tex, sizeof(side->upper_tex));
		memcpy(side->lower_tex, raw->lower_tex, sizeof(side->lower_tex));
		memcpy(side->mid_tex,   raw->mid_tex,   sizeof(side->mid_tex));

		/* sidedef indices never change */
		side->index = i;
	}
}

static inline bspsidedef_t *SafeLookupSidedef(UINT16 num)
{
	if (num == 0xFFFF) return NULL;
	if ((int)num >= num_sidedefs && (INT16)(num) < 0) return NULL;
	return LookupSidedef(num);
}

//
// GetLinedefs
//
void GetLinedefs(void)
{
	int i, count=-1;
	raw_bsplinedef_t *raw;

	count = rawlines_size / sizeof(raw_bsplinedef_t);
	if (!count)
		I_Error("GLBSP error: Couldn't find any Linedefs");

	raw = (raw_bsplinedef_t *)rawlines;

	for (i=0; i < count; i++, raw++)
	{
		bsplinedef_t *line;

		bspvertex_t *start = LookupVertex((UINT16)raw->start);
		bspvertex_t *end   = LookupVertex((UINT16)raw->end);

		start->ref_count++;
		end->ref_count++;

		line = NewLinedef();

		line->start = start;
		line->end   = end;

		/* check for zero-length line */
		line->zero_len = (fabs(start->x - end->x) < DIST_EPSILON) && (fabs(start->y - end->y) < DIST_EPSILON);

		line->flags = (UINT16)(raw->flags);
		line->type = (UINT16)(raw->type);
		line->tag  = (INT16)(raw->tag);

		line->two_sided = (line->flags & ML_TWOSIDED) ? true : false;

		line->right = SafeLookupSidedef((UINT16)(raw->sidedef1));
		line->left  = SafeLookupSidedef((UINT16)(raw->sidedef2));

		if (line->right)
		{
			line->right->ref_count++;
			line->right->on_special |= (line->type > 0) ? 1 : 0;
		}

		if (line->left)
		{
			line->left->ref_count++;
			line->left->on_special |= (line->type > 0) ? 1 : 0;
		}

		line->self_ref = (line->left && line->right && (line->left->sector == line->right->sector));

		line->index = i;
	}
}

static inline int TransformSegDist(const bspseg_t *seg)
{
	float_g sx = seg->side ? seg->linedef->end->x : seg->linedef->start->x;
	float_g sy = seg->side ? seg->linedef->end->y : seg->linedef->start->y;

	return (int) ceil(UtilComputeDist(seg->start->x - sx, seg->start->y - sy));
}

static inline int TransformAngle(angle_g angle)
{
	int result;

	result = (int)(angle * 65536.0 / 360.0);

	if (result < 0)
		result += 65536;

	return (result & 0xFFFF);
}

static int SegCompare(const void *p1, const void *p2)
{
	const bspseg_t *A = ((const bspseg_t **) p1)[0];
	const bspseg_t *B = ((const bspseg_t **) p2)[0];

	if (A->index < 0)
		I_Error("GLBSP error: Seg %p never reached a subsector!", A);

	if (B->index < 0)
		I_Error("GLBSP error: Seg %p never reached a subsector!", B);

	return (A->index - B->index);
}

/* ----- writing routines ------------------------------ */

UINT8 *PutVertices(void)
{
	UINT8 *rawoutput = Z_Malloc(sizeof(mapvertex_t) * num_vertices, PU_LEVEL, NULL);
	mapvertex_t *wverts = (mapvertex_t *)rawoutput;
	int count, i;

	for (i = 0, count = 0; i < num_vertices; i++)
	{
		bspvertex_t *vert = lev_vertices[i];

		wverts->x = I_ROUND(vert->x);
		wverts->y = I_ROUND(vert->y);

		wverts++;
		count++;
	}

	if (count != num_normal_vert)
		I_Error("PutVertices miscounted (%d != %d)", count, num_normal_vert);

	if (count > 65534)
		I_Error("GLBSP error: hit vertex limit");

	return rawoutput;
}

UINT8 *PutSectors(void)
{
	UINT8 *rawoutput = Z_Malloc(sizeof(mapsector_t) * num_sectors, PU_LEVEL, NULL);
	mapsector_t *wsectors = (mapsector_t *)rawoutput;
	int i;

	for (i = 0; i < num_sectors; i++)
	{
		bspsector_t *sector = lev_sectors[i];

		wsectors->floorheight = sector->floor_h;
		wsectors->ceilingheight = sector->ceil_h;

		strncpy(wsectors->floorpic, sector->floor_tex, 8);
		strncpy(wsectors->ceilingpic, sector->ceil_tex, 8);

		wsectors->lightlevel = sector->light;
		wsectors->special = sector->special;
		wsectors->tag = sector->tag;

		wsectors++;
	}

	if (num_sectors > 65534)
		I_Error("GLBSP error: hit vertex limit");

	return rawoutput;
}

UINT8 *PutSidedefs(void)
{
	UINT8 *rawoutput = Z_Malloc(sizeof(mapsidedef_t) * num_sidedefs, PU_LEVEL, NULL);
	mapsidedef_t *wsides = (mapsidedef_t *)rawoutput;
	int i;

	for (i = 0; i < num_sidedefs; i++)
	{
		bspsidedef_t *side = lev_sidedefs[i];

		wsides->textureoffset = side->x_offset;
		wsides->rowoffset = side->y_offset;

		strncpy(wsides->toptexture, side->upper_tex, 8);
		strncpy(wsides->bottomtexture, side->lower_tex, 8);
		strncpy(wsides->midtexture, side->mid_tex, 8);

		wsides->sector = (side->sector == NULL) ? -1 : side->sector->index;

		wsides++;
	}

	if (num_sidedefs > 65534)
		I_Error("GLBSP error: hit sidedef limit");

	return rawoutput;
}

UINT8 *PutLinedefs(void)
{
	UINT8 *rawoutput = Z_Malloc(sizeof(maplinedef_t) * num_linedefs, PU_LEVEL, NULL);
	maplinedef_t *wlines = (maplinedef_t *)rawoutput;
	int i;

	for (i = 0; i < num_linedefs; i++)
	{
		bsplinedef_t *line = lev_linedefs[i];

		wlines->v1 = line->start->index;
		wlines->v2 = line->end->index;

		wlines->flags = line->flags;
		wlines->special = line->type;
		wlines->tag = line->tag;

		wlines->sidenum[0] = line->right ? line->right->index : 0xFFFF;
		wlines->sidenum[1] = line->left ? line->left->index : 0xFFFF;

		wlines++;
	}

	if (num_linedefs > 65534)
		I_Error("GLBSP error: hit linedef limit");

	return rawoutput;
}

UINT8 *PutSegs(void)
{
	UINT8 *realrawoutput, *rawoutput = Z_Malloc(sizeof(mapseg_t) * num_segs, PU_LEVEL, NULL);
	mapseg_t *realwsegs, *wsegs = (mapseg_t *)rawoutput;
	int i, count;

	// sort segs into ascending index
	qsort(segs, num_segs, sizeof(bspseg_t *), SegCompare);

	for (i = 0, count = 0; i < num_segs; i++)
	{
		bspseg_t *seg = segs[i];

		// ignore minisegs and degenerate segs
		if (!seg->linedef || seg->degenerate)
			continue;

		wsegs->v1 = seg->start->index;
		wsegs->v2 = seg->end->index;

		wsegs->angle = TransformAngle(seg->p_angle);
		wsegs->linedef = seg->linedef->index;
		wsegs->side = seg->side;
		wsegs->offset = TransformSegDist(seg);

		wsegs++;
		count++;
	}

	if (count != num_complete_seg)
		I_Error("GLBSP error: PutSegs miscounted (%d != %d)", count, num_complete_seg);

	if (count > 65534)
		I_Error("GLBSP error: hit seg limit");

	wsegs = (mapseg_t *)rawoutput;
	realrawoutput = Z_Malloc(sizeof(mapseg_t) * count, PU_LEVEL, NULL);
	realwsegs = (mapseg_t *)realrawoutput;
	for (i = 0; i < count; i++)
	{
		realwsegs->v1 = wsegs->v1;
		realwsegs->v2 = wsegs->v2;

		realwsegs->angle = wsegs->angle;
		realwsegs->linedef = wsegs->linedef;
		realwsegs->side = wsegs->side;
		realwsegs->offset = wsegs->offset;

		wsegs++;
		realwsegs++;
	}
	num_segs = count;

	return realrawoutput;
}

UINT8 *PutSubsecs(void)
{
	UINT8 *rawoutput = Z_Malloc(sizeof(mapsubsector_t) * num_subsecs, PU_LEVEL, NULL);
	mapsubsector_t *wsubsecs = (mapsubsector_t *)rawoutput;
	int i;

	for (i = 0; i < num_subsecs; i++)
	{
		bspsubsec_t *sub = subsecs[i];

		wsubsecs->numsegs = sub->seg_count;
		wsubsecs->firstseg = sub->seg_list->index;

		wsubsecs++;
	}

	if (num_subsecs > 32767)
		I_Error("GLBSP error: hit subsector limit");

	return rawoutput;
}

static int node_cur_index = 0;

static void PutOneNode(bspnode_t *node, UINT8 *rawoutput)
{
	mapnode_t *wsubsecs;

	if (node->r.node)
		PutOneNode(node->r.node, rawoutput);

	if (node->l.node)
		PutOneNode(node->l.node, rawoutput);

	node->index = node_cur_index++;
	wsubsecs = (mapnode_t *)rawoutput + (node_cur_index - 1);

	wsubsecs->x = node->x;
	wsubsecs->y = node->y;

	wsubsecs->dx = (node->dx / (node->too_long ? 2 : 1));
	wsubsecs->dy = (node->dy / (node->too_long ? 2 : 1));

	wsubsecs->bbox[0][0] = node->r.bounds.maxy;
	wsubsecs->bbox[0][1] = node->r.bounds.miny;
	wsubsecs->bbox[0][2] = node->r.bounds.minx;
	wsubsecs->bbox[0][3] = node->r.bounds.maxx;

	wsubsecs->bbox[1][0] = node->l.bounds.maxy;
	wsubsecs->bbox[1][1] = node->l.bounds.miny;
	wsubsecs->bbox[1][2] = node->l.bounds.minx;
	wsubsecs->bbox[1][3] = node->l.bounds.maxx;

	if (node->r.node)
		wsubsecs->children[0] = node->r.node->index;
	else if (node->r.subsec)
		wsubsecs->children[0] = node->r.subsec->index | NF_SUBSECTOR;
	else
		I_Error("GLBSP error: Bad right child in node %d", node->index);

	if (node->l.node)
		wsubsecs->children[1] = node->l.node->index;
	else if (node->l.subsec)
		wsubsecs->children[1] = node->l.subsec->index | NF_SUBSECTOR;
	else
		I_Error("GLBSP error: Bad left child in node %d", node->index);
}

UINT8 *PutNodes(bspnode_t *root)
{
	UINT8 *rawoutput = Z_Malloc(sizeof(mapnode_t) * num_nodes, PU_LEVEL, NULL);

	node_cur_index = 0;
	if (root)
		PutOneNode(root, rawoutput);

	if (node_cur_index != num_nodes)
		I_Error("GLBSP error: miscounted (%d != %d)", node_cur_index, num_nodes);

	if (node_cur_index > 32767)
		I_Error("GLBSP error: hit node limit");

	return rawoutput;
}

/* ----- whole-level routines --------------------------- */

//
// LoadLevel
//
void LoadLevel(void)
{
	GetVertices();
	GetSectors();
	GetSidedefs();
	GetLinedefs();

	PruneVertices();
	CalculateWallTips();
	DetectOverlappingLines();
}

//
// FreeLevel
//
void FreeLevel(void)
{
	FreeVertices();
	FreeSidedefs();
	FreeLinedefs();
	FreeSectors();
	FreeSegs();
	FreeSubsecs();
	FreeNodes();
	FreeWallTips();
}

//
// SaveLevel
//
void SaveLevel(bspnode_t *root_node)
{
	RoundOffBspTree();
	NormaliseBspTree();

	builtvertexes = (mapvertex_t *)PutVertices();
	builtsectors = (mapsector_t *)PutSectors();
	builtsides = (mapsidedef_t *)PutSidedefs();
	builtlines = (maplinedef_t *)PutLinedefs();
	builtsegs = (mapseg_t *)PutSegs();
	builtsubsectors = (mapsubsector_t *)PutSubsecs();
	builtnodes = (mapnode_t *)PutNodes(root_node);

	numbuiltvertexes = num_vertices;
	numbuiltsectors = num_sectors;
	numbuiltsides = num_sidedefs;
	numbuiltlines = num_linedefs;
	numbuiltsegs = num_segs;
	numbuiltsubsectors = num_subsecs;
	numbuiltnodes = num_nodes;
}
