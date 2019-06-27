// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_draw32.c
/// \brief 32bpp span/column drawer functions
/// \note  no includes because this is included as part of r_draw.c

// ==========================================================================
// COLUMNS
// ==========================================================================

// A column is a vertical slice/span of a wall texture that uses
// a has a constant z depth from top to bottom.

/**	\brief The R_DrawColumn_32 function
*/
void R_DrawColumn_32(void)
{
	INT32 count;
	register UINT32 *dest;
	register fixed_t frac;
	fixed_t fracstep;

	count = dc_yh - dc_yl;

	if (count < 0) // Zero length, column does not exceed a pixel.
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		return;
#endif

	// Framebuffer destination address.
	dest = &topleft[dc_yl*vid.width + dc_x];
	count++;

	// Determine scaling, which is the only mapping to be done.
	fracstep = dc_iscale;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register const UINT8 *source = dc_source;
		register INT32 heightmask = dc_texheight-1;
		if (dc_texheight & heightmask)   // not a power of 2 -- killough
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) <  0);
			else
				while (frac >= heightmask)
					frac -= heightmask;

			do
			{
				// Re-map color indices from wall texture column
				//  using a lighting/special effects LUT.
				// heightmask is the Tutti-Frutti fix
				*dest = V_TrueColormapRGBA(source[frac>>FRACBITS]);
				dest += vid.width;

				// Avoid overflow.
				if (fracstep > 0x7FFFFFFF - frac)
					frac += fracstep - heightmask;
				else
					frac += fracstep;

				while (frac >= heightmask)
					frac -= heightmask;
			} while (--count);
		}
		else
		{
			while ((count -= 2) >= 0) // texture height is a power of 2
			{
				*dest = V_TrueColormapRGBA(source[(frac>>FRACBITS) & heightmask]);
				dest += vid.width;
				frac += fracstep;
				*dest = V_TrueColormapRGBA(source[(frac>>FRACBITS) & heightmask]);
				dest += vid.width;
				frac += fracstep;
			}
			if (count & 1)
				*dest = V_TrueColormapRGBA(source[(frac>>FRACBITS) & heightmask]);
		}
	}
}

#define alphamix(fg, bg) V_BlendTrueColor(bg, fg, (fg & 0xFF000000)>>24)

void R_DrawColumn_Ex32(void)
{
	INT32 count;
	register UINT32 *dest;
	register fixed_t frac;
	fixed_t fracstep;

	count = dc_yh - dc_yl;

	if (count < 0) // Zero length, column does not exceed a pixel.
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		return;
#endif

	// Framebuffer destination address.
	dest = &topleft[dc_yl*vid.width + dc_x];
	count++;

	// Determine scaling, which is the only mapping to be done.
	fracstep = dc_iscale;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register const UINT32 *source = (UINT32 *)dc_source;
		register INT32 heightmask = dc_texheight-1;
		if (dc_texheight & heightmask)   // not a power of 2 -- killough
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) <  0);
			else
				while (frac >= heightmask)
					frac -= heightmask;

			do
			{
				// Re-map color indices from wall texture column
				//  using a lighting/special effects LUT.
				// heightmask is the Tutti-Frutti fix
				*dest = alphamix(source[frac>>FRACBITS],*dest);
				dest += vid.width;

				// Avoid overflow.
				if (fracstep > 0x7FFFFFFF - frac)
					frac += fracstep - heightmask;
				else
					frac += fracstep;

				while (frac >= heightmask)
					frac -= heightmask;
			} while (--count);
		}
		else
		{
			while ((count -= 2) >= 0) // texture height is a power of 2
			{
				*dest = alphamix(source[(frac>>FRACBITS) & heightmask],*dest);
				dest += vid.width;
				frac += fracstep;
				*dest = alphamix(source[(frac>>FRACBITS) & heightmask],*dest);
				dest += vid.width;
				frac += fracstep;
			}
			if (count & 1)
				*dest = alphamix(source[(frac>>FRACBITS) & heightmask],*dest);
		}
	}

	/*I_OsPolling();
	I_UpdateNoBlit();
	I_FinishUpdate();*/
}

/**	\brief The R_DrawTranslucentColumn_32 function
*/
void R_DrawTranslucentColumn_32(void)
{
	register INT32 count;
	register UINT32 *dest;
	register fixed_t frac, fracstep;

	count = dc_yh - dc_yl + 1;

	if (count <= 0) // Zero length, column does not exceed a pixel.
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawTranslucentColumn_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	// FIXME. As above.
	dest = &topleft[dc_yl*vid.width + dc_x];

	// Looks familiar.
	fracstep = dc_iscale;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register const UINT8 *source = dc_source;
		register INT32 heightmask = dc_texheight-1;
		if (dc_texheight & heightmask)
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) < 0)
					;
			else
				while (frac >= heightmask)
					frac -= heightmask;

			do
			{
				// Re-map color indices from wall texture column
				// using a lighting/special effects LUT.
				// heightmask is the Tutti-Frutti fix
				*dest = (V_BlendTrueColor(*dest, V_TrueColormapRGBA(source[frac>>FRACBITS]), dc_transmap));
				dest += vid.width;
				if ((frac += fracstep) >= heightmask)
					frac -= heightmask;
			}
			while (--count);
		}
		else
		{
			while ((count -= 2) >= 0) // texture height is a power of 2
			{
				*dest = (V_BlendTrueColor(*dest, V_TrueColormapRGBA(source[(frac>>FRACBITS)&heightmask]), dc_transmap));
				dest += vid.width;
				frac += fracstep;
				*dest = (V_BlendTrueColor(*dest, V_TrueColormapRGBA(source[(frac>>FRACBITS)&heightmask]), dc_transmap));
				dest += vid.width;
				frac += fracstep;
			}
			if (count & 1)
				*dest = (V_BlendTrueColor(*dest, V_TrueColormapRGBA(source[(frac>>FRACBITS)&heightmask]), dc_transmap));
		}
	}
}

