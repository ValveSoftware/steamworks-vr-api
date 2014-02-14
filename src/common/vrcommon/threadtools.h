//========== Copyright 2005, Valve Corporation, All rights reserved. ========
//
// Purpose: A collection of utility classes to simplify thread handling, and
//			as much as possible contain portability problems. Here avoiding 
//			including windows.h.
//
//=============================================================================

#ifndef THREADTOOLS_H
#define THREADTOOLS_H
#pragma once



#include "hmdplatform_private.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef POSIX
#include <pthread.h>
#ifdef USE_BSD_SEMAPHORES
#include <semaphore.h>
typedef sem_t* sem_type;
#else
typedef int sem_type;
#endif

#endif

#include "vrlog.h"
#include "vrassert.h"

#if defined(X64BITS) && defined(_WIN32)
// for __m128i
#include <emmintrin.h>
#endif

#if !defined( _PS3 )
#pragma warning(push)
#pragma warning(disable:4251)
#pragma warning(disable:4324)
#endif

// #define THREAD_PROFILER 1

#define TT_INTERFACE	extern "C"
#define TT_OVERLOAD	
#define TT_CLASS		

#ifndef _RETAIL
#define THREAD_MUTEX_TRACING_SUPPORTED
#if defined(_WIN32) && defined(_DEBUG)
#define THREAD_MUTEX_TRACING_ENABLED
#endif
#endif

#ifdef _WIN32
typedef void *HANDLE;
#endif

#if defined( _PS3 )

// maximum number of threads that can wait on one object
#define CTHREADEVENT_MAX_WAITING_THREADS	4

#define USE_INTRINSIC_INTERLOCKED

#define CHECK_NOT_MULTITHREADED()														\
{																						\
	static int init = 0;																\
	static sys_ppu_thread_t threadIDPrev;												\
																						\
	if (!init)																			\
	{																					\
		sys_ppu_thread_get_id(&threadIDPrev);											\
		init = 1;																		\
	}																					\
	else if (gbCheckNotMultithreaded)													\
	{																					\
		sys_ppu_thread_t threadID;														\
		sys_ppu_thread_get_id(&threadID);												\
		if (threadID != threadIDPrev)													\
		{																				\
			printf("CHECK_NOT_MULTITHREADED: prev thread = %x, cur thread = %x\n",		\
			(uint32_t)threadIDPrev, (uint32_t)threadID);										\
			*(int*)0 = 0;																\
		}																				\
	}																					\
}

#else // !_PS3
#define CHECK_NOT_MULTITHREADED()
#endif // _PS3

#if defined( _PS3 )
#define MAX_THREADS_SUPPORTED 16

// when allocating through Portal 2, they could have 6-10k of stack used during malloc!
// the stack sizes below are larger than they need to be to account for these unexpected stack allocs
const int k_nThreadStackTiny = 16 * 1024;
const int k_nThreadStackSmall = 16 * 1024;
const int k_nThreadStackMedium = 64 * 1024;
const int k_nThreadStackLarge = 64 * 1024;

#else

// default sizes
const int k_nThreadStackTiny = 0;
const int k_nThreadStackSmall = 0;
const int k_nThreadStackMedium = 0;
const int k_nThreadStackLarge = 0;

#endif

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

const unsigned TT_INFINITE = 0xffffffff;

#ifndef THREAD_LOCAL
#ifdef _WIN32
#define THREAD_LOCAL __declspec(thread)
#elif defined(GNUC)
#define THREAD_LOCAL __thread
#endif
#endif


#if defined(_PS3)
typedef pthread_t ThreadHandle_t;
typedef sys_ppu_thread_t ThreadId_t;
#elif defined(_WIN32)
typedef HANDLE ThreadHandle_t;
typedef unsigned int ThreadId_t;
#elif defined(LINUX)
typedef pthread_t ThreadHandle_t;
// We deliberately use the real Linux thread ID here so
// that we can keep thread IDs integers.  Note that
// thread IDs aren't very useful then, and most things
// will need the thread handle, which is a pthread_t.
typedef unsigned int ThreadId_t;
#elif defined(OSX)
typedef pthread_t ThreadHandle_t;
// We use the Mach thread port to keep it integral.
// The thread handle is a pthread_t.
typedef unsigned int ThreadId_t;
#else
#error "Create threadid type"
#endif

// A thread reference is a lightweight reference that identifies a thread
// in a useful way for a particular platform.  It exists for code
// that doesn't want to have to open a thread handle and then be forced
// to close it.  It does not keep the thread alive or around.
// There may be multiple flavors of references to better fit behavior
// for a particular purpose.  For example, for querying whether a thread
// is still running or not on Linux it's safer to use a thread ID
// than a pthread_t since the pthread_t can be deallocated if somebody
// joins or detaches the thread.
#if defined(_WIN32) || defined(_PS3) || defined(LINUX)
typedef ThreadId_t ThreadRunningRef_t;
#else
typedef ThreadHandle_t ThreadRunningRef_t;
#endif

//-----------------------------------------------------------------------------
//
// Simple thread creation. Differs from VCR mode/CreateThread/_beginthreadex
// in that it accepts a standard C function rather than compiler specific one.
//
//-----------------------------------------------------------------------------

typedef unsigned (*ThreadFunc_t)( void *pParam );

#if defined( _PS3 )
#define TT_DEFAULT_STACKSIZE 0x10000 /*64*/
#else
#define TT_DEFAULT_STACKSIZE 0
#endif

TT_OVERLOAD ThreadHandle_t CreateSimpleThread( ThreadFunc_t, void *pParam, ThreadId_t *pID, unsigned stackSize = TT_DEFAULT_STACKSIZE );
TT_INTERFACE ThreadHandle_t CreateSimpleThread( ThreadFunc_t, void *pParam, unsigned stackSize = TT_DEFAULT_STACKSIZE );
TT_INTERFACE bool ReleaseThreadHandle( ThreadHandle_t );

//-----------------------------------------------------------------------------

TT_INTERFACE void ThreadSleep( unsigned long nMilliseconds = 0 );
TT_INTERFACE ThreadId_t ThreadGetCurrentId();
// Can return a pseudo-handle instead of a real handle.
TT_INTERFACE ThreadHandle_t ThreadGetCurrentHandle();
TT_INTERFACE ThreadRunningRef_t ThreadGetCurrentRunningRef();
TT_INTERFACE bool ThreadIsThreadRunning( ThreadRunningRef_t ThreadRef );
TT_INTERFACE int ThreadGetPriority( ThreadHandle_t hThread = NULL );
TT_INTERFACE bool ThreadSetPriority( ThreadHandle_t hThread, int priority );
inline		 bool ThreadSetPriority( int priority ) { return ThreadSetPriority( 0, priority ); }
TT_INTERFACE bool ThreadTerminate( ThreadHandle_t hThread );
TT_INTERFACE bool ThreadInMainThread();
TT_INTERFACE void DeclareCurrentThreadIsMainThread();


//-----------------------------------------------------------------------------
//
// Process handling wrapper functions
//
//-----------------------------------------------------------------------------

#ifdef _WIN32
typedef HANDLE ProcessHandle_t;
#define INVALID_PROCESS_HANDLE INVALID_HANDLE_VALUE
#else
// Process "handle" is the process ID.
typedef uint32_t ProcessHandle_t;
#define INVALID_PROCESS_HANDLE 0
#endif

enum ESimpleProcessFlags
{
    k_ESimpleProcessDefault  = 0x00000000,
    k_ESimpleProcessNoWindow = 0x00000001,
};

// Returns zero on success or platform error code.
// pHandle is guaranteed to be INVALID_PROCESS_HANDLE on failure.
TT_INTERFACE int CreateSimpleProcess( const char *pCommandLine, uint32_t nFlags, ProcessHandle_t *pHandle );

TT_INTERFACE ProcessHandle_t ThreadGetCurrentProcessHandle();
TT_INTERFACE ProcessHandle_t ThreadOpenProcess( uint32_t dwProcessId );
TT_INTERFACE bool ThreadCloseProcess( ProcessHandle_t hProcess );

TT_INTERFACE uint32_t ThreadGetCurrentProcessId();
TT_INTERFACE bool ThreadIsProcessActive( uint32_t dwProcessId );
// Note that not all platforms support providing an exit code so it
// may be silently ignored.  The ThreadTerminateProcess macro lets
// you explicitly indicate when you don't care about the code.
TT_INTERFACE bool ThreadTerminateProcessCode( uint32_t dwProcessId, int32_t nExitCode );
#define ThreadTerminateProcess( IdOrHandle ) ThreadTerminateProcessCode( IdOrHandle, 0 )
TT_INTERFACE bool ThreadGetProcessExitCode( uint32_t dwProcessId, int32_t *pExitCode );
#ifdef _WIN32
TT_OVERLOAD  bool ThreadIsProcessActive( ProcessHandle_t hProcess );
TT_OVERLOAD  bool ThreadTerminateProcessCode( ProcessHandle_t hProcess, int32_t nExitCode );
TT_OVERLOAD  bool ThreadGetProcessExitCode( ProcessHandle_t hProcess, int32_t *pExitCode );
#endif
TT_INTERFACE bool ThreadWaitForProcessExit( ProcessHandle_t hProcess, uint32_t nMillis );
TT_INTERFACE uint32_t ThreadShellExecute(  const char *lpApplicationName, const char *pchCommandLine, const char *pchCurrentDirectory ); // returns procID on success, otherwise 0 

struct SThreadProcessInfo
{
    uint32_t nProcessId;
    uint32_t nParentProcessId;
};

TT_INTERFACE int ThreadGetProcessListInfo( int nEntriesMax, SThreadProcessInfo *pEntries );

#if defined( _WIN32 ) && !defined( _WIN64 ) && !defined( _X360 )
extern "C" unsigned long __declspec(dllimport) __stdcall GetCurrentThreadId();
#define ThreadGetCurrentId GetCurrentThreadId
#endif

