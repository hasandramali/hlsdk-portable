// sv_sndlog.cpp — Sven-style sound-table visibility for local servers.
//
// Records every PRECACHE_SOUND call (name -> call-order index, first-free
// slot, duplicate reuse: the same numbering rule Sven's CSoundEngine uses
// for svc107 sndnum), optionally logs every EMIT_SOUND, dumps
// maps/soundcache/<map>.txt in recorded order at ServerActivate, and
// provides the "sv_dumpsoundcache" server command.
//
// Diagnostic only: both wrappers forward to the original engine function
// unchanged, so gameplay behavior is identical with logging off.
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "enginecallback.h"
#include "sv_sndlog.h"

#define SV_SNDLOG_MAX	4096

static char svSndNames[SV_SNDLOG_MAX][64];
static int svSndCount = 0;
static cvar_t sv_sndlog = { "sv_sndlog", "0", 0 };

static int (*sOrigPrecacheSound)( const char *s ) = NULL;
static void (*sOrigEmitSound)( edict_t *entity, int channel, const char *sample, float volume, float attenuation, int fFlags, int pitch ) = NULL;

static int SV_SndLog_PrecacheSound( const char *s )
{
	int i, ret;

	if( s && s[0] )
	{
		for( i = 0; i < svSndCount; i++ )
		{
			if( !strcmp( svSndNames[i], s ))
				break;
		}
		if( i == svSndCount && svSndCount < SV_SNDLOG_MAX )
		{
			strncpy( svSndNames[svSndCount], s, sizeof( svSndNames[0] ) - 1 );
			svSndNames[svSndCount][sizeof( svSndNames[0] ) - 1] = '\0';
			svSndCount++;
		}
		ret = sOrigPrecacheSound( s );
		// Log both our call-order index and the engine's returned index:
		// equal values prove the table matches engine precache numbering.
		if( sv_sndlog.value >= 1.0f )
			ALERT( at_console, "SV-SNDLOG: precache #%d (engine #%d) %s\n", i, ret, s );
		return ret;
	}
	return sOrigPrecacheSound( s );
}

static void SV_SndLog_EmitSound( edict_t *entity, int channel, const char *sample, float volume, float attenuation, int fFlags, int pitch )
{
	if( sv_sndlog.value >= 1.0f )
	{
		ALERT( at_console, "SV-SNDLOG: emit ent=%d ch=%d vol=%.2f attn=%.2f flags=%04x pitch=%d %s\n",
			entity ? ENTINDEX( entity ) : -1, channel, volume, attenuation,
			fFlags, pitch, sample ? sample : "(null)" );
	}
	sOrigEmitSound( entity, channel, sample, volume, attenuation, fFlags, pitch );
}

static void SV_SndLog_WriteFile( void )
{
	char gamedir[256], path[320], map[64];
	char *dot;
	FILE *f;
	int i;

	if( !gpGlobals )
		return;
	(*g_engfuncs.pfnGetGameDir)( gamedir );
	strncpy( map, STRING( gpGlobals->mapname ), sizeof( map ) - 1 );
	map[sizeof( map ) - 1] = '\0';
	dot = strrchr( map, '.' );
	if( dot ) *dot = '\0';
	if( !map[0] )
		return;
	sprintf( path, "%s/maps/soundcache/local_%s.txt", gamedir, map );
	f = fopen( path, "w" );
	if( !f )
	{
		ALERT( at_console, "SV-SNDLOG: cannot write %s (create maps/soundcache/ first)\n", path );
		return;
	}
	// NOTE: local_ prefix on purpose — never clobber the served
	// maps/soundcache/<map>.txt. The client reads this file only when
	// cl_sven_soundcache_source=1 (precache-hack source mode).
	fprintf( f, "%s\nlocal\nSOUNDLIST {\n", map );
	for( i = 0; i < svSndCount; i++ )
		fprintf( f, "%s\n", svSndNames[i] );
	fprintf( f, "}\nSENTENCELIST {\n}\nCUSTOMMATERIALS {\n}\n" );
	fclose( f );
	ALERT( at_console, "SV-SNDLOG: wrote %s (%d sounds)\n", path, svSndCount );
}

static void SV_DumpSoundCache_f( void )
{
	int i;

	ALERT( at_console, "SV-SNDLOG: %d sounds recorded:\n", svSndCount );
	for( i = 0; i < svSndCount; i++ )
		ALERT( at_console, "  [%d] %s\n", i, svSndNames[i] );
	SV_SndLog_WriteFile();
}

void SV_SndLog_Install( void )
{
	sOrigPrecacheSound = g_engfuncs.pfnPrecacheSound;
	sOrigEmitSound = g_engfuncs.pfnEmitSound;
	g_engfuncs.pfnPrecacheSound = SV_SndLog_PrecacheSound;
	g_engfuncs.pfnEmitSound = SV_SndLog_EmitSound;
	CVAR_REGISTER( &sv_sndlog );
	(*g_engfuncs.pfnAddServerCommand)( "sv_dumpsoundcache", SV_DumpSoundCache_f );
}

// Called at the end of ServerActivate: entity precaches are complete.
// NOTE: never writes the file automatically — an automatic dump would
// clobber a good served maps/soundcache/<map>.txt with a partial local
// table. Dumping happens only via the sv_dumpsoundcache command.
void SV_SndLog_OnActivate( void )
{
	ALERT( at_console, "SV-SNDLOG: map active, %d sounds recorded (sv_dumpsoundcache to write file)\n", svSndCount );
}

// Called from ServerDeactivate: fresh table for the next map.
void SV_SndLog_OnDeactivate( void )
{
	svSndCount = 0;
	memset( svSndNames, 0, sizeof( svSndNames ));
}