void R_DrawTranslucentColumn_Ex32(void)
{
	register INT32 count;
	register UINT32 *dest;
	register fixed_t frac, fracstep;

	count = dc_yh - dc_yl + 1;

	if (count <= 0) // Zero length, column does not exceed a pixel.
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawTranslucentColumn_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	// FIXME. As above.
	dest = &topleft[dc_yl*vid.width + dc_x];

	// Looks familiar.
	fracstep = dc_iscale;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register const UINT32 *source = (UINT32 *)dc_source;
		register INT32 heightmask = dc_texheight-1;
		if (dc_texheight & heightmask)
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) < 0)
					;
			else
				while (frac >= heightmask)
					frac -= heightmask;

			do
			{
				// Re-map color indices from wall texture column
				// using a lighting/special effects LUT.
				// heightmask is the Tutti-Frutti fix
				*dest = (V_BlendTrueColor(*dest, alphamix(source[frac>>FRACBITS],*dest), dc_transmap));
				dest += vid.width;
				if ((frac += fracstep) >= heightmask)
					frac -= heightmask;
			}
			while (--count);
		}
		else
		{
			while ((count -= 2) >= 0) // texture height is a power of 2
			{
				*dest = (V_BlendTrueColor(*dest, alphamix(source[(frac>>FRACBITS)&heightmask],*dest), dc_transmap));
				dest += vid.width;
				frac += fracstep;
				*dest = (V_BlendTrueColor(*dest, alphamix(source[(frac>>FRACBITS)&heightmask],*dest), dc_transmap));
				dest += vid.width;
				frac += fracstep;
			}
			if (count & 1)
				*dest = (V_BlendTrueColor(*dest, alphamix(source[(frac>>FRACBITS)&heightmask],*dest), dc_transmap));
		}
	}
}

#undef alphamix

/**	\brief The R_DrawTranslatedTranslucentColumn_32 function
	Spiffy function. Not only does it colormap a sprite, but does translucency as well.
	Uber-kudos to Cyan Helkaraxe
*/
void R_DrawTranslatedTranslucentColumn_32(void)
{
	register INT32 count;
	register UINT32 *dest;
	register fixed_t frac, fracstep;

	count = dc_yh - dc_yl + 1;

	if (count <= 0) // Zero length, column does not exceed a pixel.
		return;

	// FIXME. As above.
	dest = &topleft[dc_yl*vid.width + dc_x];

	// Looks familiar.
	fracstep = dc_iscale;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Inner loop that does the actual texture mapping, e.g. a DDA-like scaling.
	// This is as fast as it gets.
	{
		register INT32 heightmask = dc_texheight-1;
		if (dc_texheight & heightmask)
		{
			heightmask++;
			heightmask <<= FRACBITS;

			if (frac < 0)
				while ((frac += heightmask) < 0)
					;
			else
				while (frac >= heightmask)
					frac -= heightmask;

			do
			{
				// Re-map color indices from wall texture column
				//  using a lighting/special effects LUT.
				// heightmask is the Tutti-Frutti fix
				*dest = (V_BlendTrueColor(*dest, V_TrueColormapRGBA(dc_translation[dc_source[frac>>FRACBITS]]), dc_transmap));
				dest += vid.width;
				if ((frac += fracstep) >= heightmask)
					frac -= heightmask;
			}
			while (--count);
		}
		else
		{
			while ((count -= 2) >= 0) // texture height is a power of 2
			{
				*dest = (V_BlendTrueColor(*dest, V_TrueColormapRGBA(dc_translation[dc_source[(frac>>FRACBITS)&heightmask]]), dc_transmap));
				dest += vid.width;
				frac += fracstep;

				*dest = (V_BlendTrueColor(*dest, V_TrueColormapRGBA(dc_translation[dc_source[(frac>>FRACBITS)&heightmask]]), dc_transmap));
				dest += vid.width;
				frac += fracstep;
			}
			if (count & 1)
				*dest = (V_BlendTrueColor(*dest, V_TrueColormapRGBA(dc_translation[dc_source[(frac>>FRACBITS)&heightmask]]), dc_transmap));
		}
	}
}

/**	\brief The R_DrawTranslatedColumn_32 function
*/
void R_DrawTranslatedColumn_32(void)
{
	register INT32 count;
	register UINT32 *dest;
	register fixed_t frac, fracstep;

	count = dc_yh - dc_yl;
	if (count < 0)
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawTranslatedColumn_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	// FIXME. As above.
	dest = &topleft[dc_yl*vid.width + dc_x];

	// Looks familiar.
	fracstep = dc_iscale;
	frac = (dc_texturemid + FixedMul((dc_yl << FRACBITS) - centeryfrac, fracstep))*(!dc_hires);

	// Here we do an additional index re-mapping.
	do
	{
		// Translation tables are used
		//  to map certain colorramps to other ones,
		//  used with PLAY sprites.
		// Thus the "green" ramp of the player 0 sprite
		//  is mapped to gray, red, black/indigo.
		*dest = (V_TrueColormapRGBA(dc_translation[dc_source[frac>>FRACBITS]]));
		dest += vid.width;
		frac += fracstep;
	} while (count--);
}

