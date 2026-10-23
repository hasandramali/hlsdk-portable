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
#include "sven_ui_protocol.h"
#include <cmath>
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

struct ShockPrimedState
{
	bool active;
	float start;
};

static ShockPrimedState shockPrimed[32];
static cl_entity_t *shockViewModel;
static model_t *shockViewModelModel;
static int shockViewPlayer;

struct ElectricCrowbarState
{
	bool active, soundStarted;
	float nextSound;
	dlight_t *light;
};

static ElectricCrowbarState electricCrowbars[32];

struct ToxicCloudState
{
	bool active;
	vec3_t origin;
	float end, nextEmission;
	int emitted, color;
};

static ToxicCloudState toxicClouds[16];

static void StopElectricCrowbar( int player, bool playSound )
{
	if( !electricCrowbars[player].active ) return;
	electricCrowbars[player].active = false;
	cl_entity_t *entity = gEngfuncs.GetEntityByIndex( player );
	if( entity && entity->curstate.renderfx == kRenderFxGlowShell &&
		entity->curstate.rendercolor.r == 80 && entity->curstate.rendercolor.g == 100 && entity->curstate.rendercolor.b == 220 )
	{
		entity->curstate.renderfx = kRenderFxNone;
		entity->curstate.rendercolor.r = entity->curstate.rendercolor.g = entity->curstate.rendercolor.b = 0;
	}
	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	cl_entity_t *view = gEngfuncs.GetViewModel();
	if( local && local->index == player && view )
	{
		view->curstate.effects &= ~EF_DIMLIGHT;
		if( view->curstate.renderfx == kRenderFxGlowShell && view->curstate.rendercolor.g == 75 )
		{
			view->curstate.renderfx = kRenderFxNone;
			view->curstate.renderamt = 255;
			view->curstate.rendercolor.r = view->curstate.rendercolor.g = view->curstate.rendercolor.b = 0;
		}
	}
	if( gEngfuncs.pEventAPI->EV_StopSound )
		gEngfuncs.pEventAPI->EV_StopSound( player, CHAN_ITEM, "weapons/cbe/cbe_idle1.wav" );
	if( entity && playSound ) gEngfuncs.pEventAPI->EV_PlaySound( player, entity->origin,
		CHAN_ITEM, "weapons/cbe/cbe_off.wav", 1, ATTN_NORM, 0, 100 );
	dlight_t *light = electricCrowbars[player].light;
	if( light && light->key == ( 0x43420000 | player ) )
	{
		light->radius = 0;
		light->die = gEngfuncs.GetClientTime();
	}
	electricCrowbars[player].light = NULL;
}

static void StopShockGlow()
{
	cl_entity_t *view = gEngfuncs.GetViewModel ? gEngfuncs.GetViewModel() : NULL;
	if( view && view == shockViewModel && view->model == shockViewModelModel &&
		view->curstate.renderfx == kRenderFxGlowShell )
	{
		view->curstate.renderfx = kRenderFxNone;
		view->curstate.renderamt = 255;
		view->curstate.rendercolor.r = 0;
		view->curstate.rendercolor.g = 0;
		view->curstate.rendercolor.b = 0;
	}
	shockViewModel = NULL;
	shockViewModelModel = NULL;
	shockViewPlayer = 0;
}

static int NextSporeToken()
{
	sporeToken = sporeToken == 0x7fffffff ? 1 : sporeToken + 1;
	return sporeToken;
}

