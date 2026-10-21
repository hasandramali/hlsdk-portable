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
#include <map>

struct SporeTrailState
{
	int token;
	bool glowStarted;
	float nextEmission;
	SporeTrailState(): token(0), glowStarted(false), nextEmission(0) {}
};

static std::map<int, SporeTrailState> sporeTrails;
static int sporeToken;
static int sporeGlowModel, sporeSprayModel;

static int NextSporeToken()
{
	sporeToken = sporeToken == 0x7fffffff ? 1 : sporeToken + 1;
	return sporeToken;
}

void SvenEffects_Reset()
{
	sporeTrails.clear();
	sporeGlowModel = sporeSprayModel = 0;
}

void SvenEffects_VidInit()
{
	sporeGlowModel = sporeSprayModel = 0;
	for( std::map<int, SporeTrailState>::iterator trail = sporeTrails.begin(); trail != sporeTrails.end(); ++trail )
	{
		trail->second.glowStarted = false;
		trail->second.token = NextSporeToken();
	}
}

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

static cl_entity_t *SporeEntity( int index )
{
	cl_entity_t *entity = gEngfuncs.GetEntityByIndex( index );
	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	if( !entity || !entity->model || !local || entity->curstate.messagenum < local->curstate.messagenum ) return NULL;
	return entity;
}

static void SporeGlow( TEMPENTITY *glow, float, float time )
{
	std::map<int, SporeTrailState>::const_iterator trail = sporeTrails.find( glow->clientIndex );
	cl_entity_t *entity = SporeEntity( glow->clientIndex );
	if( trail == sporeTrails.end() || trail->second.token != glow->entity.curstate.iuser4 || !entity )
	{
		glow->die = time;
		glow->flags &= ~FTENT_FADEOUT;
		return;
	}
	VectorCopy( entity->origin, glow->entity.origin );
	glow->die = time + 500;
}

static void SporeBurst( int index )
{
	cl_entity_t *entity = gEngfuncs.GetEntityByIndex( index );
	if( !entity || !entity->model ) return;
	vec3_t direction( 0, 0, 1 ), velocity( 0, 0, 0 );
	int spray = EffectModel( "sprites/pitdronespit.spr" );
	if( spray ) gEngfuncs.pEfxAPI->R_Sprite_Spray( entity->origin, direction, spray, 100, 150, 200 );
	int model = EffectModel( gEngfuncs.pfnRandomLong( 0, 1 ) ? "sprites/spore_exp_c_01.spr" : "sprites/spore_exp_01.spr" );
	TEMPENTITY *burst = model ? gEngfuncs.pEfxAPI->R_TempSprite( entity->origin, velocity, 3, model,
		kRenderTransAdd, kRenderFxNoDissipation, 1, 1, FTENT_SPRANIMATE ) : NULL;
	if( burst )
	{
		burst->entity.curstate.rendercolor.r = 200;
		burst->entity.curstate.rendercolor.g = 190;
		burst->entity.curstate.rendercolor.b = 95;
	}
	direction[2] = -1;
	if( spray ) gEngfuncs.pEfxAPI->R_Sprite_Spray( entity->origin, direction, spray, 100, 150, 200 );
}

static int SporeTrail( const char *, int size, void *buffer )
{
	if( size != 3 ) return 0;
	BEGIN_READ( buffer, size );
	int entity = READ_SHORT();
	bool enabled = READ_BYTE() != 0;
	if( entity <= 0 ) return 0;
	if( !enabled )
	{
		if( sporeTrails.erase( entity ) ) SporeBurst( entity );
	}
	else if( sporeTrails.find( entity ) == sporeTrails.end() && sporeTrails.size() < 2048 )
		sporeTrails[entity].token = NextSporeToken();
	return 1;
}