/**	\brief The R_DrawFogSpan_32 function
	Draws the actual span with fogging.
*/
void R_DrawFogSpan_32(void)
{
	UINT32 *dest = &topleft[ds_y*vid.width+ds_x1];
	size_t count = (ds_x2-ds_x1)+1;

	while (count >= 4)
	{
		#define FOG(i) *(dest+i) = (V_BlendTrueColor(*(dest+i), 0xFF000000, ((ds_foglight/256)*8)));
		FOG(0)
		FOG(1)
		FOG(2)
		FOG(3)

		dest += 4;
		count -= 4;
		#undef FOG
	}

	while (count--)
	{
		*dest = (V_BlendTrueColor(*dest, 0xFF000000, ((ds_foglight/256)*8)));
		dest++;
	}
}

/**	\brief The R_DrawFogColumn_32 function
	Fog wall.
*/
void R_DrawFogColumn_32(void)
{
	UINT32 *dest;
	INT32 count = (dc_yh-dc_yl);

	// Zero length, column does not exceed a pixel.
	if (count < 0)
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawFogColumn_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	dest = &topleft[dc_yl*vid.width + dc_x];
	do
	{
		*dest = V_BlendTrueColor(*dest, 0xFF000000, (dc_foglight*8));
		dest += vid.width;
	} while (count--);
}

/**	\brief The R_DrawColumnShadowed_32 function
	This is for 3D floors that cast shadows on walls.

	This function just cuts the column up into sections and calls R_DrawColumn_32
*/
void R_DrawColumnShadowed_32(void)
{
	INT32 count, realyh, i, height, bheight = 0, solid = 0;

	realyh = dc_yh;

	count = dc_yh - dc_yl;

	// Zero length, column does not exceed a pixel.
	if (count < 0)
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawColumnShadowed_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	// This runs through the lightlist from top to bottom and cuts up the column accordingly.
	for (i = 0; i < dc_numlights; i++)
	{
		// If the height of the light is above the column, get the colormap
		// anyway because the lighting of the top should be affected.
		solid = dc_lightlist[i].flags & FF_CUTSOLIDS;

		height = dc_lightlist[i].height >> LIGHTSCALESHIFT;
		if (solid)
		{
			bheight = dc_lightlist[i].botheight >> LIGHTSCALESHIFT;
			if (bheight < height)
			{
				// confounded slopes sometimes allow partial invertedness,
				// even including cases where the top and bottom heights
				// should actually be the same!
				// swap the height values as a workaround for this quirk
				INT32 temp = height;
				height = bheight;
				bheight = temp;
			}
		}
		if (height <= dc_yl)
		{
			dc_lighting = dc_lightlist[i].rlighting;
			dc_levelcolormap = dc_lightlist[i].rcolormap;
			if (solid && dc_yl < bheight)
				dc_yl = bheight;
			continue;
		}
		// Found a break in the column!
		dc_yh = height;

		if (dc_yh > realyh)
			dc_yh = realyh;
		basecolfunc_ex();		// R_DrawColumn_Ex32
		if (solid)
			dc_yl = bheight;
		else
			dc_yl = dc_yh + 1;

		dc_lighting = dc_lightlist[i].rlighting;
		dc_levelcolormap = dc_lightlist[i].rcolormap;
	}
	dc_yh = realyh;
	if (dc_yl <= realyh)
		basecolfunc_ex();		// R_DrawColumn_Ex32
}

// ==========================================================================
// SPANS
// ==========================================================================

/**	\brief The R_DrawSpan_32 function
	Draws the actual span.
*/
void R_DrawSpan_32(void)
{
	fixed_t xposition;
	fixed_t yposition;
	fixed_t xstep, ystep;

	UINT32 *source;
	UINT32 *dest;

	size_t count = (ds_x2 - ds_x1 + 1);
	xposition = ds_xfrac; yposition = ds_yfrac;
	xstep = ds_xstep; ystep = ds_ystep;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	if (ds_powersoftwo)
	{
		xposition <<= nflatshiftup; yposition <<= nflatshiftup;
		xstep <<= nflatshiftup; ystep <<= nflatshiftup;
	}

	source = ds_source;
	dest = &topleft[ds_y*vid.width + ds_x1];

	if (!ds_powersoftwo)
	{
		while (count--)
		{
			fixed_t x = (xposition >> FRACBITS);
			fixed_t y = (yposition >> FRACBITS);

			// Carefully align all of my Friends.
			if (x < 0)
				x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
			if (y < 0)
				y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

			x %= ds_flatwidth;
			y %= ds_flatheight;

			*dest++ = source[((y * ds_flatwidth) + x)];
			xposition += xstep;
			yposition += ystep;
		}
	}
	else
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			#define MAINSPANLOOP(i) \
			dest[i] = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]; \
			xposition += xstep; \
			yposition += ystep;

			MAINSPANLOOP(0)
			MAINSPANLOOP(1)
			MAINSPANLOOP(2)
			MAINSPANLOOP(3)
			MAINSPANLOOP(4)
			MAINSPANLOOP(5)
			MAINSPANLOOP(6)
			MAINSPANLOOP(7)

			#undef MAINSPANLOOP

			dest += 8;
			count -= 8;
		}
		while (count--)
		{
			*dest++ = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			xposition += xstep;
			yposition += ystep;
		}
	}
}

