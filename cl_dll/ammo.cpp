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
// Ammo.cpp
//
// implementation of CHudAmmo class
//

#include "hud.h"
#include "sven_ui.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "pm_shared.h"
#include "event_api.h"

#include <string.h>
#include <stdio.h>

#include "ammohistory.h"
#if USE_VGUI
#include "vgui_TeamFortressViewport.h"
#endif

WEAPON *gpActiveSel;	// NULL means off, 1 means just the menu bar, otherwise
						// this points to the active weapon menu item
WEAPON *gpLastSel;		// Last weapon menu selection 

client_sprite_t *GetSpriteList(client_sprite_t *pList, const char *psz, int iRes, int iCount);

static bool SpriteNameHasScope( const char *name )
{
	for( const char *current = name; current && *current; current++ )
	{
		if( strlen( current ) < 5 )
			break;
		if( ( current[0] == 's' || current[0] == 'S' ) &&
			( current[1] == 'c' || current[1] == 'C' ) &&
			( current[2] == 'o' || current[2] == 'O' ) &&
			( current[3] == 'p' || current[3] == 'P' ) &&
			( current[4] == 'e' || current[4] == 'E' ) )
			return true;
	}
	return false;
}

static HSPRITE LoadWeaponSprite( const char *spriteDir, const char *spriteName )
{
	char path[512];
	char resource[384];
	const char *slash;
	const char *extension;
	HSPRITE sprite;

	if( !spriteName || !spriteName[0] )
		return 0;

	if( !strncmp( spriteName, "sprites/", 8 ) )
	{
		strlcpy( resource, spriteName, sizeof( resource ) );
	}
	else
	{
		snprintf( resource, sizeof( resource ), "sprites/%s", spriteName );
	}

	slash = strrchr( resource, '/' );
	extension = strrchr( resource, '.' );
	if( extension && slash && extension < slash )
		extension = NULL;
	if( !extension )
		strlcat( resource, ".spr", sizeof( resource ) );

	sprite = SPR_Load( resource );
	if( sprite || !spriteDir || !spriteDir[0] || !strncmp( spriteName, "sprites/", 8 ) )
		return sprite;

	snprintf( path, sizeof( path ), "sprites/%s/%s", spriteDir, spriteName );
	slash = strrchr( path, '/' );
	extension = strrchr( path, '.' );
	if( extension && slash && extension < slash )
		extension = NULL;
	if( !extension )
		strlcat( path, ".spr", sizeof( path ) );
	return SPR_Load( path );
}

WeaponsResource gWR;

int g_weaponselect = 0;

// Sven custom-weapon sprite subdirectory table, indexed by weapon id.
// CustWeapon carries [SHORT id][STRING subdir] (stock MsgFunc_CustWeapon
// 0x100047d0 stores it at WEAPON+0x1b5); it is NEVER a class name, so it
// must not enter the weapon inventory — it only selects the sprite set.
static char s_szCustSprDir[MAX_HUD_WEAPONS][64];
static char s_szWeaponSpr[MAX_HUD_WEAPONS][264];

void WeaponsResource::LoadAllWeaponSprites( void )
{
	for( int i = 0; i < MAX_HUD_WEAPONS; i++ )
	{
		if( rgWeapons[i].iId )
			LoadWeaponSprites( &rgWeapons[i] );
	}
}

int WeaponsResource::CountAmmo( int iId ) 
{ 
	if( iId < 0 || iId >= MAX_AMMO_TYPES )
		return 0;

	return riAmmo[iId];
}

int WeaponsResource::HasAmmo( WEAPON *p )
{
	if( !p )
		return FALSE;

	// weapons with no max ammo can always be selected
	if( p->iMax1 == -1 )
		return TRUE;

	return ( p->iAmmoType == -1 ) || p->iClip > 0 || CountAmmo( p->iAmmoType ) 
		|| CountAmmo( p->iAmmo2Type ) || ( p->iFlags & WEAPON_FLAGS_SELECTONEMPTY );
}

void WeaponsResource::LoadWeaponSprites( WEAPON *pWeapon )
{
	int i, iRes;

	iRes = GetSpriteRes( ScreenWidth, ScreenHeight );

	char sz[256];

	if( !pWeapon )
		return;
	if( pWeapon->iId > 0 && pWeapon->iId < MAX_HUD_WEAPONS )
	{
		if( !pWeapon->szSpriteDir[0] && s_szCustSprDir[pWeapon->iId][0] )
			strlcpy( pWeapon->szSpriteDir, s_szCustSprDir[pWeapon->iId], sizeof( pWeapon->szSpriteDir ) );
		if( !pWeapon->szSpriteRecord[0] && s_szWeaponSpr[pWeapon->iId][0] )
			strlcpy( pWeapon->szSpriteRecord, s_szWeaponSpr[pWeapon->iId], sizeof( pWeapon->szSpriteRecord ) );
	}

	memset( &pWeapon->rcActive, 0, sizeof(wrect_t) );
	memset( &pWeapon->rcInactive, 0, sizeof(wrect_t) );
	memset( &pWeapon->rcAmmo, 0, sizeof(wrect_t) );
	memset( &pWeapon->rcAmmo2, 0, sizeof(wrect_t) );
	pWeapon->hInactive = 0;
	pWeapon->hActive = 0;
	pWeapon->hAmmo = 0;
	pWeapon->hAmmo2 = 0;
	pWeapon->hCrosshair = 0;
	pWeapon->hAutoaim = 0;
	pWeapon->hZoomedCrosshair = 0;
	pWeapon->hZoomedAutoaim = 0;
	pWeapon->hScopeOverlay = 0;

	// Stock client.dll LoadWeaponSprites (0x10003d10): when the CustWeapon
	// subdirectory is present it loads "sprites/<subdir>/<weapon>.txt",
	// otherwise the plain "sprites/<weapon>.txt". Custom map weapons (e.g.
	// They Hunger: subdir "hunger/weapons") only exist under their subdir.
	const char *spriteDir = pWeapon->szSpriteDir[0] ? pWeapon->szSpriteDir :
		( pWeapon->iId > 0 && pWeapon->iId < MAX_HUD_WEAPONS ? s_szCustSprDir[pWeapon->iId] : "" );
	if( spriteDir[0] )
		snprintf( sz, sizeof( sz ), "sprites/%s/%s.txt", spriteDir, pWeapon->szName );
	else
		snprintf( sz, sizeof( sz ), "sprites/%s.txt", pWeapon->szName );
	client_sprite_t *pList = SPR_GetList( sz, &i );
	if( !pList && spriteDir[0] )
	{
		snprintf( sz, sizeof( sz ), "sprites/%s.txt", pWeapon->szName );
		pList = SPR_GetList( sz, &i );
	}
	const char *spriteRecord = pWeapon->szSpriteRecord[0] ? pWeapon->szSpriteRecord :
		( pWeapon->iId > 0 && pWeapon->iId < MAX_HUD_WEAPONS ? s_szWeaponSpr[pWeapon->iId] : "" );
	if( !pList && spriteRecord[0] )
	{
		snprintf( sz, sizeof( sz ), "sprites/%s.txt", spriteRecord );
		pList = SPR_GetList( sz, &i );
	}

	if( !pList )
		return;

	client_sprite_t *p;

	p = GetSpriteList( pList, "crosshair", iRes, i );
	if( p )
	{
		pWeapon->hCrosshair = LoadWeaponSprite( spriteDir, p->szSprite );
		pWeapon->rcCrosshair = p->rc;
	}
	else
		pWeapon->hCrosshair = 0;

	p = GetSpriteList( pList, "autoaim", iRes, i );
	if( p )
	{
		pWeapon->hAutoaim = LoadWeaponSprite( spriteDir, p->szSprite );
		pWeapon->rcAutoaim = p->rc;
	}
	else
		pWeapon->hAutoaim = 0;

	p = GetSpriteList( pList, "zoom", iRes, i );
	client_sprite_t *zoomSprite = p;
	if( p )
	{
		pWeapon->hZoomedCrosshair = LoadWeaponSprite( spriteDir, p->szSprite );
		pWeapon->rcZoomedCrosshair = p->rc;
	}
	else
	{
		pWeapon->hZoomedCrosshair = pWeapon->hCrosshair; //default to non-zoomed crosshair
		pWeapon->rcZoomedCrosshair = pWeapon->rcCrosshair;
	}

	// Custom weapons may publish a separate scope entry instead of calling it
	// "zoom". Discover it from the server-provided sprite list, never from a
	// weapon classname, so newly-added AngelScript weapons work automatically.
	for( int scopeIndex = 0; scopeIndex < i; scopeIndex++ )
	{
		client_sprite_t *scope = &pList[scopeIndex];
		if( scope != zoomSprite && scope->iRes == iRes &&
			( SpriteNameHasScope( scope->szName ) || SpriteNameHasScope( scope->szSprite ) ) )
		{
			HSPRITE hScope = LoadWeaponSprite( spriteDir, scope->szSprite );
			if( hScope )
			{
				pWeapon->hScopeOverlay = hScope;
				pWeapon->rcScopeOverlay = scope->rc;
			}
			if( hScope )
				break;
		}
	}

	p = GetSpriteList( pList, "zoom_autoaim", iRes, i );
	if( p )
	{
		pWeapon->hZoomedAutoaim = LoadWeaponSprite( spriteDir, p->szSprite );
		pWeapon->rcZoomedAutoaim = p->rc;
	}
	else
	{
		pWeapon->hZoomedAutoaim = pWeapon->hZoomedCrosshair;  //default to zoomed crosshair
		pWeapon->rcZoomedAutoaim = pWeapon->rcZoomedCrosshair;
	}

	p = GetSpriteList( pList, "weapon", iRes, i );
	if( p )
	{
		pWeapon->hInactive = LoadWeaponSprite( spriteDir, p->szSprite );
		pWeapon->rcInactive = p->rc;

		gHR.iHistoryGap = Q_max( gHR.iHistoryGap, pWeapon->rcActive.bottom - pWeapon->rcActive.top );
	}
	else
		pWeapon->hInactive = 0;

	p = GetSpriteList( pList, "weapon_s", iRes, i );
	if( p )
	{
		pWeapon->hActive = LoadWeaponSprite( spriteDir, p->szSprite );
		pWeapon->rcActive = p->rc;
	}
	else
		pWeapon->hActive = 0;

	p = GetSpriteList( pList, "ammo", iRes, i );
	if( p )
	{
		pWeapon->hAmmo = LoadWeaponSprite( spriteDir, p->szSprite );
		pWeapon->rcAmmo = p->rc;

		gHR.iHistoryGap = Q_max( gHR.iHistoryGap, pWeapon->rcActive.bottom - pWeapon->rcActive.top );
	}
	else
		pWeapon->hAmmo = 0;

	p = GetSpriteList( pList, "ammo2", iRes, i );
	if( p )
	{
		pWeapon->hAmmo2 = LoadWeaponSprite( spriteDir, p->szSprite );
		pWeapon->rcAmmo2 = p->rc;

		gHR.iHistoryGap = Q_max( gHR.iHistoryGap, pWeapon->rcActive.bottom - pWeapon->rcActive.top );
	}
	else
		pWeapon->hAmmo2 = 0;
}

