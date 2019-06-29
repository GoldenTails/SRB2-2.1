//------------------------------------------------------------------------
// NODE : Recursively create nodes and return the pointers.
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
// Split a list of segs into two using the method described at bottom
// of the file, this was taken from OBJECTS.C in the DEU5beta source.
//
// This is done by scanning all of the segs and finding the one that
// does the least splitting and has the least difference in numbers of
// segs on either side.
//
// If the ones on the left side make a SSector, then create another SSector
// else put the segs into lefts list.
// If the ones on the right side make a SSector, then create another SSector
// else put the segs into rights list.
//
// Rewritten by Andrew Apted (-AJA-), 1999-2000.
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

static superblock_t *quick_alloc_supers = NULL;

//
// PointOnLineSide
//
// Returns -1 for left, +1 for right, or 0 for intersect.
//
static int PointOnLineSide(bspseg_t *part, float_g x, float_g y)
{
	float_g perp = UtilPerpDist(part, x, y);

	if (fabs(perp) <= DIST_EPSILON)
		return 0;

	return (perp < 0) ? -1 : +1;
}

//
// BoxOnLineSide
//
int BoxOnLineSide(superblock_t *box, bspseg_t *part)
{
	float_g x1 = (float_g)box->x1 - IFFY_LEN * 1.5;
	float_g y1 = (float_g)box->y1 - IFFY_LEN * 1.5;
	float_g x2 = (float_g)box->x2 + IFFY_LEN * 1.5;
	float_g y2 = (float_g)box->y2 + IFFY_LEN * 1.5;

	int p1, p2;

	// handle simple cases (vertical & horizontal lines)
	if (part->pdx == 0)
	{
		p1 = (x1 > part->psx) ? +1 : -1;
		p2 = (x2 > part->psx) ? +1 : -1;

		if (part->pdy < 0)
		{
			p1 = -p1;
			p2 = -p2;
		}
	}
	else if (part->pdy == 0)
	{
		p1 = (y1 < part->psy) ? +1 : -1;
		p2 = (y2 < part->psy) ? +1 : -1;

		if (part->pdx < 0)
		{
			p1 = -p1;
			p2 = -p2;
		}
	}
	// now handle the cases of positive and negative slope
	else if (part->pdx * part->pdy > 0)
	{
		p1 = PointOnLineSide(part, x1, y2);
		p2 = PointOnLineSide(part, x2, y1);
	}
	else  // NEGATIVE
	{
		p1 = PointOnLineSide(part, x1, y1);
		p2 = PointOnLineSide(part, x2, y2);
	}

	if (p1 == p2)
		return p1;

	return 0;
}


/* ----- super block routines ------------------------------------ */

//
// NewSuperBlock
//
static superblock_t *NewSuperBlock(void)
{
	superblock_t *block;

	if (quick_alloc_supers == NULL)
		return UtilCalloc(sizeof(superblock_t));

	block = quick_alloc_supers;
	quick_alloc_supers = block->subs[0];

	// clear out any old rubbish
	memset(block, 0, sizeof(superblock_t));

	return block;
}

//
// FreeQuickAllocSupers
//
void FreeQuickAllocSupers(void)
{
	while (quick_alloc_supers)
	{
		superblock_t *block = quick_alloc_supers;
		quick_alloc_supers = block->subs[0];

		UtilFree(block);
	}
}

//
// FreeSuper
//
void FreeSuper(superblock_t *block)
{
	int num;

	if (block->segs)
		block->segs = NULL;

	// recursively handle sub-blocks
	for (num=0; num < 2; num++)
	{
		if (block->subs[num])
			FreeSuper(block->subs[num]);
	}

	// add block to quick-alloc list. Note that subs[0] is used for
	// linking the blocks together.
	block->subs[0] = quick_alloc_supers;
	quick_alloc_supers = block;
}

