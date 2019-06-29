//------------------------------------------------------------------------
// ANALYZE : Analyzing level structures
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

#include "bsp_analyze.h"
#include "bsp_blockmap.h"
#include "bsp_level.h"
#include "bsp_node.h"
#include "bsp_seg.h"
#include "bsp_structs.h"
#include "bsp_util.h"

#include "../i_system.h"

// stuff needed from bsp_level.c (this file closely related)
extern bspvertex_t  **lev_vertices;
extern bsplinedef_t **lev_linedefs;
extern bspsidedef_t **lev_sidedefs;
extern bspsector_t  **lev_sectors;

/* ----- analysis routines ----------------------------- */

void PruneVertices(void)
{
	int i;
	int new_num;
	int unused = 0;

	// scan all vertices
	for (i=0, new_num=0; i < num_vertices; i++)
	{
		bspvertex_t *V = lev_vertices[i];

		if (V->ref_count < 0)
			I_Error("GLBSP error: Vertex %d ref_count is %d", i, V->ref_count);

		if (V->ref_count == 0)
		{
			if (V->equiv == NULL)
			unused++;

			UtilFree(V);
			continue;
		}

		V->index = new_num;
		lev_vertices[new_num++] = V;
	}

	if (new_num < num_vertices)
		num_vertices = new_num;

	if (new_num == 0)
		I_Error("GLBSP error: Couldn't find any vertices");

	num_normal_vert = num_vertices;
}

static inline int LineVertexLowest(const bsplinedef_t *L)
{
	// returns the "lowest" vertex (normally the left-most, but if the
	// line is vertical, then the bottom-most) => 0 for start, 1 for end.

	return ((int)L->start->x < (int)L->end->x ||
		((int)L->start->x == (int)L->end->x &&
		(int)L->start->y <  (int)L->end->y)) ? 0 : 1;
}

static int LineStartCompare(const void *p1, const void *p2)
{
	int line1 = ((const int *) p1)[0];
	int line2 = ((const int *) p2)[0];

	bsplinedef_t *A = lev_linedefs[line1];
	bsplinedef_t *B = lev_linedefs[line2];

	bspvertex_t *C;
	bspvertex_t *D;

	if (line1 == line2)
		return 0;

	// determine left-most vertex of each line
	C = LineVertexLowest(A) ? A->end : A->start;
	D = LineVertexLowest(B) ? B->end : B->start;

	if ((int)C->x != (int)D->x)
		return (int)C->x - (int)D->x;

	return (int)C->y - (int)D->y;
}

static int LineEndCompare(const void *p1, const void *p2)
{
	int line1 = ((const int *) p1)[0];
	int line2 = ((const int *) p2)[0];

	bsplinedef_t *A = lev_linedefs[line1];
	bsplinedef_t *B = lev_linedefs[line2];

	bspvertex_t *C;
	bspvertex_t *D;

	if (line1 == line2)
		return 0;

	// determine right-most vertex of each line
	C = LineVertexLowest(A) ? A->start : A->end;
	D = LineVertexLowest(B) ? B->start : B->end;

	if ((int)C->x != (int)D->x)
		return (int)C->x - (int)D->x;

	return (int)C->y - (int)D->y;
}

void DetectOverlappingLines(void)
{
	// Algorithm:
	//   Sort all lines by left-most vertex.
	//   Overlapping lines will then be near each other in this set.
	//   Note: does not detect partially overlapping lines.

	int i;
	int *array = UtilCalloc(num_linedefs * sizeof(int));
	int count = 0;

	// sort array of indices
	for (i=0; i < num_linedefs; i++)
		array[i] = i;

	qsort(array, num_linedefs, sizeof(int), LineStartCompare);

	for (i=0; i < num_linedefs - 1; i++)
	{
		int j;

		for (j = i+1; j < num_linedefs; j++)
		{
			if (LineStartCompare(array + i, array + j) != 0)
				break;

			if (LineEndCompare(array + i, array + j) == 0)
			{
				bsplinedef_t *A = lev_linedefs[array[i]];
				bsplinedef_t *B = lev_linedefs[array[j]];

				// found an overlap !
				B->overlap = A->overlap ? A->overlap : A;

				count++;
			}
		}
	}

	UtilFree(array);
}

/* ----- vertex routines ------------------------------- */

