//========== Copyright Valve Corporation, All rights reserved. ========
//
// Purpose:
//
//=============================================================================

#include "hmdplatform_private.h"

#if defined( _WIN32 ) && !defined( _X360 )
#include <windows.h>
#include <shellapi.h>
#endif

#include <stdio.h>
#include <ctype.h>

#define V_ARRAYSIZE(a) sizeof(a)/sizeof(a[0]) 


#ifdef _WIN32
	#include <process.h>
    #include <tlhelp32.h>
#elif defined( _PS3 )
	#include <sched.h>
	#include <unistd.h>
	#include <exception>
	#include <errno.h>
	#include <pthread.h>
	#include <sys/time.h>
	#include <sys/timer.h>
	#include "ps3/ps3_posixextensions.h"
	#define GetLastError() errno
	typedef void *LPVOID;

	#include "ps3/ps3_win32stubs.h"
	bool gbCheckNotMultithreaded = true;

	#define DEBUG_ERROR(XX) Assert(0)
	#define MAXIMUM_WAIT_OBJECTS 64
#elif defined(POSIX)

#ifdef LINUX
// We want to use the 64-bit file I/O APIs.  We aren't using
// _FILE_OFFSET_BITS=64 as we want to make our usage explicit and
// so avoid issues such as where a struct uses off_t and is shared elsewhere,
// but that code doesn't set 64-bit file offsets so there's a mismatch.
// By using off64_t the other code won't compile unless it does something
// consistent with this code.
#define _LARGEFILE64_SOURCE 1
#endif

	#include <unistd.h>
	#include <sys/stat.h>
    #include <dlfcn.h>
	#include <sys/wait.h>

	#if !defined(OSX)
        #if defined(LINUX)
        #include <syscall.h>
        #include <dirent.h>
        #endif
		#include <sys/fcntl.h>
    	#define sem_unlink( arg )
	#else
		#define pthread_yield pthread_yield_np
        #include <mach/mach.h>
	#endif // !OSX
	// signal handler for SIGCHLD
	void ReapChildProcesses( int sig )
	{
        // Wait for all dead processes.
        // We use a non-blocking call to be sure this signal handler will not
        // block if a child was cleaned up in another part of the program.
        while (waitpid(-1, NULL, WNOHANG) > 0)
        {
            // Loop until all dead processes are reaped.
        }
	};

	typedef int (*PTHREAD_START_ROUTINE)(
		void *lpThreadParameter
		);
	typedef PTHREAD_START_ROUTINE LPTHREAD_START_ROUTINE;
	#include <exception>
	#include <errno.h>
	#include <signal.h>
	#include <pthread.h>
	#ifdef USE_BSD_SEMAPHORES
	#include <semaphore.h>
	#ifndef SEM_NAME_LEN
	#define SEM_NAME_LEN 32
	#endif
	#else /* SYSV semaphores */
	#include <sys/types.h>
	#include <sys/ipc.h>
	#include <sys/sem.h>
	// note this is an include of a cpp file - it defines a single fn, crc32()
	// used to generate semaphore keys based on names
	#include "crc32.cpp"
	#ifndef SEM_A
	#define SEM_A 0200
	#endif
	#ifndef SEM_R
	#define SEM_R 0400
	#endif
	#define SEM_NAME_LEN MAX_PATH
	#define SEM_FAILED -1
	#ifdef LINUX
	// from TFM (and what's up with them not defining this in their headers?)
	union semun 
	{
		int              val;    /* Value for SETVAL */
		struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
		unsigned short  *array;  /* Array for GETALL, SETALL */
		struct seminfo  *__buf;  /* Buffer for IPC_INFO
									(Linux-specific) */
	};
	#endif
	#endif

	#include <sys/time.h>
	#define GetLastError() errno
	typedef void *LPVOID;
	#undef max
	#undef min
	#define MAXIMUM_WAIT_OBJECTS 64

#endif
#include <memory>

#include "threadtools.h"

#ifdef _PS3
	#include "tls_ps3.h"
#endif


#define THREADS_DEBUG 1

// Need to ensure initialized before other clients call in for main thread ID
#ifdef _WIN32
#pragma warning(disable:4073)
#pragma init_seg(lib)
#endif

#ifdef _WIN32
ASSERT_INVARIANT(TT_SIZEOF_CRITICALSECTION == sizeof(CRITICAL_SECTION));
ASSERT_INVARIANT(TT_INFINITE == INFINITE);
#endif

#if defined( _PS3 )
//-----------------------------------------------------------------------------
// Purpose: Linked list implementation
//-----------------------------------------------------------------------------
CThreadEventWaitObject* LLinkNode(CThreadEventWaitObject* list, CThreadEventWaitObject *node)
{
	node->m_pNext = list->m_pNext;
	if (node->m_pNext)
		node->m_pNext->m_pPrev = node;

	list->m_pNext = node;
	node->m_pPrev = list;

	return node;
}

CThreadEventWaitObject* LLUnlinkNode(CThreadEventWaitObject *node)
{
	node->m_pPrev->m_pNext = node->m_pNext;
	if (node->m_pNext) 
		node->m_pNext->m_pPrev = node->m_pPrev;

	node->m_pNext = node->m_pPrev = NULL;

	return node;
}
#endif


//-----------------------------------------------------------------------------
// Simple thread functions. 
// Because _beginthreadex uses stdcall, we need to convert to cdecl
//-----------------------------------------------------------------------------
struct ThreadProcInfo_t
{
	ThreadProcInfo_t( ThreadFunc_t pfnThread, void *pParam, ThreadId_t *pSaveId )
	  : m_pfnThread( pfnThread),
		m_pParam( pParam ),
        m_pSaveId( pSaveId )
	{
	}
	
	ThreadFunc_t m_pfnThread;
	void *		 m_pParam;
    ThreadId_t  *m_pSaveId;
};

//---------------------------------------------------------

#ifdef _WIN32
static unsigned __stdcall ThreadProcConvert( void *pParam )
#elif defined(POSIX)
static void *ThreadProcConvert( void *pParam )
#else
#Log
#endif
{
	ThreadProcInfo_t info = *((ThreadProcInfo_t *)pParam);
	delete ((ThreadProcInfo_t *)pParam);
#ifdef _WIN32
	return (*info.m_pfnThread)(info.m_pParam);
#elif defined(POSIX)
    if ( info.m_pSaveId )
    {
        // OSX doesn't need this writeback.
#if defined(LINUX)
        *info.m_pSaveId = syscall( SYS_gettid );
#elif defined(_PS3)
        if ( sys_ppu_thread_get_id( info.m_pSaveId ) != CELL_OK )
            AssertMsg( false, "sys_ppu_thread_get_id failed" );
#endif
    }
	return (void *)(uintptr_t)(*info.m_pfnThread)(info.m_pParam);
#else
#Log
#endif
}


//---------------------------------------------------------

ThreadHandle_t CreateSimpleThread( ThreadFunc_t pfnThread, void *pParam, ThreadId_t *pID, unsigned stackSize )
{
#ifdef _WIN32
	ThreadId_t idIgnored;
	if ( !pID )
		pID = &idIgnored;
	COMPILE_TIME_ASSERT( sizeof( ThreadId_t ) == sizeof( DWORD ) );
	return CreateThread(NULL, stackSize, (LPTHREAD_START_ROUTINE)ThreadProcConvert, new ThreadProcInfo_t( pfnThread, pParam, pID ), 0, (LPDWORD)pID);
#elif defined(POSIX)
    if ( pID )
    {
        *pID = 0;
    }
	pthread_t pthr;
	pthread_create( &pthr, NULL, ThreadProcConvert, new ThreadProcInfo_t( pfnThread, pParam, pID ) );
	if ( pID )
    {
#if defined(OSX)
        *pID = pthread_mach_thread_np( pthr );
#else
        // We can't get the Linux TID from the pthread_t so
        // we have to wait for the thread to write its thread ID.
        int nTimeout = 100000;
        while ( nTimeout-- > 0 &&
                *pID == 0 )
        {
            usleep( 100 );
        }
        if ( nTimeout <= 0 )
        {
            AssertMsg( false, "Probably deadlock or failure waiting for thread to initialize." );
            return 0;
        }
#endif
    }
	return pthr;
#endif
}

bool ReleaseThreadHandle( ThreadHandle_t hThread )
{
#ifdef _WIN32
	return CloseHandle( hThread ) != 0;
#else
	return true;
#endif
}

//-----------------------------------------------------------------------------
//
// Wrappers for other simple threading operations
//
//-----------------------------------------------------------------------------

void ThreadYield()
{
#if defined(_WIN32)
	::Sleep(0);
#elif defined(_PS3)
	sys_timer_usleep( 60 );  
#elif defined(POSIX)
	sched_yield();
#endif
}

//-----------------------------------------------------------------------------

void ThreadSleep( unsigned long nMilliseconds )
{
	if ( nMilliseconds == 0 )
	{
		ThreadYield();
		return;
	}

#if defined(_WIN32)
	::Sleep( nMilliseconds );
#elif defined(_PS3)
	sys_timer_usleep( nMilliseconds * 1000 );
#elif defined(POSIX)
   usleep( nMilliseconds * 1000 ); 
#endif
}

//-----------------------------------------------------------------------------

#ifndef ThreadGetCurrentId
ThreadId_t ThreadGetCurrentId()
{
#if defined(_WIN32)
	return GetCurrentThreadId();
#elif defined(_PS3)
	sys_ppu_thread_t threadid;
	if ( sys_ppu_thread_get_id( &threadid ) != CELL_OK )
		AssertMsg( false, "sys_ppu_thread_get_id failed" );
	return threadid;
#elif defined(OSX)
    return mach_thread_self(); // mach_port_t is always 32 bits, so now we're ok on 64 bit osx...
#elif defined(LINUX)
    // Use a syscall to get the raw Linux thread ID, which is small.
    return syscall( SYS_gettid );
#else
#Log Unknown platform
#endif
}
#endif

//-----------------------------------------------------------------------------

ThreadHandle_t ThreadGetCurrentHandle()
{
#if defined(_WIN32)
	return GetCurrentThread();
#elif defined(POSIX)
	return pthread_self();
#endif
}

//-----------------------------------------------------------------------------

ThreadRunningRef_t ThreadGetCurrentRunningRef()
{
#if defined(_WIN32)
    return GetCurrentThreadId();
#elif defined(_PS3)
	sys_ppu_thread_t threadid;
	if ( sys_ppu_thread_get_id( &threadid ) != CELL_OK )
		AssertMsg( false, "sys_ppu_thread_get_id failed" );
	return threadid;
#elif defined(LINUX)
    // We use the thread ID as the reference because a pthread_t
    // can become invalid after a join/detach and further use of it can
    // cause segfaults.
    return syscall( SYS_gettid );
#elif defined(POSIX)
    return pthread_self();
#endif
}

//-----------------------------------------------------------------------------

// On PS3, this will return true for zombie threads
bool ThreadIsThreadRunning( ThreadRunningRef_t ThreadRef )
{
#if defined(_WIN32)
	bool bRunning = true;
	HANDLE hThread = ::OpenThread( THREAD_QUERY_INFORMATION, false, ThreadRef );
	if ( hThread )
	{
		DWORD dwExitCode;
		if( !::GetExitCodeThread( hThread, &dwExitCode ) || dwExitCode != STILL_ACTIVE )
			bRunning = false;

		CloseHandle( hThread );
	}
	else
	{
		bRunning = false;
	}
	return bRunning;
#elif defined( _PS3 )
	// will return CELL_OK for zombie threads
	int priority;
	return sys_ppu_thread_get_priority( ThreadRef, &priority ) == CELL_OK;
#elif defined(LINUX)
    return syscall( SYS_tgkill, getpid(), ThreadRef, 0 ) == 0;
#elif defined(POSIX)
    return pthread_kill( ThreadRef, 0 ) == 0;
#endif
}

//-----------------------------------------------------------------------------

int ThreadGetPriority( ThreadHandle_t hThread )
{
#ifdef _WIN32
	if ( !hThread )
	{
		return ::GetThreadPriority( GetCurrentThread() );
	}
	return ::GetThreadPriority( hThread );
#elif defined(POSIX)
	struct sched_param thread_param;
	int policy;
	pthread_getschedparam( hThread, &policy, &thread_param );
	return thread_param.sched_priority;
#endif
}

//-----------------------------------------------------------------------------

bool ThreadSetPriority( ThreadHandle_t hThread, int priority )
{
	if ( !hThread )
	{
		hThread = ThreadGetCurrentHandle();
	}

#ifdef _WIN32
	return SetThreadPriority(hThread, priority) != 0;
#elif defined(POSIX)
	struct sched_param thread_param; 
	thread_param.sched_priority = priority; 
	pthread_setschedparam( hThread, SCHED_RR, &thread_param );
	return true;
#endif
}

//-----------------------------------------------------------------------------

bool ThreadTerminate( ThreadHandle_t hThread )
{
#if defined(_WIN32)
    return ::TerminateThread( hThread, 0 ) != 0;
#elif defined(POSIX)
    return pthread_cancel( hThread ) == 0;
#else
    AssertMsg1( false, "%s not implemented", __FUNCTION__ );
    return false;
#endif
}

//-----------------------------------------------------------------------------

void ThreadSetAffinity( ThreadHandle_t hThread, int nAffinityMask )
{
#ifndef POSIX
	if ( !hThread )
	{
		hThread = ThreadGetCurrentHandle();
	}

	SetThreadAffinityMask( hThread, nAffinityMask );
#endif
}

//-----------------------------------------------------------------------------

ThreadId_t InitMainThread()
{
#ifndef LINUX
	ThreadSetDebugName( "MainThrd" );
#endif 
	return ThreadGetCurrentId();
}

static ThreadId_t g_ThreadMainThreadID = InitMainThread();

bool ThreadInMainThread()
{
	return ( ThreadGetCurrentId() == g_ThreadMainThreadID );
}

//-----------------------------------------------------------------------------

void DeclareCurrentThreadIsMainThread()
{
	g_ThreadMainThreadID = ThreadGetCurrentId();
}

//-----------------------------------------------------------------------------

#if defined(_PS3)
static bool BThreadJoinPosixInternal( ThreadId_t idThread )
{
	uint64_t unExitStatus = 0;
	return ( sys_ppu_thread_join( (sys_ppu_thread_t) idThread, &unExitStatus ) == CELL_OK );
}
#elif defined(POSIX)
static bool BThreadJoinPosixInternal( ThreadHandle_t hThread )
{
	return pthread_join( hThread, NULL ) == 0;
}
#endif

//-----------------------------------------------------------------------------

void ThreadSetDebugName( const char *pszName )
{
	if ( !pszName )
	    return;
#if defined(_WIN32)
	if ( Plat_IsInDebugSession() )
	{
#define MS_VC_EXCEPTION 0x406d1388

		typedef struct tagTHREADNAME_INFO
		{
			DWORD dwType;        // must be 0x1000
			LPCSTR szName;       // pointer to name (in same addr space)
			DWORD dwThreadID;    // thread ID (-1 caller thread)
			DWORD dwFlags;       // reserved for future use, most be zero
		} THREADNAME_INFO;

		THREADNAME_INFO info;
		info.dwType = 0x1000;
		info.szName = pszName;
		info.dwThreadID = (DWORD)-1;
		info.dwFlags = 0;

		__try
		{
			RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR *)&info);
		}
		__except (EXCEPTION_CONTINUE_EXECUTION)
		{
		}
	}
#elif defined(OSX)
    // pthread_setname_np is only available in 10.6 or later, so test
    // for it at runtime.
    int (*dynamic_pthread_setname_np)(const char*);
    *reinterpret_cast<void**>(&dynamic_pthread_setname_np) = dlsym(RTLD_DEFAULT, "pthread_setname_np");

    if ( !dynamic_pthread_setname_np )
        return;
    dynamic_pthread_setname_np( pszName );
#elif defined( LINUX ) && ( ( __GLIBC__ > 2 ) || ( ( __GLIBC__ == 2 ) && ( __GLIBC_MINOR__ >= 12 ) ) )
	// As of glibc v2.12, we can use pthread_setname_np.