/**	\brief The R_DrawSplat_32 function
	Just like R_DrawSpan_32, but skips transparent pixels.
*/
void R_DrawSplat_32(void)
{
	fixed_t xposition;
	fixed_t yposition;
	fixed_t xstep, ystep;

	UINT32 *source;
	UINT32 *dest;

	UINT32 val;
	size_t count = (ds_x2 - ds_x1 + 1);
	xposition = ds_xfrac; yposition = ds_yfrac;
	xstep = ds_xstep; ystep = ds_ystep;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	if (ds_powersoftwo)
	{
		xposition <<= nflatshiftup; yposition <<= nflatshiftup;
		xstep <<= nflatshiftup; ystep <<= nflatshiftup;
	}

	source = ds_source;
	dest = &topleft[ds_y*vid.width + ds_x1];

	if (!ds_powersoftwo)
	{
		while (count--)
		{
			fixed_t x = (xposition >> FRACBITS);
			fixed_t y = (yposition >> FRACBITS);

			// Carefully align all of my Friends.
			if (x < 0)
				x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
			if (y < 0)
				y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

			x %= ds_flatwidth;
			y %= ds_flatheight;

			val = source[((y * ds_flatwidth) + x)];
			if (val != TRANSPARENTPIXEL)
				*dest = val;
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
	else
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			//
			// <Callum> 4194303 = (2048x2048)-1 (2048x2048 is maximum flat size)
			#define MAINSPANLOOP(i) \
			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift); \
			val &= 4194303; \
			val = source[val]; \
			if (val != V_GetTrueColor(TRANSPARENTPIXEL)) \
				dest[i] = val; \
			xposition += xstep; \
			yposition += ystep;

			MAINSPANLOOP(0)
			MAINSPANLOOP(1)
			MAINSPANLOOP(2)
			MAINSPANLOOP(3)
			MAINSPANLOOP(4)
			MAINSPANLOOP(5)
			MAINSPANLOOP(6)
			MAINSPANLOOP(7)

			#undef MAINSPANLOOP

			dest += 8;
			count -= 8;
		}
		while (count--)
		{
			val = (((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift);
			val &= 4194303;
			val = source[val];
			if (val != V_GetTrueColor(TRANSPARENTPIXEL))
				*dest = val;

			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
}

/**	\brief The R_DrawTranslucentSplat_32 function
	Just like R_DrawSplat_32 but is translucent!
*/
void R_DrawTranslucentSplat_32(void)
{
	fixed_t xposition;
	fixed_t yposition;
	fixed_t xstep, ystep;

	UINT32 *source;
	UINT32 *dest;

	UINT32 val;
	size_t count = (ds_x2 - ds_x1 + 1);
	xposition = ds_xfrac; yposition = ds_yfrac;
	xstep = ds_xstep; ystep = ds_ystep;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	if (ds_powersoftwo)
	{
		xposition <<= nflatshiftup; yposition <<= nflatshiftup;
		xstep <<= nflatshiftup; ystep <<= nflatshiftup;
	}

	source = ds_source;
	dest = &topleft[ds_y*vid.width + ds_x1];

	if (!ds_powersoftwo)
	{
		while (count--)
		{
			fixed_t x = (xposition >> FRACBITS);
			fixed_t y = (yposition >> FRACBITS);

			// Carefully align all of my Friends.
			if (x < 0)
				x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
			if (y < 0)
				y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

			x %= ds_flatwidth;
			y %= ds_flatheight;

			val = source[((y * ds_flatwidth) + x)];
			if (val != TRANSPARENTPIXEL)
				*dest = V_BlendTrueColor(*dest, val, ds_transmap);
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
	else
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			#define MAINSPANLOOP(i) \
			val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]; \
			if (val != TRANSPARENTPIXEL) \
				dest[i] = V_BlendTrueColor(dest[i], val, ds_transmap); \
			xposition += xstep; \
			yposition += ystep;

			MAINSPANLOOP(0)
			MAINSPANLOOP(1)
			MAINSPANLOOP(2)
			MAINSPANLOOP(3)
			MAINSPANLOOP(4)
			MAINSPANLOOP(5)
			MAINSPANLOOP(6)
			MAINSPANLOOP(7)

			#undef MAINSPANLOOP

			dest += 8;
			count -= 8;
		}
		while (count--)
		{
			val = source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)];
			if (val != V_GetTrueColor(TRANSPARENTPIXEL))
				*dest = V_BlendTrueColor(*dest, val, ds_transmap);
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
}

/**	\brief The R_DrawTranslucentSpan_32 function
	Draws the actual span with translucency.
*/
void R_DrawTranslucentSpan_32(void)
{
	fixed_t xposition;
	fixed_t yposition;
	fixed_t xstep, ystep;

	UINT32 *source;
	UINT32 *dest;

	size_t count = (ds_x2 - ds_x1 + 1);
	UINT32 val;

	xposition = ds_xfrac; yposition = ds_yfrac;
	xstep = ds_xstep; ystep = ds_ystep;

	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	if (ds_powersoftwo)
	{
		xposition <<= nflatshiftup; yposition <<= nflatshiftup;
		xstep <<= nflatshiftup; ystep <<= nflatshiftup;
	}

	source = ds_source;
	dest = &topleft[ds_y*vid.width + ds_x1];

	if (!ds_powersoftwo)
	{
		while (count--)
		{
			fixed_t x = (xposition >> FRACBITS);
			fixed_t y = (yposition >> FRACBITS);

			// Carefully align all of my Friends.
			if (x < 0)
				x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
			if (y < 0)
				y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

			x %= ds_flatwidth;
			y %= ds_flatheight;

			val = ((y * ds_flatwidth) + x);
			*dest = V_BlendTrueColor(*dest, val, ds_transmap);
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
	else
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			#define MAINSPANLOOP(i) \
			dest[i] = V_BlendTrueColor(dest[i], source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], ds_transmap); \
			xposition += xstep; \
			yposition += ystep;

			MAINSPANLOOP(0)
			MAINSPANLOOP(1)
			MAINSPANLOOP(2)
			MAINSPANLOOP(3)
			MAINSPANLOOP(4)
			MAINSPANLOOP(5)
			MAINSPANLOOP(6)
			MAINSPANLOOP(7)

			#undef MAINSPANLOOP

			dest += 8;
			count -= 8;
		}
		while (count--)
		{
			*dest = V_BlendTrueColor(*dest, source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], ds_transmap);
			dest++;
			xposition += xstep;
			yposition += ystep;
		}
	}
}

