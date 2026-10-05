//========= Copyright (c) 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

// Camera.h  --  defines and such for a 3rd person camera
// NOTE: must include quakedef.h first
#pragma once
#if !defined(_CAMERA_H_)
#define _CAMERA_H_

// pitch, yaw, dist
extern vec3_t cam_ofs;
// Camera point offset, relative to the camera's own axes:
// [0] = right/left, [1] = extra forward/back on top of cam_ofs[2],
// [2] = up/down. Driven by cam_xoffset/cam_yoffset/cam_zoffset.
extern vec3_t cam_extra_ofs;
// Using third person camera
extern int cam_thirdperson;

// Requested view mode (first/third person). Unlike cam_thirdperson,
// which the collision code may clear temporarily, this remembers intent.
#define VIEW_FIRSTPERSON 0
#define VIEW_THIRDPERSON 1
extern int cam_idealview;

#ifdef __cplusplus
extern "C" {
#endif
int CL_IsThirdPerson( void );
#ifdef __cplusplus
}
#endif

void CAM_Init( void );
void CAM_ClearStates( void );
void CAM_StartMouseMove( void );
void CAM_EndMouseMove( void );
#endif // _CAMERA_H_