static void VertexAddWallTip(bspvertex_t *vert, float_g dx, float_g dy, bspsector_t *left, bspsector_t *right)
{
	wall_tip_t *tip = NewWallTip();
	wall_tip_t *after;

	tip->angle = UtilComputeAngle(dx, dy);
	tip->left  = left;
	tip->right = right;

	// find the correct place (order is increasing angle)
	for (after=vert->tip_set; after && after->next; after=after->next)
		{ }

	while (after && tip->angle + ANG_EPSILON < after->angle)
		after = after->prev;

	// link it in
	tip->next = after ? after->next : vert->tip_set;
	tip->prev = after;

	if (after)
	{
		if (after->next)
			after->next->prev = tip;

		after->next = tip;
	}
	else
	{
		if (vert->tip_set)
			vert->tip_set->prev = tip;

		vert->tip_set = tip;
	}
}

void CalculateWallTips(void)
{
	int i;

	for (i=0; i < num_linedefs; i++)
	{
		bsplinedef_t *line = lev_linedefs[i];

		if (line->self_ref)
			continue;

		float_g x1 = line->start->x;
		float_g y1 = line->start->y;
		float_g x2 = line->end->x;
		float_g y2 = line->end->y;

		bspsector_t *left  = (line->left)  ? line->left->sector  : NULL;
		bspsector_t *right = (line->right) ? line->right->sector : NULL;

		VertexAddWallTip(line->start, x2-x1, y2-y1, left, right);
		VertexAddWallTip(line->end,   x1-x2, y1-y2, right, left);
	}
}

//
// NewVertexFromSplitSeg
//
bspvertex_t *NewVertexFromSplitSeg(bspseg_t *seg, float_g x, float_g y)
{
	bspvertex_t *vert = NewVertex();

	vert->x = x;
	vert->y = y;

	vert->ref_count = seg->partner ? 4 : 2;

	vert->index = num_normal_vert;
	num_normal_vert++;

	// compute wall_tip info
	VertexAddWallTip(vert, -seg->pdx, -seg->pdy, seg->sector,
	seg->partner ? seg->partner->sector : NULL);

	VertexAddWallTip(vert, seg->pdx, seg->pdy,
	seg->partner ? seg->partner->sector : NULL, seg->sector);

	// create a duplex vertex if needed
	vert->normal_dup = NewVertex();

	vert->normal_dup->x = x;
	vert->normal_dup->y = y;
	vert->normal_dup->ref_count = vert->ref_count;

	vert->normal_dup->index = num_normal_vert;
	num_normal_vert++;

	return vert;
}

//
// NewVertexDegenerate
//
bspvertex_t *NewVertexDegenerate(bspvertex_t *start, bspvertex_t *end)
{
	float_g dx = end->x - start->x;
	float_g dy = end->y - start->y;

	float_g dlen = UtilComputeDist(dx, dy);

	bspvertex_t *vert = NewVertex();

	vert->ref_count = start->ref_count;

	vert->index = num_normal_vert;
	num_normal_vert++;

	// compute new coordinates
	vert->x = start->x;
	vert->y = start->x;

	if (dlen == 0)
		I_Error("GLBSP error: bad delta!");

	dx /= dlen;
	dy /= dlen;

	while (I_ROUND(vert->x) == I_ROUND(start->x) &&
		I_ROUND(vert->y) == I_ROUND(start->y))
	{
		vert->x += dx;
		vert->y += dy;
	}

	return vert;
}

//
// VertexCheckOpen
//
bspsector_t * VertexCheckOpen(bspvertex_t *vert, float_g dx, float_g dy)
{
	wall_tip_t *tip;
	angle_g angle = UtilComputeAngle(dx, dy);

	// first check whether there's a wall_tip that lies in the exact
	// direction of the given direction (which is relative to the
	// vertex).
	for (tip=vert->tip_set; tip; tip=tip->next)
	{
		if (fabs(tip->angle - angle) < ANG_EPSILON ||
			fabs(tip->angle - angle) > (360.0 - ANG_EPSILON))
		{
			// yes, found one
			return NULL;
		}
	}

	// OK, now just find the first wall_tip whose angle is greater than
	// the angle we're interested in.  Therefore we'll be on the RIGHT
	// side of that wall_tip.
	for (tip=vert->tip_set; tip; tip=tip->next)
	{
		if (angle + ANG_EPSILON < tip->angle)
		{
			// found it
			return tip->right;
		}

		if (! tip->next)
		{
			// no more tips, thus we must be on the LEFT side of the tip
			// with the largest angle.
			return tip->left;
		}
	}

	I_Error("GLBSP error: Vertex %d has no tips !", vert->index);
	return false;
}