#ifdef ESLOPE
// R_CalcTiltedLighting
// Exactly what it says on the tin. I wish I wasn't too lazy to explain things properly.
static INT32 tiltlighting[MAXVIDWIDTH];
void R_CalcTiltedLighting(fixed_t start, fixed_t end)
{
	// ZDoom uses a different lighting setup to us, and I couldn't figure out how to adapt their version
	// of this function. Here's my own.
	INT32 left = ds_x1, right = ds_x2;
	fixed_t step = (end-start)/(ds_x2-ds_x1+1);
	INT32 i;

	// I wanna do some optimizing by checking for out-of-range segments on either side to fill in all at once,
	// but I'm too bad at coding to not crash the game trying to do that. I guess this is fast enough for now...

	for (i = left; i <= right; i++) {
		tiltlighting[i] = (start += step) >> FRACBITS;
		if (tiltlighting[i] < 0)
			tiltlighting[i] = 0;
		else if (tiltlighting[i] >= MAXLIGHTSCALE)
			tiltlighting[i] = MAXLIGHTSCALE-1;
	}
}

/**	\brief The R_DrawTiltedSpan_32 function
*/
void R_DrawTiltedSpan_32(void)
{
	// x1, x2 = ds_x1, ds_x2
	int width = ds_x2 - ds_x1;
	double iz, uz, vz;
	UINT32 u, v;
	int i;

	UINT32 *source;
	UINT32 *dest;

	double startz, startu, startv;
	double izstep, uzstep, vzstep;
	double endz, endu, endv;
	UINT32 stepu, stepv;

	iz = ds_sz.z + ds_sz.y*(centery-ds_y) + ds_sz.x*(ds_x1-centerx);

	// Lighting is simple. It's just linear interpolation from start to end
	{
		float planelightfloat = BASEVIDWIDTH*BASEVIDWIDTH/vid.width / (zeroheight - FIXED_TO_FLOAT(viewz)) / 21.0f;
		float lightstart, lightend;

		lightend = (iz + ds_sz.x*width) * planelightfloat;
		lightstart = iz * planelightfloat;

		R_CalcTiltedLighting(FLOAT_TO_FIXED(lightstart), FLOAT_TO_FIXED(lightend));
		//CONS_Printf("tilted lighting %f to %f (foc %f)\n", lightstart, lightend, focallengthf);
	}

	uz = ds_su.z + ds_su.y*(centery-ds_y) + ds_su.x*(ds_x1-centerx);
	vz = ds_sv.z + ds_sv.y*(centery-ds_y) + ds_sv.x*(ds_x1-centerx);

	dest = &topleft[ds_y*vid.width + ds_x1];
	source = ds_source;

	#define SPANSIZE 16
	#define INVSPAN	0.0625f

	startz = 1.f/iz;
	startu = uz*startz;
	startv = vz*startz;

	izstep = ds_sz.x * SPANSIZE;
	uzstep = ds_su.x * SPANSIZE;
	vzstep = ds_sv.x * SPANSIZE;
	width++;

	while (width >= SPANSIZE)
	{
		iz += izstep;
		uz += uzstep;
		vz += vzstep;

		endz = 1.f/iz;
		endu = uz*endz;
		endv = vz*endz;
		stepu = (INT64)((endu - startu) * INVSPAN);
		stepv = (INT64)((endv - startv) * INVSPAN);
		u = (INT64)(startu) + viewx;
		v = (INT64)(startv) + viewy;

		for (i = SPANSIZE-1; i >= 0; i--)
		{
			if (!ds_powersoftwo)
			{
				fixed_t x = ((u-viewx) >> FRACBITS);
				fixed_t y = ((v-viewy) >> FRACBITS);

				// Carefully align all of my Friends.
				if (x < 0)
					x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
				if (y < 0)
					y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

				x %= ds_flatwidth;
				y %= ds_flatheight;

				*dest = source[((y * ds_flatwidth) + x)];
			}
			else
				*dest = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
			dest++;
			u += stepu;
			v += stepv;
		}
		startu = endu;
		startv = endv;
		width -= SPANSIZE;
	}
	if (width > 0)
	{
		if (width == 1)
		{
			u = (INT64)(startu);
			v = (INT64)(startv);
			if (!ds_powersoftwo)
			{
				fixed_t x = ((u-viewx) >> FRACBITS);
				fixed_t y = ((v-viewy) >> FRACBITS);

				// Carefully align all of my Friends.
				if (x < 0)
					x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
				if (y < 0)
					y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

				x %= ds_flatwidth;
				y %= ds_flatheight;

				*dest = source[((y * ds_flatwidth) + x)];
			}
			else
				*dest = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
		}
		else
		{
			double left = width;
			iz += ds_sz.x * left;
			uz += ds_su.x * left;
			vz += ds_sv.x * left;

			endz = 1.f/iz;
			endu = uz*endz;
			endv = vz*endz;
			left = 1.f/left;
			stepu = (INT64)((endu - startu) * left);
			stepv = (INT64)((endv - startv) * left);
			u = (INT64)(startu) + viewx;
			v = (INT64)(startv) + viewy;

			for (; width != 0; width--)
			{
				if (!ds_powersoftwo)
				{
					fixed_t x = ((u-viewx) >> FRACBITS);
					fixed_t y = ((v-viewy) >> FRACBITS);

					// Carefully align all of my Friends.
					if (x < 0)
						x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
					if (y < 0)
						y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

					x %= ds_flatwidth;
					y %= ds_flatheight;

					*dest = source[((y * ds_flatwidth) + x)];
				}
				else
					*dest = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
				dest++;
				u += stepu;
				v += stepv;
			}
		}
	}
}

