// Client-side (cl_lw 1) versions of the Sven Co-op weapons the original
// client.dll predicts. Offsets in comments refer to that binary.
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"
#include "usercmd.h"
#include "entity_state.h"
#include "demo_api.h"
#include "pm_defs.h"
#include "event_api.h"
#include "r_efx.h"
#include "../hud_iface.h"
#include "../com_weapons.h"

void HUD_PrepEntity( CBaseEntity *pEntity, CBasePlayer *pWeaponOwner );

namespace
{
enum { FLAGS_PREDICTED = FEV_NOTHOST, CHANNEL_WEAPON = 1 };

const Vector kNoSpread( 0, 0, 0 );

class CSvenWeapon : public CBasePlayerWeapon
{
public:
	void Spawn( void ) { Precache(); m_iId = Id(); }
	void Precache( void ) { m_usClCheck = PRECACHE_EVENT( 1, "events/clcheck.sc" ); }
	BOOL UseDecrement( void ) { return TRUE; }

	int GetItemInfo( ItemInfo *p )
	{
		memset( p, 0, sizeof( *p ));
		p->pszName = STRING( pev->classname );
		p->iId = m_iId = Id();
		p->iMaxClip = -1;
		return 1;
	}

	virtual int Id( void ) = 0;
	virtual void Decode( const weapon_data_t *data ) {}
	virtual void Encode( weapon_data_t *data ) {}

protected:
	static float Now( void ) { return UTIL_WeaponTimeBase(); }

	float SharedRandom( float low, float high ) const
	{
		return UTIL_SharedRandomFloat( m_pPlayer->random_seed, low, high );
	}

	void SetIdleDelay( float low, float high )
	{
		m_flTimeWeaponIdle = Now() + SharedRandom( low, high );
	}

	void SetAttackDelay( float delay )
	{
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = Now() + delay;
	}

	bool Underwater( void ) const { return m_pPlayer->pev->waterlevel == 3; }

	int ReserveAmmo( void ) const
	{
		return m_iPrimaryAmmoType >= 0 && m_iPrimaryAmmoType < MAX_AMMO_SLOTS ? m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] : 0;
	}

	// client.dll 0x1002ade0: moving, standing and crouched cones.
	Vector Cone( float moving, float standing, float crouched ) const
	{
		float value = moving;
		if( m_pPlayer->pev->velocity.Length() == 0.0f )
			value = FBitSet( m_pPlayer->pev->flags, FL_DUCKING ) ? crouched : standing;
		return Vector( value, value, value );
	}

	Vector Shoot( int shots, const Vector &cone, float distance )
	{
		const Vector source = m_pPlayer->GetGunPosition();
		const Vector aim = m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );
		return m_pPlayer->FireBulletsPlayer( shots, source, aim, cone, distance, BULLET_PLAYER_MP5, 2, 0, m_pPlayer->pev, m_pPlayer->random_seed );
	}

	void Playback( int flags, unsigned short eventIndex, float f1, float f2, int i1, int i2, int b1, int b2 )
	{
		PLAYBACK_EVENT_FULL( flags, m_pPlayer->edict(), eventIndex, 0.0f, g_vecZero, g_vecZero, f1, f2, i1, i2, b1, b2 );
	}

	void PlayClCheck( int sound, float volume )
	{
		Playback( FLAGS_PREDICTED, m_usClCheck, (float)sound, volume, 2, CHANNEL_WEAPON, 0, 0 );
	}

	void DryFire( float delay )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = Now() + delay;
	}

	// client.dll 0x1008ebc0 / 0x10097900: first half of a two-part reload.
	bool StartStagedReload( int maxClip, int anim, float delay, int body )
	{
		const int want = maxClip - m_iClip;
		if( ReserveAmmo() <= 0 || want <= 0 )
			return false;
		m_pPlayer->m_flNextAttack = Now() + delay;
		SendWeaponAnim( anim, 1, body );
		m_fInReload = TRUE;
		return true;
	}

	unsigned short m_usClCheck;
};

class CSvenEagle : public CSvenWeapon
{
	enum { CLIP = 7, SOUND_SIGHT_ON = 22, SOUND_SIGHT_OFF = 21 };
	unsigned short m_usFire;
	bool m_bLaserInstalled;
	bool m_bLaser;
public:
	CSvenEagle() : m_bLaserInstalled( false ), m_bLaser( false ) {}
	int Id( void ) { return 27; }
	void Precache( void ) { CSvenWeapon::Precache(); m_usFire = PRECACHE_EVENT( 1, "events/deagle.sc" ); }
	void Decode( const weapon_data_t *d ) { m_bLaserInstalled = d->iuser3 != 0; m_bLaser = d->m_fInZoom != 0; }
	void Encode( weapon_data_t *d ) { d->iuser3 = m_bLaserInstalled; d->m_fInZoom = m_bLaser; }
	BOOL Deploy( void ) { return DefaultDeploy( "models/v_desert_eagle.mdl", "models/p_desert_eagle.mdl", 9, "onehanded" ); }