// Returns the first weapon for a given slot.
WEAPON *WeaponsResource::GetFirstPos( int iSlot )
{
	WEAPON *pret = NULL;

	for( int i = 0; i < MAX_WEAPON_POSITIONS; i++ )
	{
		if ( rgSlots[iSlot][i] && HasAmmo( rgSlots[iSlot][i] ) )
		{
			pret = rgSlots[iSlot][i];
			break;
		}
	}

	return pret;
}

WEAPON* WeaponsResource::GetNextActivePos( int iSlot, int iSlotPos )
{
	if ( iSlotPos >= MAX_WEAPON_POSITIONS || iSlot >= MAX_WEAPON_SLOTS )
		return NULL;

	WEAPON *p = gWR.rgSlots[iSlot][iSlotPos + 1];
	
	if ( !p || !gWR.HasAmmo( p ) )
		return GetNextActivePos( iSlot, iSlotPos + 1 );

	return p;
}

int giBucketHeight, giBucketWidth, giABHeight, giABWidth; // Ammo Bar width and height

HSPRITE ghsprBuckets;					// Sprite for top row of weapons menu

DECLARE_MESSAGE( m_Ammo, CurWeapon )	// Current weapon and clip
DECLARE_MESSAGE( m_Ammo, WeaponList )	// new weapon type
DECLARE_MESSAGE( m_Ammo, CustWeapon )	// custom weapon sprite subdirectory (NOT a class name)
DECLARE_MESSAGE( m_Ammo, AmmoX )		// update known ammo type's count
DECLARE_MESSAGE( m_Ammo, AmmoPickup )	// flashes an ammo pickup record
DECLARE_MESSAGE( m_Ammo, WeapPickup )    // flashes a weapon pickup record
DECLARE_MESSAGE( m_Ammo, HideWeapon )	// hides the weapon, ammo, and crosshair displays temporarily
DECLARE_MESSAGE( m_Ammo, ItemPickup )
DECLARE_MESSAGE( m_Ammo, InvRemove )	// Sven inventory removal, [LONG id][BYTE]
DECLARE_MESSAGE( m_Ammo, HideHUD )	// Sven HUD hide flags, LE SHORT (client.so 0xA0026)
DECLARE_MESSAGE( m_Ammo, TE_CUSTOM )	// Sven custom effect/state, [BYTE type](1/2/3)
DECLARE_MESSAGE( m_Ammo, WeaponSpr )	// Sven weapon sprite payload, [SHORT id][STRING]
DECLARE_MESSAGE( m_Ammo, ServerVer )	// Sven server version, [STRING]
DECLARE_MESSAGE( m_Ammo, MapList )	// Sven chunked map-vote list
DECLARE_MESSAGE( m_Ammo, ClServerInfo )	// Sven server info, [BYTE][LONG][STRING44]
DECLARE_MESSAGE( m_Ammo, ClExtrasInfo )	// Sven custom HUD table (grammar TBD; consume only)

DECLARE_COMMAND( m_Ammo, Slot1 )
DECLARE_COMMAND( m_Ammo, Slot2 )
DECLARE_COMMAND( m_Ammo, Slot3 )
DECLARE_COMMAND( m_Ammo, Slot4 )
DECLARE_COMMAND( m_Ammo, Slot5 )
DECLARE_COMMAND( m_Ammo, Slot6 )
DECLARE_COMMAND( m_Ammo, Slot7 )
DECLARE_COMMAND( m_Ammo, Slot8 )
DECLARE_COMMAND( m_Ammo, Slot9 )
DECLARE_COMMAND( m_Ammo, Slot10 )
DECLARE_COMMAND( m_Ammo, Close )
DECLARE_COMMAND( m_Ammo, NextWeapon )
DECLARE_COMMAND( m_Ammo, PrevWeapon )

// width of ammo fonts
#define AMMO_SMALL_WIDTH 10
#define AMMO_LARGE_WIDTH 20

#define HISTORY_DRAW_TIME	"5"

int CHudAmmo::Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( CurWeapon );
	HOOK_MESSAGE( WeaponList );
	HOOK_MESSAGE( CustWeapon );
	HOOK_MESSAGE( AmmoPickup );
	HOOK_MESSAGE( WeapPickup );
	HOOK_MESSAGE( ItemPickup );
	HOOK_MESSAGE( HideWeapon );
	HOOK_MESSAGE( InvRemove );
	HOOK_MESSAGE( HideHUD );
	HOOK_MESSAGE( TE_CUSTOM );
	HOOK_MESSAGE( WeaponSpr );
	HOOK_MESSAGE( ServerVer );
	HOOK_MESSAGE( MapList );
	HOOK_MESSAGE( ClServerInfo );
	HOOK_MESSAGE( ClExtrasInfo );
	HOOK_MESSAGE( AmmoX );

	HOOK_COMMAND( "slot1", Slot1 );
	HOOK_COMMAND( "slot2", Slot2 );
	HOOK_COMMAND( "slot3", Slot3 );
	HOOK_COMMAND( "slot4", Slot4 );
	HOOK_COMMAND( "slot5", Slot5 );
	HOOK_COMMAND( "slot6", Slot6 );
	HOOK_COMMAND( "slot7", Slot7 );
	HOOK_COMMAND( "slot8", Slot8 );
	HOOK_COMMAND( "slot9", Slot9 );
	HOOK_COMMAND( "slot10", Slot10 );
	HOOK_COMMAND( "cancelselect", Close );
	HOOK_COMMAND( "invnext", NextWeapon );
	HOOK_COMMAND( "invprev", PrevWeapon );

	Reset();

	CVAR_CREATE( "hud_drawhistory_time", HISTORY_DRAW_TIME, 0 );
	CVAR_CREATE( "hud_fastswitch", "0", FCVAR_ARCHIVE );		// controls whether or not weapons can be selected in one keypress

	m_iFlags |= HUD_ACTIVE; //!!!

	gWR.Init();
	gHR.Init();

	return 1;
}

