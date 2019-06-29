//------------------------------------------------------------------------
// SEG : Choose the best Seg to use for a node line.
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
// To be able to divide the nodes down, this routine must decide which
// is the best Seg to use as a nodeline. It does this by selecting the
// line with least splits and has least difference of Segs on either
// side of it.
//
// Credit to Raphael Quinet and DEU, this routine is a copy of the
// nodeline picker used in DEU5beta. I am using this method because
// the method I originally used was not so good.
//
// Rewritten by Lee Killough to significantly improve performance,
// while not affecting results one bit in >99% of cases (some tiny
// differences due to roundoff error may occur, but they are
// insignificant).
//
// Rewritten again by Andrew Apted (-AJA-), 1999-2000.
//

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

typedef struct eval_info_s
{
	int cost;
	int splits;
	int iffy;
	int near_miss;

	int real_left;
	int real_right;
	int mini_left;
	int mini_right;
} eval_info_t;

static intersection_t *quick_alloc_cuts = NULL;

//
// NewIntersection
//
static intersection_t *NewIntersection(void)
{
	intersection_t *cut;

	if (quick_alloc_cuts)
	{
		cut = quick_alloc_cuts;
		quick_alloc_cuts = cut->next;
	}
	else
		cut = UtilCalloc(sizeof(intersection_t));

	return cut;
}

//
// FreeQuickAllocCuts
//
void FreeQuickAllocCuts(void)
{
	while (quick_alloc_cuts)
	{
		intersection_t *cut = quick_alloc_cuts;
		quick_alloc_cuts = cut->next;

		UtilFree(cut);
	}
}


//
// RecomputeSeg
//
// Fill in the fields 'angle', 'len', 'pdx', 'pdy', etc...
//
void RecomputeSeg(bspseg_t *seg)
{
	seg->psx = seg->start->x;
	seg->psy = seg->start->y;
	seg->pex = seg->end->x;
	seg->pey = seg->end->y;
	seg->pdx = seg->pex - seg->psx;
	seg->pdy = seg->pey - seg->psy;

	seg->p_length = UtilComputeDist(seg->pdx, seg->pdy);
	seg->p_angle  = UtilComputeAngle(seg->pdx, seg->pdy);

	if (seg->p_length <= 0)
		I_Error("GLBSP error: Seg %p has zero p_length", seg);

	seg->p_perp =  seg->psy * seg->pdx - seg->psx * seg->pdy;
	seg->p_para = -seg->psx * seg->pdx - seg->psy * seg->pdy;
}


//
// SplitSeg
//
// -AJA- Splits the given seg at the point (x,y).  The new seg is
//       returned.  The old seg is shortened (the original start
//       vertex is unchanged), whereas the new seg becomes the cut-off
//       tail (keeping the original end vertex).
//
//       If the seg has a partner, than that partner is also split.
//       NOTE WELL: the new piece of the partner seg is inserted into
//       the same list as the partner seg (and after it) -- thus ALL
//       segs (except the one we are currently splitting) must exist
//       on a singly-linked list somewhere.
//
// Note: we must update the count values of any superblock that
//       contains the seg (and/or partner), so that future processing
//       is not fucked up by incorrect counts.
//
static bspseg_t *SplitSeg(bspseg_t *old_seg, float_g x, float_g y)
{
	bspseg_t *new_seg;
	bspvertex_t *new_vert;

	// update superblock, if needed
	if (old_seg->block)
		SplitSegInSuper(old_seg->block, old_seg);

	new_vert = NewVertexFromSplitSeg(old_seg, x, y);
	new_seg  = NewSeg();

	// copy seg info
	new_seg[0] = old_seg[0];
	new_seg->next = NULL;

	old_seg->end = new_vert;
	RecomputeSeg(old_seg);

	new_seg->start = new_vert;
	RecomputeSeg(new_seg);

	// handle partners
	if (old_seg->partner)
	{
		// update superblock, if needed
		if (old_seg->partner->block)
		SplitSegInSuper(old_seg->partner->block, old_seg->partner);

		new_seg->partner = NewSeg();

		// copy seg info
		new_seg->partner[0] = old_seg->partner[0];

		// IMPORTANT: keep partner relationship valid.
		new_seg->partner->partner = new_seg;

		old_seg->partner->start = new_vert;
		RecomputeSeg(old_seg->partner);

		new_seg->partner->end = new_vert;
		RecomputeSeg(new_seg->partner);

		// link it into list
		old_seg->partner->next = new_seg->partner;
	}

	return new_seg;
}


