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
#define MAX_WEAPON_POSITIONS		26

class WeaponsResource
{
private:
	// Information about weapons & ammo
	WEAPON		rgWeapons[MAX_HUD_WEAPONS];	// Weapons Array

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
		memset( bSynced, 0, sizeof bSynced );
		for( int i = 0; i < MAX_HUD_WEAPONS; i++ )
			rgWeapons[i].iGridPos = -1;
	}

///// WEAPON /////
	int			iOldWeaponBits;
	unsigned char	bSynced[MAX_HUD_WEAPONS];	// cl_lw 1: ownership last taken from weapon_data

	WEAPON *GetWeapon( int iId ) { return &rgWeapons[iId]; }

	// Sven client.dll 0x10003cd7: the highest used bucket, excluding bucket 9,
	// decides how many are shown, from 6 up to 10.
	int VisibleSlots( void ) const
	{
		int count = 6;
		for( int i = 0; i < MAX_HUD_WEAPONS; i++ )
		{
			if( rgWeapons[i].iId && rgWeapons[i].iSlot != 9 && rgWeapons[i].iSlot + 1 > count )
				count = rgWeapons[i].iSlot + 1;
		}
		return count > 10 ? 10 : count;
	}
	void AddWeapon( WEAPON *wp ) 
	{ 
		WEAPON *dst = &rgWeapons[wp->iId];

		// Sven client.dll 0x100046a0 only refreshes the record; which cell it
		// occupies is decided by the per-frame inventory pass
		// (0x10003be0) from iSlot/iSlotPos. If the server moved the weapon to
		// a different cell, detach the old one so it can't linger twice.
		if( dst->iId && ( dst->iSlot != wp->iSlot || dst->iSlotPos != wp->iSlotPos ))
			DropWeapon( dst );

		rgWeapons[wp->iId] = *wp;	
		rgWeapons[wp->iId].iGridPos = -1; // let the inventory pass place it
		LoadWeaponSprites( &rgWeapons[wp->iId] );
	}

	void PickupWeapon( WEAPON *wp )
	{
		// Sven client.dll 0x10003be0 puts an owned weapon in the exact cell
		// the server sent (rgSlots[iSlot][iSlotPos]) and never folds it into
		// the first free position. Folding made the slot order depend on
		// drop/pickup timing; the server's layout must win.
		int b = wp->iSlot;
		int p = wp->iSlotPos;

		if( b < 0 || b >= MAX_WEAPON_SLOTS || p < 0 || p >= MAX_WEAPON_POSITIONS )
		{
			wp->iGridPos = -1;
			return;
		}

		// A record may already sit in another cell of this row (it was folded
		// into a free position before); detach it so re-placing can't leave a
		// second reference behind.
		if( wp->iGridPos >= 0 && wp->iGridPos < MAX_WEAPON_POSITIONS && wp->iGridPos != p
			&& rgSlots[b][wp->iGridPos] == wp )
			rgSlots[b][wp->iGridPos] = NULL;

		if( !rgSlots[b][p] || rgSlots[b][p] == wp )
		{
			rgSlots[b][p] = wp;
			wp->iGridPos = p;
		}
		else if( wp->bStockSlot )
		{
			// A custom weapon claims the pinned stock cell (a mod may put its
			// own weapon at 0/0). Sven would skip the stock weapon and hide it
			// from the HUD, so take the next free position in the same row.
			for( int alt = 0; alt < MAX_WEAPON_POSITIONS; alt++ )
			{
				if( !rgSlots[b][alt] )
				{
					rgSlots[b][alt] = wp;
					wp->iGridPos = alt;
					return;
				}
			}
			wp->iGridPos = -1;
		}
		else
		{
			wp->iGridPos = -1; // cell owned by another record; Sven skips it
		}
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
		wp->iGridPos = -1;
	}

	void DropAllWeapons( void )
	{
		for( int i = 0; i < MAX_HUD_WEAPONS; i++ )
		{
			if( rgWeapons[i].iId )
				DropWeapon( &rgWeapons[i] );
		}
		memset( bSynced, 0, sizeof bSynced );
	}

	WEAPON* GetWeaponSlot( int slot, int pos ) { return rgSlots[slot][pos]; }

	// Cycle position of a weapon inside its bucket. AddWeapon resets iGridPos so
	// every WeaponList lets the inventory pass re-place the record; fall back to
	// the server's iSlotPos until that happens.
	static int GridPos( const WEAPON *wp )
	{
		if( !wp )
			return MAX_WEAPON_POSITIONS;
		return wp->iGridPos >= 0 ? wp->iGridPos : wp->iSlotPos;
	}

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

struct WEAPON;
extern WEAPON *gpActiveSel; // NULL = weapon selection menu closed (ammo.cpp)

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