void CHudAmmo::Reset( void )
{
	m_fFade = 0;
	m_iFlags |= HUD_ACTIVE; //!!!

	gpActiveSel = NULL;
	gHUD.m_iHideHUDDisplay = 0;

	for( int i = 0; i < MAX_NEW_PICKUPS; i++ )
		m_iNewPickupIds[i] = 0;
	m_fMenuCloseAt = 0.0f;

	gWR.Reset();
	gHR.Reset();

	//VidInit();
	wrect_t nullrc = {0,};
	SetCrosshair( 0, nullrc, 0, 0, 0 ); // reset crosshair
	m_pWeapon = NULL; // reset last weapon
}

int CHudAmmo::VidInit( void )
{
	// Load sprites for buckets (top row of weapon menu)
	m_HUD_bucket0 = gHUD.GetSpriteIndex( "bucket1" );
	m_HUD_selection = gHUD.GetSpriteIndex( "selection" );

	ghsprBuckets = gHUD.GetSprite( m_HUD_bucket0 );
	giBucketWidth = gHUD.GetSpriteRect( m_HUD_bucket0 ).right - gHUD.GetSpriteRect( m_HUD_bucket0 ).left;
	giBucketHeight = gHUD.GetSpriteRect( m_HUD_bucket0 ).bottom - gHUD.GetSpriteRect( m_HUD_bucket0 ).top;

	gHR.iHistoryGap = gHUD.GetSpriteRect( m_HUD_bucket0 ).bottom - gHUD.GetSpriteRect( m_HUD_bucket0 ).top;

	// If we've already loaded weapons, let's get new sprites
	gWR.LoadAllWeaponSprites();

	const int res = GetSpriteRes( ScreenWidth, ScreenHeight );
	int factor;
	if( res >= 2560 )
		factor = 4;
	else if( res >= 1280 )
		factor = 3;
	else if( res >= 640 )
		factor = 2;
	else
		factor = 1;

	giABWidth = 10 * factor;
	giABHeight = 2 * factor;

	return 1;
}

//
// Think:
//  Used for selection of weapon menu item.
//
void CHudAmmo::Think( void )
{
	// Gauss spin failsafe (proedu): the pulsemachine loop is ONLY legitimate
	// while +attack2 is held (charging). Any loop with attack2 released is
	// stale by definition: a missed server stop event, a late spin-restart
	// arriving after a kill (the unlucky-timing case), or a pierce-shot
	// leftover where no fire event ever stops it (ricochets stop via their
	// event path, wall-pierces don't). Watching the local button STATE (not
	// just the release edge) guarantees the stop: kill whenever attack2 is
	// not held, for a bounded grace period, and suppress late restarts at
	// the event itself (see EV_SpinGauss).
	{
		static qboolean s_bGaussWasActive = FALSE;
		static int s_iSpinQuiet = 0;
		int iButtons = gHUD.m_iKeyBits;
		qboolean bAttack2Held = ( iButtons & IN_ATTACK2 ) ? TRUE : FALSE;
		qboolean bGaussActive = ( m_pWeapon && !strcmp( m_pWeapon->szName, "weapon_gauss" )) ? TRUE : FALSE;

		if( bGaussActive && bAttack2Held )
		{
			s_bGaussWasActive = TRUE;
			s_iSpinQuiet = 0;
		}
		else if( s_bGaussWasActive )
		{
			struct cl_entity_s *pLocal = gEngfuncs.GetLocalPlayer();
			if( pLocal )
			{
				gEngfuncs.pEventAPI->EV_KillEvents( pLocal->index, "events/gaussspin.sc" );
				gEngfuncs.pEventAPI->EV_StopSound( pLocal->index, CHAN_WEAPON, "ambience/pulsemachine.wav" );
			}
			if( ++s_iSpinQuiet > 90 || !bGaussActive )
			{
				// 1.5s of kills (or weapon switched away): stand down.
				// Anything arriving later is suppressed at EV_SpinGauss.
				s_bGaussWasActive = FALSE;
				s_iSpinQuiet = 0;
			}
		}
	}

	if( gHUD.m_fPlayerDead )
		return;

	if( gHUD.m_iWeaponBits != gWR.iOldWeaponBits )
	{
		gWR.iOldWeaponBits = gHUD.m_iWeaponBits;

		for( int i = MAX_HUD_WEAPONS-1; i > 0; i-- )
		{
			WEAPON *p = gWR.GetWeapon( i );

			if( p && p->iId )
			{
				if( gHUD.m_iWeaponBits & ( 1 << p->iId ) )
					gWR.PickupWeapon( p );
				else
					gWR.DropWeapon( p );
			}
		}
	}

	if( !gpActiveSel )
		return;

	// has the player selected one?
	if( gHUD.m_iKeyBits & IN_ATTACK )
	{
		if( gpActiveSel != (WEAPON *) 1 )
		{
			ServerCmd( gpActiveSel->szName );
			g_weaponselect = gpActiveSel->iId;
		}

		gpLastSel = gpActiveSel;
		gpActiveSel = NULL;
		gHUD.m_iKeyBits &= ~IN_ATTACK;

		PlaySound( "common/wpn_select.wav", 1 );
	}

}

//
// Helper function to return a Ammo pointer from id
//
HSPRITE* WeaponsResource::GetAmmoPicFromWeapon( int iAmmoId, wrect_t& rect )
{
	for( int i = 0; i < MAX_HUD_WEAPONS; i++ )
	{
		if( rgWeapons[i].iAmmoType == iAmmoId )
		{
			rect = rgWeapons[i].rcAmmo;
			return &rgWeapons[i].hAmmo;
		}
		else if( rgWeapons[i].iAmmo2Type == iAmmoId )
		{
			rect = rgWeapons[i].rcAmmo2;
			return &rgWeapons[i].hAmmo2;
		}
	}

	return NULL;
}

// Menu Selection Code
void WeaponsResource::SelectSlot( int iSlot, int fAdvance, int iDirection )
{
	// Server-driven menus (e.g. /buy) override slot keys entirely, so keep
	// this branch first: with SelectSlot enabled the menu becomes usable.
	if( gHUD.m_Menu.m_fMenuDisplayed && ( fAdvance  == FALSE ) && ( iDirection == 1 ) )	
	{
		// menu is overriding slot use commands
		gHUD.m_Menu.SelectMenuItem( iSlot + 1 );  // slots are one off the key numbers
		return;
	}

	if( iSlot > MAX_WEAPON_SLOTS )
		return;

	if( gHUD.m_fPlayerDead || gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) )
		return;

	if ( !( gHUD.m_iWeaponBits & ( 1 << ( WEAPON_SUIT ) ) ) )
		return;

	if( ! ( gHUD.m_iWeaponBits & ~( 1 << ( WEAPON_SUIT ) ) ) )
		return;

	// Sven-port fast-switch: a slot key selects IMMEDIATELY and never opens
	// the top-left weapon sprite menu (full hud_fastswitch effect). The press
	// still counts: the select sound plays and the weapon is sent to the
	// server right away. Pressing the same slot again cycles through the
	// owned weapons in that slot. gpActiveSel stays NULL so DrawWList draws
	// nothing from the slot path.
	WEAPON *p = NULL;

	if( gpLastSel && gpLastSel != (WEAPON *)1 && gpLastSel->iSlot == iSlot )
	{
		PlaySound( "common/wpn_moveselect.wav", 1 );
		p = GetNextActivePos( iSlot, gpLastSel->iSlotPos );
		if( !p )
			p = GetFirstPos( iSlot );
	}
	else
	{
		PlaySound( "common/wpn_hudon.wav", 1 );
		p = GetFirstPos( iSlot );
	}

	if( !p )  // empty slot: no menu, no selection
		return;

	ServerCmd( p->szName );
	g_weaponselect = p->iId;
	gpLastSel = p;
	gpActiveSel = NULL;
}

//------------------------------------------------------------------------
// Message Handlers
//------------------------------------------------------------------------

