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
// ammohistory.h
//
#pragma once
#if !defined(AMMOHISTORY_H)
#define AMMOHISTORY_H

// this is the max number of items in each bucket
#define MAX_WEAPON_POSITIONS		MAX_WEAPON_SLOTS

class WeaponsResource
{
private:
	// Information about weapons & ammo
	WEAPON		rgWeapons[MAX_WEAPONS];	// Weapons Array

	// counts of weapons * ammo
	WEAPON*		rgSlots[MAX_WEAPON_SLOTS + 1][MAX_WEAPON_POSITIONS + 1];	// The slots currently in use by weapons.  The value is a pointer to the weapon;  if it's NULL, no weapon is there
	int			riAmmo[MAX_AMMO_TYPES];					// count of each ammo type

public:
	void Init( void )
	{
		memset( rgWeapons, 0, sizeof rgWeapons );
		Reset();
	}

	void Reset( void )
	{
		iOldWeaponBits = 0;
		memset( rgSlots, 0, sizeof rgSlots );
		memset( riAmmo, 0, sizeof riAmmo );
	}

///// WEAPON /////
	int			iOldWeaponBits;

	WEAPON *GetWeapon( int iId ) { return &rgWeapons[iId]; }
	void AddWeapon( WEAPON *wp ) 
	{ 
		rgWeapons[wp->iId] = *wp;	
		LoadWeaponSprites( &rgWeapons[wp->iId] );
	}

	void PickupWeapon( WEAPON *wp )
	{
		// Sven servers number slots 0..9+ (tall layout) but the vanilla menu
		// draws buckets 0..4 and rgSlots has 6 rows. Fold higher slots into
		// the last visible bucket and clamp positions: without this, exotic
		// weapons (slot 6+) wrote past rgSlots into riAmmo, corrupting ammo
		// counts into 99999-like values and hiding the weapons from the menu.
		int b = wp->iSlot;
		int p, k;
		if( b < 0 ) b = 0;
		if( b >= MAX_WEAPON_SLOTS ) b = MAX_WEAPON_SLOTS - 1;
		p = wp->iSlotPos;
		if( p < 0 ) p = 0;
		if( p >= MAX_WEAPON_POSITIONS ) p = MAX_WEAPON_POSITIONS - 1;
		for( k = 0; k < MAX_WEAPON_POSITIONS; k++ )
		{
			int c = ( p + k ) % MAX_WEAPON_POSITIONS;
			if( !rgSlots[b][c] )
			{
				rgSlots[b][c] = wp;
				return;
			}
		}
		rgSlots[b][MAX_WEAPON_POSITIONS] = wp; // spill cell: in bounds, just not drawn
	}

	void DropWeapon( WEAPON *wp )
	{
		// Search the whole table: folded weapons no longer sit at their raw
		// server indices, so blind indexing would leak them (and raw indices
		// could overflow for high Sven slots).
		for( int r = 0; r <= MAX_WEAPON_SLOTS; r++ )
		{
			for( int c = 0; c <= MAX_WEAPON_POSITIONS; c++ )
			{
				if( rgSlots[r][c] == wp )
					rgSlots[r][c] = NULL;
			}
		}
	}

	void DropAllWeapons( void )
	{
		for( int i = 0; i < MAX_WEAPONS; i++ )
		{
			if( rgWeapons[i].iId )
				DropWeapon( &rgWeapons[i] );
		}
	}

	WEAPON* GetWeaponSlot( int slot, int pos ) { return rgSlots[slot][pos]; }

	void LoadWeaponSprites( WEAPON* wp );
	void LoadAllWeaponSprites( void );
	WEAPON* GetFirstPos( int iSlot );
	void SelectSlot( int iSlot, int fAdvance, int iDirection );
	WEAPON* GetNextActivePos( int iSlot, int iSlotPos );

	int HasAmmo( WEAPON *p );

///// AMMO /////
	AMMO GetAmmo( int iId ) { return iId; }

	void SetAmmo( int iId, int iCount ) { riAmmo[ iId ] = iCount;	}

	int CountAmmo( int iId );

	HSPRITE* GetAmmoPicFromWeapon( int iAmmoId, wrect_t& rect );
};

extern WeaponsResource gWR;

#define MAX_HISTORY 12
enum {
	HISTSLOT_EMPTY,
	HISTSLOT_AMMO,
	HISTSLOT_WEAP,
	HISTSLOT_ITEM
};

class HistoryResource
{
private:
	struct HIST_ITEM {
		int type;
		float DisplayTime;  // the time at which this item should be removed from the history
		int iCount;
		int iId;
	};

	HIST_ITEM rgAmmoHistory[MAX_HISTORY];

public:

	void Init( void )
	{
		Reset();
	}

	void Reset( void )
	{
		memset( rgAmmoHistory, 0, sizeof rgAmmoHistory );
		iCurrentHistorySlot = 0;
	}

	int iHistoryGap;
	int iCurrentHistorySlot;

	void AddToHistory( int iType, int iId, int iCount = 0 );
	void AddToHistory( int iType, const char *szName, int iCount = 0 );

	void CheckClearHistory( void );
	int DrawAmmoHistory( float flTime );
};

extern HistoryResource gHR;
#endif // AMMOHISTORY_H
