// sven_snd.cpp — Sven soundcache table + StartSound resolver for cl_dll.
//
// Loads maps/soundcache/<map>.txt with the stock rule (every non-empty
// trimmed SOUNDLIST line is a slot, in file order) and resolves Sven
// StartSound indices to filenames. Playback ownership is shared with the
// engine via cl_sven_sndhandler: 0 (default) = engine plays, cl_dll only
// logs; 1 = cl_dll plays, engine skips. The "test_sven_sound <idx> [sent]"
// client command plays a fabricated index for ground-truth testing.
#include "cl_dll.h"
#include "parsemsg.h"
#include "sven_snd.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SVEN_SND_MAX	4096
#define SVEN_SENT_MAX	2048

static char svenSndTable[SVEN_SND_MAX][64];
static char svenSndSent[SVEN_SENT_MAX][64];
static char svenSndMap[64];

static void SvenSnd_TrimLine( char *line )
{
	char *s = line, *t;

	while( *s == ' ' || *s == '\t' ) s++;
	if( s != line )
		memmove( line, s, strlen( s ) + 1 );
	t = line + strlen( line );
	while( t > line && ( t[-1] == '\r' || t[-1] == ' ' || t[-1] == '\t' )) *--t = '\0';
}

static void SvenSnd_Load( void )
{
	const char *level = gEngfuncs.pfnGetLevelName();
	char map[64], path[128], *base, *dot;
	char *buf, *p, *end;
	int len = 0, n = 0, ns = 0, inlist = 0;

	if( !level )
		return;
	strncpy( map, level, sizeof( map ) - 1 );
	map[sizeof( map ) - 1] = '\0';
	base = strrchr( map, '/' );
	base = base ? base + 1 : map;
	dot = strrchr( base, '.' );
	if( dot ) *dot = '\0';
	if( !base[0] || !strcmp( svenSndMap, base ))
		return; // already loaded for this map
	strncpy( svenSndMap, base, sizeof( svenSndMap ) - 1 );

	memset( svenSndTable, 0, sizeof( svenSndTable ));
	memset( svenSndSent, 0, sizeof( svenSndSent ));
	sprintf( path, "maps/soundcache/%s.txt", base );
	buf = (char *)gEngfuncs.COM_LoadFile( path, 5, &len );
	if( !buf || len <= 0 )
		return;
	end = buf + len;
	for( p = buf; p < end && ( n < SVEN_SND_MAX || ns < SVEN_SENT_MAX ); )
	{
		char *eol = p, line[256];
		int ll;

		while( eol < end && *eol != '\n' ) eol++;
		ll = (int)( eol - p );
		if( ll > 255 ) ll = 255;
		memcpy( line, p, ll );
		line[ll] = '\0';
		p = ( eol < end ) ? eol + 1 : end;
		SvenSnd_TrimLine( line );
		if( !strcmp( line, "SOUNDLIST {" )) { inlist = 1; continue; }
		if( !strcmp( line, "SENTENCELIST {" )) { inlist = 2; continue; }
		if( !strcmp( line, "}" ) || !strcmp( line, "CUSTOMMATERIALS {" ))
		{
			if( inlist == 2 )
				break;
			inlist = 0;
			continue;
		}
		if( !line[0] )
			continue; // empty line: no slot (stock rule)
		if( inlist == 1 && n < SVEN_SND_MAX )
		{
			strncpy( svenSndTable[n++], line, sizeof( svenSndTable[0] ) - 1 );
		}
		else if( inlist == 2 && ns < SVEN_SENT_MAX )
		{
			strncpy( svenSndSent[ns++], line, sizeof( svenSndSent[0] ) - 1 );
		}
	}
	gEngfuncs.COM_FreeFile( buf );
	gEngfuncs.Con_Printf( "SVEN-CLDLL: %d sounds + %d sentences for %s\n", n, ns, base );
}