//
// AddSegToSuper
//
void AddSegToSuper(superblock_t *block, bspseg_t *seg)
{
	for (;;)
	{
		int p1, p2;
		int child;

		int x_mid = (block->x1 + block->x2) / 2;
		int y_mid = (block->y1 + block->y2) / 2;

		superblock_t *sub;

		// update seg counts
		if (seg->linedef)
			block->real_num++;
		else
			block->mini_num++;

		if (SUPER_IS_LEAF(block))
		{
			// block is a leaf -- no subdivision possible

			seg->next = block->segs;
			seg->block = block;

			block->segs = seg;
			return;
		}

		if (block->x2 - block->x1 >= block->y2 - block->y1)
		{
			// block is wider than it is high, or square

			p1 = seg->start->x >= x_mid;
			p2 = seg->end->x   >= x_mid;
		}
		else
		{
			// block is higher than it is wide

			p1 = seg->start->y >= y_mid;
			p2 = seg->end->y   >= y_mid;
		}

		if (p1 && p2)
			child = 1;
		else if (!p1 && !p2)
			child = 0;
		else
		{
			// line crosses midpoint -- link it in and return

			seg->next = block->segs;
			seg->block = block;

			block->segs = seg;
			return;
		}

		// OK, the seg lies in one half of this block.  Create the block
		// if it doesn't already exist, and loop back to add the seg.

		if (!block->subs[child])
		{
			block->subs[child] = sub = NewSuperBlock();
			sub->parent = block;

			if (block->x2 - block->x1 >= block->y2 - block->y1)
			{
				sub->x1 = child ? x_mid : block->x1;
				sub->y1 = block->y1;

				sub->x2 = child ? block->x2 : x_mid;
				sub->y2 = block->y2;
			}
			else
			{
				sub->x1 = block->x1;
				sub->y1 = child ? y_mid : block->y1;

				sub->x2 = block->x2;
				sub->y2 = child ? block->y2 : y_mid;
			}
		}

		block = block->subs[child];
	}
}

//
// SplitSegInSuper
//
void SplitSegInSuper(superblock_t *block, bspseg_t *seg)
{
	do
	{
		// update seg counts
		if (seg->linedef)
			block->real_num++;
		else
			block->mini_num++;

		block = block->parent;
	}
	while (block != NULL);
}

static bspseg_t *CreateOneSeg(bsplinedef_t *line, bspvertex_t *start, bspvertex_t *end, bspsidedef_t *side, int side_num)
{
	bspseg_t *seg = NewSeg();

	seg->start   = start;
	seg->end     = end;
	seg->linedef = line;
	seg->side    = side_num;
	seg->sector  = side->sector;
	seg->partner = NULL;

	seg->source_line = seg->linedef;
	seg->index = -1;

	RecomputeSeg(seg);

	return seg;
}

//
// CreateSegs
//
// Initially create all segs, one for each linedef.  Must be called
// _after_ InitBlockmap().
//
superblock_t *CreateSegs(void)
{
	int i;
	int bw, bh;

	bspseg_t *left, *right;
	superblock_t *block = NewSuperBlock();

	GetBlockmapBounds(&block->x1, &block->y1, &bw, &bh);

	block->x2 = block->x1 + 128 * UtilRoundPOW2(bw);
	block->y2 = block->y1 + 128 * UtilRoundPOW2(bh);

	// step through linedefs and get side numbers
	for (i=0; i < num_linedefs; i++)
	{
		bsplinedef_t *line = LookupLinedef(i);

		right = NULL;

		// ignore zero-length lines
		if (line->zero_len) continue;

		// ignore overlapping lines
		if (line->overlap) continue;

		// ignore self-referencing lines
		if (line->self_ref) continue;

		if (line->right)
		{
			right = CreateOneSeg(line, line->start, line->end, line->right, 0);
			AddSegToSuper(block, right);
		}

		if (line->left)
		{
			left = CreateOneSeg(line, line->end, line->start, line->left, 1);
			AddSegToSuper(block, left);

			if (right)
			{
				left->partner = right;
				right->partner = left;
			}
		}
		else if (line->two_sided)
			line->two_sided = 0;
	}

	return block;
}

//
// DetermineMiddle
//
static void DetermineMiddle(bspsubsec_t *sub)
{
	bspseg_t *cur;

	float_g mid_x=0, mid_y=0;
	int total=0;

	// compute middle coordinates
	for (cur=sub->seg_list; cur; cur=cur->next)
	{
		mid_x += cur->start->x + cur->end->x;
		mid_y += cur->start->y + cur->end->y;

		total += 2;
	}

	sub->mid_x = mid_x / total;
	sub->mid_y = mid_y / total;
}