inline void ThreadPause()
{
#if defined( COMPILER_PS3 )
	__db16cyc();
#elif _WIN64
	;
#elif defined( _WIN32 ) && !defined( _X360 )
	__asm pause;
#elif defined(GNUC)
	__asm __volatile("pause");
#elif defined( _X360 )
#else
#error "implement me"
#endif
}

TT_INTERFACE void ThreadSetDebugName( const char *pszName );

TT_INTERFACE void ThreadSetAffinity( ThreadHandle_t hThread, int nAffinityMask );

//-----------------------------------------------------------------------------

enum ThreadWaitResult_t
{
	TW_FAILED = 0xffffffff, // WAIT_FAILED
	TW_TIMEOUT = 0x00000102, // WAIT_TIMEOUT
	TW_OBJECT_0 = 0x00000000, // WAIT_OBJECT_0
};

#ifdef _WIN32
TT_INTERFACE int ThreadWaitForObjects( int nEvents, const HANDLE *pHandles, bool bWaitAll = true, unsigned timeout = TT_INFINITE );
inline int ThreadWaitForObject( HANDLE handle, bool bWaitAll = true, unsigned timeout = TT_INFINITE ) { return ThreadWaitForObjects( 1, &handle, bWaitAll, timeout ); }
#endif

//-----------------------------------------------------------------------------
//
// Interlock methods. These perform very fast atomic thread
// safe operations. These are especially relevant in a multi-core setting.
//
//-----------------------------------------------------------------------------

#if defined(_WIN32) && !defined(_X360) && !defined( _PS3 )
#if ( _MSC_VER >= 1310 )
#define USE_INTRINSIC_INTERLOCKED
#endif
#endif

#if defined( _PS3 )
#define ThreadMemoryBarrier() __lwsync()
#ifdef USE_INTRINSIC_INTERLOCKED
	#undef USE_INTRINSIC_INTERLOCKED
#endif
#else
#define ThreadMemoryBarrier() ((void)0)
#endif


#if !defined( _PS3 )
#ifdef USE_INTRINSIC_INTERLOCKED
extern "C"
{
	long __cdecl _InterlockedIncrement(volatile long*);
	long __cdecl _InterlockedDecrement(volatile long*);
	long __cdecl _InterlockedExchange(volatile long*, long);
	long __cdecl _InterlockedExchangeAdd(volatile long*, long);
	long __cdecl _InterlockedCompareExchange(volatile long*, long, long);
}

#pragma intrinsic( _InterlockedCompareExchange )
#pragma intrinsic( _InterlockedDecrement )
#pragma intrinsic( _InterlockedExchange )
#pragma intrinsic( _InterlockedExchangeAdd ) 
#pragma intrinsic( _InterlockedIncrement )

inline long ThreadInterlockedIncrement( int32_t volatile *p )										{ DbgAssert( (size_t)p % 4 == 0 ); return _InterlockedIncrement( (long volatile *)p ); }
inline long ThreadInterlockedDecrement( int32_t volatile *p )										{ DbgAssert( (size_t)p % 4 == 0 ); return _InterlockedDecrement( (long volatile *)p ); }
inline long ThreadInterlockedExchange( int32_t volatile *p, int32_t value )							{ DbgAssert( (size_t)p % 4 == 0 ); return _InterlockedExchange( (long volatile *)p, (long)value ); }
inline long ThreadInterlockedExchangeAdd( int32_t volatile *p, int32_t value )						{ DbgAssert( (size_t)p % 4 == 0 ); return _InterlockedExchangeAdd( (long volatile *)p, (long)value ); }
inline long ThreadInterlockedCompareExchange( int32_t volatile *p, int32_t value, int32_t comperand )	{ DbgAssert( (size_t)p % 4 == 0 ); return _InterlockedCompareExchange( (long volatile *)p, (long)value, (long)comperand ); }
inline bool ThreadInterlockedAssignIf( int32_t volatile *p, int32_t value, int32_t comperand )		{ DbgAssert( (size_t)p % 4 == 0 ); return ( _InterlockedCompareExchange( (long volatile *)p, (long)value, (long)comperand ) == comperand ); }
#else
TT_INTERFACE long ThreadInterlockedIncrement( int32_t volatile * );
TT_INTERFACE long ThreadInterlockedDecrement( int32_t volatile * );
TT_INTERFACE long ThreadInterlockedExchange( int32_t volatile *, int32_t value );
TT_INTERFACE long ThreadInterlockedExchangeAdd( int32_t volatile *, int32_t value );
TT_INTERFACE long ThreadInterlockedCompareExchange( int32_t volatile *, int32_t value, int32_t comperand );
TT_INTERFACE bool ThreadInterlockedAssignIf( int32_t volatile *, int32_t value, int32_t comperand );
#endif // USE_INTRINSIC_INTERLOCKED
#endif // _PS3

#if defined ( _PS3 )
TT_INTERFACE inline int32_t ThreadInterlockedIncrement( int32_t volatile * ea ) { return cellAtomicIncr32( (uint32_t*)ea ) + 1; }
TT_INTERFACE inline int32_t ThreadInterlockedDecrement( int32_t volatile * ea ) { return cellAtomicDecr32( (uint32_t*)ea ) - 1; }
TT_INTERFACE inline int32_t ThreadInterlockedExchange( int32_t volatile * ea, int32_t value ) { return cellAtomicStore32( ( uint32_t* )ea, value); }
TT_INTERFACE inline int32_t ThreadInterlockedExchangeAdd( int32_t volatile * ea, int32_t value ) { return cellAtomicAdd32( ( uint32_t* )ea, value ); }
TT_INTERFACE inline int32_t ThreadInterlockedCompareExchange( int32_t volatile * ea, int32_t value, int32_t comperand ) { return cellAtomicCompareAndSwap32( (uint32_t*)ea, comperand, value ) ; }
TT_INTERFACE inline bool ThreadInterlockedAssignIf( int32_t volatile * ea, int32_t value, int32_t comperand ) { return ( cellAtomicCompareAndSwap32( (uint32_t*)ea, comperand, value ) == ( uint32_t ) comperand );  }

TT_INTERFACE inline int64_t ThreadInterlockedCompareExchange64( int64_t volatile *pDest, int64_t value, int64_t comperand ) {	return cellAtomicCompareAndSwap64( ( uint64_t* ) pDest, comperand, value ); }
TT_INTERFACE inline bool ThreadInterlockedAssignIf64( volatile int64_t *pDest, int64_t value, int64_t comperand ) { return ( cellAtomicCompareAndSwap64( ( uint64_t* ) pDest, comperand, value ) == ( uint64_t ) comperand ); }

TT_INTERFACE void *ThreadInterlockedCompareExchangePointer( void * volatile *ea, void *value, void *comperand );
#endif

inline int32_t ThreadInterlockedExchangeSubtract( int32_t volatile *p, int32_t value )	{ return ThreadInterlockedExchangeAdd( p, -value ); }

#if defined( USE_INTRINSIC_INTERLOCKED ) && !defined( _WIN64 )
#define TIPTR()
inline void *ThreadInterlockedExchangePointer( void * volatile *p, void *value )							{ COMPILE_TIME_ASSERT(sizeof(void*) == sizeof(int32_t)); return (void *)_InterlockedExchange( reinterpret_cast<long volatile *>(p), reinterpret_cast<long>(value) ); }
inline void *ThreadInterlockedCompareExchangePointer( void * volatile *p, void *value, void *comperand )	{ COMPILE_TIME_ASSERT(sizeof(void*) == sizeof(int32_t)); return (void *)_InterlockedCompareExchange( reinterpret_cast<long volatile *>(p), reinterpret_cast<long>(value), reinterpret_cast<long>(comperand) ); }
inline bool ThreadInterlockedAssignPointerIf( void * volatile *p, void *value, void *comperand )			{ COMPILE_TIME_ASSERT(sizeof(void*) == sizeof(int32_t)); return ( _InterlockedCompareExchange( reinterpret_cast<long volatile *>(p), reinterpret_cast<long>(value), reinterpret_cast<long>(comperand) ) == reinterpret_cast<long>(comperand) ); }
#else
TT_INTERFACE void *ThreadInterlockedExchangePointer( void * volatile *, void *value );
TT_INTERFACE void *ThreadInterlockedCompareExchangePointer( void * volatile *, void *value, void *comperand );
TT_INTERFACE bool ThreadInterlockedAssignPointerIf( void * volatile *, void *value, void *comperand );

#endif

inline void const *ThreadInterlockedExchangePointerToConst( void const * volatile *p, void const *value )							{ return ThreadInterlockedExchangePointer( const_cast < void * volatile * > ( p ), const_cast < void * > ( value ) );  }
inline void const *ThreadInterlockedCompareExchangePointerToConst( void const * volatile *p, void const *value, void const *comperand )	{ return ThreadInterlockedCompareExchangePointer( const_cast < void * volatile * > ( p ), const_cast < void * > ( value ), const_cast < void * > ( comperand ) ); }
inline bool ThreadInterlockedAssignPointerToConstIf( void const * volatile *p, void const *value, void const *comperand )			{ return ThreadInterlockedAssignPointerIf( const_cast < void * volatile * > ( p ), const_cast < void * > ( value ), const_cast < void * > ( comperand ) ); }

#if defined( X64BITS )
#if defined (_WIN32) 
typedef __m128i int128;
inline int128 int128_zero()	{ return _mm_setzero_si128(); }
#else
typedef __int128_t int128;
#define int128_zero() 0
#endif

TT_INTERFACE bool ThreadInterlockedAssignIf128( volatile int128 *pDest, const int128 &value, const int128 &comperand );

#endif

TT_INTERFACE int64_t ThreadInterlockedIncrement64( int64_t volatile * );
TT_INTERFACE int64_t ThreadInterlockedDecrement64( int64_t volatile * );