//
// ComputeIntersection
//
// -AJA- In the quest for slime-trail annihilation :->, this routine
//       calculates the intersection location between the current seg
//       and the partitioning seg, and takes advantage of some common
//       situations like horizontal/vertical lines.
//
static inline void ComputeIntersection(bspseg_t *cur, bspseg_t *part, float_g perp_c, float_g perp_d, float_g *x, float_g *y)
{
	double ds;

	// horizontal partition against vertical seg
	if (part->pdy == 0 && cur->pdx == 0)
	{
		*x = cur->psx;
		*y = part->psy;
		return;
	}

	// vertical partition against horizontal seg
	if (part->pdx == 0 && cur->pdy == 0)
	{
		*x = part->psx;
		*y = cur->psy;
		return;
	}

	// 0 = start, 1 = end
	ds = perp_c / (perp_c - perp_d);

	if (cur->pdx == 0)
		*x = cur->psx;
	else
		*x = cur->psx + (cur->pdx * ds);

	if (cur->pdy == 0)
		*y = cur->psy;
	else
		*y = cur->psy + (cur->pdy * ds);
}


//
// AddIntersection
//
static void AddIntersection(intersection_t ** cut_list, bspvertex_t *vert, bspseg_t *part, boolean self_ref)
{
	intersection_t *cut;
	intersection_t *after;

	/* check if vertex already present */
	for (cut=(*cut_list); cut; cut=cut->next)
	{
		if (vert == cut->vertex)
			return;
	}

	/* create new intersection */
	cut = NewIntersection();

	cut->vertex = vert;
	cut->along_dist = UtilParallelDist(part, vert->x, vert->y);
	cut->self_ref = self_ref;

	cut->before = VertexCheckOpen(vert, -part->pdx, -part->pdy);
	cut->after  = VertexCheckOpen(vert,  part->pdx,  part->pdy);

	/* enqueue the new intersection into the list */
	for (after=(*cut_list); after && after->next; after=after->next);
	while (after && cut->along_dist < after->along_dist)
		after = after->prev;

	/* link it in */
	cut->next = after ? after->next : (*cut_list);
	cut->prev = after;

	if (after)
	{
		if (after->next) after->next->prev = cut;
		after->next = cut;
	}
	else
	{
		if (*cut_list) (*cut_list)->prev = cut;
		(*cut_list) = cut;
	}
}