/* 
    pthread_setname_np() in phthread_setname.c has the following code:
 
	#define TASK_COMM_LEN 16
	  size_t name_len = strlen (name);
	  if (name_len >= TASK_COMM_LEN)
    	return ERANGE;
 
	So we need to truncate the threadname to 16 or the call will just fail.
*/
	char szThreadName[ 16 ];
	strncpy( szThreadName, pszName, V_ARRAYSIZE( szThreadName ) );
	szThreadName[ V_ARRAYSIZE( szThreadName ) - 1 ] = 0;
	pthread_setname_np( pthread_self(), szThreadName );
#endif
}

//-----------------------------------------------------------------------------

int CreateSimpleProcess( const char *pCommandLine, uint32_t nFlags, ProcessHandle_t *pHandle )
{
    // Guarantee the handle is invalid on all failures.
    *pHandle = INVALID_PROCESS_HANDLE;
    
#ifdef _WIN32
    
	STARTUPINFO startupInfo = { 0 };
	PROCESS_INFORMATION processInformation = { 0 };
	startupInfo.cb = sizeof( startupInfo );

    uint32_t nOsFlags = 0;

    if ( (nFlags & k_ESimpleProcessNoWindow) != 0 )
    {
        nOsFlags |= CREATE_NO_WINDOW;
    }

    // The command line argument only needs to be writable in
    // the WCHAR version, so we can safely cast away const here.
	if ( !CreateProcess( NULL, (LPSTR)pCommandLine, NULL, NULL, false, nOsFlags, NULL, NULL,
                         &startupInfo, &processInformation ) )
    {
        return ::GetLastError();
    }

	*pHandle = processInformation.hProcess;
	CloseHandle( processInformation.hThread );

#else

	pid_t pid = fork();
	if ( pid < 0 )
	{
		// fork failed
		return errno;
	}
	else if ( pid == 0 )
	{
		// we're the child process - if we need to exit, use _exit, 
		// so we don't wander into any of the process tear-down code.
        int nRet = system( pCommandLine );
        if ( nRet == -1 || !WIFEXITED( nRet ) )
        {
            _exit( -1 );
        }
        _exit( WEXITSTATUS( nRet ) );
	}
	else
	{
		// parent
        *pHandle = (uint32_t)pid;
	}

#endif

    return 0;
}

//-----------------------------------------------------------------------------

ProcessHandle_t ThreadGetCurrentProcessHandle()
{
#ifdef _WIN32
    return ::GetCurrentProcess();
#else
    return (ProcessHandle_t)getpid();
#endif
}

//-----------------------------------------------------------------------------

ProcessHandle_t ThreadOpenProcess( uint32_t dwProcessId )
{
    ProcessHandle_t hProcess;
    
#ifdef _WIN32
    hProcess = ::OpenProcess( PROCESS_ALL_ACCESS, FALSE, dwProcessId );
    if ( hProcess == NULL )
    {
        return INVALID_PROCESS_HANDLE;
    }
#else
    hProcess = dwProcessId;
#endif

    return hProcess;
}

//-----------------------------------------------------------------------------

bool ThreadCloseProcess( ProcessHandle_t hProcess )
{
#ifdef _WIN32
    return ::CloseHandle( hProcess ) != 0;
#else
    // Clean up the process in case it's a zombie.
    waitpid( hProcess, NULL, WNOHANG );
    return hProcess != INVALID_PROCESS_HANDLE;
#endif
}

//-----------------------------------------------------------------------------

uint32_t ThreadGetCurrentProcessId()
{
#ifdef _WIN32
	return ::GetCurrentProcessId();
#else
	return (uint32_t)getpid();
#endif
}


//-----------------------------------------------------------------------------

bool ThreadIsProcessActive( uint32_t dwProcessId )
{
#ifdef _WIN32
	bool bActive = false;
	HANDLE hProcess = ::OpenProcess( SYNCHRONIZE, false, dwProcessId );

	if ( hProcess != NULL )
	{
		// if we get a timeout, that means the app is still running
		bActive = WAIT_TIMEOUT == ::WaitForSingleObject( hProcess, 0 );
		::CloseHandle( hProcess );
	}

	return bActive;
#elif defined( _PS3 )
	AssertFatalMsg( 0, "Need to implement!");
	return true;
#else
	if ( dwProcessId == 0 )
		return false;
		
	int ret = kill( dwProcessId, 0 );
	return ( ret >= 0 || ( ret < 0 && errno != ESRCH ) );
#endif
}

#ifdef _WIN32
bool ThreadIsProcessActive( ProcessHandle_t hProcess )
{
    // if we get a timeout, that means the app is still running
    return WAIT_TIMEOUT == ::WaitForSingleObject( hProcess, 0 );
}
#endif

//-----------------------------------------------------------------------------

bool ThreadTerminateProcessCode( uint32_t dwProcessId, int32_t nExitCode )
{
	bool bSuccess = false;
#ifdef _WIN32
	HANDLE hProcess = ::OpenProcess( PROCESS_TERMINATE, false, dwProcessId );

	if ( hProcess != NULL )
	{
		bSuccess = ::TerminateProcess( hProcess, nExitCode )?true:false;
		::CloseHandle( hProcess );
	}
#elif defined ( _PS3 )
	AssertFatalMsg( 0, "Need to implement!");
	bSuccess = true;
#else
    // Can't support code, ignore it.
	bSuccess = 0 == kill( dwProcessId, SIGTERM );
#endif

	return bSuccess;
}

#ifdef _WIN32
bool ThreadTerminateProcessCode( ProcessHandle_t hProcess, int32_t nExitCode )
{
    return ::TerminateProcess( hProcess, nExitCode )?true:false;
}
#endif

//-----------------------------------------------------------------------------

bool ThreadGetProcessExitCode( uint32_t dwProcessId, int32_t *pExitCode )
{
	bool bSuccess = false;
#ifdef _WIN32
	HANDLE hProcess = ::OpenProcess( PROCESS_QUERY_INFORMATION, false, dwProcessId );

	if ( hProcess != NULL )
	{
		bSuccess = ::GetExitCodeProcess( hProcess, (DWORD*)pExitCode )?true:false;
		::CloseHandle( hProcess );
	}
#elif defined ( _PS3 )
	AssertFatalMsg( 0, "Need to implement!");
	bSuccess = true;
#else
	bSuccess = 0 < waitpid( dwProcessId, pExitCode, WNOHANG );
#endif

	return bSuccess;
}

#ifdef _WIN32
bool ThreadGetProcessExitCode( ProcessHandle_t hProcess, int32_t *pExitCode )
{
    return ::GetExitCodeProcess( hProcess, (DWORD*)pExitCode ) != 0;
}
#endif

//-----------------------------------------------------------------------------

bool ThreadWaitForProcessExit( ProcessHandle_t hProcess, uint32_t nMillis )
{
#if defined(_WIN32)
    return WAIT_OBJECT_0 == ::WaitForSingleObject( hProcess, nMillis );
#elif defined( _PS3 )
	AssertFatalMsg( 0, "Need to implement!");
	return false;
#else
    for (;;)
    {
        int ret = kill( hProcess, 0 );
        if ( ret < 0 && errno == ESRCH )
        {
            return true;
        }

        if ( nMillis == 0 )
        {
            return false;
        }

        int nWaitMillis = nMillis >= 10 ? 10 : 1;
        nMillis -= nWaitMillis;
        usleep( nWaitMillis * 1000 );
    }
#endif
}

//-----------------------------------------------------------------------------

#ifdef _WIN32

#if !defined NTSTATUS
typedef LONG NTSTATUS;
#endif

#if !defined PROCESSINFOCLASS
typedef LONG PROCESSINFOCLASS;
#endif

#if !defined PPEB
typedef struct _PEB *PPEB;
#endif

#if !defined PROCESS_BASIC_INFORMATION
typedef struct _PROCESS_BASIC_INFORMATION {
	PVOID Reserved1;
	PPEB PebBaseAddress;
	PVOID Reserved2[2];
	ULONG_PTR UniqueProcessId;
	PVOID Reserved3;
} PROCESS_BASIC_INFORMATION;
#endif

typedef NTSTATUS (WINAPI * LPFN_ZwQueryInformationProcess)( HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG );

typedef DWORD (WINAPI *LPFN_GetProcessId)( HANDLE );

DWORD GetProcessIdWin2kSafe( HANDLE hProc )
{
	LPFN_GetProcessId pfnGetProcessId = NULL;
	HMODULE hModKernel32 = GetModuleHandleA("kernel32.dll");
	if ( hModKernel32 )
		pfnGetProcessId = (LPFN_GetProcessId)GetProcAddress( hModKernel32, "GetProcessId" );

	if ( pfnGetProcessId )
	{
		return pfnGetProcessId( hProc );
	}
	else
	{
		LPFN_ZwQueryInformationProcess pfnZwQueryInformationProcess = NULL;
		HMODULE hModNTDLL = GetModuleHandleA("ntdll.dll");
		if ( hModNTDLL )
			pfnZwQueryInformationProcess = (LPFN_ZwQueryInformationProcess)GetProcAddress( hModNTDLL, "ZwQueryInformationProcess" );

		if ( pfnZwQueryInformationProcess )
		{
			PROCESS_BASIC_INFORMATION pbi;
			ZeroMemory( &pbi, sizeof( PROCESS_BASIC_INFORMATION ) );

			if ( pfnZwQueryInformationProcess( hProc, 0, &pbi, sizeof(PROCESS_BASIC_INFORMATION), NULL ) == 0 )
			{
				return (DWORD)pbi.UniqueProcessId;
			}
		}
	}
	return 0xFFFFFFFF;
}

#endif

//-----------------------------------------------------------------------------
#if !defined( _PS3 )
uint32_t ThreadShellExecute(	const char *lpApplicationName, // executable name with path
							const char *lpCommandLine, // command line options
							const char *lpCurrentDirectory ) // start directory
{
	uint32_t dwProcID = 0;
#ifdef _WIN32
	SHELLEXECUTEINFOW shInfo = {0};
	shInfo.cbSize = sizeof( SHELLEXECUTEINFO );
	shInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	shInfo.hwnd = NULL;
	shInfo.lpVerb = NULL;

	if ( lpApplicationName )
	{
		size_t nNumChars = strlen(lpApplicationName)+1;
		shInfo.lpFile = (wchar_t*) malloc( nNumChars*sizeof(wchar_t) );
		MultiByteToWideChar( CP_UTF8, 0, lpApplicationName, -1, (wchar_t*)shInfo.lpFile, (int)nNumChars );
	}

	if ( lpCommandLine )
	{
		size_t nNumChars = strlen(lpCommandLine)+1;
		shInfo.lpParameters = (wchar_t*) malloc( nNumChars*sizeof(wchar_t) );
		MultiByteToWideChar( CP_UTF8, 0, lpCommandLine, -1, (wchar_t*)shInfo.lpParameters, (int)nNumChars );
	}

	if ( lpCurrentDirectory )
	{	
		size_t nNumChars = strlen(lpCurrentDirectory)+1;
		shInfo.lpDirectory = (wchar_t*) malloc( nNumChars*sizeof(wchar_t) );
		MultiByteToWideChar( CP_UTF8, 0, lpCurrentDirectory, -1, (wchar_t*)shInfo.lpDirectory, (int)nNumChars );
	}

	shInfo.nShow = SW_SHOWDEFAULT;
	shInfo.hInstApp = NULL;
	shInfo.hProcess = NULL;

	if ( ::ShellExecuteExW( &shInfo ) )
	{
		// ShellExecuteEx can be successful without a new process being started 
		if ( shInfo.hProcess )
		{
			// add process to list of currently running steam games
			dwProcID =  GetProcessIdWin2kSafe( shInfo.hProcess );
			::CloseHandle( shInfo.hProcess );
		}
	}

	free( (wchar_t*)shInfo.lpFile );
	free( (wchar_t*)shInfo.lpParameters );
	free( (wchar_t*)shInfo.lpDirectory );
#else
	// make sure the working directory exists
	if ( lpCurrentDirectory )
	{	
#ifdef LINUX
		struct stat64 buf;
		int ret = stat64( lpCurrentDirectory, &buf );
#else
		struct stat buf;
		int ret = stat( lpCurrentDirectory, &buf );
#endif
		if ( ret < 0 )
			return 0;
	
		if ( ! ( buf.st_mode & S_IFDIR ) )
			return 0;
	}

	static bool bInstalledSignalHandler = false;
	if ( !bInstalledSignalHandler )
	{
		struct sigaction sa;
		sa.sa_flags = SA_NOCLDSTOP;
		sa.sa_handler = ReapChildProcesses;
		sigaction( SIGCHLD, &sa, NULL );
		bInstalledSignalHandler = true;
	}

	pid_t pid = fork();
	if ( pid < 0 )
	{
		// fork failed
		return 0;
	}
	else if ( pid == 0 )
	{
		// we're the child process - if we need to exit, use _exit, 
		// so we don't wander into any of the process tear-down code
		if ( lpCurrentDirectory )
		{
			int ret = chdir( lpCurrentDirectory );
			if ( ret < 0 )
				_exit( -1 );
		}

		char szFullCmdLine[ 2048 ];
		snprintf( szFullCmdLine, sizeof(szFullCmdLine), "%s %s", lpApplicationName, lpCommandLine );
		_exit( system( szFullCmdLine ) );
	}
	else
	{
		// parent
		dwProcID = pid;
	}
#endif
	return dwProcID;
}
#endif // _PS3




//-----------------------------------------------------------------------------
// Returns a snapshot of the current list of processes
//-----------------------------------------------------------------------------
int ThreadGetProcessListInfo( int nEntriesMax, SThreadProcessInfo *pEntries )
{
#if defined(_WIN32)
    
	HANDLE hProcessSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	if ( hProcessSnap == INVALID_HANDLE_VALUE )
    {
		return -1;
    }

    int nCount = -1;

	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof( PROCESSENTRY32 );

	if ( Process32First( hProcessSnap, &pe32 ) )
	{
        nCount = 0;

        do
        {
            if ( nCount < nEntriesMax )
            {
                pEntries[nCount].nProcessId = pe32.th32ProcessID;
                pEntries[nCount].nParentProcessId = pe32.th32ParentProcessID;
            }

            nCount++;
        }
        while ( Process32Next( hProcessSnap, &pe32 ) );
    }

	CloseHandle( hProcessSnap );

    return nCount;

#elif defined(LINUX)

    DIR *pProcDir = opendir( "/proc" );
    if ( !pProcDir )
    {
        return -1;
    }

    int nCount = 0;

    struct dirent *pProcEnt;

    for (;;)
    {
        pProcEnt = readdir( pProcDir );
        if ( !pProcEnt )
        {
            break;
        }
        
        char *pScan;
        uint32_t nProcId = 0;

        pScan = pProcEnt->d_name;
        while ( *pScan >= '0' && *pScan <= '9' )
        {
            nProcId = nProcId * 10 + (uint32_t)( *pScan - '0' );
            pScan++;
        }
        if ( *pScan != 0 )
        {
            // This doesn't look like an all-digits PID entry.
            continue;
        }

        char CharBuf[MAX_PATH];

        sprintf( CharBuf, "/proc/%u/stat", nProcId );
        
        FILE *pProcStat = fopen( CharBuf, "r" );
        if ( !pProcStat )
        {
            // If there's no stat file we'll just assume we're not
            // looking at a PID entry and continue on.
            continue;
        }

        if ( fgets( CharBuf, sizeof( CharBuf ), pProcStat ) == NULL )
        {
            fclose( pProcStat );
            nCount = -1;
            break;
        }

        fclose( pProcStat );

        CharBuf[ sizeof( CharBuf ) - 1 ] = 0;
        
        uint32_t nCheckProcId = 0;

        pScan = CharBuf;
        while ( *pScan >= '0' && *pScan <= '9' )
        {
            nCheckProcId = nCheckProcId * 10 + (uint32_t)( *pScan - '0' );
            pScan++;
        }
        if ( nCheckProcId != nProcId ||
             *pScan != ' ' )
        {
            // Invalid stat text.
            nCount = -1;
            break;
        }

        while ( *pScan == ' ' )
        {
            *pScan++;
        }
        if ( *pScan != '(' )
        {
            // Invalid stat text.
            nCount = -1;
            break;
        }

        // Skip over the name.
        pScan++;
        while ( *pScan && *pScan != ')' )
        {
            pScan++;
        }
        if ( *pScan != ')' ||
             *(pScan + 1) != ' ' )
        {
            // Invalid stat text.
            nCount = -1;
            break;
        }

        pScan += 2;
        while ( *pScan == ' ' )
        {
            *pScan++;
        }
        if ( *pScan < 'A' || *pScan > 'Z' )
        {
            // Invalid stat text.
            nCount = -1;
            break;
        }

        // Skip over the status character.
        pScan++;
        if ( *pScan != ' ' )
        {
            // Invalid stat text.
            nCount = -1;
            break;
        }

        while ( *pScan == ' ' )
        {
            *pScan++;
        }
        if ( *pScan < '0' || *pScan > '9' )
        {
            // Invalid stat text.
            nCount = -1;
            break;
        }

        uint32_t nParentId = 0;

        while ( *pScan >= '0' && *pScan <= '9' )
        {
            nParentId = nParentId * 10 + (uint32_t)( *pScan - '0' );
            pScan++;
        }
        if ( *pScan != ' ' )
        {
            // Invalid stat text.
            nCount = -1;
            break;
        }

        if ( nCount < nEntriesMax )
        {
            pEntries->nProcessId = nProcId;
            pEntries->nParentProcessId = nParentId;
            pEntries++;
        }
        nCount++;
    }
    
    closedir( pProcDir );

    return nCount;
    
#else

    // Not yet implemented.
    return -1;
    
#endif
}


