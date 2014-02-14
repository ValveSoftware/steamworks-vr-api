#include "hmdplatform_private.h"

#if defined(_WIN32)
#include <Windows.h>
#elif defined(POSIX)
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

bool Plat_IsInDebugSession()
{
#ifdef _WIN32
	return (IsDebuggerPresent() != 0);
#elif defined(OSX)
	int mib[4];
	struct kinfo_proc info;
	size_t size;
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PID;
	mib[3] = getpid();
	size = sizeof(info);
	info.kp_proc.p_flag = 0;
	sysctl(mib,4,&info,&size,NULL,0);
	return ((info.kp_proc.p_flag & P_TRACED) == P_TRACED);
#elif defined(LINUX)
	static FILE *fp;
	if ( !fp )
	{
		char rgchProcStatusFile[256]; rgchProcStatusFile[0] = '\0';
		snprintf( rgchProcStatusFile, sizeof(rgchProcStatusFile), "/proc/%d/status", getpid() );
		fp = fopen( rgchProcStatusFile, "r" );
	}

	char rgchLine[256]; rgchLine[0] = '\0';
	int nTracePid = 0;
	if ( fp )
	{
		const char *pszSearchString = "TracerPid:";
		const uint cchSearchString = strlen( pszSearchString );
		rewind( fp );
		while ( fgets( rgchLine, sizeof(rgchLine), fp ) )
		{
			if ( !strncasecmp( pszSearchString, rgchLine, cchSearchString ) )
			{
				char *pszVal = rgchLine+cchSearchString+1;
				nTracePid = atoi( pszVal );
				break;
			}
		}
	}
	return (nTracePid != 0);
#elif defined( _PS3 )
#ifdef _CERT
	return false;
#else
	return snIsDebuggerPresent();
#endif
#endif
}


bool IsPosix()
{
#if defined( _WIN32 )
	return false;
#elif defined( OSX ) || defined( LINUX )
	return true;
#endif
};