void SvenEffects_CreateEntities()
{
	if( sporeTrails.empty() ) return;
	float time = gEngfuncs.GetClientTime();
	for( std::map<int, SporeTrailState>::iterator trail = sporeTrails.begin(); trail != sporeTrails.end(); )
	{
		cl_entity_t *entity = SporeEntity( trail->first );
		if( !entity ) { sporeTrails.erase( trail++ ); continue; }
		SporeTrailState &state = trail->second;
		if( !state.glowStarted )
		{
			if( !sporeGlowModel ) sporeGlowModel = EffectModel( "sprites/glow01.spr" );
			vec3_t velocity( 0, 0, 0 );
			TEMPENTITY *glow = sporeGlowModel ? gEngfuncs.pEfxAPI->R_TempSprite( entity->origin, velocity, 0.7f,
				sporeGlowModel, kRenderTransAdd, kRenderFxDistort, 150.0f / 255, 500, FTENT_PERSIST | FTENT_CLIENTCUSTOM ) : NULL;
			if( glow )
			{
				glow->flags &= ~(FTENT_SPRANIMATE | FTENT_FADEOUT);
				glow->entity.curstate.rendercolor.r = 200;
				glow->entity.curstate.rendercolor.g = 190;
				glow->entity.curstate.rendercolor.b = 95;
				glow->entity.curstate.iuser4 = state.token;
				glow->clientIndex = trail->first;
				glow->callback = SporeGlow;
				state.glowStarted = true;
			}
		}
		if( time >= state.nextEmission || state.nextEmission - time > 1 )
		{
			if( !sporeSprayModel ) sporeSprayModel = EffectModel( "sprites/pitdronespit.spr" );
			vec3_t direction;
			if( Length( entity->curstate.velocity ) > 1 )
			{
				AngleVectors( entity->angles, direction, NULL, NULL );
				VectorScale( direction, -1, direction );
			}
			else { direction[0] = direction[1] = 0; direction[2] = 1; }
			if( sporeSprayModel ) gEngfuncs.pEfxAPI->R_Sprite_Spray( entity->origin, direction, sporeSprayModel, 1, 35, 50 );
			state.nextEmission = time + 0.05f;
		}
		++trail;
	}
}

extern void EV_HLDM_DecalGunshot( pmtrace_t *trace, int bulletType );

static int TracerDecal( const char *, int size, void *buffer )
{
	if( size != 26 ) return 0;
	BEGIN_READ( buffer, size );
	vec3_t start, end, direction, traceEnd;
	ReadPosition( start );
	ReadPosition( end );
	bool tracer = READ_BYTE() != 0;
	READ_BYTE();
	if( tracer ) gEngfuncs.pEfxAPI->R_TracerEffect( start, end );
	VectorSubtract( end, start, direction );
	float distance = Length( direction );
	if( distance <= 0 ) return 1;
	VectorScale( direction, 1.0f / distance, direction );
	VectorMA( end, 10, direction, traceEnd );
	gEngfuncs.pEventAPI->EV_PushPMStates();
	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	pmtrace_t trace;
	gEngfuncs.pEventAPI->EV_PlayerTrace( start, traceEnd, PM_WORLD_ONLY, -1, &trace );
	if( trace.fraction < 1 && !trace.startsolid && !trace.allsolid )
	{
		EV_HLDM_DecalGunshot( &trace, 13 );
		dlight_t *light = gEngfuncs.pEfxAPI->CL_AllocDlight( 0 );
		if( light )
		{
			VectorCopy( trace.endpos, light->origin );
			light->color.r = 255; light->color.g = 200; light->color.b = 80;
			light->radius = 64; light->decay = 0;
			light->die = gEngfuncs.GetClientTime() + 0.1f;
		}
	}
	gEngfuncs.pEventAPI->EV_PopPMStates();
	return 1;
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
	SvenEffects_Reset();
	gEngfuncs.pfnHookUserMsg( "SporeTrail", SporeTrail );
	gEngfuncs.pfnHookUserMsg( "TracerDecal", TracerDecal );
	gEngfuncs.pfnHookUserMsg( "CreateBlood", CreateBlood );
	gEngfuncs.pfnHookUserMsg( "Gib", Gib );
	gEngfuncs.pfnHookUserMsg( "ShkFlash", ShkFlash );
	if( !gEngfuncs.pfnGetCvarPointer( "cl_gibcount" ) )
		gEngfuncs.pfnRegisterVariable( "cl_gibcount", "6", FCVAR_ARCHIVE );
}
