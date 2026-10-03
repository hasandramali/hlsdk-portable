/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// Train.cpp
//
// implementation of CHudAmmo class
//

#include "hud.h"
#include "cl_util.h"
#include <string.h>
#include <stdio.h>
#include "parsemsg.h"

DECLARE_MESSAGE( m_Train, Train )

int CHudTrain::Init( void )
{
	HOOK_MESSAGE( Train );

	m_iPos = 0;
	m_iFlags = 0;
	gHUD.AddHudElem( this );

	return 1;
}

int CHudTrain::VidInit( void )
{
	// Sven client.dll 1005b980 loads the unscaled, multi-frame train HUD.
	m_hSprite = SPR_Load( "sprites/train.spr" );
	if( !m_hSprite )
		m_hSprite = LoadSprite( "sprites/%d_train.spr" );

	return 1;
}

int CHudTrain::Draw( float fTime )
{
	// Never pass an invalid handle/frame to SPR_Set: the engine retains
	// the previous sprite on an invalid handle, drawing unrelated HUD art.
	if( !m_hSprite || m_iPos <= 0 || m_iPos > SPR_Frames( m_hSprite ) )
		return 1;

	if( m_iPos )
	{
		int r, g, b, x, y;

		UnpackRGB( r, g, b, RGB_BLUEISH );

		// This should show up to the right and part way up the armor number
		y = ScreenHeight - SPR_Height( m_hSprite, 0 ) - gHUD.m_iFontHeight;
		x = ScreenWidth / 3 + gHUD.m_iFontHeight + SPR_Width( m_hSprite, 0 ) / 4;

		gHUD.DrawSprite( x, y, m_hSprite, NULL, r, g, b, m_iPos - 1, SPR_ADDITIVE );
	}

	return 1;
}

int CHudTrain::MsgFunc_Train( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );

	// update Train data
	m_iPos = READ_BYTE();

	if( m_iPos )
		m_iFlags |= HUD_ACTIVE;
	else
		m_iFlags &= ~HUD_ACTIVE;

	return 1;
}
