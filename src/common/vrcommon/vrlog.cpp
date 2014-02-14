//========= Copyright Valve Corporation ============//

#include "vrlog.h"
#include "pathtools.h"
#include "dirtools.h"
#include "threadtools.h"

#include <time.h>
#include <stdio.h>
#include <stdarg.h>

#if defined(_WIN32)
#include <Windows.h>
#endif

static FILE * s_pLogFile = NULL;

// Logging mutex
static CThreadMutex *g_pLoggingMutex = NULL;

bool InitLog( const char *pchLogDir, const char *pchLogFilePrefix )
{
	if( s_pLogFile )
		return false;

	// setup mutex for logging
	g_pLoggingMutex = new CThreadMutex();

	if( !BCreateDirectoryRecursive( pchLogDir ) )
		return false;

	std::string sExeFilename = Path_StripExtension( Path_StripDirectory( Path_GetExecutablePath() ) );
	std::string sLogFilename = Path_Join( pchLogDir, std::string( pchLogFilePrefix ) + "_" + sExeFilename + ".txt" );

	s_pLogFile = fopen( sLogFilename.c_str(), "a" );
	return s_pLogFile != NULL;
}

void CleanupLog()
{
	delete g_pLoggingMutex;
	g_pLoggingMutex = NULL;

	if( s_pLogFile )
	{
		fclose( s_pLogFile );
		s_pLogFile = NULL;
	}
}

#if defined( POSIX )
#define gmtime_s(a, b) gmtime_r(b, a)
#endif

void Log( const char *pMsgFormat, ... )
{
	va_list args;
	va_start( args, pMsgFormat );

	if( !g_pLoggingMutex )
		return;

	g_pLoggingMutex->Lock();

	if( s_pLogFile != NULL )
	{
		time_t t;
		char rgchTime[128];
		t = time(NULL);
		struct tm tmStruct;
		gmtime_s( &tmStruct, &t );
		strftime( rgchTime, sizeof( rgchTime ), "%a %b %d %H:%M:%S %Y UTC", &tmStruct );
		fprintf( s_pLogFile, "%s - ", rgchTime );
		vfprintf( s_pLogFile, pMsgFormat, args );
		fflush( s_pLogFile );

#if defined( _WIN32 )
		if( Plat_IsInDebugSession() )
		{
			// on Windows, show the first 1024 characters in the debugger too
			char buf[1024];
			vsprintf_s( buf, pMsgFormat, args );
			OutputDebugString( buf );
		}
#endif
	}

	g_pLoggingMutex->Unlock();

	va_end(args);
}