/**	\brief The R_DrawTiltedTranslucentSpan_32 function
	Like R_DrawTiltedSpan_32, but translucent
*/
void R_DrawTiltedTranslucentSpan_32(void)
{
	// x1, x2 = ds_x1, ds_x2
	int width = ds_x2 - ds_x1;
	double iz, uz, vz;
	UINT32 u, v;
	int i;

	UINT32 *source;
	UINT32 *dest;

	double startz, startu, startv;
	double izstep, uzstep, vzstep;
	double endz, endu, endv;
	UINT32 stepu, stepv;

	iz = ds_sz.z + ds_sz.y*(centery-ds_y) + ds_sz.x*(ds_x1-centerx);

	// Lighting is simple. It's just linear interpolation from start to end
	{
		float planelightfloat = BASEVIDWIDTH*BASEVIDWIDTH/vid.width / (zeroheight - FIXED_TO_FLOAT(viewz)) / 21.0f;
		float lightstart, lightend;

		lightend = (iz + ds_sz.x*width) * planelightfloat;
		lightstart = iz * planelightfloat;

		R_CalcTiltedLighting(FLOAT_TO_FIXED(lightstart), FLOAT_TO_FIXED(lightend));
		//CONS_Printf("tilted lighting %f to %f (foc %f)\n", lightstart, lightend, focallengthf);
	}

	uz = ds_su.z + ds_su.y*(centery-ds_y) + ds_su.x*(ds_x1-centerx);
	vz = ds_sv.z + ds_sv.y*(centery-ds_y) + ds_sv.x*(ds_x1-centerx);

	dest = &topleft[ds_y*vid.width + ds_x1];
	source = ds_source;

#define SPANSIZE 16
#define INVSPAN	0.0625f

	startz = 1.f/iz;
	startu = uz*startz;
	startv = vz*startz;

	izstep = ds_sz.x * SPANSIZE;
	uzstep = ds_su.x * SPANSIZE;
	vzstep = ds_sv.x * SPANSIZE;
	width++;

	while (width >= SPANSIZE)
	{
		iz += izstep;
		uz += uzstep;
		vz += vzstep;

		endz = 1.f/iz;
		endu = uz*endz;
		endv = vz*endz;
		stepu = (INT64)((endu - startu) * INVSPAN);
		stepv = (INT64)((endv - startv) * INVSPAN);
		u = (INT64)(startu) + viewx;
		v = (INT64)(startv) + viewy;

		for (i = SPANSIZE-1; i >= 0; i--)
		{
			if (!ds_powersoftwo)
			{
				fixed_t x = ((u-viewx) >> FRACBITS);
				fixed_t y = ((v-viewy) >> FRACBITS);

				// Carefully align all of my Friends.
				if (x < 0)
					x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
				if (y < 0)
					y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

				x %= ds_flatwidth;
				y %= ds_flatheight;

				*dest = V_BlendTrueColor(*dest, source[((y * ds_flatwidth) + x)], ds_transmap);
			}
			else
				*dest = V_BlendTrueColor(*dest, source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], ds_transmap);

			dest++;
			u += stepu;
			v += stepv;
		}
		startu = endu;
		startv = endv;
		width -= SPANSIZE;
	}
	if (width > 0)
	{
		if (width == 1)
		{
			u = (INT64)(startu);
			v = (INT64)(startv);
			if (!ds_powersoftwo)
			{
				fixed_t x = ((u-viewx) >> FRACBITS);
				fixed_t y = ((v-viewy) >> FRACBITS);

				// Carefully align all of my Friends.
				if (x < 0)
					x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
				if (y < 0)
					y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

				x %= ds_flatwidth;
				y %= ds_flatheight;

				*dest = V_BlendTrueColor(*dest, source[((y * ds_flatwidth) + x)], ds_transmap);
			}
			else
				*dest = V_BlendTrueColor(*dest, source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], ds_transmap);
		}
		else
		{
			double left = width;
			iz += ds_sz.x * left;
			uz += ds_su.x * left;
			vz += ds_sv.x * left;

			endz = 1.f/iz;
			endu = uz*endz;
			endv = vz*endz;
			left = 1.f/left;
			stepu = (INT64)((endu - startu) * left);
			stepv = (INT64)((endv - startv) * left);
			u = (INT64)(startu) + viewx;
			v = (INT64)(startv) + viewy;

			for (; width != 0; width--)
			{
				if (!ds_powersoftwo)
				{
					fixed_t x = ((u-viewx) >> FRACBITS);
					fixed_t y = ((v-viewy) >> FRACBITS);

					// Carefully align all of my Friends.
					if (x < 0)
						x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
					if (y < 0)
						y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

					x %= ds_flatwidth;
					y %= ds_flatheight;

					*dest = V_BlendTrueColor(*dest, source[((y * ds_flatwidth) + x)], ds_transmap);
				}
				else
					*dest = V_BlendTrueColor(*dest, source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)], ds_transmap);
				dest++;
				u += stepu;
				v += stepv;
			}
		}
	}
}