	void PrimaryAttack( void )
	{
		if( Underwater() || m_iClip <= 0 )
		{
			DryFire( 0.3f );
			return;
		}

		m_iClip--;
		const Vector cone = m_bLaser ? Cone( 0.0436f, 0.0349f, 0.02618f ) : Cone( 0.02618f, 0.01745f, 0.00873f );
		Shoot( 1, cone, 8192 );
		Playback( FLAGS_PREDICTED, m_usFire, 0, 0, 0, 0, m_iClip == 0, 1 );
		m_flNextPrimaryAttack = Now() + ( m_bLaser ? 0.3f : 0.6f );
		SetIdleDelay( 10, 15 );
	}

	void SecondaryAttack( void )
	{
		if( !m_bLaserInstalled )
		{
			m_bLaser = true;
			m_flNextSecondaryAttack = Now() + 1.0f;
			SetIdleDelay( 6, 12 );
			return;
		}

		m_bLaser = !m_bLaser;
		PlayClCheck( m_bLaser ? SOUND_SIGHT_ON : SOUND_SIGHT_OFF, 1.0f );
		m_flNextSecondaryAttack = Now() + 0.6f;
		SetIdleDelay( 6, 12 );
	}

	void Reload( void )
	{
		if( m_iClip == CLIP )
			return;
		if( DefaultReload( CLIP, m_iClip ? 8 : 7, 1.5f ))
		{
			if( m_bLaser )
				m_flNextSecondaryAttack = Now() + 1.6f;
			SetIdleDelay( 8, 14 );
		}
	}

	void WeaponIdle( void )
	{
		ResetEmptySound();
		if( m_flTimeWeaponIdle > Now())
			return;

		const float roll = SharedRandom( 0, 1 );
		int anim;
		float delay;
		if( roll <= 0.2f ) { anim = 0; delay = 2.533f + roll * 60.0f; }
		else if( roll <= 0.3f ) { anim = 1; delay = 2.583f + ( roll - 0.2f ) * 70.0f; }
		else if( roll <= 0.6f ) { anim = 2; delay = 1.733f + ( roll - 0.3f ) * 25.0f; }
		else if( roll <= 0.8f ) { anim = 3; delay = 2.533f + ( roll - 0.6f ) * 60.0f; }
		else { anim = 4; delay = 3.0f + ( roll - 0.8f ) * 60.0f; }
		m_flTimeWeaponIdle = Now() + delay;
		SendWeaponAnim( anim );
	}
};

class CSvenM249 : public CSvenWeapon
{
	enum { CLIP = 200, ANIM_RELOAD_START = 2, ANIM_RELOAD_FINISH = 3, ANIM_DEPLOY = 5 };
	unsigned short m_usLegacy, m_usFire;
	bool m_bBelt;
	int m_iReloadStage;

	int Body( void ) const
	{
		if( m_bBelt )
			return m_iClip > 7 ? 0 : 8 - m_iClip;
		return m_iClip > 15 ? 0 : 15 - m_iClip;
	}
public:
	CSvenM249() : m_bBelt( false ), m_iReloadStage( 0 ) {}
	int Id( void ) { return 24; }
	void Precache( void )
	{
		CSvenWeapon::Precache();
		m_usLegacy = PRECACHE_EVENT( 1, "events/m249_LEGACY.sc" );
		m_usFire = PRECACHE_EVENT( 1, "events/m249.sc" );
	}
	void Decode( const weapon_data_t *d ) { m_bBelt = d->iuser3 != 0; m_iReloadStage = (int)d->fuser1; }
	void Encode( weapon_data_t *d ) { d->iuser3 = m_bBelt; d->fuser1 = (float)m_iReloadStage; }

	BOOL Deploy( void )
	{
		const BOOL deployed = DefaultDeploy( "models/v_saw.mdl", "models/p_saw.mdl", ANIM_DEPLOY, "saw", 0, Body());
		if( deployed )
			m_bBelt = true;
		return deployed;
	}

	void PrimaryAttack( void )
	{
		if( Underwater() || m_iClip <= 0 )
		{
			DryFire( 0.15f );
			return;
		}

		m_iClip--;
		Shoot( 1, Cone( 0.06105f, 0.0349f, 0.02618f ), 2048 );
		Playback( FLAGS_PREDICTED, m_bBelt ? m_usLegacy : m_usFire,
			SvenEventCone( m_pPlayer, m_bBelt ? 0.05234f : 0.06105f, 0.0349f, 0.02618f ),
			SvenEventCone( m_pPlayer, m_bBelt ? 0.05234f : 0.06105f, 0.0349f, 0.02618f ), m_iClip, Body(), 0, 0 );
		SetAttackDelay( 0.08f );

		// client.dll 0x1008f050: the view kick is applied here, not in the event.
		m_pPlayer->pev->punchangle.x -= SharedRandom( 0, 2 );
		m_pPlayer->pev->punchangle.y += SharedRandom( -0.5f, 0.5f );
		SetIdleDelay( 10, 15 );
	}

