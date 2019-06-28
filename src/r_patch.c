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
/// \file  r_patch.c
/// \brief patch handling for truecolor

#include "doomdef.h"
#include "r_patch.h"
#include "r_data.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_setup.h"
#include "v_video.h"

#ifdef HAVE_PNG

#ifndef _MSC_VER
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#endif

#ifndef _LFS64_LARGEFILE
#define _LFS64_LARGEFILE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 0
#endif

#include "png.h"
#ifndef PNG_READ_SUPPORTED
#undef HAVE_PNG
#endif
#endif

// https://github.com/coelckers/prboom-plus/blob/master/prboom2/src/r_patch.c#L350
boolean R_CheckIfPatch(lumpnum_t lump)
{
	size_t size;
	INT16 width, height;
	patch_t *patch;
	boolean result;

	size = W_LumpLength(lump);

	// minimum length of a valid Doom patch
	if (size < 13)
		return false;

	patch = (patch_t *)W_CacheLumpNum(lump, PU_STATIC);

	width = SHORT(patch->width);
	height = SHORT(patch->height);

	result = (height > 0 && height <= 16384 && width > 0 && width <= 16384 && width < (INT16)(size / 4));

	if (result)
	{
		// The dimensions seem like they might be valid for a patch, so
		// check the column directory for extra security. All columns
		// must begin after the column directory, and none of them must
		// point past the end of the patch.
		INT16 x;

		for (x = 0; x < width; x++)
		{
			UINT32 ofs = LONG(patch->columnofs[x]);

			// Need one byte for an empty column (but there's patches that don't know that!)
			if (ofs < (UINT32)width * 4 + 8 || ofs >= (UINT32)size)
			{
				result = false;
				break;
			}
		}
	}

	return result;
}

void R_PatchToFlat(patch_t *patch, UINT32 *flat)
{
	fixed_t col, ofs;
	column_t *column;
	UINT32 *desttop, *dest, *deststop;
	UINT8 *source;

	desttop = flat;
	deststop = desttop + (SHORT(patch->width) * SHORT(patch->height));

	for (col = 0; col < SHORT(patch->width); col++, desttop++)
	{
		INT32 topdelta, prevdelta = -1;
		column = (column_t *)((UINT8 *)patch + LONG(patch->columnofs[col]));

		while (column->topdelta != 0xff)
		{
			topdelta = column->topdelta;
			if (topdelta <= prevdelta)
				topdelta += prevdelta;
			prevdelta = topdelta;

			dest = desttop + (topdelta * SHORT(patch->width));
			source = (UINT8 *)(column) + 3;
			for (ofs = 0; dest < deststop && ofs < column->length; ofs++)
			{
				UINT32 pixel = V_GetTrueColor(source[ofs]);
				if (source[ofs] != TRANSPARENTPIXEL)
					*dest = pixel;
				dest += SHORT(patch->width);
			}
			column = (column_t *)((UINT8 *)column + column->length + 4);
		}
	}
}

#ifndef NO_PNG_LUMPS
boolean R_IsLumpPNG(UINT8 *d, size_t s)
{
	if (s < 67) // http://garethrees.org/2007/11/14/pngcrush/
		return false;
	// Check for PNG file signature using memcmp
	// As it may be faster on CPUs with slow unaligned memory access
	// Ref: http://www.libpng.org/pub/png/spec/1.2/PNG-Rationale.html#R.PNG-file-signature
	return (memcmp(&d[0], "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a", 8) == 0);
}

#ifdef HAVE_PNG
typedef struct {
	png_bytep buffer;
	png_uint_32 bufsize;
	png_uint_32 current_pos;
} png_ioread;

static void PNG_IOReader(png_structp png_ptr, png_bytep data, png_size_t length)
{
	png_ioread *f = png_get_io_ptr(png_ptr);
	if (length > (f->bufsize - f->current_pos))
		png_error(png_ptr, "read error in PNG_IOReader");
	memcpy(data, f->buffer + f->current_pos, length);
	f->current_pos += length;
}

static void PNG_error(png_structp PNG, png_const_charp pngtext)
{
	CONS_Debug(DBG_RENDER, "libpng error at %p: %s", PNG, pngtext);
	//I_Error("libpng error at %p: %s", PNG, pngtext);
}

static void PNG_warn(png_structp PNG, png_const_charp pngtext)
{
	CONS_Debug(DBG_RENDER, "libpng warning at %p: %s", PNG, pngtext);
}

static png_bytep *PNG_Read(UINT8 *png, UINT16 *w, UINT16 *h, size_t size)
{
	png_structp png_ptr;
	png_infop png_info_ptr;
	png_uint_32 width, height;
	int bit_depth, color_type;
	png_uint_32 y;
#ifdef PNG_SETJMP_SUPPORTED
#ifdef USE_FAR_KEYWORD
	jmp_buf jmpbuf;
#endif
#endif

	png_ioread png_io;
	png_bytep *row_pointers;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
		PNG_error, PNG_warn);
	if (!png_ptr)
	{
		CONS_Debug(DBG_RENDER, "PNG_Load: Error on initialize libpng\n");
		return NULL;
	}

	png_info_ptr = png_create_info_struct(png_ptr);
	if (!png_info_ptr)
	{
		CONS_Debug(DBG_RENDER, "PNG_Load: Error on allocate for libpng\n");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return NULL;
	}