void R_DrawTiltedSplat_32(void)
{
	// x1, x2 = ds_x1, ds_x2
	int width = ds_x2 - ds_x1;
	double iz, uz, vz;
	UINT32 u, v;
	int i;

	UINT32 *source;
	UINT32 *dest;

	double startz, startu, startv;
	double izstep, uzstep, vzstep;
	double endz, endu, endv;
	UINT32 stepu, stepv;

	UINT8 val;

	iz = ds_sz.z + ds_sz.y*(centery-ds_y) + ds_sz.x*(ds_x1-centerx);

	// Lighting is simple. It's just linear interpolation from start to end
	{
		float planelightfloat = BASEVIDWIDTH*BASEVIDWIDTH/vid.width / (zeroheight - FIXED_TO_FLOAT(viewz)) / 21.0f;
		float lightstart, lightend;

		lightend = (iz + ds_sz.x*width) * planelightfloat;
		lightstart = iz * planelightfloat;

		R_CalcTiltedLighting(FLOAT_TO_FIXED(lightstart), FLOAT_TO_FIXED(lightend));
		//CONS_Printf("tilted lighting %f to %f (foc %f)\n", lightstart, lightend, focallengthf);
	}

	uz = ds_su.z + ds_su.y*(centery-ds_y) + ds_su.x*(ds_x1-centerx);
	vz = ds_sv.z + ds_sv.y*(centery-ds_y) + ds_sv.x*(ds_x1-centerx);

	dest = &topleft[ds_y*vid.width + ds_x1];
	source = ds_source;

	#define SPANSIZE 16
	#define INVSPAN	0.0625f

	startz = 1.f/iz;
	startu = uz*startz;
	startv = vz*startz;

	izstep = ds_sz.x * SPANSIZE;
	uzstep = ds_su.x * SPANSIZE;
	vzstep = ds_sv.x * SPANSIZE;
	width++;

	while (width >= SPANSIZE)
	{
		iz += izstep;
		uz += uzstep;
		vz += vzstep;

		endz = 1.f/iz;
		endu = uz*endz;
		endv = vz*endz;
		stepu = (INT64)((endu - startu) * INVSPAN);
		stepv = (INT64)((endv - startv) * INVSPAN);
		u = (INT64)(startu) + viewx;
		v = (INT64)(startv) + viewy;

		for (i = SPANSIZE-1; i >= 0; i--)
		{
			if (!ds_powersoftwo)
			{
				fixed_t x = ((u-viewx) >> FRACBITS);
				fixed_t y = ((v-viewy) >> FRACBITS);

				// Carefully align all of my Friends.
				if (x < 0)
					x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
				if (y < 0)
					y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

				x %= ds_flatwidth;
				y %= ds_flatheight;

				val = source[((y * ds_flatwidth) + x)];
			}
			else
				val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
			if (val != V_GetTrueColor(TRANSPARENTPIXEL))
				*dest = val;
			dest++;
			u += stepu;
			v += stepv;
		}
		startu = endu;
		startv = endv;
		width -= SPANSIZE;
	}
	if (width > 0)
	{
		if (width == 1)
		{
			u = (INT64)(startu);
			v = (INT64)(startv);
			if (!ds_powersoftwo)
			{
				fixed_t x = ((u-viewx) >> FRACBITS);
				fixed_t y = ((v-viewy) >> FRACBITS);

				// Carefully align all of my Friends.
				if (x < 0)
					x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
				if (y < 0)
					y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

				x %= ds_flatwidth;
				y %= ds_flatheight;

				val = source[((y * ds_flatwidth) + x)];
			}
			else
				val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
			if (val != V_GetTrueColor(TRANSPARENTPIXEL))
				*dest = val;
		}
		else
		{
			double left = width;
			iz += ds_sz.x * left;
			uz += ds_su.x * left;
			vz += ds_sv.x * left;

			endz = 1.f/iz;
			endu = uz*endz;
			endv = vz*endz;
			left = 1.f/left;
			stepu = (INT64)((endu - startu) * left);
			stepv = (INT64)((endv - startv) * left);
			u = (INT64)(startu) + viewx;
			v = (INT64)(startv) + viewy;

			for (; width != 0; width--)
			{
				if (!ds_powersoftwo)
				{
					fixed_t x = ((u-viewx) >> FRACBITS);
					fixed_t y = ((v-viewy) >> FRACBITS);

					// Carefully align all of my Friends.
					if (x < 0)
						x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
					if (y < 0)
						y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

					x %= ds_flatwidth;
					y %= ds_flatheight;

					val = source[((y * ds_flatwidth) + x)];
				}
				else
					val = source[((v >> nflatyshift) & nflatmask) | (u >> nflatxshift)];
				if (val != V_GetTrueColor(TRANSPARENTPIXEL))
					*dest = val;
				dest++;
				u += stepu;
				v += stepv;
			}
		}
	}
}
#endif // ESLOPE