//-----------------------------------------------------------------------------

#ifdef WIN32
ASSERT_INVARIANT( TW_FAILED == WAIT_FAILED );
ASSERT_INVARIANT( TW_TIMEOUT  == WAIT_TIMEOUT );
ASSERT_INVARIANT( WAIT_OBJECT_0 == 0 );

int ThreadWaitForObjects( int nEvents, const HANDLE *pHandles, bool bWaitAll, unsigned timeout )
{
	return WaitForMultipleObjects( nEvents, pHandles, bWaitAll, timeout );
}
#endif

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

#ifdef _PS3
uint32_t CThreadSyncObject::m_bstaticMutexInitialized = false;
uint32_t CThreadSyncObject::m_bstaticMutexInitializing = false;
sys_lwmutex_t CThreadSyncObject::m_staticMutex;
#endif

CThreadSyncObject::CThreadSyncObject()
#ifdef _WIN32
  : m_hSyncObject( NULL ),
	m_bOwnEventHandle( true )
#elif defined( POSIX ) && !defined( PLATFORM_PS3 )
  : m_pszSemName( NULL ),
	m_pSemaphore( 0 ),
	m_bSemOwner( false ),
	m_bInitialized( false ),
    m_bManualReset( false ),
    m_bWakeForEvent( false )
#endif
{
#if defined ( _PS3 )
	m_bSet = false;
	m_bManualReset = false;

	// set up linked list of wait objects
	memset(&m_waitObjects[0], 0, sizeof(m_waitObjects));
	m_pWaitObjectsList = &m_waitObjects[0];
	m_pWaitObjectsPool = &m_waitObjects[1];

	for (int i = 2; i < CTHREADEVENT_MAX_WAITING_THREADS + 2; i++)
		LLinkNode(m_pWaitObjectsPool, &m_waitObjects[i]);

	//Do we need to initialize the staticMutex?
	if (m_bstaticMutexInitialized)
		return;

	//If we are the first thread then create the mutex
	if ( cellAtomicCompareAndSwap32(&m_bstaticMutexInitializing, false, true) == false )
	{
		sys_lwmutex_attribute_t mutexAttr;
		sys_lwmutex_attribute_initialize( mutexAttr );
		mutexAttr.attr_recursive = SYS_SYNC_RECURSIVE;
		int err = sys_lwmutex_create( &m_staticMutex, &mutexAttr );
		Assert(err == CELL_OK);
		m_bstaticMutexInitialized = true;
	}
	else
	{
		//Another thread is already in the process of initialising the mutex, wait for it
		while ( !m_bstaticMutexInitialized )
		{
			ThreadYield();
		}
	}
#endif

}


//---------------------------------------------------------

CThreadSyncObject::~CThreadSyncObject()
{
#ifdef _WIN32
   if (m_hSyncObject && m_bOwnEventHandle )
   {
      if ( !CloseHandle(m_hSyncObject) )
	  {
		  Assert( 0 );
	  }
   }
#elif defined( POSIX ) && !defined( _PS3 )
   if ( m_bInitialized )
   {
	   if ( m_pszSemName == NULL ) 
	   {
		   pthread_cond_destroy( &m_Condition );
		   pthread_mutex_destroy( &m_Mutex );
	   }
	   else
	   {
		   CloseSemaphoreInternal( m_pSemaphore, m_bSemOwner, m_pszSemName );
		   delete[] m_pszSemName;
	   }
	   m_bInitialized = false;
   }
#endif
}

//---------------------------------------------------------

bool CThreadSyncObject::operator!() const
{
#if PLATFORM_PS3
	return m_bstaticMutexInitialized;
#elif defined( _WIN32 ) 
   return !m_hSyncObject;
#elif defined(POSIX)
   return !m_bInitialized;
#endif
}

//---------------------------------------------------------

void CThreadSyncObject::AssertUseable()
{
#ifdef THREADS_DEBUG
#if PLATFORM_PS3
	AssertMsg( m_bstaticMutexInitialized, "Thread synchronization object is unuseable" );
#elif defined( _WIN32 )
   AssertMsg( m_hSyncObject, "Thread synchronization object is unuseable" );
#elif defined(POSIX)
   AssertMsg( m_bInitialized, "Thread synchronization object is unuseable" );
#endif
#endif
}

//---------------------------------------------------------