	void SecondaryAttack( void ) { m_flNextSecondaryAttack = Now() + 1.0f; }

	void Reload( void )
	{
		if( m_flNextPrimaryAttack > Now() || ( m_fInReload && m_iReloadStage ))
			return;
		if( StartStagedReload( CLIP, ANIM_RELOAD_START, m_bBelt ? 1.5f : 3.5f, Body()))
		{
			m_flTimeWeaponIdle = Now() + ( m_bBelt ? 1.5f : 2.5f );
			m_iReloadStage = ANIM_RELOAD_FINISH;
		}
	}

	void WeaponIdle( void )
	{
		ResetEmptySound();
		if( m_flTimeWeaponIdle > Now())
			return;

		if( m_iReloadStage )
		{
			SendWeaponAnim( m_iReloadStage, 1, Body());
			m_flTimeWeaponIdle = Now() + ( m_bBelt ? 2.5f : 3.0f );
			m_iReloadStage = 0;
			m_fInReload = TRUE;
			m_flNextPrimaryAttack = m_flNextSecondaryAttack = m_flTimeWeaponIdle + 0.1f;
			return;
		}

		const int anim = UTIL_SharedRandomLong( m_pPlayer->random_seed, 0, 4 ) == 1 ? 0 : 1;
		m_flTimeWeaponIdle = Now() + ( anim ? 13.0f : 9.0f );
		SendWeaponAnim( anim, 1, Body());
	}
};

class CSvenSniper : public CSvenWeapon
{
	enum { CLIP = 5, ANIM_DRAW = 0, ANIM_IDLE = 1, ANIM_RELOAD_EMPTY = 4, ANIM_RELOAD_FINISH = 5, ANIM_RELOAD = 6, SOUND_ZOOM = 20 };
	unsigned short m_usFire;
	bool m_bZoomed;
	int m_iReloadStage;
public:
	CSvenSniper() : m_bZoomed( false ), m_iReloadStage( 0 ) {}
	int Id( void ) { return 23; }
	void Precache( void ) { CSvenWeapon::Precache(); m_usFire = PRECACHE_EVENT( 1, "events/sniperrifle.sc" ); }
	void Decode( const weapon_data_t *d ) { m_bZoomed = d->m_fInZoom != 0; m_iReloadStage = (int)d->fuser1; }
	void Encode( weapon_data_t *d ) { d->m_fInZoom = m_bZoomed; d->fuser1 = (float)m_iReloadStage; }
	BOOL Deploy( void ) { return DefaultDeploy( "models/v_m40a1.mdl", "models/p_m40a1.mdl", ANIM_DRAW, "sniper" ); }

	// client.dll 0x10097790: the scope is a 20 degree field of view.
	void SetZoom( bool zoomed ) { m_pPlayer->pev->fov = m_pPlayer->m_iFOV = zoomed ? 20 : 0; }

	void Holster( int skiplocal )
	{
		m_bZoomed = false;
		SetZoom( false );
		CSvenWeapon::Holster( skiplocal );
	}

	void PrimaryAttack( void )
	{
		if( Underwater() || m_iClip <= 0 )
		{
			DryFire( 0.15f );
			return;
		}

		const bool lastRound = m_iClip == 1;
		m_iClip--;
		const Vector cone = m_bZoomed ? Cone( 0.00873f, 0, 0 ) : Cone( 0.07846f, 0.05234f, 0.02618f );
		Shoot( 1, cone, 8192 );
		Playback( FLAGS_PREDICTED, m_usFire,
			SvenEventCone( m_pPlayer, 0.06105f, 0.0349f, 0.02618f ), SvenEventCone( m_pPlayer, 0.06105f, 0.0349f, 0.02618f ), lastRound, 0, m_bZoomed, 0 );
		m_flNextPrimaryAttack = Now() + 1.8f;
		SetIdleDelay( 10, 15 );
	}

	void SecondaryAttack( void )
	{
		m_bZoomed = !m_bZoomed;
		SetZoom( m_bZoomed );
		PlayClCheck( SOUND_ZOOM, 1.0f );
		m_flNextSecondaryAttack = Now() + 0.6f;
	}

	void Reload( void )
	{
		if( m_flNextPrimaryAttack > Now() || ( m_fInReload && m_iReloadStage ))
			return;

		const bool empty = m_iClip == 0;
		if( !StartStagedReload( CLIP, empty ? ANIM_RELOAD_EMPTY : ANIM_RELOAD, empty ? 2.5f : 2.6f, 0 ))
			return;
		m_bZoomed = false;
		SetZoom( false );
		if( empty )
		{
			m_flTimeWeaponIdle = Now() + 2.2f;
			m_flNextPrimaryAttack = Now() + 3.5f;
			m_iReloadStage = ANIM_RELOAD_FINISH;
		}
		else
		{
			m_flTimeWeaponIdle = Now() + 3.0f;
			m_iReloadStage = 0;
		}
	}

