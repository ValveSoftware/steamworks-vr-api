//========= Copyright Valve Corporation ============//
#pragma once


// figure out how to import from the dll
#if defined(_WIN32)
#define HMD_DLL_EXPORT extern "C" __declspec( dllexport )
#define HMD_DLL_IMPORT extern "C" __declspec( dllimport )
#elif defined(GNUC) || defined(COMPILER_GCC)
#define HMD_DLL_EXPORT extern "C" __attribute__((visibility("default")))
#define HMD_DLL_IMPORT extern "C" 
#else
#error "Unsupported Platform."
#endif

bool Plat_IsInDebugSession();

// This is a trick to get the DLL extension off the -D option on the command line.
#define DLLExtTokenPaste(x) #x
#define DLLExtTokenPaste2(x) DLLExtTokenPaste(x)
#define HMD_DLL_EXT_STRING DLLExtTokenPaste2( _DLL_EXT )

// @todo: why do we ifndef fix it for WIN32 but not the others?
#if defined(_WIN32)
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define MAX_UNICODE_PATH 32767

#elif defined(OSX) || defined(LINUX)

#ifndef MAX_PATH 
#define MAX_PATH 260
#endif

#define MAX_UNICODE_PATH MAX_PATH

#else
#error "Unsupported platform"
#endif

#define MAX_UNICODE_PATH_IN_UTF8 (MAX_UNICODE_PATH * 4)

#ifdef _MSC_VER
// Use this to specify that a function is an override of a virtual function.
// This lets the compiler catch cases where you meant to override a virtual
// function but you accidentally changed the function signature and created
// an overloaded function. Usage in function declarations is like this:
// int GetData() const OVERRIDE;
#define OVERRIDE override
#pragma warning(disable : 4481) // warning C4481: nonstandard extension used: override specifier 'override'
#elif defined(__clang__)
#ifndef OVERRIDE
#define OVERRIDE override
#endif
#else
#define OVERRIDE
#endif

#ifndef NULL
#define NULL 0L
#endif

// turn on limit macros on posix
#if !defined( _WIN32 )
#define __STDC_LIMIT_MACROS
#endif

#include <stdint.h>

#if defined( _WIN32 )
#define snprintf _snprintf
#endif

// huge pile of stuff to try and make filesystem/dir stuff work on OSX

// @todo: !win32, or posix, or (osx||ps3) - we seem to use all 3 patterns
// @todo: is this the right int64 for POSIX? Seems dubious. And maybe move it?
// @todo: get rid of most of this
#if !defined(_WIN32)

#include <typeinfo>
#include <dirent.h>
#include <sys/types.h>
#include <string.h>

typedef off_t offBig_t;
typedef struct stat statBig_t;
typedef struct statvfs statvfsBig_t;
typedef struct dirent direntBig_t;

#define statBig stat
#define scandirBig scandir
#define alphasortBig alphasort

struct _finddata_t
{   
  _finddata_t()
  {
    name[0] = '\0';
    dirBase[0] = '\0';
    curName = 0;
    numNames = 0;
    namelist = NULL;
  }
  // public data
  char name[MAX_PATH]; // the file name returned from the call
  char dirBase[MAX_PATH];
  offBig_t size;
  mode_t attrib;
  time_t time_write;
  time_t time_create;
  int curName;
  int numNames;
  direntBig_t **namelist;  
};

#define _A_SUBDIR S_IFDIR

// FUTURE map _A_HIDDEN via checking filename against .*
#define _A_HIDDEN 0

// FUTURE check 'read only' by checking mode against S_IRUSR
#define _A_RDONLY 0

// no files under posix are 'system' or 'archive'
#define _A_SYSTEM 0
#define _A_ARCH   0

int _findfirst( const char *pchBasePath, struct _finddata_t *pFindData );
int _findnext( const int64_t hFind, struct _finddata_t *pFindData );
bool _findclose( int64_t hFind );
static int FileSelect( const char *name, const char *mask );

#endif // !defined( _WIN32 )

//-----------------------------------------------------------------------------
// Macro to assist in asserting constant invariants during compilation
//
#if defined(OSX)
// C++11 is not yet enabled for OSX compiles.
#define PLAT_COMPILE_TIME_ASSERT( pred ) typedef int UNIQUE_ID[ (pred) ? 1 : -1]
#else
// If available use static_assert instead of weird language tricks. This
// leads to much more readable messages when compile time assert constraints
// are violated.
#if (_MSC_VER > 1500)
#define PLAT_COMPILE_TIME_ASSERT( pred ) static_assert( pred, "Compile time assert constraint is not true: " #pred )
#else
#define PLAT_COMPILE_TIME_ASSERT( pred ) typedef int UNIQUE_ID[ (pred) ? 1 : -1]
#endif

