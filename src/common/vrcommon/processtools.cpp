#include "processtools.h"

#include "vrlog.h"

#ifdef _WIN32
#include <Windows.h>
#include <string>

bool BCreateProcess( const char *pchWorkingDir, const char **argv )
{
	char bufParam[ 32768 ];
	int i=0;
	while( argv[ i ] )
	{
		if( i > 0 )
			strcat_s( bufParam, " " );
		strcat_s( bufParam, argv[i] );

		i++;
	}

	STARTUPINFO	sInfoProcess = {0};
	sInfoProcess.cb = sizeof( STARTUPINFO );
	sInfoProcess.dwFlags =  STARTF_USESHOWWINDOW;
	sInfoProcess.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pInfoOverlay;

	if( CreateProcess( argv[0], bufParam,
		NULL, NULL, FALSE, NULL, NULL, pchWorkingDir, &sInfoProcess, &pInfoOverlay ) )
	{
		// we aren't going to use these handles, so close them now
		CloseHandle( pInfoOverlay.hProcess );
		CloseHandle( pInfoOverlay.hThread );
		return true;
	}
	else
	{
		return false;
	}
}

#else

#include <unistd.h>
#include <stdio.h>
#include <errno.h>

bool BCreateProcess( const char *pchWorkingDir, const char **argv )
{
	pid_t pid = fork();
	if ( pid == 0 )
	{
		chdir( pchWorkingDir );
		execvp( argv[0], (char* const *)argv );
		fprintf( stderr, "Failed to execute process '%s': %d\n", argv[0], errno );
		_exit( 1 );
	}
	else if (pid < 0)            // failed to fork
	{
		Log( "Unable to to fork to start process %s\n", argv[0] );
		return false;
	}
	else
	{
		return true;
	}
}

#endif