TT_INTERFACE int64_t ThreadInterlockedCompareExchange64( int64_t volatile *, int64_t value, int64_t comperand );
TT_INTERFACE int64_t ThreadInterlockedExchange64( int64_t volatile *, int64_t value );

TT_INTERFACE int64_t ThreadInterlockedExchangeAdd64( int64_t volatile *, int64_t value );
TT_INTERFACE bool ThreadInterlockedAssignIf64( int64_t volatile *pDest, int64_t value, int64_t comperand );

inline unsigned ThreadInterlockedExchangeSubtract( uint32_t volatile *p, uint32_t value )	{ return ThreadInterlockedExchangeSubtract( (int32_t volatile *)p, value ); }
inline unsigned ThreadInterlockedIncrement( uint32_t volatile *p )	{ return ThreadInterlockedIncrement( (int32_t volatile *)p ); }
inline unsigned ThreadInterlockedDecrement( uint32_t volatile *p )	{ return ThreadInterlockedDecrement( (int32_t volatile *)p ); }
inline unsigned ThreadInterlockedExchange( uint32_t volatile *p, uint32_t value )	{ return ThreadInterlockedExchange( (int32_t volatile *)p, value ); }
inline unsigned ThreadInterlockedExchangeAdd( uint32_t volatile *p, uint32_t value )	{ return ThreadInterlockedExchangeAdd( (int32_t volatile *)p, value ); }
inline unsigned ThreadInterlockedCompareExchange( uint32_t volatile *p, uint32_t value, unsigned comperand )	{ return ThreadInterlockedCompareExchange( (int32_t volatile *)p, value, comperand ); }
inline bool ThreadInterlockedAssignIf( uint32_t volatile *p, uint32_t value, uint32_t comperand )	{ return ThreadInterlockedAssignIf( (int32_t volatile *)p, value, comperand ); }

//-----------------------------------------------------------------------------
// Access to VTune thread profiling
//-----------------------------------------------------------------------------
#if defined(_WIN32) && defined(THREAD_PROFILER)
TT_INTERFACE void ThreadNotifySyncPrepare(void *p);
TT_INTERFACE void ThreadNotifySyncCancel(void *p);
TT_INTERFACE void ThreadNotifySyncAcquired(void *p);
TT_INTERFACE void ThreadNotifySyncReleasing(void *p);
#else
#define ThreadNotifySyncPrepare(p)		((void)0)
#define ThreadNotifySyncCancel(p)		((void)0)
#define ThreadNotifySyncAcquired(p)		((void)0)
#define ThreadNotifySyncReleasing(p)	((void)0)
#endif

#if !defined( NO_THREAD_LOCAL ) && defined( _PS3 )
// PS3 totally supports compiler thread locals, even across dll's.
#define PLAT_COMPILER_SUPPORTED_THREADLOCALS 1
#define CTHREADLOCALINTEGER( typ ) __thread int
#define CTHREADLOCALINT __thread int
#define CTHREADLOCALPTR( typ ) __thread typ *
#define CTHREADLOCAL( typ ) __thread typ
#define GETLOCAL( x ) ( x )
#endif // _PS3

#if defined(POSIX) && !defined(_PS3)
// Unix OS's have a real runtime loader for binaries so we need to make sure our exported C++ symbols don't collide with the Source engine, so wrap em in a namespace 
// to "managle" the symbol name to be unique
namespace SteamThreadTools
{
#endif

//-----------------------------------------------------------------------------
// Encapsulation of a thread local datum (needed because THREAD_LOCAL doesn't
// work in a DLL loaded with LoadLibrary()
//-----------------------------------------------------------------------------

#ifndef __AFXTLS_H__ // not compatible with some Windows headers

#ifndef NO_THREAD_LOCAL

class TT_CLASS CThreadLocalBase
{
public:
	CThreadLocalBase();
	~CThreadLocalBase();

	void * Get() const;
	void   Set(void *);

private:
#if defined(_WIN32) || defined(_PS3)
	uint32_t m_index;
#elif defined(POSIX) 
	pthread_key_t m_index;
#endif
};

//---------------------------------------------------------

template <class T>
class CThreadLocal : public CThreadLocalBase
{
public:
	CThreadLocal()
	{
		COMPILE_TIME_ASSERT( sizeof(T) == sizeof(void *) );
	}

	T Get() const
	{
		return reinterpret_cast<T>(CThreadLocalBase::Get());
	}

	void Set(T val)
	{
		CThreadLocalBase::Set(reinterpret_cast<void *>(val));
	}
};

//---------------------------------------------------------

template <class T>
class CThreadLocalStackCounter
{
public:
	CThreadLocalStackCounter( CThreadLocal<T> &threadLocal )
		: m_threadLocalRef( threadLocal )
	{
		Assert( &m_threadLocalRef == &threadLocal );
		m_cReentrancyCount = m_threadLocalRef.Get();
		Assert( m_cReentrancyCount >= 0 );
		m_cReentrancyCount++;
		m_threadLocalRef.Set( m_cReentrancyCount );	
	}

	~CThreadLocalStackCounter( )
	{
		m_cReentrancyCount--;
		Assert( m_cReentrancyCount >= 0 );
		m_threadLocalRef.Set( m_cReentrancyCount );
	}

	T Get() const
	{
		return m_cReentrancyCount;
	}

protected:
	CThreadLocal<T> &m_threadLocalRef;
	T m_cReentrancyCount;
};

//---------------------------------------------------------

template <class T = intptr_t>
class CThreadLocalInt : public CThreadLocal<T>
{
public:
	CThreadLocalInt()
	{
		COMPILE_TIME_ASSERT( sizeof(T) >= sizeof(int) );
	}

	operator int() const { return (int)this->Get(); }
	int	operator=( int i ) { this->Set( (intptr_t)i ); return i; }

	int operator++()					{ T i = this->Get(); this->Set( ++i ); return (int)i; }
	int operator++(int)				{ T i = this->Get(); this->Set( i + 1 ); return (int)i; }

	int operator--()					{ T i = this->Get(); this->Set( --i ); return (int)i; }
	int operator--(int)				{ T i = this->Get(); this->Set( i - 1 ); return (int)i; }
};

//---------------------------------------------------------

template <class T>
class CThreadLocalPtr : private CThreadLocalBase
{
public:
	CThreadLocalPtr() {}

	operator const void *() const          					{ return (T *)Get(); }
	operator void *()                      					{ return (T *)Get(); }

	operator const T *() const							    { return (T *)Get(); }
	operator const T *()          							{ return (T *)Get(); }
	operator T *()											{ return (T *)Get(); }

	T *			operator=( T *p )							{ Set( p ); return p; }

	bool        operator !() const							{ return (!Get()); }
#ifndef __LP64__
    // 64 bit gcc likes to use __null as the target of NULL, and it
    // behaves badly in ptr/int overload matching.
	int			operator=( int i )							{ AssertMsg( i == 0, "Only NULL allowed on integer assign" ); Set( NULL ); return 0; }
	bool        operator!=( int i ) const					{ AssertMsg( i == 0, "Only NULL allowed on integer compare" ); return (Get() != NULL); }
	bool        operator==( int i ) const					{ AssertMsg( i == 0, "Only NULL allowed on integer compare" ); return (Get() == NULL); }

	bool		operator==( const void *p ) const			{ return (Get() == p); }
	bool		operator!=( const void *p ) const			{ return (Get() != p); }
#endif
	bool		operator==( const T *p ) const				{ return (Get() == p); }
	bool		operator!=( const T *p ) const				{ return (Get() != p); }

	T *  		operator->()								{ return (T *)Get(); }
	T &  		operator *()								{ return *((T *)Get()); }

	const T *   operator->() const							{ return (T *)Get(); }
	const T &   operator *() const							{ return *((T *)Get()); }

	const T &	operator[]( int i ) const					{ return *((T *)Get() + i); }
	T &			operator[]( int i )							{ return *((T *)Get() + i); }

    // Another way to get around gcc's issues with handling of NULL.
    bool        IsNull() const                              { return Get() == NULL; }
    bool        IsNonNull() const                           { return Get() != NULL; }
    void        SetNull()                                   { Set( NULL ); }

private:
	// Disallowed operations
	CThreadLocalPtr( T *pFrom );
	CThreadLocalPtr( const CThreadLocalPtr<T> &from );
	T **operator &();
	T * const *operator &() const;
	void operator=( const CThreadLocalPtr<T> &from );
	bool operator==( const CThreadLocalPtr<T> &p ) const;
	bool operator!=( const CThreadLocalPtr<T> &p ) const;
};

#endif // NO_THREAD_LOCAL
#endif // !__AFXTLS_H__


//-----------------------------------------------------------------------------
//
// A super-fast thread-safe integer A simple class encapsulating the notion of an 
// atomic integer used across threads that uses the built in and faster 
// "interlocked" functionality rather than a full-blown mutex. Useful for simple 
// things like reference counts, etc.
//
//-----------------------------------------------------------------------------

template <typename T>
class CInterlockedIntT
{
private:
public:
	CInterlockedIntT() : m_value( 0 ) 				{ COMPILE_TIME_ASSERT( sizeof(T) == sizeof(int32_t) ); }
	CInterlockedIntT( T value ) : m_value( value ) 	{}

	T GetRaw() const				{ return m_value; }

	operator T() const				{ return m_value; }

	bool operator!() const			{ return ( m_value == 0 ); }
	bool operator==( T rhs ) const	{ return ( m_value == rhs ); }
	bool operator!=( T rhs ) const	{ return ( m_value != rhs ); }

	T operator++()					{ return (T)ThreadInterlockedIncrement( (int32_t *)&m_value ); }
	T operator++(int)				{ return operator++() - 1; }

	T operator--()					{ return (T)ThreadInterlockedDecrement( (int32_t *)&m_value ); }
	T operator--(int)				{ return operator--() + 1; }