//
// ClockwiseOrder
//
// -AJA- Put the list of segs into clockwise order.
//       Uses the now famous "double bubble" sorter :).
//
static void ClockwiseOrder(bspsubsec_t *sub)
{
	bspseg_t *cur;
	bspseg_t **array;
	bspseg_t *seg_buffer[32];

	int i;
	int total = 0;

	int first = 0;
	int score = -1;

	// count segs and create an array to manipulate them
	for (cur = sub->seg_list; cur; cur = cur->next)
		total++;

	// use local array if small enough
	if (total <= 32)
		array = seg_buffer;
	else
		array = UtilCalloc(total * sizeof(bspseg_t *));

	for (cur = sub->seg_list, i = 0; cur; cur = cur->next, i++)
		array[i] = cur;

	if (i != total)
		I_Error("GLBSP error: ClockwiseOrder miscounted");

	// sort segs by angle (from the middle point to the start vertex).
	// The desired order (clockwise) means descending angles.

	i = 0;

	while (i+1 < total)
	{
		bspseg_t *A = array[i];
		bspseg_t *B = array[i+1];

		angle_g angle1, angle2;

		angle1 = UtilComputeAngle(A->start->x - sub->mid_x, A->start->y - sub->mid_y);
		angle2 = UtilComputeAngle(B->start->x - sub->mid_x, B->start->y - sub->mid_y);

		if (angle1 + ANG_EPSILON < angle2)
		{
			// swap 'em
			array[i] = B;
			array[i+1] = A;

			// bubble down
			if (i > 0) i--;
		}
		else i++; // bubble up
	}

	// choose the seg that will be first (the game engine will typically use
	// that to determine the sector).  In particular, we don't like self
	// referencing linedefs (they are often used for deep-water effects).
	for (i=0; i < total; i++)
	{
		int cur_score = 3;

		if (! array[i]->linedef)
			cur_score = 0;
		else if (array[i]->linedef->window_effect)
			cur_score = 1;
		else if (array[i]->linedef->self_ref)
			cur_score = 2;

		if (cur_score > score)
		{
			first = i;
			score = cur_score;
		}
	}

	// transfer sorted array back into sub
	sub->seg_list = NULL;

	for (i=total-1; i >= 0; i--)
	{
		int j = (i + first) % total;

		array[j]->next = sub->seg_list;
		sub->seg_list  = array[j];
	}

	if (total > 32)
		UtilFree(array);
}

//
// RenumberSubsecSegs
//
static void RenumberSubsecSegs(bspsubsec_t *sub)
{
	bspseg_t *cur;

	sub->seg_count = 0;

	for (cur=sub->seg_list; cur; cur=cur->next)
	{
		cur->index = num_complete_seg;
		num_complete_seg++;
		sub->seg_count++;
	}
}


static void CreateSubsecWorker(bspsubsec_t *sub, superblock_t *block)
{
	int num;

	while (block->segs)
	{
		// unlink first seg from block
		bspseg_t *cur = block->segs;
		block->segs = cur->next;

		// link it into head of the subsector's list
		cur->next = sub->seg_list;
		cur->block = NULL;

		sub->seg_list = cur;
	}

	// recursively handle sub-blocks
	for (num=0; num < 2; num++)
	{
		superblock_t *A = block->subs[num];

		if (A)
		{
			CreateSubsecWorker(sub, A);

			if (A->real_num + A->mini_num > 0)
				I_Error("GLBSP error: CreateSubsec: child %d not empty !", num);

			FreeSuper(A);
			block->subs[num] = NULL;
		}
	}

	block->real_num = block->mini_num = 0;
}

//
// CreateSubsec
//
// Create a subsector from a list of segs.
//
static bspsubsec_t *CreateSubsec(superblock_t *seg_list)
{
	bspsubsec_t *sub = NewSubsec();

	// compute subsector's index
	sub->index = num_subsecs - 1;

	// copy segs into subsector
	CreateSubsecWorker(sub, seg_list);

	DetermineMiddle(sub);

	return sub;
}

//
// BuildNodes
//