#ifndef NOWATER
void R_DrawTranslucentWaterSpan_32(void)
{
	fixed_t xposition;
	fixed_t yposition;
	fixed_t xstep, ystep;

	UINT32 *source;
	UINT32 *dest;
	UINT32 *dsrc;

	size_t count = (ds_x2 - ds_x1 + 1);

	xposition = ds_xfrac; yposition = (ds_yfrac + ds_wateroffset);
	xstep = ds_xstep; ystep = ds_ystep;
	// SoM: we only need 6 bits for the integer part (0 thru 63) so the rest
	// can be used for the fraction part. This allows calculation of the memory address in the
	// texture with two shifts, an OR and one AND. (see below)
	// for texture sizes > 64 the amount of precision we can allow will decrease, but only by one
	// bit per power of two (obviously)
	// Ok, because I was able to eliminate the variable spot below, this function is now FASTER
	// than the original span renderer. Whodathunkit?
	if (ds_powersoftwo)
	{
		xposition <<= nflatshiftup; yposition <<= nflatshiftup;
		xstep <<= nflatshiftup; ystep <<= nflatshiftup;
	}

	source = ds_source;
	dest = &topleft[ds_y*vid.width + ds_x1];
	dsrc = screen_altblit + (ds_y+ds_bgoffset)*vid.width + ds_x1;

	if (!ds_powersoftwo)
	{
		while (count--)
		{
			fixed_t x = (xposition >> FRACBITS);
			fixed_t y = (yposition >> FRACBITS);

			// Carefully align all of my Friends.
			if (x < 0)
				x = ds_flatwidth - ((UINT32)(ds_flatwidth - x) % ds_flatwidth);
			if (y < 0)
				y = ds_flatheight - ((UINT32)(ds_flatheight - y) % ds_flatheight);

			x %= ds_flatwidth;
			y %= ds_flatheight;

			*dest++ = V_BlendTrueColor(*dsrc++, source[((y * ds_flatwidth) + x)], ds_transmap);
			xposition += xstep;
			yposition += ystep;
		}
	}
	else
	{
		while (count >= 8)
		{
			// SoM: Why didn't I see this earlier? the spot variable is a waste now because we don't
			// have the uber complicated math to calculate it now, so that was a memory write we didn't
			// need!
			#define MAINSPANLOOP(i) \
			dest[i] = (V_BlendTrueColor(*dsrc++, (source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)]), ds_transmap)); \
			xposition += xstep; \
			yposition += ystep;

			MAINSPANLOOP(0)
			MAINSPANLOOP(1)
			MAINSPANLOOP(2)
			MAINSPANLOOP(3)
			MAINSPANLOOP(4)
			MAINSPANLOOP(5)
			MAINSPANLOOP(6)
			MAINSPANLOOP(7)

			#undef MAINSPANLOOP

			dest += 8;
			count -= 8;
		}
		while (count--)
		{
			*dest++ = V_BlendTrueColor(*dsrc++, source[(((UINT32)yposition >> nflatyshift) & nflatmask) | ((UINT32)xposition >> nflatxshift)], ds_transmap);
			xposition += xstep;
			yposition += ystep;
		}
	}
}
#endif

void R_DrawBlendColumn_32(void)
{
	UINT32 *dest;
	INT32 count = (dc_yh-dc_yl);

	// Zero length, column does not exceed a pixel.
	if (count < 0)
		return;

#ifdef RANGECHECK
	if ((unsigned)dc_x >= (unsigned)vid.width || dc_yl < 0 || dc_yh >= vid.height)
		I_Error("R_DrawBlendColumn_32: %d to %d at %d", dc_yl, dc_yh, dc_x);
#endif

	dest = &topleft[dc_yl*vid.width + dc_x];
	do
	{
		*dest = V_BlendTrueColor(*dest, dc_blendcolor, dc_transmap);
		dest += vid.width;
	} while (count--);
}

void R_DrawBlendSpan_32(void)
{
	UINT32 *dest = &topleft[ds_y*vid.width+ds_x1];
	size_t count = (ds_x2-ds_x1)+1;

	while (count >= 4)
	{
		#define FOG(i) *(dest+i) = V_BlendTrueColor(*(dest+i), ds_blendcolor, ds_transmap);
		FOG(0)
		FOG(1)
		FOG(2)
		FOG(3)

		dest += 4;
		count -= 4;
		#undef FOG
	}

	while (count--)
	{
		*dest = V_BlendTrueColor(*dest, ds_blendcolor, ds_transmap);
		dest++;
	}
}