//
// AmmoX  -- Update the count of a known type of ammo
// 
int CHudAmmo::MsgFunc_AmmoX( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );

	// Sven server.dll writes [BYTE ammo type][LONG count] (regsize 5); the
	// stock client reads the count as a signed LONG and stores abs(). Our old
	// BYTE/BYTE read truncated to the low byte, so modded servers that push
	// big reserves (e.g. 10000) displayed the low byte (16). Read the full
	// LONG so values up to 2^31-1 survive into the HUD.
	if( iSize < 5 )
		return 0;
	int iIndex = READ_BYTE();
	int iCount = READ_LONG();

	if( iIndex >= 0 && iIndex < MAX_AMMO_TYPES )
		gWR.SetAmmo( iIndex, abs( iCount ) );

	// TEMP-DIAG (dual-uzi HUD): confirm the LONG count arrives intact.
	if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0 )
		gEngfuncs.Con_Printf( "TEMP-DIAG AmmoX idx=%d count=%d\n", iIndex, iCount );

	return 1;
}

int CHudAmmo::MsgFunc_AmmoPickup( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	int iIndex = READ_BYTE();
	int iCount = READ_BYTE();

	// Add ammo to the history
	gHR.AddToHistory( HISTSLOT_AMMO, iIndex, abs( iCount ) );

	return 1;
}

int CHudAmmo::MsgFunc_WeapPickup( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	int iIndex = READ_SHORT();

	// Add the weapon to the history
	gHR.AddToHistory( HISTSLOT_WEAP, iIndex );

	// Top-left text notifier (menu closed) or green menu highlight (menu open).
	// WeapPickup itself is only sent for genuine ground pickups, so a weapon
	// the player already holds never triggers anything here.
	OnWeaponPickup( iIndex );

	return 1;
}

// Seconds of menu quiet before it auto-closes. Refreshed on every nav step
// and on every menu-open pickup.
#define WEAPONMENU_CLOSE_TIME 5.0f

void CHudAmmo::RefreshMenuTimer( void )
{
	m_fMenuCloseAt = gHUD.m_flTime + WEAPONMENU_CLOSE_TIME;
}

void CHudAmmo::CloseWeaponMenu( bool playSound )
{
	if( !gpActiveSel )
		return;
	gpLastSel = gpActiveSel;
	gpActiveSel = NULL;
	for( int i = 0; i < MAX_NEW_PICKUPS; i++ )
		m_iNewPickupIds[i] = 0;
	if( playSound )
		PlaySound( "common/wpn_hudoff.wav", 1 );
}

bool CHudAmmo::IsNewPickupWeapon( int iId ) const
{
	if( iId <= 0 )
		return false;
	for( int i = 0; i < MAX_NEW_PICKUPS; i++ )
		if( m_iNewPickupIds[i] == iId )
			return true;
	return false;
}

bool CHudAmmo::IsNewPickupSlot( int iSlot ) const
{
	for( int i = 0; i < MAX_NEW_PICKUPS; i++ )
	{
		if( m_iNewPickupIds[i] <= 0 )
			continue;
		WEAPON *p = gWR.GetWeapon( m_iNewPickupIds[i] );
		if( p && p->iId && p->iSlot == iSlot )
			return true;
	}
	return false;
}

void CHudAmmo::OnWeaponPickup( int iId )
{
	if( iId <= 0 || iId >= MAX_HUD_WEAPONS )
		return;
	if( gpActiveSel )
	{
		// Menu open: no top-left text (and the menu is never auto-opened
		// from here); remember the id for a temporary green highlight and
		// refresh the menu close timer.
		for( int i = 0; i < MAX_NEW_PICKUPS; i++ )
		{
			if( m_iNewPickupIds[i] == iId )
				break;
			if( m_iNewPickupIds[i] <= 0 )
			{
				m_iNewPickupIds[i] = iId;
				break;
			}
		}
		RefreshMenuTimer();
	}
	else
	{
		// Menu closed: top-left text notifier only, menu stays closed.
		gHUD.m_PickupNotify.OnWeaponPickup( iId );
	}
}

int CHudAmmo::MsgFunc_ItemPickup( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	const char *szName = READ_STRING();

	// Add the weapon to the history
	gHR.AddToHistory( HISTSLOT_ITEM, szName );

	return 1;
}

int CHudAmmo::MsgFunc_HideWeapon( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	
	gHUD.m_iHideHUDDisplay = READ_BYTE();

	if( gEngfuncs.IsSpectateOnly() )
		return 1;

	if( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) )
	{
		wrect_t nullrc = {0,};
		gpActiveSel = NULL;
		SetCrosshair( 0, nullrc, 0, 0, 0 );
	}
	else
	{
		if( m_pWeapon )
			SetCrosshair( m_pWeapon->hCrosshair, m_pWeapon->rcCrosshair, 255, 255, 255 );
	}

	return 1;
}

//
// Map inventory IDs are independent of weapon IDs.
int CHudAmmo::MsgFunc_InvRemove( const char *name, int size, void *data )
{
 return SvenUI_InvRemove(name,size,data);
}

//
// HideHUD -- Sven HUD hide flags, svc 91. client.so reverse (0xA0026):
// LE SHORT straight into the HUD hide field (+0x88); low-byte bits match
// the classic HIDEHUD_* layout (0x01 weapons, 0x02 flashlight), upper bits
// are Sven extras (0x100 crosshair/weapon side). NOT two separate bytes.
//
int CHudAmmo::MsgFunc_HideHUD( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );

	gHUD.m_iHideHUDDisplay = READ_SHORT();

	if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0f )
		gEngfuncs.Con_Printf( "TEMP-DIAG HideHUD hide=0x%x\n", gHUD.m_iHideHUDDisplay );

	if( gEngfuncs.IsSpectateOnly() )
		return 1;

	if( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) )
	{
		wrect_t nullrc = {0,};
		gpActiveSel = NULL;
		SetCrosshair( 0, nullrc, 0, 0, 0 );
	}
	else
	{
		if( m_pWeapon )
			SetCrosshair( m_pWeapon->hCrosshair, m_pWeapon->rcCrosshair, 255, 255, 255 );
	}

	return 1;
}

// Sven TE_CUSTOM global flag (type 3), client.so 0x1065B6.
static int s_iSvenTECustomFlag = 0;

//
// TE_CUSTOM -- Sven custom effect/state message, svc 99. client.so reverse
// (wrapper 0xD3330 -> core 0x1065B6): first field BYTE type (1/2/3, else
// "TE_CUSTOM error: unknown type %d"). Type 1 carries two SHORTs into an
// effect record; type 2 builds a client-side effect object (full wire still
// open); type 3 is a BYTE flag. This is NOT a vanilla svc_temp_entity.
//
int CHudAmmo::MsgFunc_TE_CUSTOM( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );

	int type = READ_BYTE();

	switch( type )
	{
	case 1:
	{
		int v1 = READ_SHORT();
		int v2 = READ_SHORT();
		if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0f )
			gEngfuncs.Con_Printf( "TEMP-DIAG TE_CUSTOM type=1 v1=%d v2=%d (effect record TBD)\n", v1, v2 );
		break;
	}
	case 2:
		// Wire fields feed 0xB542C directly without plain READs; the exact
		// layout is still open, so consume nothing further here (the engine
		// already consumed the stream by registered size).
		if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0f )
			gEngfuncs.Con_Printf( "TEMP-DIAG TE_CUSTOM type=2 (effect object, wire TBD)\n" );
		break;
	case 3:
	{
		int value = READ_BYTE();
		s_iSvenTECustomFlag = ( value == 1 );
		if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0f )
			gEngfuncs.Con_Printf( "TEMP-DIAG TE_CUSTOM type=3 flag=%d\n", s_iSvenTECustomFlag );
		break;
	}
	default:
		gEngfuncs.Con_Printf( "TE_CUSTOM error: unknown type %d\n", type );
		break;
	}

	return 1;
}