	bool AssignIf( T conditionValue, T newValue )	{ return ThreadInterlockedAssignIf( (int32_t *)&m_value, (int32_t)newValue, (int32_t)conditionValue ); }

	T operator=( T newValue )		{ ThreadInterlockedExchange((int32_t *)&m_value, newValue); return m_value; }

	void operator+=( T add )		{ ThreadInterlockedExchangeAdd( (int32_t *)&m_value, (int32_t)add ); }
	void operator-=( T subtract )	{ operator+=( -subtract ); }
	void operator*=( T multiplier )	{ 
		T original, result; 
		do 
		{ 
			original = m_value; 
			result = original * multiplier; 
		} while ( !AssignIf( original, result ) );
	}
	void operator/=( T divisor )	{ 
		T original, result; 
		do 
		{ 
			original = m_value; 
			result = original / divisor;
		} while ( !AssignIf( original, result ) );
	}

	T operator+( T rhs ) const		{ return m_value + rhs; }
	T operator-( T rhs ) const		{ return m_value - rhs; }

private:
	volatile T m_value;
};

typedef CInterlockedIntT<int32_t> CInterlockedInt;
typedef CInterlockedIntT<uint32_t> CInterlockedUInt;

//-----------------------------------------------------------------------------

template <typename T>
class CInterlockedPtr
{
public:
	CInterlockedPtr() : m_value( 0 ) {} 
	CInterlockedPtr( T *value ) : m_value( value ) 	{}

	operator T *() const			{ return m_value; }

	bool operator!() const			{ return ( m_value == 0 ); }
	bool operator==( T *rhs ) const	{ return ( m_value == rhs ); }
	bool operator!=( T *rhs ) const	{ return ( m_value != rhs ); }

#ifdef X64BITS
	T *operator++()					{ return ((T *)ThreadInterlockedExchangeAdd64( (int64_t *)&m_value, sizeof(T) )) + 1; }
	T *operator++(int)				{ return (T *)ThreadInterlockedExchangeAdd64( (int64_t *)&m_value, sizeof(T) ); }

	T *operator--()					{ return ((T *)ThreadInterlockedExchangeAdd64( (int64_t *)&m_value, -sizeof(T) )) - 1; }
	T *operator--(int)				{ return (T *)ThreadInterlockedExchangeAdd64( (int64_t *)&m_value, -sizeof(T) ); }

	bool AssignIf( T *conditionValue, T *newValue )	{ return ThreadInterlockedAssignPointerToConstIf( (void const **) &m_value, (void const *) newValue, (void const *) conditionValue ); }

	T *operator=( T *newValue )		{ ThreadInterlockedExchangePointerToConst( (void const **) &m_value, (void const *) newValue ); return newValue; }

	void operator+=( int add )		{ ThreadInterlockedExchangeAdd64( (int64_t *)&m_value, add * sizeof(T) ); }
#else
	T *operator++()					{ return ((T *)ThreadInterlockedExchangeAdd( (int32_t *)&m_value, sizeof(T) )) + 1; }
	T *operator++(int)				{ return (T *)ThreadInterlockedExchangeAdd( (int32_t *)&m_value, sizeof(T) ); }

	T *operator--()					{ return ((T *)ThreadInterlockedExchangeAdd( (int32_t *)&m_value, -sizeof(T) )) - 1; }
	T *operator--(int)				{ return (T *)ThreadInterlockedExchangeAdd( (int32_t *)&m_value, -sizeof(T) ); }

	bool AssignIf( T *conditionValue, T *newValue )	{ return ThreadInterlockedAssignPointerToConstIf( (void const **) &m_value, (void const *) newValue, (void const *) conditionValue ); }

	T *operator=( T *newValue )		{ ThreadInterlockedExchangePointerToConst( (void const **) &m_value, (void const *) newValue ); return newValue; }

	void operator+=( int add )		{ ThreadInterlockedExchangeAdd( (int32_t *)&m_value, add * sizeof(T) ); }
#endif

	void operator-=( int subtract )	{ operator+=( -subtract ); }

	T *operator+( int rhs ) const		{ return m_value + rhs; }
	T *operator-( int rhs ) const		{ return m_value - rhs; }
	T *operator+( unsigned rhs ) const	{ return m_value + rhs; }
	T *operator-( unsigned rhs ) const	{ return m_value - rhs; }
	size_t operator-( T *p ) const		{ return m_value - p; }
	size_t operator-( const CInterlockedPtr<T> &p ) const	{ return m_value - p.m_value; }

private:
	T * volatile m_value;
};


//-----------------------------------------------------------------------------
//
// Platform independent for critical sections management
//
//-----------------------------------------------------------------------------

class TT_CLASS CThreadMutex
{
public:
	CThreadMutex();
	~CThreadMutex();

	//------------------------------------------------------
	// Mutex acquisition/release. Const intentionally defeated.
	//------------------------------------------------------
	void Lock();
	void Lock() const		{ (const_cast<CThreadMutex *>(this))->Lock(); }
	void Unlock();
	void Unlock() const		{ (const_cast<CThreadMutex *>(this))->Unlock(); }

#ifdef POSIX
	bool TryLock(int blah)	volatile;	// We need the extra parameter here because Linux thinks it's ambiguous to have only const and volatile distinguish the	two.
#else
	bool TryLock(int blah);				// We need the extra parameter here because Linux thinks it's ambiguous to have only const and volatile distinguish the	two.
#endif
	bool TryLock() const	{ return (const_cast<CThreadMutex *>(this))->TryLock(3); }

	//------------------------------------------------------
	// Use this to make deadlocks easier to track by asserting
	// when it is expected that the current thread owns the mutex
	//------------------------------------------------------
	bool AssertOwnedByCurrentThread();

	//------------------------------------------------------
	// Enable tracing to track deadlock problems
	//------------------------------------------------------
	void SetTrace( bool );

private:
	// Disallow copying
	CThreadMutex( const CThreadMutex & );
	CThreadMutex &operator=( const CThreadMutex & );

#if defined( _WIN32 )
	// Efficient solution to breaking the windows.h dependency, invariant is tested.
#ifdef _WIN64
	#define TT_SIZEOF_CRITICALSECTION 40	
#else
#ifndef _XBOX
	#define TT_SIZEOF_CRITICALSECTION 24
#else
	#define TT_SIZEOF_CRITICALSECTION 28
#endif // !_XBOX
#endif // _WIN64
	uint8_t m_CriticalSection[TT_SIZEOF_CRITICALSECTION];
#elif defined( _PS3 )
	sys_mutex_t m_Mutex;
#elif defined(POSIX)
	pthread_mutex_t m_Mutex;
	pthread_mutexattr_t m_Attr;
#else
#error
#endif

#ifdef THREAD_MUTEX_TRACING_SUPPORTED
	// Debugging (always here to allow mixed debug/release builds w/o changing size)
	uint32_t	m_currentOwnerID;
	uint16_t	m_lockCount;
	bool	m_bTrace;
#endif
};

//-----------------------------------------------------------------------------
//
// An alternative mutex that is useful for cases when thread contention is 
// rare, but a mutex is required. Instances should be declared volatile.
// Sleep of 0 may not be sufficient to keep high priority threads from starving 
// lesser threads. This class is not a suitable replacement for a critical
// section if the resource contention is high.
//
//-----------------------------------------------------------------------------

#if ( defined(_WIN32) || defined( _PS3 ) || defined( POSIX ) ) && !defined(THREAD_PROFILER)

class CThreadSpinLock
{
public:
	static const int k_nMinSpinSleepTime = 2;

	CThreadSpinLock(): m_ownerID( 0 ), m_depth( 0 )
	{
	}

private:
	FORCEINLINE bool TryLockInline( const ThreadId_t threadId ) volatile
	{
#ifdef _PS3
		COMPILE_TIME_ASSERT( sizeof( ThreadId_t ) == sizeof( int64_t ) );
		if ( threadId != m_ownerID && !ThreadInterlockedAssignIf64( (volatile int64_t *)&m_ownerID, (int64_t)threadId, 0 ) )
#else
		COMPILE_TIME_ASSERT( sizeof( ThreadId_t ) == sizeof( int32_t ) );
		if ( threadId != m_ownerID && !ThreadInterlockedAssignIf( &m_ownerID, threadId, 0 ) )
#endif
			return false;

		++m_depth;
		return true;
	}

	bool TryLock( const ThreadId_t threadId ) volatile
	{
		return TryLockInline( threadId );
	}

	TT_CLASS void Lock( const ThreadId_t threadId ) volatile;

public:
	bool TryLock() volatile
	{
#ifdef _DEBUG
		if ( m_depth == INT_MAX )
			DebuggerBreak();

		if ( m_depth < 0 )
			DebuggerBreak();
#endif
		return TryLockInline( ThreadGetCurrentId() );
	}

#ifndef _DEBUG
	FORCEINLINE 
#endif
	
	void Lock() volatile
	{
		const ThreadId_t threadId = ThreadGetCurrentId();

		if ( !TryLockInline( threadId ) )
		{
			ThreadPause();
			Lock( threadId );
		}
#ifdef _DEBUG
		if ( m_ownerID != ThreadGetCurrentId() )
			DebuggerBreak();

		if ( m_depth == INT_MAX )
			DebuggerBreak();

		if ( m_depth < 0 )
			DebuggerBreak();
#endif
	}

#ifndef _DEBUG
	FORCEINLINE 
#endif
		void Unlock() volatile
	{
#ifdef _DEBUG
		if ( m_ownerID != ThreadGetCurrentId() )
			DebuggerBreak();

		if ( m_depth <= 0 )
			DebuggerBreak();
#endif

		--m_depth;
		if ( !m_depth )
		{
#ifdef _PS3
			ThreadInterlockedExchange64( (volatile int64_t *)&m_ownerID, 0 );
#else
			ThreadInterlockedExchange( &m_ownerID, 0 );
#endif
		}
	}