	void WeaponIdle( void )
	{
		ResetEmptySound();
		if( m_flTimeWeaponIdle > Now())
			return;

		if( m_iReloadStage )
		{
			SendWeaponAnim( m_iReloadStage );
			m_flTimeWeaponIdle = Now() + 1.5f;
			m_iReloadStage = 0;
			m_fInReload = TRUE;
			m_flNextPrimaryAttack = m_flNextSecondaryAttack = m_flTimeWeaponIdle - 0.2f;
			return;
		}

		SendWeaponAnim( ANIM_IDLE );
		SetIdleDelay( 10, 15 );
	}
};

class CSvenM16 : public CSvenWeapon
{
	enum { CLIP = 30, BURST_SHOTS = 3, ANIM_DRAW = 0, ANIM_RELOAD = 6, ANIM_GRENADE_FIRE = 7, ANIM_GRENADE_RELOAD = 8, SOUND_GRENADE_A = 2, SOUND_GRENADE_B = 3 };
	unsigned short m_usFire;
	float m_flBurst;
	int m_iGrenades;
	int m_iGrenadeLoaded;
	int m_iPendingAnim;

	void FireBurstShot( void )
	{
		const bool first = m_flBurst == BURST_SHOTS;
		Shoot( 1, Cone( 0.05234f, 0.0349f, 0.02618f ), 8192 );
		Playback( FLAGS_PREDICTED, m_usFire, 0, 0, m_iClip, first, 0, 0 );
		m_iClip--;
		m_flBurst -= 1.0f;
		m_flNextSecondaryAttack += 0.07f;
	}
public:
	CSvenM16() : m_flBurst( 0 ), m_iGrenades( 0 ), m_iGrenadeLoaded( 1 ), m_iPendingAnim( 0 ) {}
	int Id( void ) { return 25; }
	void Precache( void ) { CSvenWeapon::Precache(); m_usFire = PRECACHE_EVENT( 1, "events/m16a2.sc" ); }
	void Decode( const weapon_data_t *d ) { m_iGrenades = d->iuser3; m_iPendingAnim = (int)d->fuser1; m_flBurst = d->fuser2; }
	void Encode( weapon_data_t *d ) { d->iuser3 = m_iGrenades; d->fuser1 = (float)m_iPendingAnim; d->fuser2 = m_flBurst; }

	BOOL Deploy( void )
	{
		m_flBurst = 0;
		return DefaultDeploy( "models/v_m16a2.mdl", "models/p_m16.mdl", ANIM_DRAW, "m16" );
	}

	void ItemPostFrame( void )
	{
		if( m_iClip <= 0 )
			m_flBurst = 0;
		if( m_flBurst > 0 && m_flNextSecondaryAttack <= Now())
			FireBurstShot();
		CSvenWeapon::ItemPostFrame();
	}

	void PrimaryAttack( void )
	{
		if( Underwater() || m_iClip <= 0 )
		{
			DryFire( 0.15f );
			return;
		}
		if( m_flBurst <= 0 )
		{
			m_flBurst = BURST_SHOTS;
			m_flNextPrimaryAttack = Now() + 0.5f;
		}
	}

	void SecondaryAttack( void )
	{
		if( Underwater() || ( m_iGrenades <= 0 && m_iGrenadeLoaded <= 0 ))
		{
			DryFire( 0.15f );
			return;
		}

		if( m_iPendingAnim > 0 )
		{
			SendWeaponAnim( m_iPendingAnim );
			m_pPlayer->m_flNextAttack = m_flNextPrimaryAttack = m_flNextSecondaryAttack = m_flTimeWeaponIdle = Now() + 1.5f;
			m_iPendingAnim = 0;
			m_iGrenadeLoaded = 1;
			m_iGrenades--;
			return;
		}

		m_iGrenadeLoaded = 0;
		m_iPendingAnim = ANIM_GRENADE_RELOAD;
		SendWeaponAnim( ANIM_GRENADE_FIRE );
		PlayClCheck( UTIL_SharedRandomLong( m_pPlayer->random_seed, 0, 1 ) ? SOUND_GRENADE_B : SOUND_GRENADE_A, 0.8f );
		SetAttackDelay( 0.8f );
		m_flTimeWeaponIdle = Now() + 5.0f;
	}

	void Reload( void )
	{
		if( m_flBurst == 0 && DefaultReload( CLIP, ANIM_RELOAD, 3.0f ))
			SetIdleDelay( 10, 15 );
	}

	void WeaponIdle( void )
	{
		ResetEmptySound();
		if( m_flTimeWeaponIdle > Now())
			return;
		SendWeaponAnim( UTIL_SharedRandomLong( m_pPlayer->random_seed, 0, 1 ) ? 2 : 3 );
		SetIdleDelay( 10, 15 );
	}
};