// Sven per-weapon sprite payload table, indexed by weapon id. WeaponSpr
// (client.so 0xA328A) writes [SHORT id][STRING] into rgWeapons[id]+0xB0 and
// reprocesses the weapon record (0xA2522); CustWeapon (+0x1B5) is the
// separate sprite-subdirectory context. The payload is also used as a
// fallback sprite-list name when the normal weapon list is unavailable.
//
// WeaponSpr -- Sven weapon sprite payload, svc 138: [SHORT id][STRING].
//
int CHudAmmo::MsgFunc_WeaponSpr( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );

	int iId = READ_SHORT();
	const char *pszSpr = READ_STRING();

	if( iId > 0 && iId < MAX_HUD_WEAPONS && pszSpr && pszSpr[0] )
	{
		strlcpy( s_szWeaponSpr[iId], pszSpr, sizeof( s_szWeaponSpr[iId] ) );
		if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0f )
			gEngfuncs.Con_Printf( "TEMP-DIAG WeaponSpr id=%d spr=%s\n", iId, pszSpr );
		WEAPON *pWeapon = gWR.GetWeapon( iId );
		if( pWeapon && pWeapon->iId )
		{
			strlcpy( pWeapon->szSpriteRecord, pszSpr, sizeof( pWeapon->szSpriteRecord ) );
			gWR.LoadWeaponSprites( pWeapon );
		}
	}

	return 1;
}

// Last Sven server version string (ServerVer, svc 124). Stock disconnects
// on mismatch (client "5.26"); we deliberately never disconnect (Xash is not
// 5.26) and only record/report it.
static char s_szSvenServerVer[64];

//
// ServerVer -- Sven server version, svc 124: [STRING].
//
int CHudAmmo::MsgFunc_ServerVer( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );

	const char *pszVer = READ_STRING();
	if( pszVer )
	{
		strlcpy( s_szSvenServerVer, pszVer, sizeof( s_szSvenServerVer ) );
		gEngfuncs.Con_Printf( "ServerVer: server reports version %s\n", pszVer );
	}

	return 1;
}

//
// MapList: [BYTE mode][SHORT range][STRING names], handled by Sven UI.
int CHudAmmo::MsgFunc_MapList( const char *name, int size, void *data )
{
 return SvenUI_MapList(name,size,data);
}

// Sven server info record (ClServerInfo, svc 147):
// [BYTE][LONG][STRING up to 44]. Real client-side state in stock.
static struct
{
	int b;
	int l;
	char s[44];
} s_svenServerInfo;

//
// ClServerInfo -- Sven server info, svc 147: [BYTE][LONG][STRING44].
//
int CHudAmmo::MsgFunc_ClServerInfo( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );

	s_svenServerInfo.b = READ_BYTE();
	s_svenServerInfo.l = READ_LONG();
	const char *pszInfo = READ_STRING();
	if( pszInfo )
		strlcpy( s_svenServerInfo.s, pszInfo, sizeof( s_svenServerInfo.s ) );
	else
		s_svenServerInfo.s[0] = '\0';

	if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0f )
		gEngfuncs.Con_Printf( "TEMP-DIAG ClServerInfo b=%d l=%d s=%s\n",
			s_svenServerInfo.b, s_svenServerInfo.l, s_svenServerInfo.s );

	return 1;
}

//
// ClExtrasInfo -- Sven custom HUD table feed, svc 148. The payload goes
// through a text/config parser (sscanf 5 values) in stock; the exact grammar
// needs its own reverse pass, so consume only for now.
//
int CHudAmmo::MsgFunc_ClExtrasInfo( const char *pszName, int iSize, void *pbuf )
{
	if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0f )
		gEngfuncs.Con_Printf( "TEMP-DIAG ClExtrasInfo stub size=%d\n", iSize );

	return 1;
}

//
//  CurWeapon: Update hud state with the current weapon and clip count. Ammo
//  counts are updated with AmmoX. Server assures that the Weapon ammo type 
//  numbers match a real ammo type.
//
int HUD_ActiveWeaponId()
{
	return gHUD.m_Ammo.ActiveWeaponId();
}

int CHudAmmo::ActiveWeaponId() const
{
	return m_pWeapon ? m_pWeapon->iId : 0;
}

int CHudAmmo::MsgFunc_CurWeapon( const char *pszName, int iSize, void *pbuf )
{
	wrect_t nullrc = {0,};
	int fOnTarget = FALSE;

	BEGIN_READ( pbuf, iSize );

	int iState = READ_BYTE();
	int iId = READ_SHORT();
	int iClip = READ_LONG();
	int iAmmo = READ_LONG(); // clip and ammo are sent as LONG by Sven's server, -1 means infinite

	// Match Sven client.dll (0x10002fb0): values below -1 are clamped to 0,
	// -1 stays -1 (infinite). Vanilla only ever sends 0..255 so both paths are safe.
	if( iClip < -1 )
		iClip = 0;
	if( iAmmo < -1 )
		iAmmo = 0;

	// detect if we're also on target (vanilla state 2, Sven bit 1)
	if( iState & 2 )
	{
		fOnTarget = TRUE;
	}

	if( iId < 1 || iId >= MAX_HUD_WEAPONS )
	{
		SetCrosshair( 0, nullrc, 0, 0, 0 );
		// Clear out the weapon so we don't keep drawing the last active weapon's ammo. - Solokiller
		m_pWeapon = 0;
		return 0;
	}

	if( g_iUser1 != OBS_IN_EYE )
	{
		// Is player dead???
		if( ( iId == -1 ) && ( iClip == -1 ) )
		{
			gHUD.m_fPlayerDead = TRUE;
			gpActiveSel = NULL;
			return 1;
		}
		gHUD.m_fPlayerDead = FALSE;
	}

	WEAPON *pWeapon = gWR.GetWeapon( iId );

	if( !pWeapon )
		return 0;

	// client.so has no weapon_shockroach definition (only monster_shockroach/
	// weapon_shockrifle), so id 28 may never get a WeaponList/name. Stamp the
	// wire id on first contact so id-keyed rules (diag, iClip2 fallback) work
	// even for nameless records; name/ammo fields stay untouched for the real
	// WeaponList to fill later.
	if( pWeapon->iId == 0 )
		pWeapon->iId = iId;

	pWeapon->iClip = iClip;

	// NOTE: CurWeapon.iAmmo is deliberately NOT mirrored into the reserve
	// slot. Reverse + live wire proved it carries the SECONDARY count on
	// dual-ammo weapons (akimbo second clip, M16 grenades), not the primary
	// reserve — mirroring poisoned primary displays (akimbo 32/32, grenades
	// over bullets). Primary reserve comes from AmmoX only. (git history has
	// the removed mirror if this ever needs revisiting.)
	// Stock client.dll (MsgFunc_CurWeapon core 0x10002fb0) stores this second
	// LONG per-weapon at WEAPON+0xa4 for EVERY weapon, so keep the same
	// stock parity here: iClip2 always mirrors iAmmo (-1 stays n/a). Only
	// the weapons below ever display it; nothing else reads iClip2.
	pWeapon->iClip2 = iAmmo;

	// TEMP-DIAG (dual-uzi HUD): while the akimbo second-clip wire source is
	// being confirmed, dump what the server actually sends for the akimbo so
	// a single test run settles it. Remove with the iClip2 work.
	// Sven minigun (id 21) and shockroach (id 28) are included: their visible
	// counts ride CurWeapon's second LONG, so the dump settles clip/reserve/
	// iAmmo semantics for both in one run. client.so carries no
	// weapon_shockroach definition, so id 28 is matched by id as well.
	if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0
		&& ( strstr( pWeapon->szName, "uziakimbo" ) || iId == 21 || iId == 28
			|| !strcmp( pWeapon->szName, "weapon_minigun" )
			|| !strcmp( pWeapon->szName, "weapon_shockroach" )))
	{
		gEngfuncs.Con_Printf( "TEMP-DIAG CurWeapon name=%s id=%d state=%d clip=%d iAmmo=%d iClip2=%d ammoT=%d count=%d ammo2T=%d count2=%d\n",
			pWeapon->szName, iId, iState, iClip, iAmmo, pWeapon->iClip2,
			pWeapon->iAmmoType, gWR.CountAmmo( pWeapon->iAmmoType ),
			pWeapon->iAmmo2Type, gWR.CountAmmo( pWeapon->iAmmo2Type ));
	}

	// not the current weapon (vanilla state 0 / Sven bit 0), so update no more
	if( !( iState & 1 ) )
		return 1;

	m_pWeapon = pWeapon;

	if( !( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) ) )
	{
		if( gHUD.m_iFOV >= 90 )
		{
			// normal crosshairs
			if( fOnTarget && m_pWeapon->hAutoaim )
				SetCrosshair( m_pWeapon->hAutoaim, m_pWeapon->rcAutoaim, 255, 255, 255 );
			else
				SetCrosshair( m_pWeapon->hCrosshair, m_pWeapon->rcCrosshair, 255, 255, 255 );
		}
		else
		{
			// zoomed crosshairs
			if( fOnTarget && m_pWeapon->hZoomedAutoaim )
				SetCrosshair( m_pWeapon->hZoomedAutoaim, m_pWeapon->rcZoomedAutoaim, 255, 255, 255 );
			else
				SetCrosshair( m_pWeapon->hZoomedCrosshair, m_pWeapon->rcZoomedCrosshair, 255, 255, 255 );
			SetScopeOverlay( m_pWeapon->hScopeOverlay, m_pWeapon->rcScopeOverlay );
		}
	}

	m_fFade = 200.0f; //!!!
	m_iFlags |= HUD_ACTIVE;
	
	return 1;
}