boolean BuildNodes(superblock_t *seg_list, bspnode_t ** N, bspsubsec_t ** S, int depth)
{
	bspnode_t *node;
	bspseg_t *best;
	boolean ret;

	superblock_t *rights;
	superblock_t *lefts;

	intersection_t *cut_list;

	*N = NULL;
	*S = NULL;

	/* pick best node to use. None indicates convexicity */
	best = PickNode(seg_list, depth);

	if (best == NULL)
	{
		*S = CreateSubsec(seg_list);
		return true;
	}

	/* create left and right super blocks */
	lefts  = (superblock_t *) NewSuperBlock();
	rights = (superblock_t *) NewSuperBlock();

	lefts->x1 = rights->x1 = seg_list->x1;
	lefts->y1 = rights->y1 = seg_list->y1;
	lefts->x2 = rights->x2 = seg_list->x2;
	lefts->y2 = rights->y2 = seg_list->y2;

	/* divide the segs into two lists: left & right */
	cut_list = NULL;

	SeparateSegs(seg_list, best, lefts, rights, &cut_list);

	/* sanity checks... */
	if (rights->real_num + rights->mini_num == 0)
		I_Error("GLBSP error: Separated seg-list has no RIGHT side");

	if (lefts->real_num + lefts->mini_num == 0)
		I_Error("GLBSP error: Separated seg-list has no LEFT side");

	AddMinisegs(best, lefts, rights, cut_list);

	*N = node = NewNode();

	assert(best->linedef);

	if (best->side == 0)
	{
		node->x  = best->linedef->start->x;
		node->y  = best->linedef->start->y;
		node->dx = best->linedef->end->x - node->x;
		node->dy = best->linedef->end->y - node->y;
	}
	else  /* left side */
	{
		node->x  = best->linedef->end->x;
		node->y  = best->linedef->end->y;
		node->dx = best->linedef->start->x - node->x;
		node->dy = best->linedef->start->y - node->y;
	}

	/* check for really long partition (overflows dx,dy in NODES) */
	if (best->p_length >= 30000)
		node->too_long = 1;

	/* find limits of vertices */
	FindLimits(lefts,  &node->l.bounds);
	FindLimits(rights, &node->r.bounds);

	ret = BuildNodes(lefts,  &node->l.node, &node->l.subsec, depth+1);
	FreeSuper(lefts);

	if (ret != true)
	{
		FreeSuper(rights);
		return ret;
	}

	ret = BuildNodes(rights, &node->r.node, &node->r.subsec, depth+1);
	FreeSuper(rights);

	return true;
}

//
// ClockwiseBspTree
//
void ClockwiseBspTree(void)
{
	int i;

	for (i=0; i < num_subsecs; i++)
	{
		bspsubsec_t *sub = LookupSubsec(i);

		ClockwiseOrder(sub);
		RenumberSubsecSegs(sub);
	}
}

static void NormaliseSubsector(bspsubsec_t *sub)
{
	bspseg_t *new_head = NULL;
	bspseg_t *new_tail = NULL;

	while (sub->seg_list)
	{
		// remove head
		bspseg_t *cur = sub->seg_list;
		sub->seg_list = cur->next;

		// only add non-minisegs to new list
		if (cur->linedef)
		{
			cur->next = NULL;

			if (new_tail)
				new_tail->next = cur;
			else
				new_head = cur;

			new_tail = cur;

			// this updated later
			cur->index = -1;
		}
		else
		{
			// set index to a really high value, so that SortSegs() will
			// move all the minisegs to the top of the seg array.
			cur->index = 1<<24;
		}
	}

	if (new_head == NULL)
		I_Error("GLBSP error: Subsector %d normalised to being EMPTY", sub->index);

	sub->seg_list = new_head;
}

//
// NormaliseBspTree
//
void NormaliseBspTree(void)
{
	int i;

	// unlink all minisegs from each subsector:
	num_complete_seg = 0;

	for (i=0; i < num_subsecs; i++)
	{
		bspsubsec_t *sub = LookupSubsec(i);

		NormaliseSubsector(sub);
		RenumberSubsecSegs(sub);
	}
}