#ifdef USE_FAR_KEYWORD
	if (setjmp(jmpbuf))
#else
	if (setjmp(png_jmpbuf(png_ptr)))
#endif
	{
		//CONS_Debug(DBG_RENDER, "libpng load error on %s\n", filename);
		png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);
		return NULL;
	}
#ifdef USE_FAR_KEYWORD
	png_memcpy(png_jmpbuf(png_ptr), jmpbuf, sizeof jmp_buf);
#endif

	// set our own read_function
	png_io.buffer = (png_bytep)png;
	png_io.bufsize = size;
	png_io.current_pos = 0;
	png_set_read_fn(png_ptr, &png_io, PNG_IOReader);

#ifdef PNG_SET_USER_LIMITS_SUPPORTED
	png_set_user_limits(png_ptr, 2048, 2048);
#endif

	png_read_info(png_ptr, png_info_ptr);

	png_get_IHDR(png_ptr, png_info_ptr, &width, &height, &bit_depth, &color_type,
	 NULL, NULL, NULL);

	if (bit_depth == 16)
		png_set_strip_16(png_ptr);

	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png_ptr);
	else if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png_ptr);

	if (png_get_valid(png_ptr, png_info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);
	else if (color_type != PNG_COLOR_TYPE_RGB_ALPHA && color_type != PNG_COLOR_TYPE_GRAY_ALPHA)
	{
#if PNG_LIBPNG_VER < 10207
		png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
#else
		png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
#endif
	}

	png_read_update_info(png_ptr, png_info_ptr);

	// Read the image
	row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
	for (y = 0; y < height; y++)
		row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png_ptr, png_info_ptr));
	png_read_image(png_ptr, row_pointers);
	png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);

	*w = (INT32)width;
	*h = (INT32)height;
	return row_pointers;
}

// Convert a PNG to a raw image.
static UINT32 *PNG_RawConvert(UINT8 *png, UINT16 *w, UINT16 *h, size_t size)
{
	UINT32 *flat;
	png_uint_32 x, y;
	png_bytep *row_pointers = PNG_Read(png, w, h, size);
	png_uint_32 width = *w, height = *h;

	if (!row_pointers)
		return NULL;

	// Convert the image to 32bpp
	flat = Z_Calloc((width * height) * sizeof(UINT32), PU_LEVEL, NULL);
	for (y = 0; y < height; y++)
	{
		png_bytep row = row_pointers[y];
		for (x = 0; x < width; x++)
		{
			png_bytep px = &(row[x * 4]);
			flat[((y * width) + x)] = ((UINT8)px[3]<<24)|((UINT8)px[2]<<16)|((UINT8)px[1]<<8)|((UINT8)px[0]);
		}
	}
	free(row_pointers);

	return flat;
}

// Convert a PNG to a flat.
UINT32 *R_PNGToFlat(levelflat_t *levelflat, UINT8 *png, size_t size)
{
	return PNG_RawConvert(png, &levelflat->width, &levelflat->height, size);
}

// Convert a PNG to a patch.
// This is adapted from the "kartmaker" utility
static unsigned char imgbuf[1<<26];
patch_t *R_PNGToPatch(UINT8 *png, size_t size)
{
	UINT16 width, height;
	UINT32 *raw = PNG_RawConvert(png, &width, &height, size);

	UINT32 x, y;
	UINT8 *img;
	UINT8 *imgptr = imgbuf;
	UINT8 *colpointers, *startofspan;

	#define WRITE8(buf, a) ({*buf = (a); buf++;})
	#define WRITE16(buf, a) ({*buf = (a)&255; buf++; *buf = (a)>>8; buf++;})
	#define WRITE32(buf, a) ({WRITE16(buf, (a)&65535); WRITE16(buf, (a)>>16);})

	if (!raw)
		return NULL;

	// Write image size and offset
	WRITE16(imgptr, width);
	WRITE16(imgptr, height);
	// no offsets
	WRITE16(imgptr, 0);
	WRITE16(imgptr, 0);

	// Leave placeholder to column pointers
	colpointers = imgptr;
	imgptr += width*4;

	// Write columns
	for (x = 0; x < width; x++)
	{
		int lastStartY = 0;
		int spanSize = 0;
		startofspan = NULL;

		//printf("%d ", x);
		// Write column pointer (@TODO may be wrong)
		WRITE32(colpointers, imgptr - imgbuf);

		// Write pixels
		for (y = 0; y < height; y++)
		{
			UINT32 pixel = raw[((y * width) + x)];
			UINT8 opaque = (pixel & 0xFF000000)>>24; // If 1, we have a pixel

			// End span if we have a transparent pixel
			if (!opaque)
			{
				if (startofspan)
					WRITE8(imgptr, 0);
				startofspan = NULL;
				continue;
			}

			// Start new column if we need to
			if (!startofspan || spanSize == 255)
			{
				int writeY = y;

				// If we reached the span size limit, finish the previous span
				if (startofspan)
					WRITE8(imgptr, 0);

				if (y > 254)
				{
					// Make sure we're aligned to 254
					if (lastStartY < 254)
					{
						WRITE8(imgptr, 254);
						WRITE8(imgptr, 0);
						imgptr += 2;
						lastStartY = 254;
					}

					// Write stopgap empty spans if needed
					writeY = y - lastStartY;

					while (writeY > 254)
					{
						WRITE8(imgptr, 254);
						WRITE8(imgptr, 0);
						imgptr += 2;
						writeY -= 254;
					}
				}

				startofspan = imgptr;
				WRITE8(imgptr, writeY);///@TODO calculate starting y pos
				imgptr += 2;
				spanSize = 0;

				lastStartY = y;
			}

			// Write the pixel
			WRITE32(imgptr, pixel);
			spanSize++;
			startofspan[1] = spanSize;
		}

		if (startofspan)
			WRITE8(imgptr, 0);

		WRITE8(imgptr, 0xFF);
	}

	#undef WRITE8
	#undef WRITE16
	#undef WRITE32

	size = imgptr-imgbuf;
	img = malloc(size);
	memcpy(img, imgbuf, size);
	return (patch_t *)img;
}