//
// WeaponList -- Tells the hud about a new weapon type.
//
int CHudAmmo::MsgFunc_WeaponList( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	
	WEAPON Weapon;
	memset( &Weapon, 0, sizeof( Weapon ) );

strlcpy( Weapon.szName, READ_STRING(), sizeof( Weapon.szName ));

	// Sven Co-op server (server.dll:0x10202560, binary-verified) writes
	// WeaponList as STRING + BYTE(ammo1 idx) + LONG(ammo1 max) + BYTE(ammo2 idx)
	// + LONG(ammo2 max) + BYTE(slot) + BYTE(pos) + SHORT(id) + BYTE(flags),
	// which matches client.dll's reader (0x100046A0). The previous LONG+LONG read
	// drifted the whole message so real weapon names never matched their ids
	// and the menu showed auto-generated weapon_<id> fallbacks instead.
	Weapon.iAmmoType = (int)READ_CHAR();
	if( Weapon.iAmmoType < 0 )
		Weapon.iAmmoType += 256; // Sven reads ammo idx as unsigned byte (client.dll 0x100046A0)

	Weapon.iMax1 = READ_LONG();

	Weapon.iAmmo2Type = (int)READ_CHAR();
	if( Weapon.iAmmo2Type < 0 )
		Weapon.iAmmo2Type += 256;
	Weapon.iMax2 = READ_LONG();
	if( Weapon.iMax2 == 255 )
		Weapon.iMax2 = -1;

	Weapon.iSlot = READ_CHAR();
	Weapon.iSlotPos = READ_CHAR();

	Weapon.iId = READ_SHORT();
	Weapon.iFlags = READ_BYTE();
	Weapon.iClip = 0;
	Weapon.iClip2 = -1; // dual-uzi second clip, filled from CurWeapon.iAmmo

	if( Weapon.iId < 0 || Weapon.iId >= MAX_HUD_WEAPONS )
		return 0;

	// Sven servers number slots 0..9+ (tall layout) while the vanilla HUD
	// prints buckets 0..4 (rgSlots has 6 rows). WeaponList must register the
	// weapon regardless of its slot: rejecting slot 6..9 weapons meant the
	// minigun (9-1, id 21) and shockroach never entered the inventory, so
	// their ammo type/max were unknown, the reserve counter stayed at 0 and
	// the bottom-right HUD read 00. Fold the same way PickupWeapon does.
	if( Weapon.iSlot < 0 ) Weapon.iSlot = 0;
	if( Weapon.iSlot >= MAX_WEAPON_SLOTS ) Weapon.iSlot = MAX_WEAPON_SLOTS - 1;
	if( Weapon.iSlotPos < 0 ) Weapon.iSlotPos = 0;
	if( Weapon.iSlotPos >= MAX_WEAPON_POSITIONS ) Weapon.iSlotPos = MAX_WEAPON_POSITIONS - 1;
	if( Weapon.iAmmoType < -1 || Weapon.iAmmoType >= MAX_AMMO_TYPES )
		return 0;
	if( Weapon.iAmmo2Type < -1 || Weapon.iAmmo2Type >= MAX_AMMO_TYPES )
		return 0;
	/*if( Weapon.iAmmoType >= 0 && Weapon.iMax1 == 0 )
		return 0;
	if( Weapon.iAmmo2Type >= 0 && Weapon.iMax2 == 0 )
		return 0;*/

	gWR.AddWeapon( &Weapon );

	// TEMP-DIAG (minigun reserve): confirm the ammo types WeaponList carries
	// for id 21 so the reserve misses are traceable against the widen-to-256.
	if( gEngfuncs.pfnGetCvarFloat( "cl_goldsrc_debug" ) >= 1.0 )
	{
		gEngfuncs.Con_Printf( "TEMP-DIAG WeaponList name=%s id=%d ammoT=%d max1=%d ammo2T=%d max2=%d\n",
			Weapon.szName, Weapon.iId, Weapon.iAmmoType, Weapon.iMax1,
			Weapon.iAmmo2Type, Weapon.iMax2 );
	}

	return 1;
}

//
// CustWeapon -- custom weapon sprite subdirectory, [SHORT id][STRING dir].
// Stock stores it at WEAPON+0x1b5 for LoadWeaponSprites ("sprites/<dir>/
// <weapon>.txt"). It is not a selectable name: ignore empty dirs and never
// let it near the weapon inventory.
//
int CHudAmmo::MsgFunc_CustWeapon( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	int iId = READ_SHORT();
	const char *pszDir = READ_STRING();

	if( iId > 0 && iId < MAX_HUD_WEAPONS && pszDir && pszDir[0] )
	{
		strlcpy( s_szCustSprDir[iId], pszDir, sizeof( s_szCustSprDir[iId] ) );
		WEAPON *pWeapon = gWR.GetWeapon( iId );
		if( pWeapon && pWeapon->iId == iId )
		{
			strlcpy( pWeapon->szSpriteDir, pszDir, sizeof( pWeapon->szSpriteDir ) );
			gWR.LoadWeaponSprites( pWeapon );
		}
	}

	return 1;
}

//------------------------------------------------------------------------
// Command Handlers
//------------------------------------------------------------------------
// Slot button pressed
void CHudAmmo::SlotInput( int iSlot )
{
	if( gHUD.m_Menu.m_fMenuDisplayed )
	{
		gHUD.m_Menu.SelectMenuItem( iSlot + 1 );
		return;
	}

#if USE_VGUI
	// Let the Viewport use it first, for menus
	if( gViewPort && gViewPort->SlotInput( iSlot ) )
		return;
#endif
	gWR.SelectSlot(iSlot, FALSE, 1);
}

void CHudAmmo::UserCmd_Slot1( void )
{
	SlotInput( 0 );
}

void CHudAmmo::UserCmd_Slot2( void )
{
	SlotInput( 1 );
}

void CHudAmmo::UserCmd_Slot3( void )
{
	SlotInput( 2 );
}

void CHudAmmo::UserCmd_Slot4( void )
{
	SlotInput( 3 );
}

void CHudAmmo::UserCmd_Slot5( void )
{
	SlotInput( 4 );
}

void CHudAmmo::UserCmd_Slot6( void )
{
	SlotInput( 5 );
}

void CHudAmmo::UserCmd_Slot7( void )
{
	SlotInput( 6 );
}

void CHudAmmo::UserCmd_Slot8( void )
{
	SlotInput( 7 );
}

void CHudAmmo::UserCmd_Slot9( void )
{
	SlotInput( 8 );
}

void CHudAmmo::UserCmd_Slot10( void )
{
	SlotInput( 9 );
}

void CHudAmmo::UserCmd_Close( void )
{
	if( gpActiveSel )
	{
		gpLastSel = gpActiveSel;
		gpActiveSel = NULL;
		PlaySound( "common/wpn_hudoff.wav", 1 );
	}
	else
		ClientCmd( "escape" );
}


// Selects the next item in the weapon menu
void CHudAmmo::UserCmd_NextWeapon( void )
{
	if( gHUD.m_fPlayerDead || ( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) ) )
		return;

	if( !gpActiveSel || gpActiveSel == (WEAPON*)1 )
		gpActiveSel = m_pWeapon;

	int pos = 0;
	int slot = 0;
	if ( gpActiveSel )
	{
		pos = gpActiveSel->iSlotPos + 1;
		slot = gpActiveSel->iSlot;
	}

	for( int loop = 0; loop <= 1; loop++ )
	{
		for( ; slot < MAX_WEAPON_SLOTS; slot++ )
		{
			for( ; pos < MAX_WEAPON_POSITIONS; pos++ )
			{
				WEAPON *wsp = gWR.GetWeaponSlot( slot, pos );

				if( wsp && gWR.HasAmmo( wsp ) )
				{
					gpActiveSel = wsp;
					RefreshMenuTimer();
					return;
				}
			}

			pos = 0;
		}

		slot = 0;  // start looking from the first slot again
	}

	gpActiveSel = NULL;
}