static void RoundOffSubsector(bspsubsec_t *sub)
{
  bspseg_t *new_head = NULL;
  bspseg_t *new_tail = NULL;

  bspseg_t *cur;
  bspseg_t *last_real_degen = NULL;

  int real_total  = 0;
  int degen_total = 0;

  // do an initial pass, just counting the degenerates
  for (cur=sub->seg_list; cur; cur=cur->next)
  {
    // handle the duplex vertices
    if (cur->start->normal_dup)
      cur->start = cur->start->normal_dup;

    if (cur->end->normal_dup)
      cur->end = cur->end->normal_dup;

    // is the seg degenerate ?
    if (I_ROUND(cur->start->x) == I_ROUND(cur->end->x) &&
        I_ROUND(cur->start->y) == I_ROUND(cur->end->y))
    {
      cur->degenerate = 1;

      if (cur->linedef)
        last_real_degen = cur;

      degen_total++;
      continue;
    }

    if (cur->linedef)
      real_total++;
  }

  // handle the (hopefully rare) case where all of the real segs
  // became degenerate.
  if (real_total == 0)
  {
    if (last_real_degen == NULL)
      I_Error("GLBSP error: Subsector %d rounded off with NO real segs", sub->index);

    // create a new vertex for this baby
    last_real_degen->end = NewVertexDegenerate(last_real_degen->start, last_real_degen->end);

    last_real_degen->degenerate = 0;
  }

  // second pass, remove the blighters...
  while (sub->seg_list)
  {
    // remove head
    cur = sub->seg_list;
    sub->seg_list = cur->next;

    if (! cur->degenerate)
    {
      cur->next = NULL;

      if (new_tail)
        new_tail->next = cur;
      else
        new_head = cur;

      new_tail = cur;

      // this updated later
      cur->index = -1;
    }
    else
    {
      // set index to a really high value, so that SortSegs() will
      // move all the minisegs to the top of the seg array.
      cur->index = 1<<24;
    }
  }

  if (new_head == NULL)
    I_Error("GLBSP error: Subsector %d rounded off to being EMPTY", sub->index);

  sub->seg_list = new_head;
}

//
// RoundOffBspTree
//
void RoundOffBspTree(void)
{
	int i;

	num_complete_seg = 0;

	for (i=0; i < num_subsecs; i++)
	{
		bspsubsec_t *sub = LookupSubsec(i);
		RoundOffSubsector(sub);
		RenumberSubsecSegs(sub);
	}
}

//---------------------------------------------------------------------------
//
//    This message has been taken, complete, from OBJECTS.C in DEU5beta
//    source.  It outlines the method used here to pick the nodelines.
//
// IF YOU ARE WRITING A DOOM EDITOR, PLEASE READ THIS:
//
// I spent a lot of time writing the Nodes builder.  There are some bugs in
// it, but most of the code is OK.  If you steal any ideas from this program,
// put a prominent message in your own editor to make it CLEAR that some
// original ideas were taken from DEU.  Thanks.
//
// While everyone was talking about LineDefs, I had the idea of taking only
// the Segs into account, and creating the Segs directly from the SideDefs.
// Also, dividing the list of Segs in two after each call to CreateNodes makes
// the algorithm faster.  I use several other tricks, such as looking at the
// two ends of a Seg to see on which side of the nodeline it lies or if it
// should be split in two.  I took me a lot of time and efforts to do this.
//
// I give this algorithm to whoever wants to use it, but with this condition:
// if your program uses some of the ideas from DEU or the whole algorithm, you
// MUST tell it to the user.  And if you post a message with all or parts of
// this algorithm in it, please post this notice also.  I don't want to speak
// legalese; I hope that you understand me...  I kindly give the sources of my
// program to you: please be kind with me...
//
// If you need more information about this, here is my E-mail address:
// Raphael.Quinet@eed.ericsson.se (Raphael Quinet).
//
// Short description of the algorithm:
//   1 - Create one Seg for each SideDef: pick each LineDef in turn.  If it
//       has a "first" SideDef, then create a normal Seg.  If it has a
//       "second" SideDef, then create a flipped Seg.
//   2 - Call CreateNodes with the current list of Segs.  The list of Segs is
//       the only argument to CreateNodes.
//   3 - Save the Nodes, Segs and SSectors to disk.  Start with the leaves of
//       the Nodes tree and continue up to the root (last Node).
//
// CreateNodes does the following:
//   1 - Pick a nodeline amongst the Segs (minimize the number of splits and
//       keep the tree as balanced as possible).
//   2 - Move all Segs on the right of the nodeline in a list (segs1) and do
//       the same for all Segs on the left of the nodeline (in segs2).
//   3 - If the first list (segs1) contains references to more than one
//       Sector or if the angle between two adjacent Segs is greater than
//       180 degrees, then call CreateNodes with this (smaller) list.
//       Else, create a SubSector with all these Segs.
//   4 - Do the same for the second list (segs2).
//   5 - Return the new node (its two children are already OK).
//
// Each time CreateSSector is called, the Segs are put in a global list.
// When there is no more Seg in CreateNodes' list, then they are all in the
// global list and ready to be saved to disk.
//