class CSvenUzi : public CSvenWeapon
{
	enum { CLIP = 32, FIRE_RIGHT_ONLY = 1, FIRE_LEFT_ONLY = 2, FIRE_RIGHT_AKIMBO = 3, FIRE_BOTH = 4 };
	unsigned short m_usFire, m_usAkimbo;
	bool m_bAkimbo;
	int m_iLeftClip;

	void Fire( int mode )
	{
		if( Underwater() || ( m_bAkimbo ? ( !m_iClip && !m_iLeftClip ) : !m_iClip ))
		{
			DryFire( 0.15f );
			return;
		}

		int anim = 14;
		switch( mode )
		{
		case FIRE_RIGHT_ONLY: anim = 5; break;
		case FIRE_RIGHT_AKIMBO: anim = 13; break;
		case FIRE_BOTH: anim = 15; break;
		}

		const Vector cone = m_bAkimbo ? Cone( 0.08716f, 0.06976f, 0.06105f ) : Cone( 0.05234f, 0.0349f, 0.02618f );
		Shoot( mode == FIRE_BOTH ? 2 : 1, cone, 8192 );
		Playback( FLAGS_PREDICTED, m_bAkimbo ? m_usAkimbo : m_usFire,
			SvenEventCone( m_pPlayer, 0.05234f, 0.0349f, 0.02618f ), SvenEventCone( m_pPlayer, 0.05234f, 0.0349f, 0.02618f ), anim, 0, 0, 0 );

		if( mode == FIRE_BOTH || mode == FIRE_RIGHT_ONLY || mode == FIRE_LEFT_ONLY )
			m_iClip = Q_max( m_iClip - 1, 0 );
		if( mode == FIRE_BOTH || mode == FIRE_RIGHT_AKIMBO )
			m_iLeftClip = Q_max( m_iLeftClip - 1, 0 );

		m_flNextPrimaryAttack = Now() + 0.07f;
		SetIdleDelay( 10, 15 );
	}
public:
	CSvenUzi() : m_bAkimbo( false ), m_iLeftClip( 0 ) {}
	int Id( void ) { return 17; }
	void Precache( void )
	{
		CSvenWeapon::Precache();
		m_usFire = PRECACHE_EVENT( 1, "events/uzi.sc" );
		m_usAkimbo = PRECACHE_EVENT( 1, "events/uziakimbo.sc" );
	}
	void Decode( const weapon_data_t *d ) { m_bAkimbo = d->iuser3 != 0; m_iLeftClip = d->iuser4; }
	void Encode( weapon_data_t *d ) { d->iuser3 = m_bAkimbo; d->iuser4 = m_iLeftClip; }
	BOOL Deploy( void ) { return DefaultDeploy( "models/v_uzi.mdl", "models/p_uzi.mdl", m_bAkimbo ? 16 : 4, "mp5" ); }

	void PrimaryAttack( void )
	{
		if( !m_bAkimbo )
			Fire( FIRE_RIGHT_ONLY );
		else if( m_iLeftClip > 0 )
			Fire( m_iClip > 0 ? FIRE_BOTH : FIRE_RIGHT_AKIMBO );
		else
			Fire( FIRE_LEFT_ONLY );
	}

	void Reload( void )
	{
		if( !m_bAkimbo )
		{
			if( DefaultReload( CLIP, 3, 2.5f ))
				SetIdleDelay( 10, 15 );
			return;
		}

		if( m_iClip >= CLIP && m_iLeftClip >= CLIP )
			return;
		const int anim = m_iClip >= CLIP ? 11 : m_iLeftClip >= CLIP ? 10 : 12;
		if( StartStagedReload( CLIP * 2 - m_iLeftClip, anim, 1.8f, 0 ))
			SetIdleDelay( 10, 15 );
	}

	void WeaponIdle( void )
	{
		ResetEmptySound();
		if( m_flTimeWeaponIdle > Now())
			return;
		SendWeaponAnim( m_bAkimbo ? 9 : UTIL_SharedRandomLong( m_pPlayer->random_seed, 0, 1 ) ? 2 : 1 );
		SetIdleDelay( 10, 15 );
	}
};

class CSvenMinigun : public CSvenWeapon
{
	enum { ANIM_IDLE = 0, ANIM_DEPLOY = 4, ANIM_SPINUP = 6, ANIM_SPIN = 8, ANIM_FIRE = 9, SPIN_UP = 1, SPIN_HOLD = 2, FLAGS_SPINUP = 0xa };
	unsigned short m_usFire, m_usSpin;
	bool m_bSpinning;

	void PlaySpin( int flags, int mode ) { Playback( flags, m_usSpin, 0, 0, mode, 0, 0, 0 ); }