void SvenEffects_Reset()
{
	StopShockGlow();
	for( int player = 0; player < 32; ++player )
	{
		shockPrimed[player].active = false;
		StopElectricCrowbar( player, false );
	}
	sporeTrails.clear();
	for( int cloud = 0; cloud < 16; ++cloud ) toxicClouds[cloud].active = false;
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

struct RampSpriteState
{
	int model, mode, effect, frame, framerate;
	float life, roll, scale, alpha, rollSpeed, scaleRate, alphaRate;
	vec3_t origin, velocity, colorRate;
	byte color[3];
	RampSpriteState(): model(0), mode(kRenderTransAdd), effect(kRenderFxNone), frame(0), framerate(0),
		life(0), roll(0), scale(1), alpha(1), rollSpeed(0), scaleRate(0), alphaRate(0),
		origin(0,0,0), velocity(0,0,0), colorRate(0,0,0)
	{
		color[0] = color[1] = color[2] = 255;
	}
};

static void RampSpriteUpdate( TEMPENTITY *sprite, float, float time )
{
	entity_state_t &base = sprite->entity.baseline;
	entity_state_t &state = sprite->entity.curstate;
	float age = time - base.fuser1;
	if( age < 0 ) age = 0;
	state.scale = base.scale + base.fuser2 * age;
	float alpha = base.renderamt + base.fuser3 * 255 * age;
	if( state.scale < 0.001f || alpha < 0 )
	{
		state.scale = 0.001f;
		sprite->die = time;
		return;
	}
	state.renderamt = alpha > 255 ? 255 : (int)alpha;
	byte *colors[3] = { &state.rendercolor.r, &state.rendercolor.g, &state.rendercolor.b };
	const byte initial[3] = { base.rendercolor.r, base.rendercolor.g, base.rendercolor.b };
	for( int channel = 0; channel < 3; ++channel )
	{
		float value = initial[channel] + base.vuser1[channel] * age;
		*colors[channel] = value < 0 ? 0 : value > 255 ? 255 : (byte)value;
	}
	sprite->entity.angles[2] = fmodf( base.angles[2] + base.fuser4 * age, 360 );
	state.frame = base.frame + state.framerate * age;
	if( sprite->frameMax >= 0 ) state.frame = fmodf( state.frame, sprite->frameMax + 1 );
}

static void CreateRampSprite( const RampSpriteState &data )
{
	if( data.model <= 0 || data.life <= 0 || data.scale <= 0 || data.mode > kRenderTransAdd ) return;
	model_t *model = gEngfuncs.pfnGetModelByIndex( data.model );
	if( !model || model->type != mod_sprite ) return;
	vec3_t origin = data.origin, velocity = data.velocity;
	TEMPENTITY *sprite = gEngfuncs.pEfxAPI->R_TempSprite( origin, velocity, data.scale, data.model,
		data.mode, data.effect, data.alpha, data.life, FTENT_PERSIST | FTENT_CLIENTCUSTOM );
	if( !sprite ) return;
	sprite->flags &= ~( FTENT_FADEOUT | FTENT_SPRANIMATE | FTENT_SPRCYCLE );
	sprite->callback = RampSpriteUpdate;
	entity_state_t &base = sprite->entity.baseline;
	base.scale = data.scale;
	base.renderamt = data.alpha * 255;
	base.fuser1 = gEngfuncs.GetClientTime();
	base.fuser2 = data.scaleRate;
	base.fuser3 = data.alphaRate;
	base.fuser4 = data.rollSpeed;
	VectorCopy( data.colorRate, base.vuser1 );
	base.angles[2] = data.roll;
	base.frame = data.frame;
	base.rendercolor.r = data.color[0];
	base.rendercolor.g = data.color[1];
	base.rendercolor.b = data.color[2];
	sprite->entity.curstate.framerate = data.framerate;
	RampSpriteUpdate( sprite, 0, base.fuser1 );
}

static int RampSprite( const char *, int size, void *buffer )
{
	SvenUI::Reader reader( buffer, size );
	RampSpriteState data;
	data.model = reader.shortInt();
	data.life = reader.byte() * 0.1f;
	for( int axis = 0; axis < 3; ++axis ) data.origin[axis] = reader.integer() / 8.0f;
	unsigned flags = (unsigned short)reader.shortInt();
	if( flags & 1 ) data.roll = reader.byte() * ( 360.0f / 255 );
	if( flags & 2 ) data.scale = reader.byte() * 0.1f;
	if( flags & 4 ) data.alpha = reader.byte() / 255.0f;
	if( flags & 8 )
	{
		unsigned mode = reader.byte();
		data.mode = ( mode >> 5 ) & 7;
		data.effect = mode & 31;
	}
	for( int channel = 0; channel < 3; ++channel )
		if( flags & ( 16 << channel ) ) data.color[channel] = reader.byte();
	if( flags & 128 ) data.frame = reader.byte();
	if( flags & 256 )
		for( int axis = 0; axis < 3; ++axis ) data.velocity[axis] = reader.integer() / 8.0f;
	if( flags & 512 ) data.rollSpeed = reader.byte() * 6;
	if( flags & 1024 ) data.scaleRate = (int8_t)reader.byte() * ( 20.0f / 255 );
	if( flags & 2048 ) data.alphaRate = (int8_t)reader.byte() * ( 20.0f / 255 );
	for( int channel = 0; channel < 3; ++channel )
		if( flags & ( 4096 << channel ) ) data.colorRate[channel] = (int8_t)reader.byte() * ( 20.0f / 255 );
	if( flags & 32768 ) data.framerate = reader.byte();
	if( !reader.done() ) return 0;
	CreateRampSprite( data );
	return 1;
}

static int ShieldRic( const char *, int size, void *buffer )
{
	if( size != 12 || !buffer ) return 0;
	BEGIN_READ( buffer, size );
	RampSpriteState data;
	ReadPosition( data.origin );
	data.model = EffectModel( "sprites/muz4.spr" );
	data.life = data.scale = 0.3f;
	data.mode = kRenderGlow;
	data.effect = kRenderFxNoDissipation;
	data.color[0] = data.color[1] = 100;
	data.scaleRate = 4;
	data.alphaRate = -10.0f / 3;
	CreateRampSprite( data );
	return 1;
}

static int GargSplash( const char *, int size, void *buffer )
{
	if(( size != 12 && size != 24 ) || !buffer ) return 0;
	BEGIN_READ( buffer, size );
	vec3_t origin, direction( 0.125f, 0.125f, 0.125f );
	ReadPosition( origin );
	if( size == 24 )
	{
		ReadPosition( direction );
		for( int axis = 0; axis < 3; ++axis ) direction[axis] = fabsf( direction[axis] );
	}
	gEngfuncs.pEfxAPI->R_StreakSplash( origin, direction, 6, 20, 50, 0, 400 );
	return 1;
}

static void ToxicPuffUpdate( TEMPENTITY *puff, float frametime, float time )
{
	entity_state_t &base = puff->entity.baseline;
	float age = time - base.fuser1;
	if( age < 0 ) age = 0;
	float size = 1 + base.vuser1[0] * age;
	puff->entity.curstate.scale = ( size > 256 ? 256 : size ) * base.fuser2;
	float transition = 3400.0f / 1350;
	float alpha = age < transition ? 3500 - 1350 * age : 100 - 85 * ( age - transition );
	if( alpha <= 0 ) { puff->die = time; puff->entity.curstate.renderamt = 0; return; }
	puff->entity.curstate.renderamt = alpha > 255 ? 255 : (int)alpha;
	puff->entity.angles[2] = fmodf( age * base.fuser3, 360 );
	if( frametime < 0 ) frametime = 0;
	if( frametime > 0.1f ) frametime = 0.1f;
	if( age < 0.1f ) base.origin[2] += base.vuser1[2] * frametime;
	if( time >= base.fuser4 )
	{
		for( int axis = 0; axis < 3; ++axis )
			base.origin[axis] += gEngfuncs.pfnRandomFloat( -100, 100 ) * frametime;
		base.fuser4 = time + 0.05f;
	}
}

static void EmitToxicPuff( ToxicCloudState &cloud, int model, float halfWidth )
{
	vec3_t origin = cloud.origin, velocity;
	origin[2] += 12;
	velocity[0] = gEngfuncs.pfnRandomFloat( -64, 64 );
	velocity[1] = gEngfuncs.pfnRandomFloat( -64, 64 );
	velocity[2] = 64;
	TEMPENTITY *puff = gEngfuncs.pEfxAPI->R_TempSprite( origin, velocity, 1 / halfWidth, model,
		kRenderTransAlpha, kRenderFxNone, 1, 5, FTENT_PERSIST | FTENT_CLIENTCUSTOM );
	++cloud.emitted;
	if( !puff ) return;
	puff->flags &= ~( FTENT_FADEOUT | FTENT_SPRANIMATE | FTENT_SPRCYCLE );
	puff->callback = ToxicPuffUpdate;
	puff->entity.curstate.rendercolor.r = puff->entity.curstate.rendercolor.b = cloud.color;
	puff->entity.curstate.rendercolor.g = 0;
	puff->entity.curstate.framerate = 0;
	entity_state_t &base = puff->entity.baseline;
	base.fuser1 = gEngfuncs.GetClientTime();
	base.fuser2 = 1 / halfWidth;
	base.fuser3 = ( cloud.emitted & 1 ) ? -64 : 64;
	base.fuser4 = base.fuser1 + 0.1f;
	base.vuser1[0] = cloud.emitted == 2 || cloud.emitted == 3 ? 256 : 270;
	base.vuser1[2] = cloud.emitted == 1 ? 100 : 200;
}

static int ToxicCloud( const char *, int size, void *buffer )
{
	if( size != 12 || !buffer ) return 0;
	BEGIN_READ( buffer, size );
	vec3_t origin;
	ReadPosition( origin );
	gEngfuncs.pEventAPI->EV_PlaySound( 0, origin, CHAN_AUTO, "doors/aliendoor1.wav", 1, ATTN_NORM, 0, 100 );
	gEngfuncs.pEventAPI->EV_PlaySound( 0, origin, CHAN_AUTO, "ambience/disgusting.wav", 1, ATTN_NORM, 0, 100 );
	int index = EffectModel( "sprites/puff1.spr" );
	model_t *model = index > 0 ? gEngfuncs.pfnGetModelByIndex( index ) : NULL;
	if( !model || model->type != mod_sprite || model->maxs[0] <= 0 || !std::isfinite( model->maxs[0] ) ) return 1;
	float time = gEngfuncs.GetClientTime();
	for( int slot = 0; slot < 16; ++slot )
	{
		ToxicCloudState &cloud = toxicClouds[slot];
		if( cloud.active && cloud.end > time ) continue;
		cloud.active = true;
		VectorCopy( origin, cloud.origin );
		cloud.end = time + 5;
		cloud.nextEmission = time + 0.1f;
		cloud.emitted = 0;
		cloud.color = gEngfuncs.pfnRandomLong( 150, 255 );
		for( int initial = 0; initial < 3; ++initial ) EmitToxicPuff( cloud, index, model->maxs[0] );
		break;
	}
	return 1;
}

static void UpdateToxicClouds()
{
	float time = gEngfuncs.GetClientTime();
	int index = 0;
	model_t *model = NULL;
	for( int slot = 0; slot < 16; ++slot )
	{
		ToxicCloudState &cloud = toxicClouds[slot];
		if( !cloud.active ) continue;
		if( time >= cloud.end || cloud.emitted >= 50 || cloud.nextEmission - time > 0.2f )
		{
			cloud.active = false;
			continue;
		}
		if( time < cloud.nextEmission ) continue;
		if( !index )
		{
			index = EffectModel( "sprites/puff1.spr" );
			model = index > 0 ? gEngfuncs.pfnGetModelByIndex( index ) : NULL;
		}
		if( !model || model->type != mod_sprite || model->maxs[0] <= 0 || !std::isfinite( model->maxs[0] ) ) { cloud.active = false; continue; }
		EmitToxicPuff( cloud, index, model->maxs[0] );
		cloud.nextEmission = time + 0.1f;
	}
}

static cl_entity_t *SporeEntity( int index )
{
	cl_entity_t *entity = gEngfuncs.GetEntityByIndex( index );
	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	if( !entity || !entity->model || !local || entity->curstate.messagenum < local->curstate.messagenum ) return NULL;
	return entity;
}

static int CbElec( const char *, int size, void *buffer )
{
	if( size != 1 || !buffer ) return 0;
	BEGIN_READ( buffer, size );
	int flags = READ_BYTE(), player = flags & 31;
	if( !player ) return 1;
	if( !( flags & 64 ) ) StopElectricCrowbar( player, true );
	else if( !electricCrowbars[player].active )
	{
		electricCrowbars[player].active = true;
		electricCrowbars[player].soundStarted = false;
		electricCrowbars[player].nextSound = gEngfuncs.GetClientTime() + 1;
		electricCrowbars[player].light = NULL;
	}
	return 1;
}

static void UpdateElectricCrowbars()
{
	for( int player = 1; player < 32; ++player )
	{
		ElectricCrowbarState &state = electricCrowbars[player];
		if( !state.active ) continue;
		cl_entity_t *entity = gEngfuncs.GetEntityByIndex( player );
		if( !entity ) { StopElectricCrowbar( player, false ); continue; }
		float time = gEngfuncs.GetClientTime();
		if( !state.soundStarted )
		{
			gEngfuncs.pEventAPI->EV_PlaySound( player, entity->origin, CHAN_ITEM,
				"weapons/cbe/cbe_on.wav", 1, ATTN_NORM, 0, 100 );
			state.soundStarted = true;
		}
		if( time >= state.nextSound || state.nextSound - time > 2 )
		{
			if( gEngfuncs.pEventAPI->EV_StopSound )
				gEngfuncs.pEventAPI->EV_StopSound( player, CHAN_ITEM, "weapons/cbe/cbe_idle1.wav" );
			gEngfuncs.pEventAPI->EV_PlaySound( player, entity->origin, CHAN_ITEM,
				"weapons/cbe/cbe_idle1.wav", 0.7f, ATTN_NORM, 0, 100 );
			state.nextSound = time + 2;
		}
		entity->curstate.renderfx = kRenderFxGlowShell;
		entity->curstate.rendercolor.r = 80;
		entity->curstate.rendercolor.g = 100;
		entity->curstate.rendercolor.b = 220;
		cl_entity_t *local = gEngfuncs.GetLocalPlayer();
		cl_entity_t *view = gEngfuncs.GetViewModel();
		if( local && local->index == player && view )
		{
			view->curstate.effects |= EF_DIMLIGHT;
			view->curstate.renderfx = kRenderFxGlowShell;
			view->curstate.renderamt = 0;
			view->curstate.rendercolor.r = 50;
			view->curstate.rendercolor.g = 75;
			view->curstate.rendercolor.b = 220;
		}
		dlight_t *light = gEngfuncs.pEfxAPI->CL_AllocDlight( 0x43420000 | player );
		state.light = light;
		if( light )
		{
			VectorCopy( entity->origin, light->origin );
			light->radius = 128;
			light->color.r = 80; light->color.g = 100; light->color.b = 220;
			light->decay = light->minlight = 0;
			light->die = time + 0.1f;
		}
	}
}

static void UpdateShockPrimed()
{
	for( int player = 1; player < 32; ++player )
	{
		if( !shockPrimed[player].active ) continue;
		if( !gEngfuncs.GetEntityByIndex( player ) )
		{
			shockPrimed[player].active = false;
			if( shockViewPlayer == player ) StopShockGlow();
			continue;
		}
		cl_entity_t *view = gEngfuncs.GetViewModel();
		if( !view ) continue;
		float fraction = ( gEngfuncs.GetClientTime() - shockPrimed[player].start ) / 5.0f;
		if( fraction < 0 ) fraction = 0;
		if( fraction > 1 ) fraction = 1;
		view->curstate.renderfx = kRenderFxGlowShell;
		view->curstate.renderamt = (int)( fraction * 20 );
		view->curstate.rendercolor.r = (byte)( fraction * 50 );
		view->curstate.rendercolor.g = (byte)( fraction * 50 );
		view->curstate.rendercolor.b = (byte)( fraction * 220 );
		shockViewModel = view;
		shockViewModelModel = view->model;
		shockViewPlayer = player;
	}
}

static int SRPrimed( const char *, int size, void *buffer )
{
	if( size != 5 || !buffer ) return 0;
	BEGIN_READ( buffer, size );
	int player = READ_BYTE() & 31;
	float start = READ_FLOAT();
	if( !std::isfinite( start ) ) return 0;
	if( !player ) return 1;
	shockPrimed[player].active = true;
	shockPrimed[player].start = start;
	return 1;
}

static int SRPrimedOff( const char *, int size, void *buffer )
{
	if( size != 1 || !buffer ) return 0;
	BEGIN_READ( buffer, size );
	int player = READ_BYTE() & 31;
	shockPrimed[player].active = false;
	if( shockViewPlayer == player ) StopShockGlow();
	return 1;
}

static int SRDetonate( const char *, int size, void *buffer )
{
	if( size != 13 || !buffer ) return 0;
	BEGIN_READ( buffer, size );
	vec3_t origin, velocity( 0, 0, 0 );
	ReadPosition( origin );
	int charge = READ_BYTE();
	int count = 3 + charge / 20;
	float radius = 32 + charge * 2;
	gEngfuncs.pEventAPI->EV_PlaySound( 0, origin, CHAN_AUTO,
		"weapons/shock_discharge2.wav", 1, ATTN_NORM, 0, 100 );
	dlight_t *light = gEngfuncs.pEfxAPI->CL_AllocDlight( 0 );
	if( light )
	{
		VectorCopy( origin, light->origin );
		light->color.r = 85;
		light->color.g = 200;
		light->color.b = 255;
		light->radius = radius;
		light->decay = 500;
		light->die = gEngfuncs.GetClientTime() + 1;
	}
	int beamModel = EffectModel( "sprites/lgtning.spr" );
	if( beamModel )
	{
		for( int beam = 0; beam < count; ++beam )
		{
			float amplitude = gEngfuncs.pfnRandomFloat( 1, 1 + charge / 10.0f );
			vec3_t end;
			for( int axis = 0; axis < 3; ++axis )
				end[axis] = origin[axis] + gEngfuncs.pfnRandomFloat( -radius, radius ) * ( axis == 2 ? 0.5f : 1.0f );
			gEngfuncs.pEfxAPI->R_BeamPoints( origin, end, beamModel, 0.25f,
				radius / 200 + 0.5f, amplitude, 1, 0, 0, 0, 96.0f / 255, 180.0f / 255, 1 );
		}
	}
	int flare = EffectModel( "sprites/blueflare1.spr" );
	if( flare ) gEngfuncs.pEfxAPI->R_TempSprite( origin, velocity, radius / 40, flare,
		kRenderGlow, kRenderFxNoDissipation, 0.75f, 0.15f, 0 );
	gEngfuncs.pEfxAPI->R_StreakSplash( origin, velocity, 72, count * 40, radius * 4, -400, 400 );
	return 1;
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
	UpdateElectricCrowbars();
	UpdateToxicClouds();
	UpdateShockPrimed();
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
	gEngfuncs.pfnHookUserMsg( "SRPrimed", SRPrimed );
	gEngfuncs.pfnHookUserMsg( "SRPrimedOff", SRPrimedOff );
	gEngfuncs.pfnHookUserMsg( "SRDetonate", SRDetonate );
	gEngfuncs.pfnHookUserMsg( "RampSprite", RampSprite );
	gEngfuncs.pfnHookUserMsg( "ShieldRic", ShieldRic );
	gEngfuncs.pfnHookUserMsg( "GargSplash", GargSplash );
	gEngfuncs.pfnHookUserMsg( "CbElec", CbElec );
	gEngfuncs.pfnHookUserMsg( "ToxicCloud", ToxicCloud );
	if( !gEngfuncs.pfnGetCvarPointer( "cl_gibcount" ) )
		gEngfuncs.pfnRegisterVariable( "cl_gibcount", "6", FCVAR_ARCHIVE );
}