static const char *SvenSnd_Resolve( int idx, int sentence, char *sentbuf )
{
	SvenSnd_Load();
	if( sentence )
	{
		if( idx < 0 || idx >= SVEN_SENT_MAX || !svenSndSent[idx][0] )
			return NULL;
		sprintf( sentbuf, "!%s", svenSndSent[idx] );
		return sentbuf;
	}
	if( idx < 0 || idx >= SVEN_SND_MAX || !svenSndTable[idx][0] )
		return NULL;
	return svenSndTable[idx];
}

static void SvenSnd_PlayResolved( const char *name, float volume, int hasOrigin, const float *origin )
{
	if( !name || !name[0] )
		return;
	if( hasOrigin )
		gEngfuncs.pfnPlaySoundByNameAtLocation( name, volume, origin );
	else gEngfuncs.pfnPlaySoundByName( name, volume );
}

// Real StartSound handler (replaces the read-and-discard stub). Wire layout
// is the verified Sven game-usermsg one: [SHORT flags][SHORT sndnum if
// 0x10][BYTE vol][BYTE pitch][BYTE attn][3xCOORD origin][FLOAT extra]
// [BYTE channel][SHORT ent]. Plays only in cl_dll ownership mode.
void SvenSnd_OnStartSound( void *pbuf, int iSize )
{
	int flags, sndnum = -1;
	float volume = 1.0f, attn = 1.0f;
	int pitch = 100, channel, ent, hasOrigin = 0;
	float origin[3] = { 0, 0, 0 };
	const char *name;
	char sentbuf[72];

	BEGIN_READ( pbuf, iSize );
	flags = READ_SHORT();
	if( flags & 0x10 )
		sndnum = READ_SHORT();
	if( flags & 0x1 )
		volume = READ_BYTE() / 255.0f;
	if( flags & 0x2 )
		pitch = READ_BYTE();
	if( flags & 0x4 )
	{
		int ab = READ_BYTE();
		attn = ( flags & 0x1000 ) ? (float)ab : ab * ( 1.0f / 64.0f );
	}
	if( flags & 0x8 )
	{
		origin[0] = READ_COORD();
		origin[1] = READ_COORD();
		origin[2] = READ_COORD();
		hasOrigin = 1;
	}
	if( flags & 0x8000 )
		READ_FLOAT();
	channel = READ_BYTE();
	ent = READ_SHORT();
	(void)attn;
	(void)pitch;

	name = SvenSnd_Resolve( sndnum, ( flags & 0x100 ) != 0, sentbuf );
	gEngfuncs.Con_Printf( "SVEN-CLDLL: flags=%04x idx=%d ch=%d ent=%d [%s] %s\n",
		flags, sndnum, channel, ent, hasOrigin ? "org" : "noloc", name ? name : "(no row)" );
	if( gEngfuncs.pfnGetCvarFloat( "cl_sven_sndhandler" ) == 0.0f )
		return; // engine owns playback (default)
	SvenSnd_PlayResolved( name, volume, hasOrigin, origin );
}

// test_sven_sound <idx> [sent] — ground-truth client test: resolve a
// fabricated index through the loaded table and play it unconditionally.
void SvenSnd_Test_f( void )
{
	int idx = 0, sent = 0;
	const char *name;
	char sentbuf[72];

	if( gEngfuncs.Cmd_Argc() < 2 )
	{
		gEngfuncs.Con_Printf( "usage: test_sven_sound <idx> [sent]\n" );
		return;
	}
	idx = atoi( gEngfuncs.Cmd_Argv( 1 ));
	if( gEngfuncs.Cmd_Argc() > 2 )
		sent = atoi( gEngfuncs.Cmd_Argv( 2 ));
	name = SvenSnd_Resolve( idx, sent, sentbuf );
	gEngfuncs.Con_Printf( "SVEN-CLDLL-TEST: idx=%d sent=%d -> %s\n", idx, sent, name ? name : "(no row)" );
	SvenSnd_PlayResolved( name, 1.0f, 0, NULL );
}