	void SpinUp( float delay )
	{
		PlaySpin( FLAGS_SPINUP, SPIN_UP );
		SendWeaponAnim( ANIM_SPINUP );
		m_bSpinning = true;
		SetAttackDelay( delay );
		m_flTimeWeaponIdle = Now() + delay;
	}

	void HoldSpin( float delay )
	{
		PlaySpin( FLAGS_PREDICTED, SPIN_HOLD );
		if( m_pPlayer->pev->weaponanim != ANIM_SPIN )
			SendWeaponAnim( ANIM_SPIN );
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = m_flTimeWeaponIdle = Now() + delay;
	}
public:
	CSvenMinigun() : m_bSpinning( false ) {}
	int Id( void ) { return 21; }
	void Precache( void )
	{
		CSvenWeapon::Precache();
		m_usFire = PRECACHE_EVENT( 1, "events/minigun.sc" );
		m_usSpin = PRECACHE_EVENT( 1, "events/minigun_spin.sc" );
	}
	void Decode( const weapon_data_t *d ) { m_bSpinning = d->fuser1 != 0.0f; }
	void Encode( weapon_data_t *d ) { d->fuser1 = m_bSpinning; }

	BOOL Deploy( void )
	{
		m_bSpinning = false;
		const BOOL deployed = DefaultDeploy( "models/v_minigun.mdl", "models/p_minigunidle.mdl", ANIM_DEPLOY, "minigun" );
		SetAttackDelay( 2.0f );
		m_flTimeWeaponIdle = Now() + 2.0f;
		return deployed;
	}

	void Holster( int skiplocal )
	{
		m_bSpinning = false;
		CSvenWeapon::Holster( skiplocal );
	}

	void PrimaryAttack( void )
	{
		if( m_flNextPrimaryAttack > Now())
			return;
		if( Underwater())
		{
			m_bSpinning = false;
			DryFire( 0.15f );
			return;
		}
		if( !m_bSpinning )
		{
			SpinUp( 1.0f );
			return;
		}
		if( ReserveAmmo() <= 0 )
		{
			HoldSpin( 0.06f );
			return;
		}

		Shoot( 2, Cone( 0.08716f, 0.06105f, 0.04362f ), 4096 );
		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;
		Playback( FLAGS_PREDICTED, m_usFire, 0, 0, 0, 0, 0, 0 );
		if( m_pPlayer->pev->weaponanim != ANIM_FIRE )
			SendWeaponAnim( ANIM_FIRE );
		m_flNextPrimaryAttack += 0.06f;
		if( m_flNextPrimaryAttack < Now())
			m_flNextPrimaryAttack = Now() + 0.06f;
	}

	void SecondaryAttack( void )
	{
		if( m_flNextSecondaryAttack > Now() || Underwater())
			return;
		if( !m_bSpinning )
			SpinUp( 0.9f );
		else
			HoldSpin( 0.4f );
	}

	void WeaponIdle( void )
	{
		ResetEmptySound();
		if( m_flTimeWeaponIdle > Now())
			return;

		if( m_bSpinning )
		{
			PlaySpin( FLAGS_SPINUP, SPIN_HOLD );
			m_bSpinning = false;
			SetAttackDelay( 0.5f );
			m_flTimeWeaponIdle = Now() + 2.0f;
			return;
		}

		SendWeaponAnim( ANIM_IDLE );
		SetIdleDelay( 10, 15 );
	}
};

class CSvenSporeLauncher : public CSvenWeapon
{
	enum { CLIP = 8, ANIM_RELOAD_START = 2, ANIM_RELOAD = 3, ANIM_RELOAD_FINISH = 4, ANIM_FIRE = 5, ANIM_DEPLOY = 7, SOUND_FIRE = 23, SOUND_ALTFIRE = 24 };
	enum { RELOAD_NONE, RELOAD_READY, RELOAD_INSERTING };
	int m_iReload;
	float m_flNextReload;

	void Fire( int sound )
	{
		if( m_iClip <= 0 )
		{
			Reload();
			return;
		}

		m_iClip--;
		SendWeaponAnim( ANIM_FIRE );
		PlayClCheck( sound, 1.0f );
		SetAttackDelay( 0.6f );
		m_flTimeWeaponIdle = Now() + 3.0f;
		m_iReload = RELOAD_NONE;
	}

	void SetReloadDelay( float delay )
	{
		SetAttackDelay( delay );
		m_flTimeWeaponIdle = Now() + delay;
	}
public:
	CSvenSporeLauncher() : m_iReload( RELOAD_NONE ), m_flNextReload( 0 ) {}
	int Id( void ) { return 26; }
	void Decode( const weapon_data_t *d ) { m_iReload = d->iuser3; }
	void Encode( weapon_data_t *d ) { d->iuser3 = m_iReload; }

	BOOL Deploy( void )
	{
		m_flTimeWeaponIdle = Now() + 1.5f;
		return DefaultDeploy( "models/v_spore_launcher.mdl", "models/p_spore_launcher.mdl", ANIM_DEPLOY, "rpg" );
	}