//
// EvalPartitionInternal
//
// Returns true if a "bad seg" was found early.
//
static int EvalPartitionInternal(superblock_t *seg_list, bspseg_t *part, int best_cost, eval_info_t *info)
{
	bspseg_t *check;

	float_g qnty;
	float_g a, b, fa, fb;

	int num;
	int factor = 1;

	// -AJA- this is the heart of my superblock idea, it tests the
	//       _whole_ block against the partition line to quickly handle
	//       all the segs within it at once.  Only when the partition
	//       line intercepts the box do we need to go deeper into it.

	num = BoxOnLineSide(seg_list, part);

	if (num < 0)
	{
		// LEFT
		info->real_left += seg_list->real_num;
		info->mini_left += seg_list->mini_num;
		return false;
	}
	else if (num > 0)
	{
		// RIGHT
		info->real_right += seg_list->real_num;
		info->mini_right += seg_list->mini_num;
		return false;
	}

# define ADD_LEFT()  \
      do {  \
        if (check->linedef) info->real_left += 1;  \
        else                info->mini_left += 1;  \
      } while (0)

# define ADD_RIGHT()  \
      do {  \
        if (check->linedef) info->real_right += 1;  \
        else                info->mini_right += 1;  \
      } while (0)

	/* check partition against all Segs */
	for (check=seg_list->segs; check; check=check->next)
	{
		// This is the heart of my pruning idea - it catches
		// bad segs early on. Killough

		if (info->cost > best_cost)
			return true;

		/* get state of lines' relation to each other */
		if (check->source_line == part->source_line)
			a = b = fa = fb = 0;
		else
		{
			a = UtilPerpDist(part, check->psx, check->psy);
			b = UtilPerpDist(part, check->pex, check->pey);

			fa = fabs(a);
			fb = fabs(b);
		}

		/* check for being on the same line */
		if (fa <= DIST_EPSILON && fb <= DIST_EPSILON)
		{
			// this seg runs along the same line as the partition.  Check
			// whether it goes in the same direction or the opposite.
			if (check->pdx*part->pdx + check->pdy*part->pdy < 0)
				ADD_LEFT();
			else
				ADD_RIGHT();
			continue;
		}

		/* check for right side */
		if (a > -DIST_EPSILON && b > -DIST_EPSILON)
		{
			ADD_RIGHT();

			/* check for a near miss */
			if ((a >= IFFY_LEN && b >= IFFY_LEN) ||
			(a <= DIST_EPSILON && b >= IFFY_LEN) ||
			(b <= DIST_EPSILON && a >= IFFY_LEN)) continue;

			info->near_miss++;

			// -AJA- near misses are bad, since they have the potential to
			//       cause really short minisegs to be created in future
			//       processing.  Thus the closer the near miss, the higher
			//       the cost.

			if (a <= DIST_EPSILON || b <= DIST_EPSILON)
				qnty = IFFY_LEN / MAX(a, b);
			else
				qnty = IFFY_LEN / MIN(a, b);

			info->cost += (int) (100 * factor * (qnty * qnty - 1.0));
			continue;
		}

		/* check for left side */
		if (a < DIST_EPSILON && b < DIST_EPSILON)
		{
			ADD_LEFT();

			/* check for a near miss */
			if ((a <= -IFFY_LEN && b <= -IFFY_LEN) ||
			(a >= -DIST_EPSILON && b <= -IFFY_LEN) ||
			(b >= -DIST_EPSILON && a <= -IFFY_LEN)) continue;

			info->near_miss++;

			// the closer the miss, the higher the cost (see note above)
			if (a >= -DIST_EPSILON || b >= -DIST_EPSILON)
				qnty = IFFY_LEN / -MIN(a, b);
			else
				qnty = IFFY_LEN / -MAX(a, b);

			info->cost += (int) (70 * factor * (qnty * qnty - 1.0));
			continue;
		}

		// When we reach here, we have a and b non-zero and opposite sign,
		// hence this seg will be split by the partition line.

		info->splits++;
		info->cost += 100 * factor;

		// -AJA- check if the split point is very close to one end, which
		//       is quite an undesirable situation (producing really short
		//       segs).  This is perhaps _one_ source of those darn slime
		//       trails.  Hence the name "IFFY segs", and a rather hefty
		//       surcharge :->.

		if (fa < IFFY_LEN || fb < IFFY_LEN)
		{
			info->iffy++;

			// the closer to the end, the higher the cost
			qnty = IFFY_LEN / MIN(fa, fb);
			info->cost += (int) (140 * factor * (qnty * qnty - 1.0));
		}
	}

	/* handle sub-blocks recursively */
	for (num=0; num < 2; num++)
	{
		if (!seg_list->subs[num])
			continue;

		if (EvalPartitionInternal(seg_list->subs[num], part, best_cost, info))
			return true;
	}

	/* no "bad seg" was found */
	return false;
}

//
// EvalPartition
//
// -AJA- Evaluate a partition seg & determine the cost, taking into
//       account the number of splits, and the difference between
//       left & right.
//
// Returns the computed cost, or a negative value if the seg should be
// skipped altogether.
//
static int EvalPartition(superblock_t *seg_list, bspseg_t *part, int best_cost)
{
	eval_info_t info;

	/* initialise info structure */
	info.cost   = 0;
	info.splits = 0;
	info.iffy   = 0;
	info.near_miss  = 0;

	info.real_left  = 0;
	info.real_right = 0;
	info.mini_left  = 0;
	info.mini_right = 0;

	if (EvalPartitionInternal(seg_list, part, best_cost, &info))
		return -1;

	/* make sure there is at least one real seg on each side */
	if (info.real_left == 0 || info.real_right == 0)
		return -1;

	/* increase cost by the difference between left & right */
	info.cost += 100 * ABS(info.real_left - info.real_right);

	// -AJA- allow miniseg counts to affect the outcome, but only to a
	//       lesser degree than real segs.

	info.cost += 50 * ABS(info.mini_left - info.mini_right);

	// -AJA- Another little twist, here we show a slight preference for
	//       partition lines that lie either purely horizontally or
	//       purely vertically.

	if (part->pdx != 0 && part->pdy != 0)
		info.cost += 25;

	return info.cost;
}

/* returns false if cancelled */
static int PickNodeInternal(superblock_t *part_list, superblock_t *seg_list, bspseg_t ** best, int *best_cost)
{
	bspseg_t *part;

	int num;
	int cost;

	/* use each Seg as partition */
	for (part = part_list->segs; part; part = part->next)
	{
		/* ignore minisegs as partition candidates */
		if (! part->linedef)
			continue;

		cost = EvalPartition(seg_list, part, *best_cost);

		/* seg unsuitable or too costly ? */
		if (cost < 0 || cost >= *best_cost)
			continue;

		/* we have a new better choice */
		(*best_cost) = cost;

		/* remember which Seg */
		(*best) = part;
	}

	/* recursively handle sub-blocks */
	for (num = 0; num < 2; num++)
	{
		if (part_list->subs[num])
			PickNodeInternal(part_list->subs[num], seg_list, best, best_cost);
	}

	return true;
}

