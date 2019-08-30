// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2019 by Jaime "Jimita" Passos.
// Copyright (C) 2019 by Vin�cius "Arkus-Kotan" Tel�sforo.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_softpoly.c
/// \brief .

#include "r_softpoly.h"

rendertarget_t rsp_target;
viewpoint_t rsp_viewpoint;
fpmatrix16_t *rsp_projectionmatrix = NULL;

// init the polygon renderer, after resolution change
void RSP_Viewport(INT32 width, INT32 height)
{
	const float den = 1.7f;
	float fov = 90.0f - (48.0f / den);
	float aspecty = (BASEVIDHEIGHT * vid.dupy);

	// viewport width and height
	rsp_target.width = width;
	rsp_target.height = height;

	// viewport aspect ratio and fov
	if (splitscreen)
	{
		fov /= den;
		aspecty /= 2.0f;
	}
	rsp_target.aspectratio = ((float)(BASEVIDWIDTH * vid.dupx) / aspecty);
	rsp_target.fov = fov;
	fov *= (M_PI / 180.f);

	// make fixed-point depth buffer
	if (rsp_target.depthbuffer)
		Z_Free(rsp_target.depthbuffer);
	rsp_target.depthbuffer = (fixed_t *)Z_Malloc(sizeof(fixed_t) * (rsp_target.width * rsp_target.height), PU_STATIC, NULL);

	// renderer modes
	rsp_target.mode = (RENDERMODE_COLOR|RENDERMODE_DEPTH);
	rsp_target.cullmode = TRICULL_FRONT;

	// far and near plane (frustum clipping)
	rsp_target.far_plane = 32768.0f;
	rsp_target.near_plane = 16.0f;

	// make projection matrix
	RSP_MakePerspectiveMatrix(&rsp_viewpoint.projection_matrix, fov, rsp_target.aspectratio, 0.1f, rsp_target.far_plane);

	// set pixel functions
	rsp_basepixelfunc = RSP_DrawPixel;
	rsp_transpixelfunc = RSP_DrawTranslucentPixel;

	// set triangle functions
	rsp_fixedtrifunc = RSP_TexturedMappedTriangle;
	rsp_floattrifunc = RSP_TexturedMappedTriangleFP;
}

// up vector
// right vector not needed
static fpvector4_t upvector = {0.0f, 1.0f, 0.0f, 1.0f};

// make all the vectors and matrixes
static void RSP_SetupFrame(fixed_t vx, fixed_t vy, fixed_t vz, angle_t vangle)
{
	fpmatrix16_t modelview;
	fixed_t angle = AngleFixed(vangle - ANGLE_90);
	float viewang = FIXED_TO_FLOAT(angle);

	// make position and target vectors
	RSP_MakeVector4(rsp_viewpoint.position_vector, FIXED_TO_FLOAT(vx), -FIXED_TO_FLOAT(vz), -FIXED_TO_FLOAT(vy));
	RSP_MakeVector4(rsp_viewpoint.target_vector, 0, 0, -1);

	// make view matrix
	RSP_VectorRotate(&rsp_viewpoint.target_vector, (viewang * M_PI / 180.0f), upvector.x, upvector.y, upvector.z);
	RSP_MakeViewMatrix(&rsp_viewpoint.view_matrix, &rsp_viewpoint.position_vector, &rsp_viewpoint.target_vector, &upvector);

	// make "model view projection" matrix
	// in reality, there is no model matrix
	modelview = RSP_MatrixMultiply(&rsp_viewpoint.view_matrix, &rsp_viewpoint.projection_matrix);
	if (rsp_projectionmatrix == NULL)
		rsp_projectionmatrix = Z_Malloc(sizeof(fpmatrix16_t), PU_STATIC, NULL);
	M_Memcpy(rsp_projectionmatrix, &modelview, sizeof(fpmatrix16_t));
}