boolean R_PNGDimensions(UINT8 *png, INT16 *width, INT16 *height, size_t size)
{
	png_structp png_ptr;
	png_infop png_info_ptr;
	png_uint_32 w, h;
	int bit_depth, color_type;
#ifdef PNG_SETJMP_SUPPORTED
#ifdef USE_FAR_KEYWORD
	jmp_buf jmpbuf;
#endif
#endif

	png_ioread png_io;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
		PNG_error, PNG_warn);
	if (!png_ptr)
	{
		CONS_Debug(DBG_RENDER, "PNG_Load: Error on initialize libpng\n");
		return false;
	}

	png_info_ptr = png_create_info_struct(png_ptr);
	if (!png_info_ptr)
	{
		CONS_Debug(DBG_RENDER, "PNG_Load: Error on allocate for libpng\n");
		png_destroy_read_struct(&png_ptr, NULL, NULL);
		return false;
	}

#ifdef USE_FAR_KEYWORD
	if (setjmp(jmpbuf))
#else
	if (setjmp(png_jmpbuf(png_ptr)))
#endif
	{
		//CONS_Debug(DBG_RENDER, "libpng load error on %s\n", filename);
		png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);
		return false;
	}
#ifdef USE_FAR_KEYWORD
	png_memcpy(png_jmpbuf(png_ptr), jmpbuf, sizeof jmp_buf);
#endif

	// set our own read_function
	png_io.buffer = (png_bytep)png;
	png_io.bufsize = size;
	png_io.current_pos = 0;
	png_set_read_fn(png_ptr, &png_io, PNG_IOReader);

#ifdef PNG_SET_USER_LIMITS_SUPPORTED
	png_set_user_limits(png_ptr, 2048, 2048);
#endif

	png_read_info(png_ptr, png_info_ptr);

	png_get_IHDR(png_ptr, png_info_ptr, &w, &h, &bit_depth, &color_type,
	 NULL, NULL, NULL);

	// okay done. stop.
	png_destroy_read_struct(&png_ptr, &png_info_ptr, NULL);

	*width = (INT32)w;
	*height = (INT32)h;
	return true;
}
#endif
#endif

void R_TextureToFlat(size_t tex, UINT32 *flat)
{
	texture_t *texture = textures[tex];

	fixed_t col, ofs;
	column_t *column;
	UINT32 *desttop, *dest, *deststop;
	UINT32 *source;

	desttop = flat;
	deststop = desttop + (texture->width * texture->height);

	for (col = 0; col < texture->width; col++, desttop++)
	{
		column = (column_t *)R_GetColumn(tex, col);
		dest = desttop;
		source = (UINT32 *)(column);
		for (ofs = 0; dest < deststop && ofs < texture->height; ofs++)
		{
			if (source[ofs] != V_GetTrueColor(TRANSPARENTPIXEL))
				*dest = source[ofs];
			dest += texture->width;
		}
	}
}

UINT32 *R_TruecolorMakeFlat(lumpnum_t lump)
{
	size_t ofs, size = W_LumpLength(lump);
	UINT8 *source = W_CacheLumpNum(lump, PU_STATIC);
	UINT32 *flat = Z_Calloc(size * sizeof(UINT32), PU_STATIC, NULL);
	for (ofs = 0; ofs < size; ofs++)
	{
		UINT32 pixel = V_GetTrueColor(source[ofs]);
		if (pixel != V_GetTrueColor(TRANSPARENTPIXEL))
			flat[ofs] = pixel;
	}
	Z_Free(source);
	return flat;
}