//
// PickNode
//
// Find the best seg in the seg_list to use as a partition line.
//
bspseg_t *PickNode(superblock_t *seg_list, int depth)
{
	bspseg_t *best = NULL;
	int best_cost = INT_MAX;

	if (!PickNodeInternal(seg_list, seg_list, &best, &best_cost))
	{
		/* hack here : BuildNodes will detect the cancellation */
		return NULL;
	}

	/* all finished, return best Seg */
	return best;
}


//
// DivideOneSeg
//
// Apply the partition line to the given seg, taking the necessary
// action (moving it into either the left list, right list, or
// splitting it).
//
// -AJA- I have rewritten this routine based on the EvalPartition
//       routine above (which I've also reworked, heavily).  I think
//       it is important that both these routines follow the exact
//       same logic when determining which segs should go left, right
//       or be split.
//
void DivideOneSeg(bspseg_t *cur, bspseg_t *part, superblock_t *left_list, superblock_t *right_list, intersection_t ** cut_list)
{
	bspseg_t *new_seg;

	float_g x, y;

	/* get state of lines' relation to each other */
	float_g a = UtilPerpDist(part, cur->psx, cur->psy);
	float_g b = UtilPerpDist(part, cur->pex, cur->pey);

	boolean self_ref = cur->linedef ? cur->linedef->self_ref : false;

	if (cur->source_line == part->source_line)
		a = b = 0;

	/* check for being on the same line */
	if (fabs(a) <= DIST_EPSILON && fabs(b) <= DIST_EPSILON)
	{
		AddIntersection(cut_list, cur->start, part, self_ref);
		AddIntersection(cut_list, cur->end,   part, self_ref);

		// this seg runs along the same line as the partition.  check
		// whether it goes in the same direction or the opposite.

		if (cur->pdx*part->pdx + cur->pdy*part->pdy < 0)
			AddSegToSuper(left_list, cur);
		else
			AddSegToSuper(right_list, cur);

		return;
	}

	/* check for right side */
	if (a > -DIST_EPSILON && b > -DIST_EPSILON)
	{
		if (a < DIST_EPSILON)
			AddIntersection(cut_list, cur->start, part, self_ref);
		else if (b < DIST_EPSILON)
			AddIntersection(cut_list, cur->end, part, self_ref);

		AddSegToSuper(right_list, cur);
		return;
	}

	/* check for left side */
	if (a < DIST_EPSILON && b < DIST_EPSILON)
	{
		if (a > -DIST_EPSILON)
			AddIntersection(cut_list, cur->start, part, self_ref);
		else if (b > -DIST_EPSILON)
			AddIntersection(cut_list, cur->end, part, self_ref);

		AddSegToSuper(left_list, cur);
		return;
	}

	// when we reach here, we have a and b non-zero and opposite sign,
	// hence this seg will be split by the partition line.

	ComputeIntersection(cur, part, a, b, &x, &y);

	new_seg = SplitSeg(cur, x, y);

	AddIntersection(cut_list, cur->end, part, self_ref);

	if (a < 0)
	{
		AddSegToSuper(left_list,  cur);
		AddSegToSuper(right_list, new_seg);
	}
	else
	{
		AddSegToSuper(right_list, cur);
		AddSegToSuper(left_list,  new_seg);
	}
}

//
// SeparateSegs
//
void SeparateSegs(superblock_t *seg_list, bspseg_t *part, superblock_t *lefts, superblock_t *rights, intersection_t ** cut_list)
{
	int num;

	while (seg_list->segs)
	{
		bspseg_t *cur = seg_list->segs;
		seg_list->segs = cur->next;

		cur->block = NULL;

		DivideOneSeg(cur, part, lefts, rights, cut_list);
	}

	// recursively handle sub-blocks
	for (num=0; num < 2; num++)
	{
		superblock_t *A = seg_list->subs[num];

		if (A)
		{
			SeparateSegs(A, part, lefts, rights, cut_list);

			if (A->real_num + A->mini_num > 0)
				I_Error("GLBSP error: SeparateSegs: child %d not empty !", num);

			FreeSuper(A);
			seg_list->subs[num] = NULL;
		}
	}

	seg_list->real_num = seg_list->mini_num = 0;
}