	bool TryLock() const volatile							{ return (const_cast<CThreadSpinLock *>(this))->TryLock(); }
	void Lock() const volatile	{ (const_cast<CThreadSpinLock *>(this))->Lock(); }
	void Unlock() const	volatile							{ (const_cast<CThreadSpinLock *>(this))->Unlock(); }

	// To match regular CThreadMutex:
	bool AssertOwnedByCurrentThread()	{ return true; }
	void SetTrace( bool )				{}

	uint32_t GetOwnerId() const			{ return m_ownerID;	}
	int	GetDepth() const				{ return m_depth; }
private:
	volatile ThreadId_t	m_ownerID;
	volatile int m_depth;
	CThreadMutex m_mutex;
};

class ALIGN128 CAlignedThreadFastMutex : public CThreadSpinLock
{
public:
	CAlignedThreadFastMutex()
	{
		DbgAssert( (size_t)this % 128 == 0 && sizeof(*this) == 128 );
	}

private:
	uint8_t pad[128-sizeof(CThreadSpinLock)];
} ALIGN128_POST;

#else
#if defined( _PS3 )
class CThreadSpinLock
{
public:
	CThreadSpinLock();
	~CThreadSpinLock();

	//------------------------------------------------------
	// Mutex acquisition/release. Const intentionally defeated.
	//------------------------------------------------------
	void Lock();
	void Lock() const		{ (const_cast<CThreadSpinLock *>(this))->Lock(); }
	void Unlock();
	void Unlock() const		{ (const_cast<CThreadSpinLock *>(this))->Unlock(); }

	bool TryLock();
	bool TryLock() const	{ return (const_cast<CThreadSpinLock *>(this))->TryLock(); }

	//------------------------------------------------------
	// Use this to make deadlocks easier to track by asserting
	// when it is expected that the current thread owns the mutex
	//------------------------------------------------------
	bool AssertOwnedByCurrentThread();

	//------------------------------------------------------
	// Enable tracing to track deadlock problems
	//------------------------------------------------------
	void SetTrace( bool );

private:
	// Disallow copying
	CThreadSpinLock( const CThreadSpinLock & );
	//CThreadSpinLock &operator=( const CThreadSpinLock & );
	sys_lwmutex_t m_Mutex;
	sys_mutex_t m_SlowMutex;
};

class ALIGN128 CAlignedThreadFastMutex : public CThreadSpinLock
{
public:
	CAlignedThreadFastMutex()
	{
		DbgAssert( (size_t)this % 128 == 0 && sizeof(*this) == 128 );
	}

private:
	uint8_t pad[128-sizeof(CThreadSpinLock)];
} ALIGN128_POST;
#endif // _PS3

typedef CThreadMutex CThreadSpinLock;
#endif

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

class CThreadNullMutex
{
public:
	static void Lock()				{}
	static void Unlock()			{}

	static bool TryLock()			{ return true; }
	static bool AssertOwnedByCurrentThread() { return true; }
	static void SetTrace( bool b )	{ (void)b; }

	static uint32_t GetOwnerId() 		{ return 0;	}
	static int	GetDepth() 			{ return 0; }
};

//-----------------------------------------------------------------------------
//
// A mutex decorator class used to control the use of a mutex, to make it
// less expensive when not multithreading
//
//-----------------------------------------------------------------------------

template <class BaseClass, bool *pCondition>
class CThreadConditionalMutex : public BaseClass
{
public:
	void Lock()				{ if ( *pCondition ) BaseClass::Lock(); }
	void Lock() const 		{ if ( *pCondition ) BaseClass::Lock(); }
	void Unlock()			{ if ( *pCondition ) BaseClass::Unlock(); }
	void Unlock() const		{ if ( *pCondition ) BaseClass::Unlock(); }

	bool TryLock()			{ if ( *pCondition ) return BaseClass::TryLock(); else return true; }
	bool TryLock() const 	{ if ( *pCondition ) return BaseClass::TryLock(); else return true; }
	bool AssertOwnedByCurrentThread() { if ( *pCondition ) return BaseClass::AssertOwnedByCurrentThread(); else return true; }
	void SetTrace( bool b ) { if ( *pCondition ) BaseClass::SetTrace( b ); }
};

//-----------------------------------------------------------------------------
// Mutex decorator that blows up if another thread enters
//-----------------------------------------------------------------------------

template <class BaseClass>
class CThreadTerminalMutex : public BaseClass
{
public:
	bool TryLock()			{ if ( !BaseClass::TryLock() ) { DebuggerBreak(); return false; } return true; }
	bool TryLock() const 	{ if ( !BaseClass::TryLock() ) { DebuggerBreak(); return false; } return true; }
	void Lock()				{ if ( !TryLock() ) BaseClass::Lock(); }
	void Lock() const 		{ if ( !TryLock() ) BaseClass::Lock(); }

};

//-----------------------------------------------------------------------------
//
// Class to Lock a critical section, and unlock it automatically
// when the lock goes out of scope
//
//-----------------------------------------------------------------------------

template <class MUTEX_TYPE = CThreadMutex>
class CAutoLockT
{
public:
	FORCEINLINE CAutoLockT( MUTEX_TYPE &lock)
		: m_lock(lock)
	{
		m_lock.Lock();
	}

	FORCEINLINE CAutoLockT(const MUTEX_TYPE &lock)
		: m_lock(const_cast<MUTEX_TYPE &>(lock))
	{
		m_lock.Lock();
	}

	FORCEINLINE ~CAutoLockT()
	{
		m_lock.Unlock();
	}


private:
	MUTEX_TYPE &m_lock;

	// Disallow copying
	CAutoLockT<MUTEX_TYPE>( const CAutoLockT<MUTEX_TYPE> & );
	CAutoLockT<MUTEX_TYPE> &operator=( const CAutoLockT<MUTEX_TYPE> & );
};

typedef CAutoLockT<CThreadMutex> CAutoLock;

//---------------------------------------------------------

template <int size>	struct CAutoLockTypeDeducer {};
template <> struct CAutoLockTypeDeducer<sizeof(CThreadMutex)> {	typedef CThreadMutex Type_t; };
template <> struct CAutoLockTypeDeducer<sizeof(CThreadNullMutex)> {	typedef CThreadNullMutex Type_t; };
#if defined(_WIN32) && !defined(THREAD_PROFILER)
template <> struct CAutoLockTypeDeducer<sizeof(CThreadSpinLock)> {	typedef CThreadSpinLock Type_t; };
template <> struct CAutoLockTypeDeducer<sizeof(CAlignedThreadFastMutex)> {	typedef CAlignedThreadFastMutex Type_t; };
#elif defined( _PS3 ) && !defined(THREAD_PROFILER)
template <> struct CAutoLockTypeDeducer<sizeof(CThreadSpinLock)> {	typedef CThreadSpinLock Type_t; };
template <> struct CAutoLockTypeDeducer<sizeof(CAlignedThreadFastMutex)> {	typedef CAlignedThreadFastMutex Type_t; };
#endif

#define AUTO_LOCK_( type, mutex ) \
	CAutoLockT< type > UNIQUE_ID( static_cast<const type &>( mutex ) )

#if defined(GNUC)
    