// Selects the previous item in the menu
void CHudAmmo::UserCmd_PrevWeapon( void )
{
	if( gHUD.m_fPlayerDead || ( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) ) )
		return;

	if( !gpActiveSel || gpActiveSel == (WEAPON*) 1 )
		gpActiveSel = m_pWeapon;

	int pos = MAX_WEAPON_POSITIONS - 1;
	int slot = MAX_WEAPON_SLOTS - 1;
	if( gpActiveSel )
	{
		pos = gpActiveSel->iSlotPos - 1;
		slot = gpActiveSel->iSlot;
	}
	
	for( int loop = 0; loop <= 1; loop++ )
	{
		for( ; slot >= 0; slot-- )
		{
			for( ; pos >= 0; pos-- )
			{
				WEAPON *wsp = gWR.GetWeaponSlot( slot, pos );

				if( wsp && gWR.HasAmmo( wsp ) )
				{
					gpActiveSel = wsp;
					RefreshMenuTimer();
					return;
				}
			}

			pos = MAX_WEAPON_POSITIONS - 1;
		}
		
		slot = MAX_WEAPON_SLOTS - 1;
	}

	gpActiveSel = NULL;
}

//-------------------------------------------------------------------------
// Drawing code
//-------------------------------------------------------------------------
int CHudAmmo::Draw( float flTime )
{
	int a, x, y, r, g, b;
	int AmmoWidth;

	if( !( gHUD.m_iWeaponBits & ( 1 << ( WEAPON_SUIT ) ) ) )
		return 1;

	if( ( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) ) )
		return 1;

	// Draw Weapon Menu
	DrawWList( flTime );

	// Draw ammo pickup history
	gHR.DrawAmmoHistory( flTime );

	if( !( m_iFlags & HUD_ACTIVE ) )
		return 0;

	if( !m_pWeapon )
		return 0;

	WEAPON *pw = m_pWeapon; // shorthand

	// SPR_Draw Ammo
	if( ( pw->iAmmoType < 0 ) && ( pw->iAmmo2Type < 0 ) )
		return 0;

	int iFlags = DHN_DRAWZERO; // draw 0 values

	AmmoWidth = gHUD.GetSpriteRect( gHUD.m_HUD_number_0 ).right - gHUD.GetSpriteRect( gHUD.m_HUD_number_0 ).left;

	a = (int)Q_max( MIN_ALPHA, m_fFade );

	if( m_fFade > 0 )
		m_fFade -= ( (float)gHUD.m_flTimeDelta * 20.0f );

	UnpackRGB( r, g, b, RGB_BLUEISH );

	ScaleColors( r, g, b, a );

	// Does this weapon have a clip?
	y = ScreenHeight - gHUD.m_iFontHeight - gHUD.m_iFontHeight / 2;
	y += gHUD.m_iHudNumbersYOffset; // a1ba: fix HL25 HUD vertical inconsistensy

	// Does weapon have any ammo at all?
	// NOTE: Sven numbers ammo types from 0 (binary-verified: the real client
	// stores the raw BYTE index with no gate), so index 0 is a real type.
	// The old "> 0" test hid the reserve counter of every 0-indexed weapon.
	if( !strcmp( m_pWeapon->szName, "weapon_uziakimbo" ) && m_pWeapon->iClip2 >= 0 )
	{
		// Dual uzis (Sven-style): one compact row right-aligned, clip1 | clip2 
		// followed by the reserve, e.g. "32 | 32 / 150". Both clips come from
		// the server's CurWeapon fields (iClip / iAmmo), reserve from iAmmoType.
		int iIconWidth = m_pWeapon->rcAmmo.right - m_pWeapon->rcAmmo.left;
		int iBarWidth = AmmoWidth / 10;
		int iOffset = ( m_pWeapon->rcAmmo.bottom - m_pWeapon->rcAmmo.top ) / 8;
		int digits1 = 1, digits2 = 1;
		float fRight;

		for( int n = Q_max( pw->iClip, 0 ); n >= 10; n /= 10 ) digits1++;
		for( int n = m_pWeapon->iClip2; n >= 10; n /= 10 ) digits2++;

		// Reserve (3 digits) + icon, right-anchored same as the single-clip row
		x = ScreenWidth - ( 8 * AmmoWidth ) - iIconWidth;
		x = gHUD.DrawHudNumber( x, y, iFlags | DHN_3DIGITS, gWR.CountAmmo( pw->iAmmoType ), r, g, b );
		gHUD.DrawSprite( x, y - iOffset, m_pWeapon->hAmmo, &m_pWeapon->rcAmmo, r, g, b, 0, SPR_ADDITIVE );

		// clip2 | just left of the reserve
		x -= AmmoWidth / 2 + iBarWidth;
		UnpackRGB( r, g, b, RGB_BLUEISH );
		FillRGBA( x, y, iBarWidth, gHUD.m_iFontHeight, r, g, b, a );
		ScaleColors( r, g, b, a );
		fRight = x - AmmoWidth / 2;
		gHUD.DrawHudNumber( (int)fRight - digits2 * AmmoWidth, y, iFlags, m_pWeapon->iClip2, r, g, b );

		// clip1 | just left of clip2
		x = (int)fRight - digits2 * AmmoWidth - AmmoWidth / 2 - iBarWidth;
		UnpackRGB( r, g, b, RGB_BLUEISH );
		FillRGBA( x, y, iBarWidth, gHUD.m_iFontHeight, r, g, b, a );
		ScaleColors( r, g, b, a );
		fRight = x - AmmoWidth / 2;
		gHUD.DrawHudNumber( (int)fRight - digits1 * AmmoWidth, y, iFlags, pw->iClip, r, g, b );
	}
	else if( m_pWeapon->iAmmoType >= 0 )
	{
		int iIconWidth = m_pWeapon->rcAmmo.right - m_pWeapon->rcAmmo.left;

		// Sven minigun/shockroach carry their visible count in CurWeapon's
		// second LONG (stock stores it per-weapon at WEAPON+0xa4; AmmoX never
		// carries those types), so a zero AmmoX reserve means "wire silent",
		// not "empty": fall back to iClip2 instead of showing 00. Scoped to
		// these two names like the akimbo rule above; everything else keeps
		// the AmmoX reserve untouched.
		int iReserve = gWR.CountAmmo( pw->iAmmoType );
		// client.so has no weapon_shockroach definition, so match id 28 too.
		if( iReserve == 0 && pw->iClip2 > 0
			&& ( !strcmp( pw->szName, "weapon_minigun" ) || !strcmp( pw->szName, "weapon_shockroach" )
				|| pw->iId == 21 || pw->iId == 28 ))
			iReserve = pw->iClip2;

		if( pw->iClip >= 0 )
		{
			// room for the number and the '|' and the current ammo
			x = ScreenWidth - ( 8 * AmmoWidth ) - iIconWidth;
			x = gHUD.DrawHudNumber( x, y, iFlags | DHN_3DIGITS, pw->iClip, r, g, b );

			/*wrect_t rc;
			rc.top = 0;
			rc.left = 0;
			rc.right = AmmoWidth;
			rc.bottom = 100;*/

			int iBarWidth =  AmmoWidth / 10;

			x += AmmoWidth / 2;

			UnpackRGB( r,g,b, RGB_BLUEISH );

			// draw the | bar
			FillRGBA( x, y, iBarWidth, gHUD.m_iFontHeight, r, g, b, a );

			x += iBarWidth + AmmoWidth / 2;

			// GL Seems to need this
			ScaleColors( r, g, b, a );
			x = gHUD.DrawHudNumber( x, y, iFlags | DHN_3DIGITS, iReserve, r, g, b );
		}
		else
		{
			// SPR_Draw a bullets only line
			x = ScreenWidth - 4 * AmmoWidth - iIconWidth;
			x = gHUD.DrawHudNumber( x, y, iFlags | DHN_3DIGITS, iReserve, r, g, b );
		}

		// Draw the ammo Icon
		int iOffset = ( m_pWeapon->rcAmmo.bottom - m_pWeapon->rcAmmo.top ) / 8;
		gHUD.DrawSprite( x, y - iOffset, m_pWeapon->hAmmo, &m_pWeapon->rcAmmo, r, g, b, 0, SPR_ADDITIVE );
	}

	// Does weapon have seconday ammo?
	if( pw->iAmmo2Type >= 0 )
	{
		int iIconWidth = m_pWeapon->rcAmmo2.right - m_pWeapon->rcAmmo2.left;

		// Do we have secondary ammo?
		if( ( pw->iAmmo2Type != 0 ) && ( gWR.CountAmmo( pw->iAmmo2Type ) > 0 ) )
		{
			y -= gHUD.m_iFontHeight + gHUD.m_iFontHeight / 4;
			x = ScreenWidth - 4 * AmmoWidth - iIconWidth;
			x = gHUD.DrawHudNumber( x, y, iFlags | DHN_3DIGITS, gWR.CountAmmo( pw->iAmmo2Type ), r, g, b );

			// Draw the ammo Icon
			int iOffset = ( m_pWeapon->rcAmmo2.bottom - m_pWeapon->rcAmmo2.top) / 8;
			gHUD.DrawSprite( x, y - iOffset, m_pWeapon->hAmmo2, &m_pWeapon->rcAmmo2, r, g, b, 0, SPR_ADDITIVE );
		}
	}
	return 1;
}