	void PrimaryAttack( void ) { Fire( SOUND_FIRE ); }
	void SecondaryAttack( void ) { Fire( SOUND_ALTFIRE ); }

	void Reload( void )
	{
		if( ReserveAmmo() <= 0 || m_iClip == CLIP || m_flNextReload > gpGlobals->time )
			return;

		if( m_iReload == RELOAD_NONE )
		{
			if( m_flNextPrimaryAttack > Now())
				return;
			SendWeaponAnim( ANIM_RELOAD_START );
			m_iReload = RELOAD_READY;
			m_flNextReload = gpGlobals->time;
			SetReloadDelay( 0.7f );
		}
		else if( m_iReload == RELOAD_READY )
		{
			if( m_flTimeWeaponIdle > Now())
				return;
			if( m_iClip >= CLIP )
			{
				SendWeaponAnim( ANIM_RELOAD_FINISH );
				m_iReload = RELOAD_NONE;
				SetReloadDelay( 0.7f );
				return;
			}
			SendWeaponAnim( ANIM_RELOAD );
			m_flNextReload = gpGlobals->time;
			SetAttackDelay( m_iClip < 1 ? 0.7f : 0.1f );
			m_flTimeWeaponIdle = Now() + 0.8f;
			m_iReload = RELOAD_INSERTING;
		}
		else
		{
			m_iClip++;
			m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;
			m_iReload = RELOAD_READY;
		}
	}

	void WeaponIdle( void )
	{
		ResetEmptySound();
		if( m_flTimeWeaponIdle > Now())
			return;

		if( m_iReload != RELOAD_NONE )
		{
			if( m_iClip < CLIP && ReserveAmmo() > 0 )
			{
				Reload();
				return;
			}
			SendWeaponAnim( ANIM_RELOAD_FINISH );
			m_iReload = RELOAD_NONE;
			SetAttackDelay( 0.5f );
			m_flTimeWeaponIdle = Now() + 2.0f;
			return;
		}

		const float roll = gEngfuncs.pfnRandomFloat( 0, 10 );
		int anim = 0;
		float delay = 2.0f;
		if( roll > 8.0f ) { anim = 1; delay = 4.0f; }
		else if( roll > 6.0f ) { anim = 8; delay = 4.0f; }
		m_flTimeWeaponIdle = Now() + delay;
		SendWeaponAnim( anim );
	}
};

class CSvenShockRifle : public CSvenWeapon
{
	enum { ANIM_IDLE = 0, ANIM_DRAW = 2, ANIM_IDLE_ALT = 4 };
	unsigned short m_usFire, m_usBeam;
	int m_iBeamState;

	void StopBeam( void )
	{
		m_iBeamState = 0;
		m_flTimeWeaponIdle = Now() + 2.0f;
		SetAttackDelay( 0.5f );
	}
public:
	CSvenShockRifle() : m_iBeamState( 0 ) {}
	int Id( void ) { return 28; }
	void Precache( void )
	{
		CSvenWeapon::Precache();
		m_usFire = PRECACHE_EVENT( 1, "events/shockrifle.sc" );
		m_usBeam = PRECACHE_EVENT( 1, "events/shockriflebeam.sc" );
	}
	BOOL Deploy( void ) { return DefaultDeploy( "models/v_shock.mdl", "models/p_shock.mdl", ANIM_DRAW, "bow" ); }

	void Holster( int skiplocal )
	{
		m_iBeamState = 0;
		CSvenWeapon::Holster( skiplocal );
	}

	void PrimaryAttack( void )
	{
		if( m_iBeamState )
		{
			StopBeam();
			return;
		}
		if( Underwater() || ReserveAmmo() <= 0 )
		{
			m_flNextPrimaryAttack = Now() + 0.5f;
			return;
		}

		m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]--;
		Playback( FLAGS_PREDICTED, m_usFire, 0, 0, 0, 0, 0, 0 );
		m_flNextPrimaryAttack = Now() + 0.2f;
		SetIdleDelay( 10, 15 );
	}

	void SecondaryAttack( void )
	{
		if( ReserveAmmo() <= 0 )
		{
			if( m_iBeamState )
				StopBeam();
			else
				PlayEmptySound();
			return;
		}

		if( m_iBeamState == 0 )
			m_iBeamState = 1;
		else
			Playback( FLAGS_PREDICTED, m_usBeam, 0, 0, 0, 0, 0, 0 );
		m_flNextSecondaryAttack = Now() + 0.2f;
	}

	void WeaponIdle( void )
	{
		if( m_iBeamState )
			StopBeam();
		if( m_flTimeWeaponIdle > Now())
			return;

		const bool alternate = SharedRandom( 0, 1 ) > 0.8f;
		SendWeaponAnim( alternate ? ANIM_IDLE_ALT : ANIM_IDLE );
		m_flTimeWeaponIdle = Now() + ( alternate ? 2.5f : 3.75f );
	}
};