bool CThreadSyncObject::Wait( uint32_t dwTimeoutMsec )
{
#ifdef THREADS_DEBUG
   AssertUseable();
#endif
#ifdef _WIN32
   return ( WaitForSingleObject( m_hSyncObject, dwTimeoutMsec ) == WAIT_OBJECT_0 );
#elif defined( _PS3 )
   AssertFatalMsg( 0, "Need to implement!");
	return false;
#elif defined( POSIX )
   bool bRet = false;
   if ( m_pszSemName == NULL )
   {
	   pthread_mutex_lock( &m_Mutex );
	   if ( m_cSet > 0 )
	   {
		   bRet = true;
		   m_bWakeForEvent = false;
	   }
	   else if ( dwTimeoutMsec > 0 )
	   {
		   volatile int ret = 0;

		   while ( !m_bWakeForEvent && ret != ETIMEDOUT )
		   {
			   struct timeval tv;
			   gettimeofday( &tv, NULL );
			   volatile struct timespec tm;

			   uint64_t actualTimeout = dwTimeoutMsec;

			   if ( dwTimeoutMsec == TT_INFINITE && m_bManualReset )
				   actualTimeout = 10; // just wait 10 msec at most for manual reset events and loop instead

			   volatile uint64_t nNanoSec = (uint64_t)tv.tv_usec*1000 + (uint64_t)actualTimeout*1000000;
			   tm.tv_sec = tv.tv_sec + nNanoSec /1000000000;
			   tm.tv_nsec = nNanoSec % 1000000000;

			   do
			   {   
				   ret = pthread_cond_timedwait( &m_Condition, &m_Mutex, (const timespec *)&tm );
			   } 
			   while( ret == EINTR );

			   bRet = ( ret == 0 );

			   if ( m_bManualReset )
			   {
				   if ( m_cSet )
					   break;
				   if ( dwTimeoutMsec == TT_INFINITE && ret == ETIMEDOUT )
					   ret = 0; // force the loop to spin back around
			   }
		   }

		   if ( bRet )
			   m_bWakeForEvent = false;
	   }
	   if ( !m_bManualReset && bRet )
		   m_cSet = 0;
	   pthread_mutex_unlock( &m_Mutex );
   }
   else
   {
	   bRet = AcquireSemaphoreInternal( dwTimeoutMsec );
   }
   return bRet;
#endif
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

CThreadEvent::CThreadEvent( bool bManualReset )
{
#ifdef _WIN32
    m_hSyncObject = CreateEvent( NULL, bManualReset, FALSE, NULL );
    AssertMsg1(m_hSyncObject, "Failed to create event (Log 0x%x)", GetLastError() );
#elif defined( _PS3 )
	m_bManualReset = bManualReset;
#elif defined(POSIX)
	CreateAnonymousSyncObjectInternal( false, bManualReset );
#else
#Log "Implement me"
#endif
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

#ifdef _PS3

//-----------------------------------------------------------------------------
// Helper function to atomically write index into destination and set semaphore
// This is used by WaitForMultipleObjects(WAIT_ANY) because once the semaphore
// is set, the waiting thread also needs to know which event triggered it
//-----------------------------------------------------------------------------
bool CThreadEventWaitObject::BSetIfUnset()
{
	// only set if no other event has set. Swap atomically in case all callers don't lock
	if ( !ThreadInterlockedAssignIf( m_pFlag, m_index, k_nInvalidIndex ) )
		return false;

	sys_semaphore_post(*m_pSemaphore, 1);
	return true;
}


//
// CThreadEvent::RegisterWaitingThread
//
void CThreadSyncObject::RegisterWaitingThread( sys_semaphore_t *pSemaphore, int index, int *flag)
{
	sys_lwmutex_lock(&m_staticMutex, 0);

	// if we are already set, then signal this semaphore
	if ( m_bSet )
	{
		CThreadEventWaitObject waitObject;
		waitObject.Init(pSemaphore, index, flag);

		// make sure this thread hasn't already been woken by another sync object
		if ( waitObject.BSetIfUnset() && !m_bManualReset )
			m_bSet = false;		
	}
	else
	{
		if (!m_pWaitObjectsPool->m_pNext)
		{
			DEBUG_ERROR("CThreadEvent: Ran out of events; cannot register waiting thread\n");
		}

		// add this semaphore to linked list - can be added more than once it doesn't matter

		CThreadEventWaitObject *pWaitObject = LLUnlinkNode(m_pWaitObjectsPool->m_pNext);
		pWaitObject->Init(pSemaphore, index, flag);
		LLinkNode(m_pWaitObjectsList, pWaitObject);
	}

	sys_lwmutex_unlock(&m_staticMutex);
}

//
// CThreadEvent::UnregisterWaitingThread
//
void CThreadSyncObject::UnregisterWaitingThread(sys_semaphore_t *pSemaphore)
{
	// remove all instances of this semaphore from linked list

	sys_lwmutex_lock(&m_staticMutex, 0);

	CThreadEventWaitObject *pWaitObject = m_pWaitObjectsList->m_pNext;

	while (pWaitObject)
	{
		CThreadEventWaitObject *pNext = pWaitObject->m_pNext;

		if (pWaitObject->m_pSemaphore == pSemaphore)
		{
			LLUnlinkNode(pWaitObject);
			LLinkNode(m_pWaitObjectsPool, pWaitObject);
		}

		pWaitObject = pNext;
	}

	sys_lwmutex_unlock(&m_staticMutex);
}

#endif // _PS3


//-----------------------------------------------------------------------------
#ifdef WIN32
#define GLOBAL_EVENT_FORMAT_PFX "Global\\"
#else
#define GLOBAL_EVENT_FORMAT_PFX ""
#endif

#if !defined( _PS3 )
CThreadEvent::CThreadEvent( const char *pchEventName, bool bCrossUserSession, bool fManualReset )
{
	AssertMsg( pchEventName[0] != '\0', "Attempting to create a named event with a null name" );
#ifdef _WIN32
	PSECURITY_ATTRIBUTES pSecAttr = NULL;
	SECURITY_ATTRIBUTES secAttr;
	char secDesc[ SECURITY_DESCRIPTOR_MIN_LENGTH ];

	const char *pchFullName = pchEventName;
	char rgchDecoratedEventName[MAX_PATH];
	if ( bCrossUserSession )
	{
		secAttr.nLength = sizeof(secAttr);
		secAttr.bInheritHandle = FALSE;
		secAttr.lpSecurityDescriptor = &secDesc;
		InitializeSecurityDescriptor( secAttr.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION );
		SetSecurityDescriptorDacl( secAttr.lpSecurityDescriptor, TRUE, NULL, FALSE );
		pSecAttr = &secAttr;
		
		rgchDecoratedEventName[0] = 0;
		strncpy( rgchDecoratedEventName, GLOBAL_EVENT_FORMAT_PFX, V_ARRAYSIZE( rgchDecoratedEventName ) );
		rgchDecoratedEventName[ V_ARRAYSIZE(rgchDecoratedEventName) - 1 ] = 0;
		strncat( rgchDecoratedEventName, pchEventName, 
				 ( V_ARRAYSIZE( rgchDecoratedEventName ) - strlen(rgchDecoratedEventName) - 1 ) );
		pchFullName = rgchDecoratedEventName;
	}

	m_hSyncObject = CreateEvent( pSecAttr, fManualReset, FALSE, pchFullName );

	AssertMsg1(m_hSyncObject, "Failed to create event (Log 0x%x)", GetLastError() );
#else
	// if this is a manual reset event, we need an extra character in the name to note that,
	// else we just need the required / prefix and the "e" for event
	size_t len = strlen( pchEventName );
	size_t maxlen = SEM_NAME_LEN - ( fManualReset ? 3 : 2 );
	AssertMsg1( len < maxlen , "Semaphore name (%s) is longer than POSIX can handle, truncating.", pchEventName );
	m_pszSemName = new char[ SEM_NAME_LEN + 1 ];
	snprintf( m_pszSemName, SEM_NAME_LEN, "/e%s%s", ( fManualReset ? "m" : "" ), pchEventName );
	m_bManualReset = fManualReset;

	m_pSemaphore = CreateSemaphoreInternal( m_pszSemName, 0, bCrossUserSession, &m_bSemOwner );
	if ( m_pSemaphore == SEM_FAILED )
	{
		AssertMsg1( m_pSemaphore != SEM_FAILED, "semaphore creation failed %s", strerror( errno ) );
		return;
	}

	// ok, now that we've got the semaphore we think we wanted, make sure that someone didn't create
	// and event with the same name but the opposite auto-reset semantic - note this ?: is intentionally
	// the opposite of the one above
	char rgchSemNameT[SEM_NAME_LEN + 1];
	snprintf( rgchSemNameT, SEM_NAME_LEN, "/e%s%s", ( fManualReset ? "" : "m" ), pchEventName );
	sem_type tsem = OpenSemaphoreInternal( rgchSemNameT, bCrossUserSession );
	if ( tsem != SEM_FAILED )
	{
		// this assert means two callers have created the same named event with different auto-reset 
		// semantics - MSDN says they'll get a handle to the previous event (and no warning that the semantics differ)
		AssertMsg1( false, "the same event name (%s) was created as both manual and auto reset, something's fubar'd", pchEventName );
		// close the semaphore we opened initially
		CloseSemaphoreInternal( m_pSemaphore, m_bSemOwner, m_pszSemName );
		m_bSemOwner = false;
		m_pSemaphore = tsem;
		m_bManualReset = !m_bManualReset;
		strncpy( m_pszSemName, rgchSemNameT, SEM_NAME_LEN );
	}
	
	if ( m_bSemOwner )
		SaveNameToFile( m_pszSemName );

    m_bInitialized = true;
#endif

}
#endif // !_PS3

#ifdef _WIN32
CThreadEvent::CThreadEvent( HANDLE hSyncObject, bool bOwnEventHandle )
{
    m_hSyncObject = hSyncObject;
	m_bOwnEventHandle = bOwnEventHandle;
    AssertMsg1( m_hSyncObject != NULL && m_hSyncObject != INVALID_HANDLE_VALUE, "Bad Event handle (handle 0x%p)", hSyncObject );
}
#endif

//---------------------------------------------------------

bool CThreadEvent::Set()
{
   AssertUseable();
#ifdef _WIN32
   return ( SetEvent( m_hSyncObject ) != 0 );
#elif defined( _PS3 )

   sys_lwmutex_lock(&m_staticMutex, 0);

   //Mark event as set
   m_bSet = true;

   // signal registered semaphores
   while ( m_pWaitObjectsList->m_pNext )
   {
	   CThreadEventWaitObject *pWaitObject = LLUnlinkNode(m_pWaitObjectsList->m_pNext);

	   // ok to add before setting.. we have the lock
	   LLinkNode(m_pWaitObjectsPool, pWaitObject);

	   if ( !pWaitObject->BSetIfUnset() )
		   continue;

	   // we woke the thread.. ok to clear set
	   if (!m_bManualReset)
	   {
		   m_bSet = false;
		   break;
	   }
   }

   sys_lwmutex_unlock(&m_staticMutex);

   return true;

#elif defined(POSIX)
   if ( m_pszSemName == NULL ) 
   {
	   return SignalThreadSyncObjectInternal();
   }
   else
   {
	   EnsureSemaphorePostedInternal( m_pSemaphore );
	   return true;
   }
#endif
}

//---------------------------------------------------------

bool CThreadEvent::Reset()
{
#ifdef THREADS_DEBUG
   AssertUseable();
#endif
#ifdef _WIN32
   return ( ResetEvent( m_hSyncObject ) != 0 );
#elif defined( _PS3 )

   //Just mark us as no longer signaled
   m_bSet = 0;

   return true;
#elif defined(POSIX)
   if ( m_pszSemName == NULL )
   {
	   pthread_mutex_lock( &m_Mutex );
	   m_cSet = 0;
	   m_bWakeForEvent = false;
	   pthread_mutex_unlock( &m_Mutex );
	   return true; 
   }
   else
   {
	   return EnsureSemaphoreClearedInternal( m_pSemaphore );
   }
#endif
}

//---------------------------------------------------------

bool CThreadEvent::Check()
{
#ifdef THREADS_DEBUG
   AssertUseable();
#endif
   return Wait( 0 );
}

#if defined ( _PS3 )
bool CThreadEvent::Wait( uint32_t dwTimeout )
{
	CThreadSyncObject *pThis = this;
	return (WaitForMultipleEvents( &pThis, 1, dwTimeout ) != -1);
}

#endif // _PS3


#if defined(POSIX) && !defined( _PS3 )
const char *g_szSemFileName = "/tmp/.steam-sem-names";
bool CThreadSyncObject::SaveNameToFile( const char *pszName )
{
#ifdef USE_BSD_SEMAPHORES
	int flags = O_WRONLY | O_APPEND | O_CREAT;
#if defined(OSX)
        flags |= O_EXLOCK;
#endif
	int fd = open( g_szSemFileName, flags, S_IRWXU | S_IRWXG | S_IRWXO );
	if ( fd < 0 )
	{
		AssertMsg1( false, "can't open/create semaphore name file (%s)", strerror(errno) );
		return false;
	}
	char rgchSemNameAndPid[SEM_NAME_LEN*2];
	snprintf( rgchSemNameAndPid, sizeof(rgchSemNameAndPid), "%s\t0x%x\n", pszName, getpid() );
	ssize_t nWritten = write( fd, rgchSemNameAndPid, strlen( rgchSemNameAndPid ) );
	if ( nWritten < 0 )
	{
		AssertMsg1( (size_t)nWritten == strlen( rgchSemNameAndPid ), "wrote too few bytes (%s)", strerror( errno ) );
		close( fd );
		return false;
	}
	close( fd );
#endif
	return true;
}

bool CThreadSyncObject::CreateAnonymousSyncObjectInternal( bool bInitiallySet, bool bManualReset )
{
	int iErr;

	m_bInitialized = false;

	pthread_mutexattr_t Attr;
    iErr = pthread_mutexattr_init( &Attr );
	if ( iErr != 0 )
	{
		AssertMsg( false, strerror( iErr ) );
		return false;
	}

    iErr = pthread_mutex_init( &m_Mutex, &Attr );

    pthread_mutexattr_destroy( &Attr );

	if (iErr)
	{
		AssertMsg( false, strerror( iErr ) );
		return false;
	}

    iErr = pthread_cond_init( &m_Condition, NULL );
    if ( iErr != 0 )
	{
		pthread_mutex_destroy( &m_Mutex );
		AssertMsg( false, strerror( iErr ) );
		return false;
	}

    m_cSet = bInitiallySet ? 1 : 0;
	m_bWakeForEvent = false;
    m_bManualReset = bManualReset;

    m_bInitialized = true;

	return true;
}

bool CThreadSyncObject::SignalThreadSyncObjectInternal()
{
    pthread_mutex_lock( &m_Mutex );
	m_cSet = 1;
	m_bWakeForEvent = true;
	int ret = pthread_cond_signal( &m_Condition );
	pthread_mutex_unlock( &m_Mutex );
	return ret == 0;
}

bool CThreadSyncObject::IsSemaphoreOrphanedInternal( sem_type sem, int iIgnorePid )
{
#ifdef USE_BSD_SEMAPHORES
	return false;
#else
	// We can't actually see how many people have handles to this
	// sem and unlike the bsd api, there are no reference
	// semantics here.
	// If there are no waiters on the semaphore,
	// see who operated on the semaphore last.  If they're dead
	// consider the semaphore orphaned.
	// This is far from perfect but as we usually use full mutexes
	// for single-instance kinds of things it fits that case
	// reasonably well.
	int nWaiters = semctl( sem, 0, GETNCNT );
	nWaiters += semctl( sem, 0, GETZCNT );
    if ( nWaiters < 0 )
    {
        // Invalid semaphore.
        return false;
    }

	pid_t lastpid = (pid_t) semctl(sem, 0, GETPID);
    if ( lastpid < 0 )
    {
        // Invalid semaphore.
        return false;
    }
    
	bool bLastUserAlive = false;
	if ( lastpid && lastpid != iIgnorePid )
	{
		int ret = kill( lastpid, 0 );
		bLastUserAlive = ( ret >= 0 || ( ret < 0 && errno != ESRCH ) );
	}
	
	return nWaiters == 0 && !bLastUserAlive;
#endif
}

sem_type CThreadSyncObject::CreateSemaphoreInternal( const char *pszName,
	                                                 long cInitialCount,
													 bool bCrossUser,
													 bool *bCreated )
{
	sem_type sem = SEM_FAILED;
	*bCreated = true;
#ifdef USE_BSD_SEMAPHORES
	sem = sem_open( pszName, O_CREAT | O_EXCL,  S_IRWXU | (bCrossUser ? (S_IRWXG | S_IRWXO) : 0), lInitialCount );
	if ( sem == SEM_FAILED && errno == EEXIST )
	{
		*bCreated = false;
		sem = sem_open( pszName, 0 );
	}
	
	if ( sem == SEM_FAILED )
	{
		const char *pchErr = strerror( errno ); 
		DebugAssert( "Create semaphore failed in CreateSemaphoreInternal" && !pchErr );
		return SEM_FAILED;
	}
	
	if ( *bCreated )
	{
		DebugAssert( SaveSemNameToFile( pszName ) );
	}
#else
	key_t semkey = crc32( 0, pszName, strlen( pszName ) );
    const char *userName = getenv( "USER" );
    if ( !bCrossUser &&
         userName &&
         userName[0] )
    {
        // This isn't a cross-user semaphore so uniquify the
        // name by adding in the user name CRC.
        semkey = crc32( semkey, userName, strlen( userName ) );
    }
	if ( semkey == -1 )
		return SEM_FAILED;
	
	int semflags = SEM_R | SEM_A;
	if (bCrossUser)
	{
		semflags |= ( (SEM_R >> 3) | (SEM_A >> 3) | (SEM_R >> 6) | (SEM_A >> 6) );
	}
	
	sem = semget( semkey, 1, semflags | IPC_CREAT | IPC_EXCL );

    // If you crash repeatedly you'll leak semaphores and eventually
    // fill up your quota.  Brute force cleanup of them if we've
    // run out of space.  This is slow but should be a very rare
    // occurrence.  If it's a problem it looks like there's some
    // Linux-specific syscall we can use to query the set of existing semaphores.
    if ( sem == SEM_FAILED && errno == ENOSPC )
    {
        int nHits = 0;
        for ( uint32_t i = 0; i < UINT_MAX; i++ )
        {
            if ( IsSemaphoreOrphanedInternal( i, 0 ) )
            {
                semctl( i, 0, IPC_RMID );
                if ( ++nHits > 100 )
                {
                    // We've freed up plenty of semaphores.
                    break;
                }
            }
        }

        sem = semget( semkey, 1, semflags | IPC_CREAT | IPC_EXCL );
    }
    
	if ( sem == SEM_FAILED && errno == EEXIST )
	{
		// The semaphore exists but it may have been orphaned as semaphores are
		// easy to leak.  See if it is an orphan and if so clean it up and
		// try creating again.
		sem = semget( semkey, 1, semflags );
		if ( sem != SEM_FAILED && IsSemaphoreOrphanedInternal( sem, 0 ) )
		{
			semctl( sem, 0, IPC_RMID );
			sem = semget( semkey, 1, semflags | IPC_CREAT | IPC_EXCL );
		}
	}
	
	if ( sem == SEM_FAILED && errno == EEXIST )
	{
		*bCreated = false;
		// there's a race condition here - the creator is going to set the semaphore
		// to their desired initial value - and we need to wait to access them until that's
		// been done - so spin (with a sleep) on the operation time of the semaphore,
		// when it's non-zero, the creator has set it up.
		union semun sun;
		struct semid_ds idbuf;
		sun.buf = &idbuf;
		bool bReady = false;
		static const int k_cMaxRetries = 100;
		while ( !bReady )
		{
			sem = semget( semkey, 1, semflags );
			if ( sem == SEM_FAILED )
			{
				break;
			}
			
			for( int i = 0; i < k_cMaxRetries && !bReady; i++ ) 
			{
				if ( semctl(sem, 0, IPC_STAT, sun) >= 0 && ( cInitialCount == 0 || sun.buf->sem_otime != 0 ) )
				{
					bReady = true;
				}
				else
				{
					usleep(250);
				}
			}

			if ( !bReady && IsSemaphoreOrphanedInternal( sem, 0 ) )
			{
				// The creator appears to have gone away and
				// orphaned the semaphore, so try and take over.
				semctl( sem, 0, IPC_RMID );
				sem = semget( semkey, 1, semflags | IPC_CREAT | IPC_EXCL );
				*bCreated = true;
				bReady = true;
			}
		}
	}
	
	if ( sem == SEM_FAILED )
	{
		const char *pchErr = strerror( errno ); 
		AssertMsg( false, pchErr );
		return SEM_FAILED;
	}
	
	if ( *bCreated )
	{
		// set the semaphore to it's desired initial value
		union semun sun;
		sun.val = cInitialCount;
		semctl( sem, 0, SETVAL, sun );
	}
#endif

	if ( sem != SEM_FAILED && !*bCreated )
		errno = EEXIST;

	return sem;
}

sem_type CThreadSyncObject::OpenSemaphoreInternal( const char *pszName, bool bCrossUser )
{
#ifdef USE_BSD_SEMAPHORES
	return sem_open( pszName, 0 );
#else
	key_t semkey = crc32( 0, pszName, strlen( pszName ) );	
    const char *userName = getenv( "USER" );
    if ( !bCrossUser &&
         userName &&
         userName[0] )
    {
        // This isn't a cross-user semaphore so uniquify the
        // name by adding in the user name CRC.
        semkey = crc32( semkey, userName, strlen( userName ) );
    }
	if ( semkey == -1 )
		return SEM_FAILED;
	
	sem_type sem = semget( semkey, 1, 0 );
    if ( sem != SEM_FAILED && IsSemaphoreOrphanedInternal( sem, 0 ) )
    {
        semctl( sem, 0, IPC_RMID );
        sem = SEM_FAILED;
    }
    return sem;
#endif
}

bool CThreadSyncObject::AcquireSemaphoreInternal( uint32_t nMsTimeout )
{
	bool bRet = false;
	if ( nMsTimeout == TT_INFINITE )
	{
		int ret;
		for (;;)
		{
#ifdef USE_BSD_SEMAPHORES
			ret = sem_wait( m_pSemaphore );
#else
			struct sembuf sb;
			sb.sem_num = 0; 
			sb.sem_op = -1;
			ret = semop( m_pSemaphore, &sb, 1 );
#endif
			if ( ret == 0)
			{
				if ( m_bManualReset )
				{
#ifdef USE_BSD_SEMAPHORES
					sem_post( m_pSemaphore );
#else
					sb.sem_op = 1;
					semop( m_pSemaphore, &sb, 1 );
#endif
				}
				bRet = true;
				break;
			}
			if ( ret < 0 && errno != EINTR )
			{
				bRet = false;
				break;
			}
		}
	}
	else
	{
		int nMicrosecondsLeft = nMsTimeout*1000;
		do
		{
#ifdef USE_BSD_SEMAPHORES
			int ret = sem_trywait( m_pSemaphore ); 
#else
			struct sembuf sb;
			sb.sem_num = 0; 
			sb.sem_op = -1;
			sb.sem_flg = IPC_NOWAIT;
			int ret = semop( m_pSemaphore, &sb, 1 );
#endif
			if ( ret == 0 )
			{
				if ( m_bManualReset )
				{
#ifdef USE_BSD_SEMAPHORES
					sem_post( m_pSemaphore );
#else
					sb.sem_op = 1;
					semop( m_pSemaphore, &sb, 1 );
#endif
				}
				bRet = true;
				break;
			}
			if ( ret < 0 && ( errno != EINTR && errno != EAGAIN ) )
			{
				bRet = false;
				break;
			}
			int sleeptime = Min( nMicrosecondsLeft, 500 );
			if ( usleep( sleeptime ) >= 0 )
				nMicrosecondsLeft -= Min( sleeptime, nMicrosecondsLeft );
		}
		while ( nMicrosecondsLeft > 0 );
	}
	return bRet;
}

bool CThreadSyncObject::ReleaseSemaphoreInternal( sem_type sem, long nReleaseCount )
{
#ifdef USE_BSD_SEMAPHORES
	while ( nReleaseCount-- > 0 )
	{
		if ( sem_post( sem ) != 0 )
		{
			return false;
		}
	}
	return true;
#else
	struct sembuf sb;
	sb.sem_num = 0; 
	sb.sem_op = nReleaseCount;
	sb.sem_flg = 0;
	return ( semop( sem, &sb, 1 ) == 0 );
#endif
}


void CThreadSyncObject::CloseSemaphoreInternal( sem_type sem, bool bSemOwner, const char *pszSemName )
{
#ifdef USE_BSD_SEMAPHORES
	sem_close( sem );
	// there are reference semantics at work here - unlinking the
	// semaphore prevents others from opening it but doesn't
	// invalidate the references held by other processes.
	// this isn't quite windows behavior, but it's as close as we can get.
	if ( m_bSemOwner )
		sem_unlink( pszSemName );
#else
	if ( bSemOwner )
	{
		// we created this monster, let's see if we can tear it down
		if ( IsSemaphoreOrphanedInternal( sem, getpid() ) )
		{
			semctl( sem, 0, IPC_RMID );
		}
	}
#endif
}


bool CThreadSyncObject::EnsureSemaphoreClearedInternal( sem_type sem )
{
	for (;;)
	{
#ifdef USE_BSD_SEMAPHORES
		int ret = sem_trywait( sem );
#else
		struct sembuf sb;
		sb.sem_num = 0; 
		sb.sem_op = -1;
		sb.sem_flg = IPC_NOWAIT;
		int ret = semop( sem, &sb, 1 );
#endif
		if ( ret == 0 || ( ret < 0 && errno == EAGAIN ) )
			break;
		if ( ret < 0 && errno != EINTR )
			return false;
	}
	return true;
}


bool CThreadSyncObject::EnsureSemaphorePostedInternal( sem_type sem )
{
	int ret;
	do
	{
#ifdef USE_BSD_SEMAPHORES
		ret = sem_trywait( sem );
#else
		struct sembuf sb;
		sb.sem_num = 0; 
		sb.sem_op = -1;
		sb.sem_flg = IPC_NOWAIT;
		ret = semop( sem, &sb, 1 );
#endif
		if ( ret == 0 || ( ret < 0 && ( errno == EAGAIN || errno == EDEADLK ) ) )
		{
			// either the semaphore was 0 (locked), or the semaphore was > 0 and we've just changed it's value
#ifdef USE_BSD_SEMAPHORES
			return ( sem_post( sem ) == 0 );
#else
			sb.sem_op = 1;
			return ( semop( sem, &sb, 1 ) == 0 );

#endif
		}
	}
	while ( ret < 0 && errno == EINTR );
	return true;
}

#endif // POSIX && !PS3


#if defined(_WIN32) || defined(_PS3)
//---------------------------------------------------------
// Purpose: wait on multiple CThreadSyncObject 
// Input:	ppThreadSyncObjects - an array of objects to wait on
//			cThreadSyncObjects - Number of objects in the array
//			unMilliSecTimeout - time to wait
// Output: returns -1 on Log/timeout and the index first event in the array that is signaled on success
//---------------------------------------------------------
int WaitForMultipleEvents( CThreadSyncObject **ppThreadSyncObjects, uint32_t cThreadSyncObjects, uint32_t unMilliSecTimeout, CThreadMutex *pSyncLock )
{
#ifdef _WIN32
	HANDLE rgHandles[MAXIMUM_WAIT_OBJECTS];
	unsigned int cActualSyncObjects = min( cThreadSyncObjects, (unsigned int)MAXIMUM_WAIT_OBJECTS );
	for ( unsigned int iThreadSyncObject = 0; iThreadSyncObject < cActualSyncObjects; iThreadSyncObject++ )
	{
		rgHandles[ iThreadSyncObject ] = ppThreadSyncObjects[ iThreadSyncObject ]->Handle();
	}

	if ( pSyncLock )
		pSyncLock->Unlock();

	DWORD dwRet = WaitForMultipleObjects( cActualSyncObjects, rgHandles, FALSE, unMilliSecTimeout );
	if ( WAIT_FAILED == dwRet )
		return -1;
	if ( WAIT_TIMEOUT == dwRet )
		return -1;
	if ( dwRet >= WAIT_ABANDONED_0 && dwRet <= WAIT_ABANDONED_0 + cActualSyncObjects )
		return -1;

	Assert( dwRet - WAIT_OBJECT_0 < cActualSyncObjects );
	return dwRet - WAIT_OBJECT_0;
#elif defined(_PS3)
	// check if we have a wait objects semaphore
	if (!gbWaitObjectsCreated)
	{
		sys_semaphore_attribute_t semAttr;
		sys_semaphore_attribute_initialize(semAttr);
		sys_semaphore_create(&gWaitObjectsSemaphore, &semAttr, 0, 0xFFFF);

		gbWaitObjectsCreated = true;
	}

	// Support for a limited amount of events
	if ( cThreadSyncObjects >= MAXIMUM_WAIT_OBJECTS )
	{
		AssertMsg( false, "WaitForMultipleEvents - Trying to wait on too many objects" );
		return -1;
	}	

	// make sure the semaphore isn't set
	while (sys_semaphore_trywait(gWaitObjectsSemaphore) != EBUSY) {}

	// run through events registering this thread with each one
	int nSetSyncObject = CThreadEventWaitObject::k_nInvalidIndex;
	for (int i = 0; i < cThreadSyncObjects; i++)
	{
		ppThreadSyncObjects[i]->RegisterWaitingThread(&gWaitObjectsSemaphore, i, &nSetSyncObject);
	}

	// timeOut of 0 means very short timeOut, not infinite timeout	
	usecond_t unMicroSecTimeout = 0;		// when passed to sys_semaphore_wait, infinite = 0
	if ( unMilliSecTimeout != TT_INFINITE )
	{
		unMicroSecTimeout = (unMilliSecTimeout * 1000);
		if( unMicroSecTimeout < 1 )
			unMicroSecTimeout = 1;
	}

	// Wait for one event to be set or timeout
	int nWaitResult = sys_semaphore_wait( gWaitObjectsSemaphore, unMicroSecTimeout );

	// run through events unregistering this thread. Need to do this before checking results. Setting & unregistering from sync objects should grab
	// the same lock, so once we are unregistered, no other thread should touch our nSetSyncObject stack variable
	for (int i = 0; i < cThreadSyncObjects; i++)
	{
		ppThreadSyncObjects[i]->UnregisterWaitingThread(&gWaitObjectsSemaphore);
	}


	// now that we aren't registered with any sync objects, check results. If we do this before unregistering, an autoreset event could
	// see us waiting, increment our semaphore, then unset itself resulting in the event being lost

	// clear semaphore. Could have been set after sys_semaphore_wait above
	bool bWaitSuccess = (nWaitResult == CELL_OK );
	while ( sys_semaphore_trywait(gWaitObjectsSemaphore) == CELL_OK )
		bWaitSuccess = true;

	// sanity check
	if ( bWaitSuccess )
	{
		Assert( nSetSyncObject >= 0 && nSetSyncObject < cThreadSyncObjects );
	}

	return nSetSyncObject;
#endif
}
#endif // _WIN32 || _PS3


//-----------------------------------------------------------------------------
//
// CThreadSemaphore
//
//-----------------------------------------------------------------------------

CThreadSemaphore::CThreadSemaphore( long initialValue, long maxValue ) : CThreadSyncObject()
{
	if ( maxValue )
	{
		AssertMsg( maxValue > 0, "Invalid max value for semaphore" );
		AssertMsg( initialValue >= 0 && initialValue <= maxValue, "Invalid initial value for semaphore" );

#ifdef WIN32
		m_hSyncObject = CreateSemaphore( NULL, initialValue, maxValue, NULL );

		AssertMsg1(m_hSyncObject, "Failed to create semaphore (Log 0x%x)", GetLastError());
#elif defined(POSIX) && !defined(_PS3)
		// Our semaphore implementation doesn't support anonymous semaphores, so we'll create a random name here
		// FIXME: Is our this pointer good enough?
		m_pszSemName = new char[ SEM_NAME_LEN + 1 ];
		snprintf( m_pszSemName, SEM_NAME_LEN, "%p", this );
		m_pszSemName[SEM_NAME_LEN] = 0;

		m_pSemaphore = CreateSemaphoreInternal( m_pszSemName, initialValue, false, &m_bSemOwner );
		if ( m_pSemaphore == SEM_FAILED )
		{
			delete [] m_pszSemName;
			m_pszSemName = NULL;
			m_pSemaphore = 0;
			m_bSemOwner = false;
			return;
		}

		// We shouldn't ever pick up someone else's semaphore
		Assert( m_bSemOwner );
		m_bInitialized = true;
#else
#Log "Implement me"
#endif
	}
}


//---------------------------------------------------------

bool CThreadSemaphore::Release( long releaseCount )
{
#ifdef THRDTOOL_DEBUG
	AssertUseable();
#endif
#ifdef WIN32
	return ( ReleaseSemaphore( m_hSyncObject, releaseCount, NULL ) != 0 );
#elif defined(POSIX) && !defined(_PS3)
	return ReleaseSemaphoreInternal( m_pSemaphore, releaseCount );
#else
#Log "Implement me"
#endif
}


#if !defined(_PS3)

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

CThreadFullMutex::CThreadFullMutex( bool bEstablishInitialOwnership,
	                                const char *pszName,
									bool bAllAccess,
									bool bInherit )
{
	// If a name is given the name must always start with a forward slash or drive:/.
	// A leading slash will be ignored on Win32.
	// A leading drive: will be ignored on non-Win32.
	Assert(pszName == NULL ||
		   *pszName == '/' ||
		   (isalpha((unsigned char) *pszName) && pszName[1] == ':' && pszName[2] == '/'));

#if defined(_WIN32)
	char secDesc[ SECURITY_DESCRIPTOR_MIN_LENGTH ];
	SECURITY_ATTRIBUTES secAttr;
	secAttr.nLength = sizeof(secAttr);
	secAttr.bInheritHandle = bInherit;
	secAttr.lpSecurityDescriptor = NULL;

	if (bAllAccess)
	{
		secAttr.lpSecurityDescriptor = &secDesc;
		InitializeSecurityDescriptor(secAttr.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
		SetSecurityDescriptorDacl(secAttr.lpSecurityDescriptor, TRUE, NULL, FALSE);
	}

	m_bIsCreator = false;

	if (pszName != NULL && *pszName == '/')
	{
		pszName++;
	}

	::SetLastError(NO_ERROR);
    m_hSyncObject = CreateMutex( &secAttr, bEstablishInitialOwnership, pszName );

    AssertMsg1( m_hSyncObject, "Failed to create mutex (Log 0x%x)", GetLastError() );

	if ( m_hSyncObject != NULL )
	{
		if ( ::GetLastError() != ERROR_ALREADY_EXISTS )
		{
			m_bIsCreator = true;
		}
		else if ( bEstablishInitialOwnership )
		{
			// When a pre-existing mutex is opened the initial ownership
			// flag to CreateMutex is ignored.
			if ( ::WaitForSingleObject( m_hSyncObject, INFINITE ) != WAIT_OBJECT_0 )
			{
				AssertMsg( false, "Failed to acquire mutex ownership" );
				::CloseHandle( m_hSyncObject );
				m_hSyncObject = NULL;
			}
		}
	}

#elif defined(POSIX) && !defined(_PS3)
	AssertMsg(!bInherit, "POSIX does not support inheriting full mutexes");
	if ( pszName == NULL )
	{
		// Not much call for an anonymous object to have special security.
		AssertMsg( !bAllAccess, "Anonymous mutex cannot have bAllAccess == true" );

		if ( !CreateAnonymousSyncObjectInternal( !bEstablishInitialOwnership, false ) )
		{
			return;
		}
	}
	else
	{
		if ( isalpha(*pszName) && pszName[1] == ':' )
		{
			pszName += 2;
		}

		size_t uNameLen = strlen(pszName);
		AssertMsg1( uNameLen < SEM_NAME_LEN , "Semaphore name (%s) is longer than POSIX can handle, truncating.", pszName );
		m_pszSemName = new char[SEM_NAME_LEN + 1];
		strncpy( m_pszSemName, pszName, SEM_NAME_LEN );
		m_pszSemName[SEM_NAME_LEN] = 0;

		m_pSemaphore = CreateSemaphoreInternal( m_pszSemName, bEstablishInitialOwnership ? 0 : 1, bAllAccess, &m_bSemOwner );
		if ( m_pSemaphore == SEM_FAILED )
		{
			delete [] m_pszSemName;
			m_pszSemName = NULL;
			m_pSemaphore = 0;
			m_bSemOwner = false;
			return;
		}

		m_bInitialized = true;
	}
#else
#Log "Implement me"
#endif
}

//---------------------------------------------------------

bool CThreadFullMutex::Release()
{
#ifdef THRDTOOL_DEBUG
   AssertUseable();
#endif
#if defined(_WIN32)
   return ( ReleaseMutex( m_hSyncObject ) != 0 );
#elif defined(POSIX) && !defined(_PS3)
   if ( m_pszSemName == NULL)
   {
	   return SignalThreadSyncObjectInternal();
   }
   else
   {
	   return ReleaseSemaphoreInternal( m_pSemaphore );
   }
#else
#Log "Implement me"
#endif
}

//---------------------------------------------------------

bool CThreadFullMutex::IsCreator() const
{
#ifdef THRDTOOL_DEBUG
   AssertUseable();
#endif
#if defined(_WIN32)
   return m_bIsCreator;
#elif defined(POSIX) && !defined(_PS3)
   if ( m_pszSemName == NULL)
   {
	   // Anonymous objects are, by definition, never shared so each is a new creation.
	   return true;
   }
   else
   {
	 return m_bSemOwner;
   }
#else
#Log "Implement me"
#endif
}

#endif // !_PS3


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

CThreadLocalBase::CThreadLocalBase()
{
#if defined(_WIN32) || defined(_PS3)
	m_index = TlsAlloc();
	AssertMsg( m_index != 0xFFFFFFFF, "Bad thread local" );
	if ( m_index == 0xFFFFFFFF )
		Log( "Out of thread local storage!\n" );
#elif defined(POSIX)
	if ( pthread_key_create( &m_index, NULL ) != 0 )
		Log( "Out of thread local storage!\n" );
#endif
}

//---------------------------------------------------------

CThreadLocalBase::~CThreadLocalBase()
{
#if defined(_WIN32) || defined(_PS3)
	if ( m_index != 0xFFFFFFFF )
		TlsFree( m_index );
	m_index = 0xFFFFFFFF;
#elif defined(POSIX) 
	pthread_key_delete( m_index );
#endif
}

//---------------------------------------------------------

void * CThreadLocalBase::Get() const
{
#if defined(_WIN32) || defined(_PS3)
	if ( m_index != 0xFFFFFFFF )
		return TlsGetValue( m_index );
	AssertMsg( 0, "Bad thread local" );
	return NULL;
#elif defined(POSIX)
	void *value = pthread_getspecific( m_index );
	return value;
#else
	return NULL;
#endif
}

//---------------------------------------------------------

void CThreadLocalBase::Set( void *value )
{
#if defined(_WIN32) || defined(_PS3)
	if (m_index != 0xFFFFFFFF)
		TlsSetValue(m_index, value);
	else
		AssertMsg( 0, "Bad thread local" );
#elif defined(POSIX)
	if ( pthread_setspecific( m_index, value ) != 0 )
		AssertMsg( 0, "Bad thread local" );
#endif
}

//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------

#ifdef _WIN32
#ifdef _X360
#define TO_INTERLOCK_PARAM(p)		((long *)p)
#define TO_INTERLOCK_PTR_PARAM(p)	((void **)p)
#else
#define TO_INTERLOCK_PARAM(p)		(p)
#define TO_INTERLOCK_PTR_PARAM(p)	(p)
#endif // _X360

#ifndef USE_INTRINSIC_INTERLOCKED
long ThreadInterlockedIncrement( int32_t volatile *pDest )
{
	return InterlockedIncrement( TO_INTERLOCK_PARAM(pDest) );
}

long ThreadInterlockedDecrement( int32_t volatile *pDest )
{
	return InterlockedDecrement( TO_INTERLOCK_PARAM(pDest) );
}

long ThreadInterlockedExchange( int32_t volatile *pDest, int32_t value )
{
	return InterlockedExchange( TO_INTERLOCK_PARAM(pDest), value );
}

long ThreadInterlockedExchangeAdd( int32_t volatile *pDest, int32_t value )
{
	return InterlockedExchangeAdd( TO_INTERLOCK_PARAM(pDest), value );
}

long ThreadInterlockedCompareExchange( int32_t volatile *pDest, int32_t value, int32_t comperand )
{
	return InterlockedCompareExchange( TO_INTERLOCK_PARAM(pDest), value, comperand );
}

bool ThreadInterlockedAssignIf( int32_t volatile *pDest, int32_t value, int32_t comperand )
{
#if !(defined(_WIN64) || defined (_X360))
	__asm 
	{
		mov	eax,comperand
		mov	ecx,pDest
		mov edx,value
		lock cmpxchg [ecx],edx 
		mov eax,0
		setz al
	}
#else
	return ( InterlockedCompareExchange( TO_INTERLOCK_PARAM(pDest), value, comperand ) == comperand );
#endif
}

#endif  // USE_INTRINSIC_INTERLOCKED

#if !defined( USE_INTRINSIC_INTERLOCKED ) || defined( _WIN64 )
void *ThreadInterlockedExchangePointer( void * volatile *pDest, void *value )
{
	Assert( (size_t)pDest % 4 == 0 );
	return InterlockedExchangePointer( TO_INTERLOCK_PARAM(pDest), value );
}

void *ThreadInterlockedCompareExchangePointer( void * volatile *pDest, void *value, void *comperand )
{
	return InterlockedCompareExchangePointer( TO_INTERLOCK_PTR_PARAM(pDest), value, comperand );
}

bool ThreadInterlockedAssignPointerIf( void * volatile *pDest, void *value, void *comperand )
{
#if !(defined(_WIN64) || defined (_X360))
	__asm 
	{
		mov	eax,comperand
		mov	ecx,pDest
		mov edx,value
		lock cmpxchg [ecx],edx 
		mov eax,0
		setz al
	}
#else
	return ( InterlockedCompareExchangePointer( TO_INTERLOCK_PTR_PARAM(pDest), value, comperand ) == comperand );
#endif
}

#endif // !defined( USE_INTRINSIC_INTERLOCKED ) || defined( _WIN64 )

int64_t ThreadInterlockedIncrement64( int64_t volatile *pDest )
{
#if defined(_WIN64) || defined (_X360)
	return InterlockedIncrement64( pDest );
#else
	int64_t Old;

	do 
	{
		Old = *pDest;
	} while (ThreadInterlockedCompareExchange64(pDest, Old + 1, Old) != Old);

	return Old + 1;
#endif
}

int64_t ThreadInterlockedDecrement64( int64_t volatile *pDest )
{
#if defined(_WIN64) || defined (_X360)
	return InterlockedDecrement64( pDest );
#else
	int64_t Old;

	do 
	{
		Old = *pDest;
	} while (ThreadInterlockedCompareExchange64(pDest, Old - 1, Old) != Old);

	return Old - 1;
#endif
}

int64_t ThreadInterlockedExchangeAdd64( int64_t volatile *pDest, int64_t value )
{
#if defined(_WIN64) || defined (_X360)
	return InterlockedExchangeAdd64( pDest, value );
#else
	int64_t Old;

	do 
	{
		Old = *pDest;
	} while (ThreadInterlockedCompareExchange64(pDest, Old + value, Old) != Old);

	return Old;
#endif
}

int64_t ThreadInterlockedExchange64( int64_t volatile *pDest, int64_t value )
{
#if defined(_WIN64) || defined (_X360)
	return InterlockedExchange64( pDest, value );
#else
	// there's almost certainly a better implementation - this is what
	// we were using before I added the intrinsic under win64
	int64_t Old;
	do 
	{
		Old = *pDest;
	} while (ThreadInterlockedCompareExchange64(pDest, value, Old) != Old);

	return Old;
#endif
}

int64_t ThreadInterlockedCompareExchange64( int64_t volatile *pDest, int64_t value, int64_t comperand )
{
#if defined(_WIN64) || defined (_X360)
	return InterlockedCompareExchange64( pDest, value, comperand );
#else
	__asm 
	{
		lea esi,comperand;
		lea edi,value;

		mov eax,[esi];
		mov edx,4[esi];
		mov ebx,[edi];
		mov ecx,4[edi];
		mov esi,pDest;
		lock CMPXCHG8B [esi];			
	}
#endif
}

bool ThreadInterlockedAssignIf64( int64_t volatile *pDest, int64_t value, int64_t comperand ) 
{
#if defined(_WIN32) && !defined(_WIN64) && !defined(_X360)
	__asm
	{
		lea esi,comperand;
		lea edi,value;

		mov eax,[esi];
		mov edx,4[esi];
		mov ebx,[edi];
		mov ecx,4[edi];
		mov esi,pDest;
		lock CMPXCHG8B [esi];			
		mov eax,0;
		setz al;
	}
#else
	return ( ThreadInterlockedCompareExchange64( pDest, value, comperand ) == comperand ); 
#endif
}

#if defined(X64BITS)

#if _MSC_VER < 1500
// This intrinsic isn't supported on VS2005.
extern "C" unsigned char _InterlockedCompareExchange128( int64_t volatile * Destination, int64_t ExchangeHigh, int64_t ExchangeLow, int64_t * ComparandResult );
#endif


bool ThreadInterlockedAssignIf128( volatile int128 *pDest, const int128 &value, const int128 &comperand )
{
	DbgAssert( ( (size_t)pDest % 16 ) == 0 );

	// Must copy comperand to stack because the intrinsic uses it as an in/out param
	int64_t comperandInOut[2] = { comperand.m128i_i64[0], comperand.m128i_i64[1] };

	// Description:
	//  The CMPXCHG16B instruction compares the 128-bit value in the RDX:RAX and RCX:RBX registers
	//  with a 128-bit memory location. If the values are equal, the zero flag (ZF) is set,
	//  and the RCX:RBX value is copied to the memory location.
	//  Otherwise, the ZF flag is cleared, and the memory value is copied to RDX:RAX.

	// _InterlockedCompareExchange128: http://msdn.microsoft.com/en-us/library/bb514094.aspx
	if ( _InterlockedCompareExchange128( ( volatile int64_t * )pDest, value.m128i_i64[1], value.m128i_i64[0], comperandInOut ) )
		return true;

	return false;
}

#endif

#elif defined(GNUC)


long ThreadInterlockedIncrement( int32_t volatile *pDest )
{
	return __sync_fetch_and_add( pDest, 1 ) + 1;
}

long ThreadInterlockedDecrement( int32_t volatile *pDest )
{
	return __sync_fetch_and_sub( pDest, 1 ) - 1;
}

long ThreadInterlockedExchange( int32_t volatile *pDest, int32_t value )
{
	return __sync_lock_test_and_set( pDest, value );
}

long ThreadInterlockedExchangeAdd( int32_t volatile *pDest, int32_t value )
{
	return  __sync_fetch_and_add( pDest, value );
}

long ThreadInterlockedCompareExchange( int32_t volatile *pDest, int32_t value, int32_t comperand )
{
	return  __sync_val_compare_and_swap( pDest, comperand, value );
}

bool ThreadInterlockedAssignIf( int32_t volatile *pDest, int32_t value, int32_t comperand )
{
	return __sync_bool_compare_and_swap( pDest, comperand, value );
}

void *ThreadInterlockedExchangePointer( void * volatile *pDest, void *value )
{
	return __sync_lock_test_and_set( pDest, value );
}

void *ThreadInterlockedCompareExchangePointer( void *volatile *pDest, void *value, void *comperand )
{	
	return  __sync_val_compare_and_swap( pDest, comperand, value );
}

bool ThreadInterlockedAssignPointerIf( void * volatile *pDest, void *value, void *comperand )
{
	return  __sync_bool_compare_and_swap( pDest, comperand, value );
}

int64_t ThreadInterlockedIncrement64( int64_t volatile *pDest )
{
	return  __sync_fetch_and_add( pDest, 1 );
}

int64_t ThreadInterlockedDecrement64( int64_t volatile *pDest )
{
	return  __sync_fetch_and_sub( pDest, 1 );
}

int64_t ThreadInterlockedExchangeAdd64( int64_t volatile *pDest, int64_t value )
{
	return  __sync_fetch_and_add( pDest, value );
}

int64_t ThreadInterlockedExchange64( int64_t volatile *pDest, int64_t value )
{
	return __sync_lock_test_and_set( pDest, value );
}

int64_t ThreadInterlockedCompareExchange64( int64_t volatile *pDest, int64_t value, int64_t comperand )
{
	return  __sync_val_compare_and_swap( pDest, comperand, value  );
}

bool ThreadInterlockedAssignIf64( int64_t volatile * pDest, int64_t value, int64_t comperand ) 
{
	return __sync_bool_compare_and_swap( pDest, comperand, value );
}

#if defined(X64BITS)

bool ThreadInterlockedAssignIf128( volatile int128 *pDest, const int128 &value, const int128 &comperand ) 
{
	// __sync_bool_compare_and_swap( pDest, comperand, value );
    // as an intrinsic isn't on the Mac as of XCode 4.2.  This implementation will
	// work on any x86_64 gcc build.
    // The gcc 4.6 that we use for Linux does not support this for x86_64 either.
#if !defined(OSX) && !defined(LINUX)
	// try the intrinsic if not on x86_64. Will fail to compile or link if not supported.
	return __sync_bool_compare_and_swap( pDest, comperand, value );
#else
    bool result;

    __int64_t loComp = (__int64_t)comperand;
    __int64_t hiComp = (__int64_t)(comperand >> 64);

    __int64_t loVal = (__int64_t)value;
    __int64_t hiVal = (__int64_t)(value >> 64);

    // http://siyobik.info/main/reference/instruction/CMPXCHG8B%2FCMPXCHG16B
    __asm__
        (
			"lock cmpxchg16b %1\n\t" // %1 bound to pDest
			"setz %0"                // %0 bound to result
			: "=q" ( result )        // '=1' - 'result' is output, any 8 bit register
			  , "+m" ( *pDest )      // '+m' - 'pDest' is in/out memory
			  , "+d" ( hiComp )      // '+d' - 'hiComp' is in/out and goes into RDX
			  , "+a" ( loComp )      // '+a' - 'loComp' is in/out and goes into RAX
			: "c" ( hiVal )          // 'c'  - 'hiVal' is input, goes in RCX
			  , "b" ( loVal )        // 'b'  - 'loVal' is input, goes into RBX
			: "cc"                   // 'cc' - asm block leaves condition codes modified
			);
    return result;
#endif
}
#endif

#elif defined(_PS3)

void *ThreadInterlockedCompareExchangePointer( void * volatile *ea, void *value, void *comperand )
{
	return (void*)cellAtomicCompareAndSwap32( (uint32_t*)ea, (uint32_t)comperand, (uint32_t)value );
}

int64_t ThreadInterlockedIncrement64( int64_t volatile *pDest )
{
	DbgAssert( (size_t)pDest % 8 == 0 );

	int64_t Old;

	do 
	{
		Old = *pDest;
	} while (ThreadInterlockedCompareExchange64(pDest, Old + 1, Old) != Old);

	return Old + 1;
}

int64_t ThreadInterlockedDecrement64( int64_t volatile *pDest )
{
	DbgAssert( (size_t)pDest % 8 == 0 );
	int64_t Old;

	do 
	{
		Old = *pDest;
	} while (ThreadInterlockedCompareExchange64(pDest, Old - 1, Old) != Old);

	return Old - 1;
}

int64_t ThreadInterlockedExchangeAdd64( int64_t volatile *pDest, int64_t value )
{
	DbgAssert( (size_t)pDest % 8 == 0 );
	int64_t Old;

	do 
	{
		Old = *pDest;
	} while (ThreadInterlockedCompareExchange64(pDest, Old + value, Old) != Old);

	return Old;
}

int64_t ThreadInterlockedExchange64( int64_t volatile *pDest, int64_t value )
{
	DbgAssert( (size_t)pDest % 8 == 0 );
	int64_t Old;

	do 
	{
		Old = *pDest;
	} while (ThreadInterlockedCompareExchange64(pDest, value, Old) != Old);

	return Old;
}

#else

#warning "Implement better Interlocked ops"
CThreadMutex g_InterlockedMutex;

long ThreadInterlockedIncrement( int32_t volatile *pDest )
{
	AUTO_LOCK( g_InterlockedMutex );
	return ++(*pDest);
}

long ThreadInterlockedDecrement( int32_t volatile *pDest )
{
	AUTO_LOCK( g_InterlockedMutex );
	return --(*pDest);
}

long ThreadInterlockedExchange( int32_t volatile *pDest, int32_t value )
{
	AUTO_LOCK( g_InterlockedMutex );
	long retVal = *pDest;
	*pDest = value;
	return retVal;
}

void *ThreadInterlockedExchangePointer( void * volatile *pDest, void *value )
{
	AUTO_LOCK( g_InterlockedMutex );
	void *retVal = *pDest;
	*pDest = value;
	return retVal;
}

long ThreadInterlockedExchangeAdd( int32_t volatile *pDest, int32_t value )
{
	AUTO_LOCK( g_InterlockedMutex );
	long retVal = *pDest;
	*pDest += value;
	return retVal;
}

long ThreadInterlockedCompareExchange( int32_t volatile *pDest, int32_t value, int32_t comperand )
{
	AUTO_LOCK( g_InterlockedMutex );
	long retVal = *pDest;
	if ( *pDest == comperand )
		*pDest = value;
	return retVal;
}

int64_t ThreadInterlockedCompareExchange64( int64_t volatile *pDest, int64_t value, int64_t comperand )
{
	AUTO_LOCK( g_InterlockedMutex );
	int64_t retVal = *pDest;
	if ( *pDest == comperand )
		*pDest = value;
	return retVal;
	
}

bool ThreadInterlockedAssignIf64(int64_t volatile *pDest, int64_t value, int64_t comperand ) 
{
	return ( ThreadInterlockedCompareExchange64( pDest, value, comperand ) == comperand ); 
}

void *ThreadInterlockedCompareExchangePointer( void * volatile *pDest, void *value, void *comperand )
{
	AUTO_LOCK( g_InterlockedMutex );
	void *retVal = *pDest;
	if ( *pDest == comperand )
		*pDest = value;
	return retVal;
}
#endif // _WIN32

//-----------------------------------------------------------------------------

#if defined(_WIN32) && defined(THREAD_PROFILER)
void ThreadNotifySyncNoop(void *p) {}

#define MAP_THREAD_PROFILER_CALL( from, to ) \
	void from(void *p) \
	{ \
		static CDynamicFunction<void (*)(void *)> dynFunc( "libittnotify.dll", #to, ThreadNotifySyncNoop ); \
		(*dynFunc)(p); \
	}

MAP_THREAD_PROFILER_CALL( ThreadNotifySyncPrepare, __itt_notify_sync_prepare );
MAP_THREAD_PROFILER_CALL( ThreadNotifySyncCancel, __itt_notify_sync_cancel );
MAP_THREAD_PROFILER_CALL( ThreadNotifySyncAcquired, __itt_notify_sync_acquired );
MAP_THREAD_PROFILER_CALL( ThreadNotifySyncReleasing, __itt_notify_sync_releasing );

#endif

//-----------------------------------------------------------------------------
//
// CThreadMutex
//
//-----------------------------------------------------------------------------

#ifdef _PS3
CThreadMutex::CThreadMutex()
{
	// sys_mutex with recursion enabled is like a win32 critical section
	sys_mutex_attribute_t mutexAttr;
	sys_mutex_attribute_initialize( mutexAttr );
	mutexAttr.attr_recursive = SYS_SYNC_RECURSIVE;
	sys_mutex_create( &m_Mutex, &mutexAttr );
}
CThreadMutex::~CThreadMutex()
{
	sys_mutex_destroy( m_Mutex );
}
#elif !defined( POSIX )
CThreadMutex::CThreadMutex()
{
#ifdef THREAD_MUTEX_TRACING_ENABLED
	memset( &m_CriticalSection, 0, sizeof(m_CriticalSection) );
#endif
	InitializeCriticalSectionAndSpinCount((CRITICAL_SECTION *)&m_CriticalSection, 4000);
#ifdef THREAD_MUTEX_TRACING_SUPPORTED
	// These need to be initialized unconditionally in case mixing release & debug object modules
	// Lock and unlock may be emitted as COMDATs, in which case may get spurious output
	m_currentOwnerID = m_lockCount = 0;
	m_bTrace = false;
#endif
}

CThreadMutex::~CThreadMutex()
{
	DeleteCriticalSection((CRITICAL_SECTION *)&m_CriticalSection);
}
#else // POSIX
CThreadMutex::CThreadMutex()
{
	// enable recursive locks as we need them
	pthread_mutexattr_init( &m_Attr );
	pthread_mutexattr_settype( &m_Attr, PTHREAD_MUTEX_RECURSIVE );
	pthread_mutex_init( &m_Mutex, &m_Attr );
}

//---------------------------------------------------------

CThreadMutex::~CThreadMutex()
{
	pthread_mutex_destroy( &m_Mutex );
}
#endif // POSIX

#ifndef POSIX
bool CThreadMutex::TryLock(int blah)
{
#if defined( _WIN32 )
#ifdef THREAD_MUTEX_TRACING_ENABLED
	uint32_t thisThreadID = ThreadGetCurrentId();
	if ( m_bTrace && m_currentOwnerID && ( m_currentOwnerID != thisThreadID ) )
		Log( "Thread %u about to try-wait for lock %p owned by %u\n", ThreadGetCurrentId(), (CRITICAL_SECTION *)&m_CriticalSection, m_currentOwnerID );
#endif
	if ( TryEnterCriticalSection ( (CRITICAL_SECTION *)&m_CriticalSection ) != FALSE )
	{
#ifdef THREAD_MUTEX_TRACING_ENABLED
		if (m_lockCount == 0)
		{
			// we now own it for the first time.  Set owner information
			m_currentOwnerID = thisThreadID;
			if ( m_bTrace )
				Log( "Thread %u now owns lock 0x%p\n", m_currentOwnerID, (CRITICAL_SECTION *)&m_CriticalSection );
		}
		m_lockCount++;
#endif
		return true;
	}
	return false;
#elif defined( _PS3 )

#ifndef NO_THREAD_SYNC
	if ( sys_mutex_trylock( m_Mutex ) == CELL_OK )
#endif

		return true;

	return false; // ?? moved from EA code

#else
#Log "Implement me!"
	return true;
#endif
}
#endif

//-----------------------------------------------------------------------------
//
// CThreadSpinLock
//
//-----------------------------------------------------------------------------
#if defined( _WIN32 ) || defined( POSIX ) || defined( _PS3 )
void CThreadSpinLock::Lock( const ThreadId_t threadId ) volatile
{
	// first trying spinning on the lock
	for ( int i = 1000; i != 0; --i )
	{
		if ( TryLock( threadId ) )
			return;
		
		ThreadPause();
	}

	// didn't get lock. To prevent a large number of threads from continuing to spin, restrict
	// the lock to one spinning thread. Rest can wait on a mutex
	{
		(const_cast<CThreadSpinLock *>(this))->m_mutex.Lock();
		for ( ;; )
		{
			if ( TryLock( threadId ) )
				break;

			ThreadPause();
		}
		(const_cast<CThreadSpinLock *>(this))->m_mutex.Unlock();
	}
}
#endif

//-----------------------------------------------------------------------------
//
// CThreadRWLock
//
//-----------------------------------------------------------------------------

void CThreadRWLock::WaitForRead()
{
	m_nPendingReaders++;

	do
	{
		m_mutex.Unlock();
		m_CanRead.Wait();
		m_mutex.Lock();
	}
	while (m_nWriters);

	m_nPendingReaders--;
}


void CThreadRWLock::LockForWrite()
{
	m_mutex.Lock();
	bool bWait = ( m_nWriters != 0 || m_nActiveReaders != 0 );
	m_nWriters++;
	m_CanRead.Reset();
	m_mutex.Unlock();

	if ( bWait )
	{
		m_CanWrite.Wait();
	}
}

void CThreadRWLock::UnlockWrite()
{
	m_mutex.Lock();
	m_nWriters--;
	if ( m_nWriters == 0)
	{
		if ( m_nPendingReaders )
		{
			m_CanRead.Set();
		}
	}
	else
	{
		m_CanWrite.Set();
	}
	m_mutex.Unlock();
}

//-----------------------------------------------------------------------------
//
// CThreadSpinRWLock
//
//-----------------------------------------------------------------------------

void CThreadSpinRWLock::SpinLockForWrite( const uint32_t threadId )
{
	int i;

	for ( i = 1000; i != 0; --i )
	{
		if ( TryLockForWrite( threadId ) )
		{
			return;
		}
		ThreadPause();
	}

	for ( i = 20000; i != 0; --i )
	{
		if ( TryLockForWrite( threadId ) )
		{
			return;
		}

		ThreadPause();
		ThreadSleep( 0 );
	}

	for ( ;; ) // coded as for instead of while to make easy to breakpoint success
	{
		if ( TryLockForWrite( threadId ) )
		{
			return;
		}

		ThreadPause();
		ThreadSleep( 1 );
	}
}

void CThreadSpinRWLock::LockForRead()
{
	int i;

	// In order to grab a read lock, the number of readers must not change and no thread can own the write lock
	LockInfo_t oldValue;
	LockInfo_t newValue;

	oldValue.m_nReaders = m_lockInfo.m_nReaders;
	oldValue.m_writerId = 0;
	newValue.m_nReaders = oldValue.m_nReaders + 1;
	newValue.m_writerId = 0;

	if( m_nWriters == 0 && AssignIf( newValue, oldValue ) )
		return;
	ThreadPause();
	oldValue.m_nReaders = m_lockInfo.m_nReaders;
	newValue.m_nReaders = oldValue.m_nReaders + 1;

	for ( i = 1000; i != 0; --i )
	{
		if( m_nWriters == 0 && AssignIf( newValue, oldValue ) )
			return;
		ThreadPause();
		oldValue.m_nReaders = m_lockInfo.m_nReaders;
		newValue.m_nReaders = oldValue.m_nReaders + 1;
	}

	for ( i = 20000; i != 0; --i )
	{
		if( m_nWriters == 0 && AssignIf( newValue, oldValue ) )
			return;
		ThreadPause();
		ThreadSleep( 0 );
		oldValue.m_nReaders = m_lockInfo.m_nReaders;
		newValue.m_nReaders = oldValue.m_nReaders + 1;
	}

	for ( ;; ) // coded as for instead of while to make easy to breakpoint success
	{
		if( m_nWriters == 0 && AssignIf( newValue, oldValue ) )
			return;
		ThreadPause();
		ThreadSleep( 1 );
		oldValue.m_nReaders = m_lockInfo.m_nReaders;
		newValue.m_nReaders = oldValue.m_nReaders + 1;
	}
}

void CThreadSpinRWLock::UnlockRead()
{
	int i;

	Assert( m_lockInfo.m_nReaders > 0 && m_lockInfo.m_writerId == 0 );
	LockInfo_t oldValue;
	LockInfo_t newValue;

	oldValue.m_nReaders = m_lockInfo.m_nReaders;
	oldValue.m_writerId = 0;
	newValue.m_nReaders = oldValue.m_nReaders - 1;
	newValue.m_writerId = 0;

	if( AssignIf( newValue, oldValue ) )
		return;
	ThreadPause();
	oldValue.m_nReaders = m_lockInfo.m_nReaders;
	newValue.m_nReaders = oldValue.m_nReaders - 1;

	for ( i = 500; i != 0; --i )
	{
		if( AssignIf( newValue, oldValue ) )
			return;
		ThreadPause();
		oldValue.m_nReaders = m_lockInfo.m_nReaders;
		newValue.m_nReaders = oldValue.m_nReaders - 1;
	}

	for ( i = 20000; i != 0; --i )
	{
		if( AssignIf( newValue, oldValue ) )
			return;
		ThreadPause();
		ThreadSleep( 0 );
		oldValue.m_nReaders = m_lockInfo.m_nReaders;
		newValue.m_nReaders = oldValue.m_nReaders - 1;
	}

	for ( ;; ) // coded as for instead of while to make easy to breakpoint success
	{
		if( AssignIf( newValue, oldValue ) )
			return;
		ThreadPause();
		ThreadSleep( 1 );
		oldValue.m_nReaders = m_lockInfo.m_nReaders;
		newValue.m_nReaders = oldValue.m_nReaders - 1;
	}
}

void CThreadSpinRWLock::UnlockWrite()
{
	Assert( m_lockInfo.m_writerId == ThreadGetCurrentId()  && m_lockInfo.m_nReaders == 0 );
	static const LockInfo_t newValue = { 0, 0 };
#if defined(_X360)
	// X360TBD: Serious Perf implications, not yet. __sync();
#endif
	ThreadInterlockedExchange64(  (int64_t *)&m_lockInfo, *((int64_t *)&newValue) );
	m_nWriters--;
}

#ifdef _PS3

PLATFORM_INTERFACE CThread *GetCurThreadPS3()
{
	return (CThread*)g_pCurThread;
}

#else // !_PS3

CThreadLocalPtr<CThread> g_pCurThread;

#ifdef POSIX

CThreadLocalInt<intptr_t> g_nCurThreadSuspendCount;
bool g_bSetSuspendHandlers;
bool g_bSuspendResumeAck;
CThreadMutex g_SuspendResumeLock;

#define SIG_SUSPEND SIGUSR1
#define SIG_RESUME SIGUSR2

void ThreadSuspendSignal( int nSig )
{
    if ( ++g_nCurThreadSuspendCount > 1 )
    {
        // Already suspended.
        g_bSuspendResumeAck = true;
        return;
    }

    g_bSuspendResumeAck = true;

    // Sleep until we get a resume signal.
    do
    {
        ThreadSleep( 5 );
    }
    while ( g_nCurThreadSuspendCount > 0 );
}

void ThreadResumeSignal( int nSig )
{
    if ( g_nCurThreadSuspendCount > 0 )
    {
        --g_nCurThreadSuspendCount;
    }
    else
    {
        AssertMsg1( false, "Ignoring resume of count %d\n",
                    (int)g_nCurThreadSuspendCount );
    }

    g_bSuspendResumeAck = true;
}

#endif // POSIX

#endif // _PS3

//-----------------------------------------------------------------------------
//
// CThread
//
//-----------------------------------------------------------------------------

CThread::CThread()
:	
#ifdef _WIN32
	m_hThread( NULL ),
    m_threadId( 0 ),
#elif defined( _PS3 )
    m_hThread( NULL ),
	m_threadId( SYS_PPU_THREAD_ID_INVALID ),
	m_eventThreadExit( true ), // manual reset
#elif defined( POSIX )
    m_hThread( 0 ),
	m_threadId( 0 ),
#endif
	m_result( 0 ), m_bExitQuietly( false )
{
	m_szName[0] = 0;
}

//---------------------------------------------------------

CThread::~CThread()
{
	if ( BHasValidThreadID() )
	{
		int nWaitForThread = 50; // wait up to 1 second for thread to go away

		// we can't check the OS thread handle since it
		// might never go away even if the thread is finished
		// in case we are in a DLL unload sequence.
		bool bIsAlive = GetResult() == -1;
		while ( nWaitForThread && bIsAlive )
		{
			::ThreadSleep( 20 );
			bIsAlive = GetResult() == -1;
			nWaitForThread--;
		}

		// CWorkerThreads must negotiate an end to the worker before the CWorkerThread object is destroyed
		AssertMsg1( !bIsAlive, "Illegal termination of worker thread '%s'", GetName() );
		AssertMsg( GetCurrentCThread() != this, "Deleting thread object from the thread, this is bad" );

		if ( ( bIsAlive || IsPosix() ) && GetCurrentCThread() != this )
			Join(); // if you hang here you must have forgotten to shutdown your thread, always join on posix so
					// we don't leak thread stacks

		// Now that the worker thread has exited (which we know because we presumably waited
		// on the thread handle for it to exit) we can finally close the thread handle. We
		// cannot do this any earlier, and certainly not in CThread::ThreadProc().
		ReleaseThreadHandle( m_hThread );
		m_hThread = 0;
		m_threadId = 0;
	}
}


//---------------------------------------------------------

const char *CThread::GetName()
{
	if ( !m_szName[0] )
	{
		snprintf( m_szName, sizeof(m_szName) - 1, "Thread(0x%p/0x%p/0x%x)",
                   this,
                   (void*)m_hThread,
                   (uint32_t)m_threadId );
		m_szName[sizeof(m_szName) - 1] = 0;
	}
	return m_szName;
}

//---------------------------------------------------------

void CThread::SetName(const char *pszName)
{
	strncpy( m_szName, pszName, sizeof(m_szName) - 1 );
	m_szName[sizeof(m_szName) - 1] = 0;
}

//---------------------------------------------------------

bool CThread::Start( unsigned int nBytesStack )
{
	if ( IsAlive() )
	{
		AssertMsg( 0, "Tried to create a thread that has already been created!" );
		return false;
	}

	bool bInitSuccess = false;
	CThreadEvent createComplete;
	ThreadInit_t init = { this, &createComplete, &bInitSuccess };

#ifdef _WIN32
	m_hThread = CreateThread( NULL,
                              nBytesStack,
                              (LPTHREAD_START_ROUTINE)GetThreadProc(),
                              new ThreadInit_t(init),
                              0,
                              (LPDWORD)&m_threadId );

	if ( !m_hThread )
	{
		AssertMsg1( 0, "Failed to create thread (Log 0x%x)", GetLastError() );
		return false;
	}
#elif defined( _PS3 )
	//EAPS3: On the PS3, a stack size of 0 doesn't imply a default stack size, so we need to force it to our
	//		 own default size of 64k.
	if (nBytesStack == 0)
	{
		nBytesStack = k_nThreadStackLarge;
	}

	//The thread is about to begin
	m_eventThreadExit.Reset();
	m_threadId = SYS_PPU_THREAD_ID_INVALID;

	const char* threadName = m_szName;
	if ( sys_ppu_thread_create( &m_threadId, (void(*)(uint64_t))GetThreadProc(), (uint64_t)(new ThreadInit_t( init )), 1001, nBytesStack, SYS_PPU_THREAD_CREATE_JOINABLE, threadName ) != CELL_OK )
	{
		AssertMsg1( 0, "Failed to create thread (Log 0x%x)", errno );
		m_eventThreadExit.Set();
		m_threadId = SYS_PPU_THREAD_ID_INVALID;
		return false;
	}

	bInitSuccess = true;
#elif defined(POSIX)
    g_SuspendResumeLock.Lock();
    
    if ( !g_bSetSuspendHandlers )
    {
        struct sigaction SigAction;
        
        SigAction.sa_handler = &ThreadSuspendSignal;
        SigAction.sa_flags = SA_NODEFER;
        sigemptyset( &SigAction.sa_mask );
        if ( sigaction( SIG_SUSPEND, &SigAction, NULL ) != 0 )
        {
            return false;
        }

        SigAction.sa_handler = &ThreadResumeSignal;
        if ( sigaction( SIG_RESUME, &SigAction, NULL ) != 0 )
        {
            return false;
        }
        
        g_bSetSuspendHandlers = true;
    }

    g_SuspendResumeLock.Unlock();
    
	pthread_attr_t attr;
	pthread_attr_init( &attr );
	pthread_attr_setstacksize( &attr, std::max( nBytesStack, 1024u*1024 ) );
	pthread_t pthread;
	if ( pthread_create( &pthread, &attr, (void *(*)(void *))GetThreadProc(), new ThreadInit_t( init ) ) != 0 )
	{
		AssertMsg1( 0, "Failed to create thread (Log 0x%x)", GetLastError() );
		return false;
	}
    m_hThread = pthread;
#ifdef OSX
    m_threadId = pthread_mach_thread_np( pthread );
#else
    // Thread ID will be written back to m_threadID by the
    // thread itself as part of creation.  We wait until
    // creation completes so we know that'll be valid before we return.
#endif
	
	bInitSuccess = true;
#endif


	// You MUST wait for the thread to spin up before returning 
	// because we pass bInitSuccess, a stack variable, into the thread create process
	// and it sets it. If we don't wait then when the thread does finally spin up it smashes
	// a random part of the stack. Yay huh?
	if ( !WaitForCreateComplete( &createComplete ) )
	{
		Log( "Thread failed to initialize\n" );
#ifdef _WIN32
		HANDLE hThread = m_hThread;
		m_hThread = NULL;
		::CloseHandle( hThread );
        m_threadId = 0;
#elif defined( _PS3 )
		m_eventThreadExit.Set();
        m_hThread = 0;
		m_threadId = SYS_PPU_THREAD_ID_INVALID;
#elif defined(POSIX)
        m_hThread = 0;
		m_threadId = 0;
#endif
		return false;
	}

	if ( !bInitSuccess )
	{
		Log( "Thread failed to initialize\n" );
#ifdef _WIN32
		HANDLE hThread = m_hThread;
		m_hThread = NULL;
		::CloseHandle( hThread );
        m_threadId = 0;
#elif defined( _PS3 )
		m_eventThreadExit.Set();
        m_hThread = 0;
		m_threadId = SYS_PPU_THREAD_ID_INVALID;
#elif defined(POSIX)
        m_hThread = 0;
		m_threadId = 0;
#endif
		return false;
	}

#ifdef _WIN32
	if ( !m_hThread )
	{
		Log( "Thread exited immediately\n" );
	}
#endif

	return BHasValidThreadID();
}

//---------------------------------------------------------
//
// Return true if the thread exists. false otherwise
//

bool CThread::IsAlive()
{
#ifdef _WIN32
	DWORD dwExitCode;

	return ( BHasValidThreadID() 
			 && ::GetExitCodeThread( m_hThread, &dwExitCode ) 
			 && dwExitCode == STILL_ACTIVE
			);
#elif defined(POSIX)
	return BHasValidThreadID() && IsThreadRunning();
#endif
}


//---------------------------------------------------------
// Purpose: Return true if we have a valid thread id
//---------------------------------------------------------
bool CThread::BHasValidThreadID()
{
#ifdef _WIN32
	return m_hThread != NULL;
#elif defined( _PS3 )
	return m_threadId != SYS_PPU_THREAD_ID_INVALID;
#elif defined(POSIX)
	return m_threadId != 0;
#endif
}


//---------------------------------------------------------
// Purpose: Return true if the thread is still running
//---------------------------------------------------------
bool CThread::IsThreadRunning()
{
    if ( !BHasValidThreadID() )
    {
        return false;
    }

#if defined(_PS3)
	// ThreadIsThreadIdRunning() doesn't work on PS3 if the thread is in a zombie state
	return m_eventThreadExit.Check();
#elif defined(_WIN32)
    DWORD dwExitCode;
    if( !::GetExitCodeThread( m_hThread, &dwExitCode ) || dwExitCode != STILL_ACTIVE )
        return false;
	return true;
#elif defined(POSIX)
    return pthread_kill( m_hThread, 0 ) == 0;
#endif
}


//---------------------------------------------------------
// Purpose: Joins current thread with this one
//---------------------------------------------------------
bool CThread::Join( unsigned int nMillisecondsTimeout )
{
	if ( !BHasValidThreadID() )
		return true;
	
	AssertMsg( GetCurrentCThread() != this, "Thread cannot be joined with self" );

#ifdef _WIN32

	DWORD dwWait = WaitForSingleObject( m_hThread, nMillisecondsTimeout );
	if ( dwWait == WAIT_TIMEOUT)
		return false;
	if ( dwWait != WAIT_OBJECT_0 && ( dwWait != WAIT_FAILED && GetLastError() != 0 ) )
	{
		Assert( 0 );
		return false;
	}
	return true;

#elif defined(_PS3)

	if ( nMillisecondsTimeout == TT_INFINITE )
		return BThreadJoinPosixInternal( m_threadId );

	if ( !m_eventThreadExit.Wait( nMillisecondsTimeout ) )
		return false;

	return BThreadJoinPosixInternal( m_threadId );

#elif defined( POSIX )

	if ( nMillisecondsTimeout == TT_INFINITE )
	{
		bool bResult = BThreadJoinPosixInternal( m_hThread );

		// A thread should not be referenced after a successful join,
		// because join is what destroys all the bookeeping info, etc.
		// We must clear the handle
        m_hThread = 0;
		m_threadId = 0;
		return bResult;
	}

	unsigned int unTimeLeftMS = nMillisecondsTimeout;
	do 
	{
		if ( !IsThreadRunning() )
		{
			bool bResult = BThreadJoinPosixInternal( m_hThread );

			// Clear handle --- see above.
            m_hThread = 0;
			m_threadId = 0;
			return bResult;
		}

		unsigned int unSleepMS = Min( 50u, unTimeLeftMS );
		ThreadSleep( unSleepMS );
		unTimeLeftMS -= unSleepMS;
	} 
	while ( unTimeLeftMS > 0 );

	return false;

#else
#Log "CThread::Join not implemented"
#endif
}

//---------------------------------------------------------

int CThread::GetResult() const
{
	return m_result;
}

//---------------------------------------------------------
//
// Forcibly, abnormally, but relatively cleanly stop the thread
//

void CThread::Stop(int exitCode)
{
	if ( !IsAlive() )
		return;


	if ( GetCurrentCThread() == this )
	{
#if defined( _PS3 )
		AssertMsg( false, "CThread::Stop not implemented" );
#else
		m_result = exitCode;
		OnExit();
		g_pCurThread = 0;
		throw exitCode;
#endif // PS3
	}
	else
		AssertMsg( 0, "Only thread can stop self: Use a higher-level protocol");
}

//---------------------------------------------------------

int CThread::GetPriority() const
{
	return ThreadGetPriority( m_hThread );
}

//---------------------------------------------------------

bool CThread::SetPriority(int priority)
{
	return ThreadSetPriority( m_hThread, priority );
}

//---------------------------------------------------------

#if defined(POSIX)
static bool SuspendResumePthread( pthread_t hThread, int nSignal )
{
    bool bRet;
    
    //
    // pthreads don't support suspend/resume so we have to hack
    // up an approximation.  This is by no means a robust solution
    // but it's good enough for the very limited use that Steam
    // makes of Suspend/Resume (such as suspending worker threads
    // temporarily during Validate).
    //

    // We have to order suspends and resumes so we hold
    // a global lock.  This also lets us use global variables
    // for signalling with the thread.
    g_SuspendResumeLock.Lock();

    // There shouldn't be any other suspend/resume occuring.
    Assert( g_bSuspendResumeAck == false );
    // Something else must have timed out, stomp on them
    // and forge ahead.
    g_bSuspendResumeAck = false;

    // Try and get control of the target thread.
    bRet = pthread_kill( hThread, nSignal ) == 0;

    // We have to wait here to see if the thread acknowledges
    // the signal, otherwise we have no way of knowing if
    // the thread has stopped or not.
    int nRetries = 1000;
    while ( nRetries-- > 0 )
    {
        if ( g_bSuspendResumeAck )
        {
            g_bSuspendResumeAck = false;
            break;
        }

        ThreadSleep( 1 );
    }

    Assert( nRetries > 0 );
    
    g_SuspendResumeLock.Unlock();

    return bRet;
}
#endif

unsigned CThread::Suspend()
{
#if defined(_WIN32)
	return ::SuspendThread( m_hThread ) != 0;
#elif defined(POSIX)
    return SuspendResumePthread( m_hThread, SIG_SUSPEND );
#else
	AssertMsg ( false, "CThread::Suspend not implemented on platform" );
	return 0;
#endif
}

//---------------------------------------------------------

unsigned CThread::Resume()
{
#if defined(_WIN32)
	return ( ::ResumeThread( m_hThread ) != 0 );
#elif defined(POSIX)
    return SuspendResumePthread( m_hThread, SIG_RESUME );
#elif defined( _PS3 )
	return 0;
#else
	AssertMsg ( false, "CThread::Resume not implemented on platform" );
	return 0;
#endif
}

//---------------------------------------------------------

bool CThread::Terminate(int exitCode)
{
#ifdef _X360
	AssertMsg( 0, "Cannot terminate a thread on the Xbox!" );
	return false;
#endif

#ifdef _WIN32
	Log( "WARNING! CThread::Terminate: %s\n", GetName() );

	// I hope you know what you're doing!
	if (!TerminateThread(m_hThread, exitCode))
		return false;
	HANDLE hThread = m_hThread;
	m_hThread = NULL;
	CloseHandle( hThread );
	m_threadId = 0;
#elif defined( _PS3 )
	// we can't really stop anything here...
	m_eventThreadExit.Set();
    m_hThread = 0;
	m_threadId = SYS_PPU_THREAD_ID_INVALID;
#elif defined(POSIX)
	pthread_kill( m_hThread, SIGKILL );
    m_hThread = 0;
	m_threadId = 0;
#endif

	return true;
}

//---------------------------------------------------------
//
// Get the Thread object that represents the current thread, if any.
// Can return NULL if the current thread was not created using
// CThread
//

CThread *CThread::GetCurrentCThread()
{
#ifdef _PS3
	return GetCurThreadPS3();
#else
	return g_pCurThread;
#endif
}

//---------------------------------------------------------
//
// Offer a context switch. Under Win32, equivalent to Sleep(0)
//

void CThread::Yield()
{
	ThreadYield();
}

void CThread::Sleep( unsigned int nMilliseconds )
{
#ifdef _WIN32
	::Sleep( nMilliseconds );
#elif defined (_PS3)
	sys_timer_usleep( nMilliseconds * 1000 );
#elif POSIX
	usleep( nMilliseconds * 1000 );
#endif
}


//---------------------------------------------------------

bool CThread::Init()
{
	return true;
}

#if defined( _PS3 )
int CThread::Run()
{
	return -1;
}
#endif // _PS3 

//---------------------------------------------------------

void CThread::OnExit()
{
}

//---------------------------------------------------------
bool CThread::WaitForCreateComplete(CThreadEvent * pEvent)
{
	// Force serialized thread creation...
	if (!pEvent->Wait(60000))
	{
		AssertMsg( 0, "Probably deadlock or failure waiting for thread to initialize." );
		return false;
	}
	return true;
}

//---------------------------------------------------------

CThread::ThreadProc_t CThread::GetThreadProc()
{
	return ThreadProc;
}

//---------------------------------------------------------

#if defined( _PS3 )

void *CThread::ThreadProc( void *pv )
{
	ThreadInit_t *pInit = reinterpret_cast<ThreadInit_t*>(pv);

	CThread *pThread = pInit->pThread;
	pThread->m_pStackBase = AlignValue( &pThread, 4096 );

    pThread->m_hThread = pthread_self();
	pInit->pThread->m_result = -1;

	if ( IsPosix() )
	{
		*(pInit->pfInitSuccess) = pInit->pThread->Init();
	}
	else
	{
		*(pInit->pfInitSuccess) = pInit->pThread->Init();
	}

	bool bInitSuccess = *(pInit->pfInitSuccess);
	pInit->pInitCompleteEvent->Set();
	if (!bInitSuccess)
	{
		pThread->m_eventThreadExit.Set();
		return 0;
	}

	pInit->pThread->m_result = pInit->pThread->Run();

	pInit->pThread->OnExit();

	pThread->m_eventThreadExit.Set();
	sys_ppu_thread_exit(0);

	return (void*)pInit->pThread->m_result;
}

#else

unsigned __stdcall CThread::ThreadProc(LPVOID pv)
{
	std::auto_ptr<ThreadInit_t> pInit((ThreadInit_t *)pv);

	CThread *pThread = pInit->pThread;

    if (strlen( pThread->m_szName ) )
        ThreadSetDebugName( pThread->m_szName );

#ifdef LINUX
    pThread->m_threadId = syscall( SYS_gettid );
#endif
    
	g_pCurThread = pThread;

	g_pCurThread->m_pStackBase = AlignValue( &pThread, 4096 );

	pInit->pThread->m_result = -1;
	
	bool bInitSuccess = true;
	if ( pInit->pfInitSuccess )
		*(pInit->pfInitSuccess) = false;

	try
	{
		bInitSuccess = pInit->pThread->Init();
	}

	catch (...)
	{
		pInit->pInitCompleteEvent->Set();
		throw;
	}

	if ( pInit->pfInitSuccess )
		 *(pInit->pfInitSuccess) = bInitSuccess;
	pInit->pInitCompleteEvent->Set();
	if (!bInitSuccess)
		return 0;

	pThread->m_result = pThread->Run();

	pInit->pThread->OnExit();
	g_pCurThread = 0;

	// we can't closehandle() here because on win32 you are meant to WaitForSingleObject() 
	// for the thread to exit, it shouldn't close itself
	// you also can't null the threadid as you don't want our owning object to be destroyed underneath us
	return pInit->pThread->m_result;
}


//-----------------------------------------------------------------------------
// another static wrapper for Exception handling
//-----------------------------------------------------------------------------
void CThread::ThreadExceptionWrapper( void *object )
{
	CThread *pThread = (CThread *)object;
	pThread->m_result = pThread->Run();
}

#endif // _PS3


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
#ifdef _WIN32
CWorkerThread::CWorkerThread()
:	m_EventSend(true),                 // must be manual-reset for PeekCall()
	m_EventComplete(true),             // must be manual-reset to handle multiple wait with thread properly
	m_Param(0),
	m_ReturnVal(0)
{
}

//---------------------------------------------------------

int CWorkerThread::CallWorker(unsigned dw, unsigned timeout, bool fBoostWorkerPriorityToMaster)
{
	return Call(dw, timeout, fBoostWorkerPriorityToMaster);
}

//---------------------------------------------------------

int CWorkerThread::CallMaster(unsigned dw, unsigned timeout)
{
	return Call(dw, timeout, false);
}

//---------------------------------------------------------

HANDLE CWorkerThread::GetCallHandle()
{
	return m_EventSend;
}

//---------------------------------------------------------

unsigned CWorkerThread::GetCallParam() const
{
	return m_Param;
}

//---------------------------------------------------------

int CWorkerThread::BoostPriority()
{
	int iInitialPriority = GetPriority();
	const int iNewPriority = ::GetThreadPriority( ::GetCurrentThread() );
	if (iNewPriority > iInitialPriority)
		SetPriority(iNewPriority);
	return iInitialPriority;
}

//---------------------------------------------------------

static uint32_t __stdcall DefaultWaitFunc( uint32_t nHandles, const HANDLE*pHandles, int bWaitAll, uint32_t timeout )
{
#ifdef _WIN32
	return WaitForMultipleObjects( nHandles, pHandles, bWaitAll, timeout );
#elif defined( _PS3 )
	return CThreadSyncObject::WaitForMultiple( nHandles, (CThreadSyncObject **)ppHandles, bWaitAll, timeout );
#endif
}


int CWorkerThread::Call(unsigned dwParam, unsigned timeout, bool fBoostPriority, WaitFunc_t pfnWait)
{
	AssertMsg(!m_EventSend.Check(), "Cannot perform call if there's an existing call pending" );

	#if !defined( _PS3 )
	AUTO_LOCK( m_Lock );
	#else
	CHECK_NOT_MULTITHREADED();
	#endif

	if (!IsAlive())
		return WTCR_FAIL;

	int iInitialPriority = 0;
	if (fBoostPriority)
	{
		iInitialPriority = BoostPriority();
	}

	// set the parameter, signal the worker thread, wait for the completion to be signaled
	m_Param = dwParam;

	m_EventComplete.Reset();
	m_EventSend.Set();

	WaitForReply( timeout, pfnWait );

	if (fBoostPriority)
		SetPriority(iInitialPriority);

	return m_ReturnVal;
}

//---------------------------------------------------------
//
// Wait for a request from the client
//
//---------------------------------------------------------
int CWorkerThread::WaitForReply( unsigned timeout )
{
	return WaitForReply( timeout, NULL );
}

int CWorkerThread::WaitForReply( unsigned timeout, WaitFunc_t pfnWait )
{
	if (!pfnWait)
	{
		pfnWait = DefaultWaitFunc;
	}

	HANDLE waits[] =
	{
		GetThreadHandle(),
		m_EventComplete
	};

	unsigned result;
	bool bInDebugger = Plat_IsInDebugSession();

	do
	{
		// Make sure the thread handle hasn't been closed
		if ( !GetThreadHandle() )
		{
			result = WAIT_OBJECT_0 + 1;
			break;
		}

		result = (*pfnWait)((sizeof(waits) / sizeof(waits[0])), waits, false,
			(timeout != TT_INFINITE) ? timeout : 30000);

		AssertMsg(timeout != TT_INFINITE || result != WAIT_TIMEOUT, "Possible hung thread, call to thread timed out");

	} while ( bInDebugger && ( timeout == TT_INFINITE && result == WAIT_TIMEOUT ) );

	if ( result != WAIT_OBJECT_0 + 1 )
	{
		if (result == WAIT_TIMEOUT)
			m_ReturnVal = WTCR_TIMEOUT;
		else if (result == WAIT_OBJECT_0)
		{
			Log( "Thread failed to respond, probably exited\n");
			m_EventSend.Reset();
			m_ReturnVal = WTCR_TIMEOUT;
		}
		else
		{
			m_EventSend.Reset();
			m_ReturnVal = WTCR_THREAD_GONE;
		}
	}

	return m_ReturnVal;
}


//---------------------------------------------------------
//
// Wait for a request from the client
//
//---------------------------------------------------------

bool CWorkerThread::WaitForCall(unsigned * pResult)
{
	return WaitForCall(TT_INFINITE, pResult);
}

//---------------------------------------------------------

bool CWorkerThread::WaitForCall(unsigned dwTimeout, unsigned * pResult)
{
	bool returnVal = m_EventSend.Wait(dwTimeout);
	if (pResult)
		*pResult = m_Param;
	return returnVal;
}

//---------------------------------------------------------
//
// is there a request?
//

bool CWorkerThread::PeekCall(unsigned * pParam)
{
	if (!m_EventSend.Check())
	{
		return false;
	}
	else
	{
		if (pParam)
		{
			*pParam = m_Param;
		}
		return true;
	}
}

//---------------------------------------------------------
//
// Reply to the request
//

void CWorkerThread::Reply(unsigned dw)
{
	m_Param = 0;
	m_ReturnVal = dw;

	// The request is now complete so PeekCall() should fail from
	// now on
	//
	// This event should be reset BEFORE we signal the client
	m_EventSend.Reset();

	// Tell the client we're finished
	m_EventComplete.Set();
}
#endif // _WIN32

//-----------------------------------------------------------------------------

#if defined( _PS3 )

/*******************************************************************************
* PS3 equivalent to Win32 function for setting events
*******************************************************************************/
BOOL SetEvent( CThreadEvent *pEvent )
{
	bool bRetVal = pEvent->Set();
	if ( !bRetVal )
		Assert(0);

	return bRetVal;
}

/*******************************************************************************
* PS3 equivalent to Win32 function for resetting events
*******************************************************************************/
BOOL ResetEvent( CThreadEvent *pEvent )
{
	return pEvent->Reset();
}

#endif
