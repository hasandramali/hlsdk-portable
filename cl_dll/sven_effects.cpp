// Sven user messages use 32-bit fixed-point coordinates (1/8 unit).
// Wire layouts verified against steam-refs/sven/client.dll.
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "event_api.h"
#include "r_efx.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "com_model.h"

static bool ViolenceEnabled( const char *name )
{
	cvar_t *cv = gEngfuncs.pfnGetCvarPointer( name );
	return !cv || cv->value != 0;
}

static int EffectModel( const char *name )
{
	int index = 0;
	gEngfuncs.CL_LoadModel( (char *)name, &index );
	return index;
}

static void ReadPosition( float *origin )
{
	for( int i = 0; i < 3; ++i ) origin[i] = READ_COORD();
}

static int CreateBlood( const char *, int size, void *buffer )
{
	if( size != 14 ) return 0;
	BEGIN_READ( buffer, size );
	vec3_t origin;
	ReadPosition( origin );
	int color = READ_BYTE(), scale = READ_BYTE();
	if( !ViolenceEnabled( color == 247 ? "violence_hblood" : "violence_ablood" ) ) return 1;
	int spray = EffectModel( "sprites/bloodspray.spr" );
	int drops = EffectModel( "sprites/blood.spr" );
	if( spray && drops ) gEngfuncs.pEfxAPI->R_BloodSprite( origin, color, spray, drops, scale );
	return 1;
}

static void GibHit( TEMPENTITY *ent, pmtrace_t *trace )
{
	int color = ent->entity.curstate.iuser1;
	if( ent->entity.curstate.iuser2 > 0 && ViolenceEnabled( color == 247 ? "violence_hblood" : "violence_ablood" ) )
	{
		char decal[32];
		snprintf( decal, sizeof(decal), color == 247 ? "{blood%d" : "{yblood%d", (int)gEngfuncs.pfnRandomLong( 1, 6 ) );
		int index = gEngfuncs.pEfxAPI->Draw_DecalIndex( gEngfuncs.pEfxAPI->Draw_DecalIndexFromName( decal ) );
		gEngfuncs.pEfxAPI->R_DecalShoot( index, gEngfuncs.pEventAPI->EV_IndexFromTrace( trace ), 0, trace->endpos, 0 );
		--ent->entity.curstate.iuser2;
	}
	if( ent->entity.curstate.iuser3 )
	{
		VectorCopy( trace->endpos, ent->entity.origin );
		VectorCopy( trace->endpos, ent->entity.prevstate.origin );
		VectorClear( ent->entity.baseline.origin );
		VectorClear( ent->entity.baseline.angles );
		ent->flags &= ~(FTENT_GRAVITY | FTENT_ROTATE | FTENT_COLLIDEWORLD);
	}
}

static int Gib( const char *, int size, void *buffer )
{
	if( size < 1 ) return 0;
	BEGIN_READ( buffer, size );
	int kind = READ_BYTE();
	if( kind == 3 ) return 1; // native client has no effect for this kind
	if( kind < 0 || kind > 4 || size != 25 ) return 0;
	vec3_t origin, velocity, angles;
	ReadPosition( origin );
	ReadPosition( velocity );
	bool alien = kind == 2;
	if( !ViolenceEnabled( alien ? "violence_agibs" : "violence_hgibs" ) ) return 1;
	const char *model = alien ? "models/agibs.mdl" : kind == 4 ? "models/stickygibpink.mdl" : "models/hgibs.mdl";
	int index = EffectModel( model );
	if( !index ) return 1;
	cvar_t *countCvar = gEngfuncs.pfnGetCvarPointer( "cl_gibcount" );
	int count = countCvar ? (int)countCvar->value : 6;
	if( count <= 0 ) return 1;
	if( count > 32 ) count = 32;
	if( kind == 4 ) count *= 3;
	else if( !alien ) ++count; // head plus body pieces
	float speed = Length( velocity );
	for( int n = 0; n < count; ++n )
	{
		vec3_t direction;
		for( int axis = 0; axis < 3; ++axis )
		{
			direction[axis] = gEngfuncs.pfnRandomFloat( -1, 1 ) * speed;
			angles[axis] = gEngfuncs.pfnRandomFloat( 0, 360 );
		}
		TEMPENTITY *ent = gEngfuncs.pEfxAPI->R_TempModel( origin, direction, angles, 15, index, 0 );
		if( !ent ) break;
		ent->entity.curstate.body = kind == 4 ? 0 : alien ? gEngfuncs.pfnRandomLong( 0, 4 ) :
			n == 0 ? 0 : gEngfuncs.pfnRandomLong( 1, kind == 1 ? 6 : 10 );
		ent->flags |= FTENT_GRAVITY | FTENT_ROTATE | FTENT_COLLIDEWORLD | FTENT_FADEOUT;
		ent->bounceFactor = 0.55f;
		for( int axis = 0; axis < 3; ++axis )
			ent->entity.baseline.angles[axis] = gEngfuncs.pfnRandomFloat( -256, 256 );
		ent->entity.curstate.iuser1 = alien ? 195 : 247;
		ent->entity.curstate.iuser2 = 5;
		ent->entity.curstate.iuser3 = kind == 4;
		ent->hitcallback = GibHit;
	}
	return 1;
}

static int ShkFlash( const char *, int size, void *buffer )
{
	if( size != 13 ) return 0;
	BEGIN_READ( buffer, size );
	vec3_t origin;
	ReadPosition( origin );
	bool large = READ_BYTE() != 0;
	dlight_t *light = large ? gEngfuncs.pEfxAPI->CL_AllocDlight( 0 ) : gEngfuncs.pEfxAPI->CL_AllocElight( 0 );
	if( light )
	{
		VectorCopy( origin, light->origin );
		light->color.r = 85; light->color.g = 200; light->color.b = 255;
		light->radius = light->decay = large ? 96 : 24;
		light->die = gEngfuncs.GetClientTime() + 0.5f;
	}
	if( large )
	{
		light = gEngfuncs.pEfxAPI->CL_AllocDlight( 0 );
		if( light )
		{
			VectorCopy( origin, light->origin );
			light->color.r = light->color.g = light->color.b = 255;
			light->radius = 48; light->decay = 32;
			light->die = gEngfuncs.GetClientTime() + 1;
		}
	}
	gEngfuncs.pEventAPI->EV_PlaySound( 0, origin, CHAN_AUTO,
		large ? "weapons/shock_impact.wav" : "shocktrooper/shock_fire.wav", 1, ATTN_NORM, 0, 100 );
	return 1;
}

void SvenEffects_Init()
{
	gEngfuncs.pfnHookUserMsg( "CreateBlood", CreateBlood );
	gEngfuncs.pfnHookUserMsg( "Gib", Gib );
	gEngfuncs.pfnHookUserMsg( "ShkFlash", ShkFlash );
	if( !gEngfuncs.pfnGetCvarPointer( "cl_gibcount" ) )
		gEngfuncs.pfnRegisterVariable( "cl_gibcount", "6", FCVAR_ARCHIVE );
}