class CSvenDisplacer : public CSvenWeapon
{
	enum { ANIM_IDLE1 = 0, ANIM_IDLE2 = 1, ANIM_SPINUP = 2, ANIM_FIRE = 4, ANIM_DRAW = 5, AMMO_PRIMARY = 20, AMMO_SECONDARY = 60 };
	unsigned short m_usFire;
	float m_flFireTime;

	void BeginSpin( int cost )
	{
		if( m_flFireTime > 0 )
			return;
		if( ReserveAmmo() < cost )
		{
			DryFire( 0.15f );
			return;
		}
		SendWeaponAnim( ANIM_SPINUP );
		m_flFireTime = gpGlobals->time + 1.0333f;
		SetAttackDelay( 1.0333f + 0.5f );
		m_flTimeWeaponIdle = Now() + 2.0f;
	}
public:
	CSvenDisplacer() : m_flFireTime( 0 ) {}
	int Id( void ) { return 29; }
	void Precache( void ) { CSvenWeapon::Precache(); m_usFire = PRECACHE_EVENT( 1, "events/displacer.sc" ); }
	BOOL Deploy( void ) { return DefaultDeploy( "models/v_displacer.mdl", "models/p_displacer.mdl", ANIM_DRAW, "egon" ); }

	void Holster( int skiplocal )
	{
		m_flFireTime = 0;
		CSvenWeapon::Holster( skiplocal );
	}

	void ItemPostFrame( void )
	{
		if( m_flFireTime > 0 && gpGlobals->time >= m_flFireTime )
		{
			m_flFireTime = 0;
			SendWeaponAnim( ANIM_FIRE );
			Playback( FLAGS_PREDICTED, m_usFire, 0, 0, 0, 0, 0, 0 );
		}
		CSvenWeapon::ItemPostFrame();
	}

	void PrimaryAttack( void ) { BeginSpin( AMMO_PRIMARY ); }
	void SecondaryAttack( void ) { BeginSpin( AMMO_SECONDARY ); }

	void WeaponIdle( void )
	{
		ResetEmptySound();
		if( m_flFireTime > 0 || m_flTimeWeaponIdle > Now())
			return;
		SendWeaponAnim( SharedRandom( 0, 1 ) <= 0.5f ? ANIM_IDLE1 : ANIM_IDLE2 );
		SetIdleDelay( 10, 15 );
	}
};

CSvenEagle g_Eagle;
CSvenM249 g_M249;
CSvenSniper g_Sniper;
CSvenM16 g_M16;
CSvenUzi g_Uzi;
CSvenMinigun g_Minigun;
CSvenSporeLauncher g_Spore;
CSvenShockRifle g_Shock;
CSvenDisplacer g_Displacer;

CSvenWeapon *const g_SvenWeapons[] =
{
	&g_Uzi, &g_Minigun, &g_Sniper, &g_M249, &g_M16, &g_Spore, &g_Eagle, &g_Shock, &g_Displacer
};

CSvenWeapon *FindSvenWeapon( int id )
{
	for( CSvenWeapon *weapon : g_SvenWeapons )
	{
		if( weapon->Id() == id )
			return weapon;
	}
	return NULL;
}
}

// client.dll fire events pick a cone from the shooter's motion and stance
// and scatter the tracer and decal inside it with the engine's random.
float SvenEventCone( CBasePlayer *player, float moving, float standing, float crouched )
{
	float cone = moving;
	if( player->pev->velocity.Length() == 0.0f )
		cone = FBitSet( player->pev->flags, FL_DUCKING ) ? crouched : standing;
	return ( gEngfuncs.pfnRandomFloat( -0.5f, 0.5f ) + gEngfuncs.pfnRandomFloat( -0.5f, 0.5f )) * cone;
}

void SvenPredictedSound( CBasePlayer *player, int sound, float volume, int channel, float delay )
{
	if( !g_bSvenLocalWeapons )
		return;
	const unsigned short eventIndex = PRECACHE_EVENT( 1, "events/clcheck.sc" );
	PLAYBACK_EVENT_FULL( FEV_NOTHOST, player->edict(), eventIndex, delay, g_vecZero, g_vecZero, (float)sound, volume, 2, channel, 0, 0 );
}

void SvenWeapons_Prepare( CBasePlayer *owner )
{
	for( CSvenWeapon *weapon : g_SvenWeapons )
		HUD_PrepEntity( weapon, owner );
}

CBasePlayerWeapon *SvenWeapons_Find( int id )
{
	return FindSvenWeapon( id );
}

void SvenWeapons_Decode( int id, const weapon_data_t *data )
{
	if( CSvenWeapon *weapon = FindSvenWeapon( id ))
		weapon->Decode( data );
}

void SvenWeapons_Encode( int id, weapon_data_t *data )
{
	if( CSvenWeapon *weapon = FindSvenWeapon( id ))
		weapon->Encode( data );
}