#endif
#if !defined( COMPILE_TIME_ASSERT )
#define COMPILE_TIME_ASSERT( pred ) PLAT_COMPILE_TIME_ASSERT( pred )
#endif
#if !defined( ASSERT_INVARIANT )
#define ASSERT_INVARIANT( pred ) PLAT_COMPILE_TIME_ASSERT( pred )
#endif



// decls for aligning data
#ifdef _WIN32
#define DECL_ALIGN(x) __declspec(align(x))
#elif defined(GNUC)
#define DECL_ALIGN(x) __attribute__((aligned(x)))
#elif defined ( COMPILER_GCC ) || defined( COMPILER_SNC )
#define DECL_ALIGN(x)			__attribute__( ( aligned( x ) ) )
#else
#define DECL_ALIGN(x) /* */
#endif

#ifdef _MSC_VER
// MSVC has the align at the start of the struct
#define ALIGN8 DECL_ALIGN(8)
#define ALIGN16 DECL_ALIGN(16)
#define ALIGN32 DECL_ALIGN(32)
#define ALIGN128 DECL_ALIGN(128)

#define ALIGN8_POST
#define ALIGN16_POST
#define ALIGN32_POST
#define ALIGN128_POST
#elif defined( GNUC ) || defined( COMPILER_PS3 )
// gnuc has the align decoration at the end
#define ALIGN4
#define ALIGN8 
#define ALIGN16
#define ALIGN32
#define ALIGN128

#define ALIGN4_POST DECL_ALIGN(4)
#define ALIGN8_POST DECL_ALIGN(8)
#define ALIGN16_POST DECL_ALIGN(16)
#define ALIGN32_POST DECL_ALIGN(32)
#define ALIGN128_POST DECL_ALIGN(128)
#else
#error
#endif


// Used to step into the debugger
#ifdef _WIN64
#define DebuggerBreak()  __debugbreak()
#elif defined(_WIN32) 
#define DebuggerBreak()  __asm { int 3 }
#elif defined( COMPILER_GCC )
#if defined( _PS3 )
#if defined( _CERT )
#define DebuggerBreak() ((void)0)
#else
#define DebuggerBreak() {  __asm volatile ("tw 31,1,1"); }
#endif
#else 
#define DebuggerBreak()  __asm__ __volatile__ ( "int $3" );
#endif
#elif defined( COMPILER_SNC ) && defined( COMPILER_PS3 )
static volatile bool sPS3_SuppressAssertsInThisFile = false; // you can throw this in the debugger to temporarily disable asserts inside any particular .cpp module. 
#define DebuggerBreak() if (!sPS3_SuppressAssertsInThisFile) __builtin_snpause(); // <sergiy> from SNC Migration Guide, tw 31,1,1
#else
#define DebuggerBreak()  __asm__ __volatile__ ( "int $3" );
#endif


extern bool IsPosix();

template <typename T>
inline T AlignValue( T val, size_t alignment )
{
	return (T)( ( (size_t)val + alignment - 1 ) & ~( alignment - 1 ) );
}


#if defined(_WIN32)
#define NOINLINE			    __declspec(noinline)
#define NORETURN				__declspec(noreturn)
#define FORCEINLINE			    __forceinline
#elif defined(GNUC) || defined(COMPILER_GCC) || defined(COMPILER_SNC)
#define NOINLINE				__attribute__ ((noinline))
#define NORETURN				__attribute__ ((noreturn))
#if defined(COMPILER_GCC) || defined(COMPILER_SNC)
#define FORCEINLINE          inline __attribute__ ((always_inline))
// GCC 3.4.1 has a bug in supporting forced inline of templated functions
// this macro lets us not force inlining in that case
#define FORCEINLINE_TEMPLATE inline
#else
#define FORCEINLINE          inline
#define FORCEINLINE_TEMPLATE inline
#endif
#endif

#if defined(_WIN32)
#define StackAlloc( size ) _alloca( size )
#elif defined( POSIX )
#include <alloca.h>
#include <errno.h>
#define StackAlloc( size ) alloca( size )
#endif

// Linux had a few areas where it didn't construct objects in the same order that Windows does.
// So when CVProfile::CVProfile() would access g_pMemAlloc, it would crash because the allocator wasn't initalized yet.
#if defined( GNUC ) || defined ( COMPILER_GCC ) || defined( COMPILER_SNC )
        #define CONSTRUCT_EARLY __attribute__((init_priority(101)))
#else
        #define CONSTRUCT_EARLY
#endif


template< typename T >
T Clamp( T value, T lowerBound, T upperBound )
{
	if( value < lowerBound )
		return lowerBound;
	else if( value > upperBound )
		return upperBound;
	else
		return value;
}

template< typename T >
T Min( T v1, T v2 )
{
	if( v1 < v2 )
		return v1;
	else
		return v2;
}

template< typename T >
T Max( T v1, T v2 )
{
	if( v1 > v2 )
		return v1;
	else
		return v2;
}


#if defined( POSIX )
#define __stdcall
#endif