//
// Draws the ammo bar on the hud
//
int DrawBar( int x, int y, int width, int height, float f )
{
	int r, g, b;

	if( f < 0 )
		f = 0;
	if( f > 1 )
		f = 1;

	if( f )
	{
		int w = f * width;

		// Always show at least one pixel if we have ammo.
		if( w <= 0 )
			w = 1;
		UnpackRGB( r, g, b, RGB_GREENISH );
		FillRGBA( x, y, w, height, r, g, b, 255 );
		x += w;
		width -= w;
	}

	UnpackRGB( r, g, b, RGB_BLUEISH );

	FillRGBA( x, y, width, height, r, g, b, 128 );

	return ( x + width );
}

void DrawAmmoBar( WEAPON *p, int x, int y, int width, int height )
{
	if( !p )
		return;

	if( p->iAmmoType != -1 )
	{
		if( !gWR.CountAmmo( p->iAmmoType ) )
			return;

		float f = (float)gWR.CountAmmo( p->iAmmoType ) / (float)p->iMax1;
		
		x = DrawBar( x, y, width, height, f );

		// Do we have secondary ammo too?
		if( p->iAmmo2Type != -1 )
		{
			f = (float)gWR.CountAmmo( p->iAmmo2Type ) / (float)p->iMax2;

			x += 5; //!!!

			DrawBar( x, y, width, height, f );
		}
	}
}

//
// Draw Weapon Menu
//
int CHudAmmo::DrawWList( float flTime )
{
	int r, g, b, x, y, a, i;

	if( !gpActiveSel )
	{
		// Menu closed (by any path): temporary pickup greens expire here,
		// so reopening the list always shows normal colors again.
		for( int k = 0; k < MAX_NEW_PICKUPS; k++ )
			m_iNewPickupIds[k] = 0;
		return 0;
	}

	// Auto-close the menu after a quiet spell. Nav steps and menu-open
	// pickups refresh the deadline via RefreshMenuTimer.
	if( m_fMenuCloseAt > 0.0f && flTime >= m_fMenuCloseAt )
	{
		CloseWeaponMenu( true );
		return 0;
	}

	int iActiveSlot;

	if( gpActiveSel == (WEAPON *) 1 )
		iActiveSlot = -1;	// current slot has no weapons
	else 
		iActiveSlot = gpActiveSel->iSlot;

	x = 10; //!!!
	y = 10; //!!!

	// Ensure that there are available choices in the active slot
	if( iActiveSlot > 0 )
	{
		if( !gWR.GetFirstPos( iActiveSlot ) )
		{
			gpActiveSel = (WEAPON *) 1;
			iActiveSlot = -1;
		}
	}

	// Draw top line
	for( i = 0; i < MAX_WEAPON_SLOTS; i++ )
	{
		int iWidth;

		UnpackRGB( r, g, b, RGB_BLUEISH );

		// Fresh pickup lives in this slot: temporary green bucket.
		if( IsNewPickupSlot( i ) )
			UnpackRGB( r, g, b, RGB_GREENISH );

		if( iActiveSlot == i )
			a = 255;
		else
			a = 192;

		ScaleColors( r, g, b, 255 );

		// make active slot wide enough to accomodate gun pictures
		if( i == iActiveSlot )
		{
			WEAPON *p = gWR.GetFirstPos( iActiveSlot );
			if( p )
				iWidth = p->rcActive.right - p->rcActive.left;
			else
				iWidth = giBucketWidth;
		}
		else
			iWidth = giBucketWidth;

		gHUD.DrawSprite( x, y, gHUD.GetSprite( m_HUD_bucket0 + i ), &gHUD.GetSpriteRect( m_HUD_bucket0 + i ), r, g, b, 0, SPR_ADDITIVE );
		
		x += iWidth + 5;
	}

	a = 128; //!!!
	x = 10;

	// Draw all of the buckets
	for( i = 0; i < MAX_WEAPON_SLOTS; i++ )
	{
		y = giBucketHeight + 10;

		// If this is the active slot, draw the bigger pictures,
		// otherwise just draw boxes
		if( i == iActiveSlot )
		{
			WEAPON *p = gWR.GetFirstPos( i );
			int iWidth = giBucketWidth;
			if( p )
				iWidth = p->rcActive.right - p->rcActive.left;

			for( int iPos = 0; iPos < MAX_WEAPON_POSITIONS; iPos++ )
			{
				p = gWR.GetWeaponSlot( i, iPos );

				if( !p || !p->iId )
					continue;

				UnpackRGB( r, g, b, RGB_BLUEISH );

				// Fresh pickup sitting in the open slot: temporary green
				// weapon next to the green bucket.
				if( IsNewPickupWeapon( p->iId ) )
					UnpackRGB( r, g, b, RGB_GREENISH );

				// if active, then we must have ammo.
				if( gpActiveSel == p )
				{
					gHUD.DrawSprite( x, y, p->hActive, &p->rcActive, r, g, b, 0, SPR_ADDITIVE );
					gHUD.DrawSprite( x, y, gHUD.GetSprite( m_HUD_selection ), &gHUD.GetSpriteRect( m_HUD_selection ), r, g, b, 0, SPR_ADDITIVE );
				}
				else
				{
					// Draw Weapon if Red if no ammo
					if( gWR.HasAmmo( p ) )
						ScaleColors( r, g, b, 192 );
					else
					{
						UnpackRGB( r, g, b, RGB_REDISH );
						ScaleColors( r, g, b, 128 );
					}

					gHUD.DrawSprite( x, y, p->hInactive, &p->rcInactive, r, g, b, 0, SPR_ADDITIVE );
				}

				// Draw Ammo Bar
				DrawAmmoBar( p, x + giABWidth / 2, y, giABWidth, giABHeight );
				
				y += p->rcActive.bottom - p->rcActive.top + 5;
			}

			x += iWidth + 5;
		}
		else
		{
			// Draw Row of weapons.
			UnpackRGB( r, g, b, RGB_BLUEISH );

			for( int iPos = 0; iPos < MAX_WEAPON_POSITIONS; iPos++ )
			{
				WEAPON *p = gWR.GetWeaponSlot( i, iPos );

				if( !p || !p->iId )
					continue;

				if( gWR.HasAmmo( p ) )
				{
					UnpackRGB( r, g, b, RGB_BLUEISH );
					a = 128;
				}
				else
				{
					UnpackRGB( r, g, b, RGB_REDISH );
					a = 96;
				}

				FillRGBA( x, y, giBucketWidth, giBucketHeight, r, g, b, a );

				y += giBucketHeight + 5;
			}

			x += giBucketWidth + 5;
		}
	}

	return 1;
}

/* =================================
	GetSpriteList

Finds and returns the matching 
sprite name 'psz' and resolution 'iRes'
in the given sprite list 'pList'
iCount is the number of items in the pList
================================= */
client_sprite_t *GetSpriteList( client_sprite_t *pList, const char *psz, int iRes, int iCount )
{
	if( !pList )
		return NULL;

	int i = iCount;
	client_sprite_t *p = pList;

	while( i-- )
	{
		if( p->iRes == iRes && !strcmp( psz, p->szName ))
			return p;
		p++;
	}

	return NULL;
}