static void FindLimitInternal(superblock_t *block, bspbbox_t *bbox)
{
	bspseg_t *cur;
	int num;

	for (cur = block->segs; cur; cur = cur->next)
	{
		float_g x1 = cur->start->x;
		float_g y1 = cur->start->y;
		float_g x2 = cur->end->x;
		float_g y2 = cur->end->y;

		int lx = (int) floor(MIN(x1, x2));
		int ly = (int) floor(MIN(y1, y2));
		int hx = (int) ceil(MAX(x1, x2));
		int hy = (int) ceil(MAX(y1, y2));

		if (lx < bbox->minx) bbox->minx = lx;
		if (ly < bbox->miny) bbox->miny = ly;
		if (hx > bbox->maxx) bbox->maxx = hx;
		if (hy > bbox->maxy) bbox->maxy = hy;
	}

	// recursive handle sub-blocks
	for (num=0; num < 2; num++)
	{
		if (block->subs[num])
			FindLimitInternal(block->subs[num], bbox);
	}
}

//
// FindLimits
//
// Find the limits from a list of segs, by stepping through the segs
// and comparing the vertices at both ends.
//
void FindLimits(superblock_t *seg_list, bspbbox_t *bbox)
{
	bbox->minx = bbox->miny = SHRT_MAX;
	bbox->maxx = bbox->maxy = SHRT_MIN;

	FindLimitInternal(seg_list, bbox);
}

//
// AddMinisegs
//
void AddMinisegs(bspseg_t *part, superblock_t *left_list, superblock_t *right_list, intersection_t *cut_list)
{
	intersection_t *cur, *next;
	bspseg_t *seg, *buddy;

	if (!cut_list)
		return;

	// STEP 1: fix problems the intersection list...
	cur  = cut_list;
	next = cur->next;

	while (cur && next)
	{
		float_g len = next->along_dist - cur->along_dist;

		if (len < -0.1)
			I_Error("GLBSP error: Bad order in intersect list: %1.3f > %1.3f\n", cur->along_dist, next->along_dist);

		if (len > 0.2)
		{
			cur  = next;
			next = cur->next;
			continue;
		}

		// merge the two intersections into one
		if (cur->self_ref && !next->self_ref)
		{
			if (cur->before && next->before)
				cur->before = next->before;

			if (cur->after && next->after)
				cur->after = next->after;

			cur->self_ref = false;
		}

		if (!cur->before && next->before)
			cur->before = next->before;

		if (!cur->after && next->after)
			cur->after = next->after;

		// free the unused cut
		cur->next = next->next;

		next->next = quick_alloc_cuts;
		quick_alloc_cuts = next;

		next = cur->next;
	}

	// STEP 2: find connections in the intersection list...
	for (cur = cut_list; cur && cur->next; cur = cur->next)
	{
		next = cur->next;

		if (!cur->after && !next->before)
			continue;

		// check for some nasty OPEN/CLOSED or CLOSED/OPEN cases
		if (cur->after && !next->before)
		{
			if (!cur->self_ref && !cur->after->warned_unclosed)
			cur->after->warned_unclosed = 1;
			continue;
		}
		else if (!cur->after && next->before)
		{
			if (!next->self_ref && !next->before->warned_unclosed)
			next->before->warned_unclosed = 1;
			continue;
		}

		// righteo, here we have definite open space.
		// do a sanity check on the sectors (just for good measure).

		if (cur->after != next->before)
		{
			// choose the non-self-referencing sector when we can
			if (cur->self_ref && !next->self_ref)
				cur->after = next->before;
		}

		// create the miniseg pair
		seg = NewSeg();
		buddy = NewSeg();

		seg->partner = buddy;
		buddy->partner = seg;

		seg->start = cur->vertex;
		seg->end   = next->vertex;

		buddy->start = next->vertex;
		buddy->end   = cur->vertex;

		// leave 'linedef' field as NULL.
		// leave 'side' as zero too (not needed for minisegs).

		seg->sector = buddy->sector = cur->after;

		seg->index = buddy->index = -1;

		seg->source_line = buddy->source_line = part->linedef;

		RecomputeSeg(seg);
		RecomputeSeg(buddy);

		// add the new segs to the appropriate lists
		AddSegToSuper(right_list, seg);
		AddSegToSuper(left_list, buddy);
	}

	// free intersection structures into quick-alloc list
	while (cut_list)
	{
		cur = cut_list;
		cut_list = cur->next;

		cur->next = quick_alloc_cuts;
		quick_alloc_cuts = cur;
	}
}