// setup frame
void RSP_ModelView(void)
{
	// Arkus: Set pixel drawer.
	rsp_curpixelfunc = rsp_basepixelfunc;

	// also set triangle drawer
	if (cv_texturemapping.value == TEXMAP_FIXED)
		rsp_curtrifunc = rsp_fixedtrifunc;
	else if (cv_texturemapping.value == TEXMAP_FLOAT)
		rsp_curtrifunc = rsp_floattrifunc;
	else
		rsp_curtrifunc = NULL;

	// Clear the depth buffer, and setup the matrixes.
	RSP_ClearDepthBuffer();
	RSP_SetupFrame(viewx, viewy, viewz, viewangle);
}

// on frame start
void RSP_OnFrame(void)
{
	RSP_ModelView();
	rsp_viewwindowx = viewwindowx;
	rsp_viewwindowy = viewwindowy;
	rsp_target.aiming = true;
}

// PORTAL STUFF

// Store the current viewpoint
void RSP_StoreViewpoint(void)
{
	rsp_viewpoint.viewx = viewx;
	rsp_viewpoint.viewy = viewy;
	rsp_viewpoint.viewz = viewz;
	rsp_viewpoint.viewangle = viewangle;
	rsp_viewpoint.aimingangle = aimingangle;
	rsp_viewpoint.viewcos = viewcos;
	rsp_viewpoint.viewsin = viewsin;
}

// Restore the stored viewpoint
void RSP_RestoreViewpoint(void)
{
	viewx = rsp_viewpoint.viewx;
	viewy = rsp_viewpoint.viewy;
	viewz = rsp_viewpoint.viewz;
	viewangle = rsp_viewpoint.viewangle;
	aimingangle = rsp_viewpoint.aimingangle;
	viewcos = rsp_viewpoint.viewcos;
	viewsin = rsp_viewpoint.viewsin;
	RSP_ModelView();
}

// Store a sprite's viewpoint
void RSP_StoreSpriteViewpoint(vissprite_t *spr)
{
	spr->viewx = viewx;
	spr->viewy = viewy;
	spr->viewz = viewz;
	spr->viewangle = viewangle;
	spr->aimingangle = aimingangle;
	spr->viewcos = viewcos;
	spr->viewsin = viewsin;
}

// Set viewpoint to a sprite's viewpoint
void RSP_RestoreSpriteViewpoint(vissprite_t *spr)
{
	viewx = spr->viewx;
	viewy = spr->viewy;
	viewz = spr->viewz;
	viewangle = spr->viewangle;
	aimingangle = spr->aimingangle;
	viewcos = spr->viewcos;
	viewsin = spr->viewsin;
	RSP_ModelView();
}

// clear the depth buffer
void RSP_ClearDepthBuffer(void)
{
	memset(rsp_target.depthbuffer, 0, sizeof(fixed_t) * rsp_target.width * rsp_target.height);
}

// Arkus: Debug rendering.
void RSP_DebugRender(fixed_t vx, fixed_t vy, fixed_t vz, INT32 wx, INT32 wy)
{
	fixed_t z = vz;
	static float angle;
	angle -= 2.0f;

	RSP_ClearDepthBuffer();
	rsp_viewwindowx = wx;
	rsp_viewwindowy = wy;
	rsp_target.aiming = false;

	//RSP_RenderModelSimple(SPR_PLAY, 0, angle, SKINCOLOR_BLUE, NULL, false);

	RSP_SetupFrame(vx, vy, z, ANGLE_90);
	RSP_RenderModelSimple(SPR_SIGN, Color_Opposite[SKINCOLOR_BLUE*2+1], angle, Color_Opposite[SKINCOLOR_BLUE*2], NULL, false);
	RSP_SetupFrame(vx, vy, z - 24*FRACUNIT, ANGLE_90);
	RSP_RenderModelSimple(SPR_PLAY, states[S_PLAY_SIGN].frame, angle, SKINCOLOR_BLUE, &skins[0], false);
}