    template<typename T> T strip_cv_quals_for_mutex(T&);
    template<typename T> T strip_cv_quals_for_mutex(const T&);
    template<typename T> T strip_cv_quals_for_mutex(volatile T&);
    template<typename T> T strip_cv_quals_for_mutex(const volatile T&);
    
#define AUTO_LOCK( mutex ) \
    AUTO_LOCK_( typeof(::strip_cv_quals_for_mutex(mutex)), mutex )

#else // GNUC
    
#define AUTO_LOCK( mutex ) \
	AUTO_LOCK_( CAutoLockTypeDeducer<sizeof(mutex)>::Type_t, mutex )

#endif // GNUC
    
#define AUTO_LOCK_FM( mutex ) \
	AUTO_LOCK_( CThreadSpinLock, mutex )

#define LOCAL_THREAD_LOCK_( tag ) \
	; \
	static CThreadSpinLock autoMutex_##tag; \
	AUTO_LOCK( autoMutex_##tag )

#define LOCAL_THREAD_LOCK() \
	LOCAL_THREAD_LOCK_(_)

#if defined( _PS3 )
//---------------------------------------------------------------------------
// CThreadEventWaitObject - the purpose of this class is to help implement
// WaitForMultipleObejcts on PS3. 
//
// Each event maintains a linked list of CThreadEventWaitObjects. When a 
// thread wants to wait on an event it passes the event a semaphore that 
// ptr to see the index of the event that triggered it
//
// The thread-specific mutex is to ensure that setting the index and setting the
// semaphore are atomic
//---------------------------------------------------------------------------

class CThreadEventWaitObject
{
public:
	CThreadEventWaitObject *m_pPrev, *m_pNext;
	sys_semaphore_t			*m_pSemaphore;
	int						m_index;
	int						*m_pFlag;

	CThreadEventWaitObject() {}

	void Init(sys_semaphore_t *pSem, int index, int *pFlag)
	{
		m_pSemaphore = pSem;
		m_index = index;
		m_pFlag = pFlag;
	}

	bool BSetIfUnset();

	static const int k_nInvalidIndex = -1;		// 0 or greater are valid
};
#endif //_PS3

//-----------------------------------------------------------------------------
//
// Base class for event, semaphore and mutex objects.
//
//-----------------------------------------------------------------------------

class TT_CLASS CThreadSyncObject
{
public:
	~CThreadSyncObject();

	//-----------------------------------------------------
	// Query if object is useful
	//-----------------------------------------------------
	bool operator!() const;
	bool IsValid() const { return !operator!(); }

	//-----------------------------------------------------
	// Access handle
	//-----------------------------------------------------
#ifdef _WIN32
	operator HANDLE() { return m_hSyncObject; }
	HANDLE Handle() { return m_hSyncObject; }
#endif
	//-----------------------------------------------------
	// Wait for a signal from the object
	//-----------------------------------------------------
	bool Wait( uint32_t dwTimeoutMsec = TT_INFINITE );

#if defined( _PS3 )
	// used for implementing WaitForMultipleObjects
	void RegisterWaitingThread( sys_semaphore_t *pSemaphore, int index, int *flag );
	void UnregisterWaitingThread( sys_semaphore_t *pSemaphore );
#endif // _PS3

protected:
	CThreadSyncObject();
	void AssertUseable();
#if defined(POSIX)   && !defined( _PS3 )
	bool SaveNameToFile( const char *pszName );
	bool CreateAnonymousSyncObjectInternal( bool bInitiallySet, bool bManualReset );
	bool SignalThreadSyncObjectInternal();
	bool IsSemaphoreOrphanedInternal( sem_type sem, int iIgnorePid );
	sem_type CreateSemaphoreInternal( const char *pszName,
		                              long cInitialCount,
									  bool bCrossUser,
									  bool *bCreated );
	sem_type OpenSemaphoreInternal( const char *pszName, bool bCrossUser );
	bool AcquireSemaphoreInternal( uint32_t nMsTimeout );
	bool ReleaseSemaphoreInternal( sem_type sem, long nReleaseCount = 1 );
	void CloseSemaphoreInternal( sem_type sem, bool bOwner, const char *pszName );
	bool EnsureSemaphoreClearedInternal( sem_type sem );
	bool EnsureSemaphorePostedInternal( sem_type sem );
#endif

#ifdef _WIN32
	HANDLE m_hSyncObject;
	bool m_bOwnEventHandle;
#elif defined( _PS3 )
	static sys_lwmutex_t	m_staticMutex;
	static uint32_t			m_bstaticMutexInitialized;
	static uint32_t			m_bstaticMutexInitializing;

	// bugbug ps3 - cleanup
	uint32_t				m_bSet;
	bool					m_bManualReset;

	CThreadEventWaitObject	m_waitObjects[CTHREADEVENT_MAX_WAITING_THREADS+2];	
	CThreadEventWaitObject	*m_pWaitObjectsPool;
	CThreadEventWaitObject	*m_pWaitObjectsList;
#elif defined(POSIX)
    // if we're a named object, we use the semaphore and associated operations rather than pthreads
	char *m_pszSemName;
	sem_type m_pSemaphore;
	bool m_bSemOwner;
	pthread_mutex_t	m_Mutex;
	pthread_cond_t	m_Condition;
	bool m_bInitialized;
	CInterlockedInt m_cSet;
	bool m_bManualReset;
	bool m_bWakeForEvent;
#else
#error "Implement me"
#endif

private:
	CThreadSyncObject( const CThreadSyncObject & );
	CThreadSyncObject &operator=( const CThreadSyncObject & );
};

#if defined(_WIN32) || defined(_PS3)
// wait for multiple sync objects (for either any one or all to trigger)
TT_INTERFACE int WaitForMultipleEvents( CThreadSyncObject **ppThreadSyncObjects, uint32_t cThreadSyncObjects, uint32_t unMilliSecTimeout, CThreadMutex *pSyncLock );
#endif


//-----------------------------------------------------------------------------
//
// CThreadSemaphore
//
//-----------------------------------------------------------------------------

class TT_CLASS CThreadSemaphore : public CThreadSyncObject
{
public:
	CThreadSemaphore(long initialValue, long maxValue);

	//-----------------------------------------------------
	// Increases the count of the semaphore object by a specified
	// amount.  Wait() decreases the count by one on return.
	//-----------------------------------------------------
	bool Release( long releaseCount = 1 );

private:
	CThreadSemaphore(const CThreadSemaphore &);
	CThreadSemaphore &operator=(const CThreadSemaphore &);
};


//-----------------------------------------------------------------------------
//
// A mutex suitable for out-of-process, multi-processor usage
//
//-----------------------------------------------------------------------------

#if !defined(_PS3)
class TT_CLASS CThreadFullMutex : public CThreadSyncObject
{
public:
	// If a name is given the name must always start with a forward slash or drive:/.
	// A leading slash will be ignored on Win32.
	// A leading drive: will be ignored on non-Win32.
	CThreadFullMutex( bool bEstablishInitialOwnership = false,
		              const char * pszName = NULL,
					  bool bAllAccess = false,
					  bool bInherit = false );

	//-----------------------------------------------------
	// Release ownership of the mutex
	//-----------------------------------------------------
	bool Release();

	// To match regular CThreadMutex:
	void Lock()							{ Wait(); }
	void Lock( unsigned timeout )		{ Wait( timeout ); }
	void Unlock()						{ Release(); }
	bool AssertOwnedByCurrentThread()	{ return true; }
	void SetTrace( bool )				{}

	bool IsCreator() const;

#if defined(_WIN32)
protected:
	bool m_bIsCreator;
#endif

private:
	CThreadFullMutex( const CThreadFullMutex & );
	CThreadFullMutex &operator=( const CThreadFullMutex & );
};
#endif // !_PS3


//-----------------------------------------------------------------------------
//
// Wrapper for unnamed event objects
//
//-----------------------------------------------------------------------------

#ifndef __AFXTLS_H__ // not compatible with some Windows headers
class TT_CLASS CThreadEvent : public CThreadSyncObject
{
public:
	CThreadEvent( bool fManualReset = false );

#if !defined(_PS3)
	CThreadEvent( const char *pchEventName, bool bCrossUserSession, bool fManualReset = false );
#endif

#ifdef _WIN32
	CThreadEvent( HANDLE hSyncObject, bool bOwnEventHandle );
#endif


	//-----------------------------------------------------
	// Set the state to signaled
	//-----------------------------------------------------
	bool Set();

	//-----------------------------------------------------
	// Set the state to nonsignaled
	//-----------------------------------------------------
	bool Reset();

	//-----------------------------------------------------
	// Check if the event is signaled
	//-----------------------------------------------------
	bool Check();

#if defined( _PS3 )
	// bugbug cboyd - wtf.. why are we overriding a member that isn't virtual?
	bool Wait( uint32_t dwTimeout = TT_INFINITE );
#endif

private:
	CThreadEvent( const CThreadEvent & );
	CThreadEvent &operator=( const CThreadEvent & );
};

// Hard-wired manual event for use in array declarations
class CThreadManualEvent : public CThreadEvent
{
public:
	CThreadManualEvent()
	 :	CThreadEvent( true )
	{
	}
};


#if defined( _PS3 )
PLATFORM_INTERFACE int ThreadWaitForObjects( int nEvents, const HANDLE *pHandles, bool bWaitAll = true, unsigned timeout = TT_INFINITE );
#endif // _PS3

inline int ThreadWaitForEvents( int nEvents, const CThreadEvent *pEvents, bool bWaitAll = true, unsigned timeout = TT_INFINITE ) 
{
#ifdef POSIX
	Assert( 0 );
	return 0;
#else
	return ThreadWaitForObjects( nEvents, (const HANDLE *)pEvents, bWaitAll, timeout ); 
#endif
}

//-----------------------------------------------------------------------------
//
// CThreadRWLock
//
//-----------------------------------------------------------------------------

class TT_CLASS CThreadRWLock
{
public:
	CThreadRWLock();

	void LockForRead();
	void UnlockRead();
	void LockForWrite();
	void UnlockWrite();

	void LockForRead() const { const_cast<CThreadRWLock *>(this)->LockForRead(); }
	void UnlockRead() const { const_cast<CThreadRWLock *>(this)->UnlockRead(); }
	void LockForWrite() const { const_cast<CThreadRWLock *>(this)->LockForWrite(); }
	void UnlockWrite() const { const_cast<CThreadRWLock *>(this)->UnlockWrite(); }

private:
	void WaitForRead();

	CThreadSpinLock m_mutex;
	CThreadEvent m_CanWrite;
	CThreadEvent m_CanRead;

	int m_nWriters;
	int m_nActiveReaders;
	int m_nPendingReaders;
};

#endif // !__AFXTLS_H__
//-----------------------------------------------------------------------------
//
// CThreadSpinRWLock
//
//-----------------------------------------------------------------------------


class ALIGN8 TT_CLASS CThreadSpinRWLock
{
public:
	CThreadSpinRWLock()	{ COMPILE_TIME_ASSERT( sizeof( LockInfo_t ) == sizeof( int64_t ) ); DbgAssert( (intp)this % 8 == 0 ); memset( this, 0, sizeof( *this ) ); }

	bool TryLockForWrite();
	bool TryLockForRead();

	void LockForRead();
	void UnlockRead();
	void LockForWrite();
	void UnlockWrite();

	bool TryLockForWrite() const { return const_cast<CThreadSpinRWLock *>(this)->TryLockForWrite(); }
	bool TryLockForRead() const { return const_cast<CThreadSpinRWLock *>(this)->TryLockForRead(); }
	void LockForRead() const { const_cast<CThreadSpinRWLock *>(this)->LockForRead(); }
	void UnlockRead() const { const_cast<CThreadSpinRWLock *>(this)->UnlockRead(); }
	void LockForWrite() const { const_cast<CThreadSpinRWLock *>(this)->LockForWrite(); }
	void UnlockWrite() const { const_cast<CThreadSpinRWLock *>(this)->UnlockWrite(); }

private:
	// This structure is used as an atomic & exchangeable 64-bit value. It would probably be better to just have one 64-bit value
	// and accessor functions that make/break it, but at this late stage of development, I'm just wrapping it into union
	// Beware of endianness: on Xbox/PowerPC m_writerId is high-word of m_i64; on PC, it's low-dword of m_i64
	struct LockInfo_t
	{
		uint32_t	m_writerId;
		int		m_nReaders;
	};

	bool AssignIf( const LockInfo_t &newValue, const LockInfo_t &comperand );
	bool TryLockForWrite( const uint32_t threadId );
	void SpinLockForWrite( const uint32_t threadId );

	volatile LockInfo_t m_lockInfo;
	CInterlockedInt m_nWriters;
} ALIGN8_POST;


// auto-lock class for read-write locks
template< class T >
class CRWLockAutoWrite
{
	T &m_RWLock;
public:
	CRWLockAutoWrite( T &RWLock ) : m_RWLock( RWLock )
	{
		m_RWLock.LockForWrite();
	}

	~CRWLockAutoWrite()
	{
		m_RWLock.UnlockWrite();
	}
};

#define AUTO_LOCK_WRITE( mutex ) CRWLockAutoWrite<CThreadRWLock> UNIQUE_ID( mutex )
#define AUTO_LOCK_SPIN_WRITE( mutex ) CRWLockAutoWrite<CThreadSpinRWLock> UNIQUE_ID( mutex )


// auto-lock class for read-write locks
template< class T >
class CRWLockAutoRead
{
	T &m_RWLock;
public:
	CRWLockAutoRead( T &RWLock ) : m_RWLock( RWLock )
	{
		m_RWLock.LockForRead();
	}

	~CRWLockAutoRead()
	{
		m_RWLock.UnlockRead();
	}
};

#define AUTO_LOCK_READ( mutex ) CRWLockAutoRead<CThreadRWLock> UNIQUE_ID( mutex )
#define AUTO_LOCK_SPIN_READ( mutex ) CRWLockAutoRead<CThreadSpinRWLock> UNIQUE_ID( mutex )

//-----------------------------------------------------------------------------
//
// A thread wrapper similar to a Java thread.
//
//-----------------------------------------------------------------------------

#ifdef _PS3

// Everything must be inline for this to work across PRX boundaries

class CThread;
PLATFORM_INTERFACE CThread *GetCurThreadPS3();
//PLATFORM_INTERFACE void AllocateThreadID( void );
//PLATFORM_INTERFACE void FreeThreadID( void );

// Access the thread handle directly
//ThreadHandle_t GetThreadHandle()
//{
//	return (ThreadHandle_t)m_threadId;
//}

#endif

//-----------------------------------------------------------------------------
//
// A thread wrapper similar to a Java thread.
//
//-----------------------------------------------------------------------------
class TT_CLASS CThread
{
public:
	CThread();
	virtual ~CThread();

	//-----------------------------------------------------

	const char *GetName();
	void SetName( const char * );

	// if set, write minidump on crash and then terminate process quietly.
	// By default it will write a minidump and then pass exception to higher handler,
	// which might end up showing a Windows crash dialog
	void SetExitQuietly() { m_bExitQuietly = true; }

	size_t CalcStackDepth( void *pStackVariable )		{ return ((uint8_t *)m_pStackBase - (uint8_t *)pStackVariable); }

	//-----------------------------------------------------
	// Functions for the other threads
	//-----------------------------------------------------

	// Start thread running  - error if already running
	bool Start( unsigned int nBytesStack = 0 );

	// Returns true if thread has been created and hasn't yet exited
	bool IsAlive();

	// This method causes the current thread to wait until this thread
	// is no longer alive.
	bool Join( unsigned int nMillisecondsTimeout = TT_INFINITE );

	// Access the thread handle directly
	ThreadHandle_t GetThreadHandle() const { return m_hThread; }

	ThreadId_t GetThreadId() const { return m_threadId; }

	//-----------------------------------------------------

	int GetResult() const;

	//-----------------------------------------------------
	// Functions for both this, and maybe, and other threads
	//-----------------------------------------------------

	// Forcibly, abnormally, but relatively cleanly stop the thread
	void Stop( int exitCode = EXIT_SUCCESS );

	// Get the priority
	int GetPriority() const;

	// Set the priority
	bool SetPriority( int );

	// Suspend a thread
	unsigned Suspend();

	// Resume a suspended thread
	unsigned Resume();

	// Force hard-termination of thread.  Used for critical failures.
	bool Terminate( int exitCode = EXIT_SUCCESS );

	//-----------------------------------------------------
	// Global methods
	//-----------------------------------------------------

	// Get the Thread object that represents the current thread, if any.
	// Can return NULL if the current thread was not created using
	// CThread
	static CThread *GetCurrentCThread();

	// Offer a context switch. Under Win32, equivalent to Sleep(0)
#ifdef Yield
#undef Yield
#endif
	static void Yield();

	static void Sleep( unsigned int nMilliseconds );

protected:

	bool BHasValidThreadID();

	// Optional pre-run call, with ability to fail-create. Note Init()
	// is forced synchronous with Start()
	virtual bool Init();

	// Thread will run this function on startup, must be supplied by
	// derived class, performs the intended action of the thread.
	virtual int Run() = 0;

	// Called when the thread exits
	virtual void OnExit();

	virtual bool IsThreadRunning();

private:
	bool WaitForCreateComplete( CThreadEvent *pEvent );

	// "Virtual static" facility
#ifdef _PS3
	typedef void* (*ThreadProc_t)( void * pv );
	static void* ThreadProc( void * pv );
#else
	typedef unsigned (__stdcall *ThreadProc_t)( void * );	
	static unsigned __stdcall ThreadProc( void * pv );
	static void ThreadExceptionWrapper( void *object );
#endif

	virtual ThreadProc_t GetThreadProc();

	// Thread initially runs this. param is actually 'this'. function
	// just gets this and calls ThreadProc
	struct ThreadInit_t
	{
		CThread *     pThread;
		CThreadEvent *pInitCompleteEvent;
		bool *        pfInitSuccess;
	};

	// make copy constructor and assignment operator inaccessible
	CThread( const CThread & );
	CThread &operator=( const CThread & );

	ThreadHandle_t m_hThread;
	ThreadId_t m_threadId;
#if defined(_PS3)
	CThreadEvent m_eventThreadExit;
#endif
	volatile int m_result; // -1 while running
	char	m_szName[32];
	void *	m_pStackBase;
	bool	m_bExitQuietly;
};


//-----------------------------------------------------------------------------
//
// A helper class to let you sleep a thread for memory validation, you need to handle
//	 m_bSleepForValidate in your ::Run() call and set m_bSleepingForValidate when sleeping
//
//-----------------------------------------------------------------------------
class CValidatableThread: public CThread
{
public:
	CValidatableThread()
	{
		m_bSleepForValidate = false;
		m_bSleepingForValidate = false;
	}

#ifdef DBGFLAG_VALIDATE
	virtual void SleepForValidate() { m_bSleepForValidate = true; }
	bool BSleepingForValidate() { return m_bSleepingForValidate; }
	virtual void WakeFromValidate() { m_bSleepForValidate = false; }
#endif
protected:
	bool m_bSleepForValidate;
	bool m_bSleepingForValidate;
};


//-----------------------------------------------------------------------------
// Simple thread class encompasses the notion of a worker thread, handing
// synchronized communication.
//-----------------------------------------------------------------------------

#ifdef _WIN32

// These are internal reserved error results from a call attempt
enum WTCallResult_t
{
	WTCR_FAIL			= -1,
	WTCR_TIMEOUT		= -2,
	WTCR_THREAD_GONE	= -3,
};

class TT_CLASS CWorkerThread : public CThread
{
public:
	CWorkerThread();

	//-----------------------------------------------------
	//
	// Inter-thread communication
	//
	// Calls in either direction take place on the same "channel."
	// Separate functions are specified to make identities obvious
	//
	//-----------------------------------------------------

	// Master: Signal the thread, and block for a response
	int CallWorker( unsigned, unsigned timeout = TT_INFINITE, bool fBoostWorkerPriorityToMaster = true );

	// Worker: Signal the thread, and block for a response
	int CallMaster( unsigned, unsigned timeout = TT_INFINITE );

	// Wait for the next request
	bool WaitForCall( unsigned dwTimeout, unsigned *pResult = NULL );
	bool WaitForCall( unsigned *pResult = NULL );

	// Is there a request?
	bool PeekCall( unsigned *pParam = NULL );

	// Reply to the request
	void Reply( unsigned );

	// Wait for a reply in the case when CallWorker() with timeout != TT_INFINITE
	int WaitForReply( unsigned timeout = TT_INFINITE );

	// If you want to do WaitForMultipleObjects you'll need to include
	// this handle in your wait list or you won't be responsive
	HANDLE GetCallHandle();

	// Find out what the request was
	unsigned GetCallParam() const;

	// Boost the worker thread to the master thread, if worker thread is lesser, return old priority
	int BoostPriority();

protected:
	typedef uint32_t (__stdcall *WaitFunc_t)( uint32_t nHandles, const HANDLE*pHandles, int bWaitAll, uint32_t timeout );
	int Call( unsigned, unsigned timeout, bool fBoost, WaitFunc_t = NULL );
	int WaitForReply( unsigned timeout, WaitFunc_t );

private:
	CWorkerThread( const CWorkerThread & );
	CWorkerThread &operator=( const CWorkerThread & );

#if !defined( _PS3 )
	CThreadMutex	m_Lock;
#endif

#ifdef _WIN32
	CThreadEvent	m_EventSend;
	CThreadEvent	m_EventComplete;
#endif

	unsigned        m_Param;
	int				m_ReturnVal;
};

#else

typedef CThread CWorkerThread;

#endif

// a unidirectional message queue. A queue of type T. Not especially high speed since each message
// is malloced/freed. Note that if your message class has destructors/constructors, they MUST be
// thread safe!
template<class T> class CMessageQueue
{
	CThreadEvent SignalEvent;								// signals presence of data
	CThreadMutex QueueAccessMutex;

	// the parts protected by the mutex
	struct MsgNode
	{
		MsgNode *Next;
		T Data;
	};

	MsgNode *Head;
	MsgNode *Tail;

public:
	CMessageQueue( void )
	{
		Head = Tail = NULL;
	}

	// check for a message. not 100% reliable - someone could grab the message first
	bool MessageWaiting( void ) 
	{
		return ( Head != NULL );
	}

	void WaitMessage( T *pMsg )
	{
		for(;;)
		{
			while( ! MessageWaiting() )
				SignalEvent.Wait();
			QueueAccessMutex.Lock();
			if (! Head )
			{
				// multiple readers could make this null
				QueueAccessMutex.Unlock();
				continue;
			}
			*( pMsg ) = Head->Data;
			MsgNode *remove_this = Head;
			Head = Head->Next;
			if (! Head)										// if empty, fix tail ptr
				Tail = NULL;
			QueueAccessMutex.Unlock();
			delete remove_this;
			break;
		}
	}

	void QueueMessage( T const &Log)
	{
		MsgNode *new1=new MsgNode;
		new1->Data=Log;
		new1->Next=NULL;
		QueueAccessMutex.Lock();
		if ( Tail )
		{
			Tail->Next=new1;
			Tail = new1;
		}
		else
		{
			Head = new1;
			Tail = new1;
		}
		SignalEvent.Set();
		QueueAccessMutex.Unlock();
	}
};


//-----------------------------------------------------------------------------
//
// CThreadMutex. Inlining to reduce overhead and to allow client code
// to decide debug status (tracing)
//
//-----------------------------------------------------------------------------

#ifdef _WIN32
typedef struct _RTL_CRITICAL_SECTION RTL_CRITICAL_SECTION;
typedef RTL_CRITICAL_SECTION CRITICAL_SECTION;

#ifndef _X360
extern "C"
{
	void __declspec(dllimport) __stdcall InitializeCriticalSection(CRITICAL_SECTION *);
	void __declspec(dllimport) __stdcall EnterCriticalSection(CRITICAL_SECTION *);
	void __declspec(dllimport) __stdcall LeaveCriticalSection(CRITICAL_SECTION *);
	void __declspec(dllimport) __stdcall DeleteCriticalSection(CRITICAL_SECTION *);
};
#endif

//---------------------------------------------------------

inline void CThreadMutex::Lock()
{
#ifdef THREAD_MUTEX_TRACING_ENABLED
	ThreadId_t thisThreadID = ThreadGetCurrentId();
	if ( m_bTrace && m_currentOwnerID && ( m_currentOwnerID != thisThreadID ) )
		Log( "Thread %u about to wait for lock %p owned by %u\n", ThreadGetCurrentId(), (CRITICAL_SECTION *)&m_CriticalSection, m_currentOwnerID );
#endif

	::EnterCriticalSection((CRITICAL_SECTION *)&m_CriticalSection);

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
}

//---------------------------------------------------------

inline void CThreadMutex::Unlock()
{
#ifdef THREAD_MUTEX_TRACING_ENABLED
	AssertMsg( m_lockCount >= 1, "Invalid unlock of thread lock" );
	m_lockCount--;
	if (m_lockCount == 0)
	{
		if ( m_bTrace )
			Log( "Thread %u releasing lock 0x%p\n", m_currentOwnerID, (CRITICAL_SECTION *)&m_CriticalSection );
		m_currentOwnerID = 0;
	}
#endif
	LeaveCriticalSection((CRITICAL_SECTION *)&m_CriticalSection);
}

//---------------------------------------------------------

inline bool CThreadMutex::AssertOwnedByCurrentThread()
{
#ifdef THREAD_MUTEX_TRACING_ENABLED
	if (ThreadGetCurrentId() == m_currentOwnerID)
		return true;
	AssertMsg3( 0, "Expected thread %u as owner of lock 0x%p, but %u owns", ThreadGetCurrentId(), (CRITICAL_SECTION *)&m_CriticalSection, m_currentOwnerID );
	return false;
#else
	return true;
#endif
}

//---------------------------------------------------------

inline void CThreadMutex::SetTrace( bool bTrace )
{
#ifdef THREAD_MUTEX_TRACING_ENABLED
	m_bTrace = bTrace;
#endif
}

//---------------------------------------------------------
#elif defined( _PS3 )
//---------------------------------------------------------

inline void CThreadMutex::Lock()
{
#if defined(_PS3)
#ifndef NO_THREAD_SYNC
	sys_mutex_lock( m_Mutex, 0 );
#endif
#endif
}

//---------------------------------------------------------

inline void CThreadMutex::Unlock()
{
#if defined( _PS3 )

#ifndef NO_THREAD_SYNC
	sys_mutex_unlock( m_Mutex );
#endif

#endif
}

//---------------------------------------------------------

inline bool CThreadMutex::AssertOwnedByCurrentThread()
{
	return true;
}

//---------------------------------------------------------

inline void CThreadMutex::SetTrace( bool bTrace )
{
#ifdef THREAD_MUTEX_TRACING_ENABLED
	m_bTrace = bTrace;
#endif
}

inline bool CThreadMutex::TryLock(int blah) volatile
{
	return ( sys_mutex_trylock( m_Mutex ) == CELL_OK );
}

#elif defined(POSIX) 

//---------------------------------------------------------

inline void CThreadMutex::Lock()
{
	pthread_mutex_lock( &m_Mutex );
}

//---------------------------------------------------------

inline void CThreadMutex::Unlock()
{
	pthread_mutex_unlock( &m_Mutex );
}

//---------------------------------------------------------

inline bool CThreadMutex::TryLock(int blah) volatile
{
	return pthread_mutex_trylock( (pthread_mutex_t *)&m_Mutex ) == 0;
}

//---------------------------------------------------------

inline bool CThreadMutex::AssertOwnedByCurrentThread()
{
	return true;
}

//---------------------------------------------------------

inline void CThreadMutex::SetTrace(bool fTrace)
{
}

#endif // defined(POSIX)

//-----------------------------------------------------------------------------
//
// CThreadRWLock inline functions
//
//-----------------------------------------------------------------------------

inline CThreadRWLock::CThreadRWLock()
:	m_CanRead( true ),
	m_nWriters( 0 ),
	m_nActiveReaders( 0 ),
	m_nPendingReaders( 0 )
{
}

inline void CThreadRWLock::LockForRead()
{
	m_mutex.Lock();
	if ( m_nWriters)
	{
		WaitForRead();
	}
	m_nActiveReaders++;
	m_mutex.Unlock();
}

inline void CThreadRWLock::UnlockRead()
{
	m_mutex.Lock();
	m_nActiveReaders--;
	if ( m_nActiveReaders == 0 && m_nWriters != 0 )
	{
		m_CanWrite.Set();
	}
	m_mutex.Unlock();
}


//-----------------------------------------------------------------------------
//
// CThreadSpinRWLock inline functions
//
//-----------------------------------------------------------------------------

inline bool CThreadSpinRWLock::AssignIf( const LockInfo_t &newValue, const LockInfo_t &comperand )
{
	return ThreadInterlockedAssignIf64( (int64_t *)&m_lockInfo, *((int64_t *)&newValue), *((int64_t *)&comperand) );
}

inline bool CThreadSpinRWLock::TryLockForWrite( const uint32_t threadId )
{
	// In order to grab a write lock, there can be no readers and no owners of the write lock
	if ( m_lockInfo.m_nReaders > 0 || ( m_lockInfo.m_writerId && m_lockInfo.m_writerId != threadId ) )
	{
		return false;
	}

	static const LockInfo_t oldValue = { 0, 0 };
	LockInfo_t newValue = { threadId, 0 };
	const bool bSuccess = AssignIf( newValue, oldValue );
#if defined(_X360)
	if ( bSuccess )
	{
		// X360TBD: Serious perf implications. Not Yet. __sync();
	}
#endif
	return bSuccess;
}

inline bool CThreadSpinRWLock::TryLockForWrite()
{
	m_nWriters++;
	if ( !TryLockForWrite( ThreadGetCurrentId() ) )
	{
		m_nWriters--;
		return false;
	}
	return true;
}

inline bool CThreadSpinRWLock::TryLockForRead()
{
	if ( m_nWriters != 0 )
	{
		return false;
	}
	// In order to grab a write lock, the number of readers must not change and no thread can own the write
	LockInfo_t oldValue;
	LockInfo_t newValue;


#if defined( _PS3 )
		// this is the code equivalent to original code (see below) that doesn't cause LHS on Xbox360
		// WARNING: This code assumes BIG Endian CPU
		oldValue.m_i64 = uint32_t( m_lockInfo.m_nReaders );
		newValue.m_i64 = oldValue.m_i64 + 1; // NOTE: when we have -1 (or 0xFFFFFFFF) readers, this will result in non-equivalent code
#else
	oldValue.m_nReaders = m_lockInfo.m_nReaders;
	oldValue.m_writerId = 0;
	newValue.m_nReaders = oldValue.m_nReaders + 1;
	newValue.m_writerId = 0;
#endif // _PS3
	const bool bSuccess = AssignIf( newValue, oldValue );
#if defined(_X360)
	if ( bSuccess )
	{
		// X360TBD: Serious perf implications. Not Yet. __sync();
	}
#endif
	return bSuccess;
}

inline void CThreadSpinRWLock::LockForWrite()
{
	const uint32_t threadId = ThreadGetCurrentId();

	m_nWriters++;

	if ( !TryLockForWrite( threadId ) )
	{
		ThreadPause();
		SpinLockForWrite( threadId );
	}
}


#if defined(POSIX) && !defined(_PS3)
}
using namespace SteamThreadTools;
#endif
 
// read data from a memory address
template<class T> FORCEINLINE T ReadVolatileMemory( T const *pPtr )
{
	volatile const T * pVolatilePtr = ( volatile const T * ) pPtr;
	return *pVolatilePtr;
}

//-----------------------------------------------------------------------------

#pragma warning(pop)

#if defined( _PS3 )
BOOL SetEvent( CThreadEvent *pEvent );
BOOL ResetEvent( CThreadEvent *pEvent );
#endif

#endif // THREADTOOLS_H
