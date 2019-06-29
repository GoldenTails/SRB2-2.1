//------------------------------------------------------------------------
// GLBSP.H : Interface to Node Builder
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

#ifndef __GLBSP_GLBSP_H__
#define __GLBSP_GLBSP_H__

// certain GCC attributes can be useful
#undef GCCATTR
#ifdef __GNUC__
#define GCCATTR(xyz)  __attribute__ (xyz)
#else
#define GCCATTR(xyz)  /* nothing */
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "../doomtype.h"
boolean GLBSP_HandleLevel(void);

/* ----- basic types --------------------------- */

typedef double float_g;
typedef double angle_g;  // degrees, 0 is E, 90 is N

// boolean type
typedef int boolean_g;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif /* __GLBSP_GLBSP_H__ */
