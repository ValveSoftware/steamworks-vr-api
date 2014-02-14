//========= Copyright Valve Corporation ============//
#include "vrcommon/pathtools.h"
#include "hmdplatform_private.h"
#include "vrcommon/strtools.h"

#if defined( _WIN32)
#include <Windows.h>
#include <direct.h>
#include <Shobjidl.h>
#include <KnownFolders.h>
#elif defined OSX
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include "osxfilebridge.h"
#define _S_IFDIR S_IFDIR     // really from tier0/platform.h which we dont have yet
#define _MAX_PATH MAX_PATH   // yet another form of _PATH define we use
#elif defined(LINUX)
#include <dlfcn.h>
#endif

#include <sys/stat.h>

#include <algorithm>

/** Returns the path (including filename) to the current executable */
std::string Path_GetExecutablePath()
{
	bool bSuccess = false;
	char rchPath[ 1024 ];
	size_t nBuff = sizeof(rchPath);
#if defined( _WIN32 )
	bSuccess = ::GetModuleFileNameA(NULL, rchPath, (DWORD)nBuff) > 0;
#elif defined OSX
	uint32_t _nBuff = nBuff; 
	bSuccess = _NSGetExecutablePath(rchPath, &_nBuff) == 0;
	rchPath[nBuff-1] = '\0';
#elif defined LINUX
	ssize_t nRead = readlink("/proc/self/exe", rchPath, nBuff-1 );
	if ( nRead != -1 )
	{
		rchPath[ nRead ] = 0;
		bSuccess = true;
	}
	else
	{
		rchPath[ 0 ] = '\0';
	}
#else
	AssertMsg( false, "Implement Plat_GetExecutablePath" );
#endif

	if( bSuccess )
		return rchPath;
	else
		return "";
}

/** Returns the path of the current working directory */
std::string Path_GetWorkingDirectory()
{
	std::string sPath;
	char buf[ 1024 ];
#if defined( _WIN32 )
	sPath = _getcwd( buf, sizeof( buf ) );
#else
	sPath = getcwd( buf, sizeof( buf ) );
#endif
	return sPath;
}

/** Sets the path of the current working directory. Returns true if this was successful. */
bool Path_SetWorkingDirectory( const std::string & sPath )
{
	bool bSuccess;
#if defined( _WIN32 )
	bSuccess = 0 == _chdir( sPath.c_str() );
#else
	bSuccess = 0 == chdir( sPath.c_str() );
#endif
	return bSuccess;
}

std::string Path_GetModulePath()
{
#if defined( _WIN32 )
	char path[32768];
	HMODULE hm = NULL;

	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		(LPCSTR) &Path_GetModulePath, 
		&hm))
	{
		int ret = GetLastError();
		fprintf(stderr, "GetModuleHandle returned %d\n", ret);
		return "";
	}
	GetModuleFileNameA(hm, path, sizeof(path));
	FreeLibrary( hm );
	return path;
#else
	Dl_info dl_info;
	dladdr((void *)Path_GetModulePath, &dl_info);
	return dl_info.dli_fname;
#endif
}

/** Returns the root of the directory the system wants us to store user config data in */
std::string Path_GetUserConfigDirectory()
{
#if defined( WIN32 )
	// Map the root to a KnownFolderID
	const GUID *pFolderID = &FOLDERID_Profile;

	// Resolve the file path through COM. Can't use the statically linked APIs because they don't exist in XP
	CoInitialize(NULL);
	IKnownFolderManager *pManager;
	IKnownFolder *pFolder;
	if ( FAILED( CoCreateInstance( CLSID_KnownFolderManager, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &pManager ) ) ) )
	{
		return "c:\\userdata";
	}

	if ( FAILED( pManager->GetFolder( *pFolderID, &pFolder ) ) )
		return "c:\\userdata";

	WCHAR* wstrPathInCoTaskMem = NULL;
	if ( FAILED( pFolder->GetPath( 0, &wstrPathInCoTaskMem ) ) )
		return "c:\\userdata";

	// Convert the path to UTF-8 and store in the output
	std::string sUserPath = UTF16to8( wstrPathInCoTaskMem );

	// Get path will have allocated this buffer which we must free before returning
	CoTaskMemFree( wstrPathInCoTaskMem );

	pFolder->Release();
	pManager->Release();

	return sUserPath;
#elif defined( OSX )
    return GetHomeDir();
#elif defined( LINUX )
	  // @todo: not solved/changed as part of OSX - still not real - just removed old class based steam cut and paste
	const char *pchHome = getenv("HOME");
	if ( pchHome == NULL )
	{
		return "";
	}
    return pchHome;
#endif
}


/** Returns the specified path without its filename */
std::string Path_StripFilename( const std::string & sPath, char slash )
{
	if( slash == 0 )
		slash = Path_GetSlash();

	std::string::size_type n = sPath.find_last_of( slash );
	if( n == std::string::npos )
		return sPath;
	else
		return std::string( sPath.begin(), sPath.begin() + n );
}

/** returns just the filename from the provided full or relative path. */
std::string Path_StripDirectory( const std::string & sPath, char slash )
{
	if( slash == 0 )
		slash = Path_GetSlash();

	std::string::size_type n = sPath.find_last_of( slash );
	if( n == std::string::npos )
		return sPath;
	else
		return std::string( sPath.begin() + n + 1, sPath.end() );
}

/** returns just the filename with no extension of the provided filename. 
* If there is a path the path is left intact. */
std::string Path_StripExtension( const std::string & sPath )
{
	for( std::string::const_reverse_iterator i = sPath.rbegin(); i != sPath.rend(); i++ )
	{
		if( *i == '.' )
		{
			return std::string( sPath.begin(), i.base() - 1 );
		}

		// if we find a slash there is no extension
		if( *i == '\\' || *i == '/' )
			break;
	}

	// we didn't find an extension
	return sPath;
}


bool Path_IsAbsolute( const std::string & sPath )
{
	if( sPath.empty() )
		return false;

	if( sPath.find( ':' ) != std::string::npos )
		return true;

	if( sPath[0] == '\\' || sPath[0] == '/' )
		return true;

	return false;
}


/** Makes an absolute path from a relative path and a base path */
std::string Path_MakeAbsolute( const std::string & sRelativePath, const std::string & sBasePath, char slash )
{
	if( slash == 0 )
		slash = Path_GetSlash();

	if( Path_IsAbsolute( sRelativePath ) )
		return sRelativePath;
	else
	{
		if( !Path_IsAbsolute( sBasePath ) )
			return "";

		std::string sCompacted = Path_Compact( Path_Join( sBasePath, sRelativePath, slash ), slash );
		if( Path_IsAbsolute( sCompacted ) )
			return sCompacted;
		else
			return "";
	}
}


/** Fixes the directory separators for the current platform */
std::string Path_FixSlashes( const std::string & sPath, char slash )
{
	if( slash == 0 )
		slash = Path_GetSlash();

	std::string sFixed = sPath;
	std::replace( sFixed.begin(), sFixed.end(), '/', slash );
	std::replace( sFixed.begin(), sFixed.end(), '\\', slash );

	return sFixed;
}


char Path_GetSlash()
{
#if defined(_WIN32)
	return '\\';
#else
	return '/';
#endif
}

/** Jams two paths together with the right kind of slash */
std::string Path_Join( const std::string & first, const std::string & second, char slash )
{
	if( slash == 0 )
		slash = Path_GetSlash();

	// only insert a slash if we don't already have one
	std::string::size_type nLen = first.length();
#if defined(_WIN32)
	if( first.back() == '\\' || first.back() == '/' )
	    nLen--;
#else
	char last_char = first[first.length()-1];
	if (last_char == '\\' || last_char == '/')
	    nLen--;
#endif

	return first.substr( 0, nLen ) + std::string( 1, slash ) + second;
}


/** Removes redundant <dir>/.. elements in the path. Returns an empty path if the 
* specified path has a broken number of directories for its number of ..s */
std::string Path_Compact( const std::string & sRawPath, char slash )
{
	if( slash == 0 )
		slash = Path_GetSlash();

	std::string sPath = Path_FixSlashes( sRawPath, slash );
	std::string sSlashString( 1, slash );

	// strip out all /./
	for( std::string::size_type i = 0; (i + 3) < sPath.length();  )
	{
		if( sPath[ i ] == slash && sPath[ i+1 ] == '.' && sPath[ i+2 ] == slash )
		{
			sPath.replace( i, 3, sSlashString );
		}
		else
		{
			++i;
		}
	}


	// get rid of trailing /. but leave the path separator
	if( sPath.length() > 2 )
	{
		std::string::size_type len = sPath.length();
		if( sPath[ len-1 ] == '.'  && sPath[ len-2 ] == slash )
		{
		  // sPath.pop_back();
		  sPath[len-1] = 0;  // for now, at least
		}
	}

	// get rid of leading ./ 
	if( sPath.length() > 2 )
	{
		if( sPath[ 0 ] == '.'  && sPath[ 1 ] == slash )
		{
			sPath.replace( 0, 2, "" );
		}
	}

	// each time we encounter .. back up until we've found the previous directory name
	// then get rid of both
	std::string::size_type i = 0;
	while( i < sPath.length() )
	{
		if( i > 0 && sPath.length() - i > 2 
			&& sPath[i] == '.'
			&& sPath[i+1] == '.'
			&& ( i + 2 == sPath.length() || sPath[ i+2 ] == slash )
			&& sPath[ i-1 ] == slash )
		{
			// check if we've hit the start of the string and have a bogus path
			if( i == 1 )
				return "";
			
			// find the separator before i-1
			std::string::size_type iDirStart = i-2;
			while( iDirStart > 0 && sPath[ iDirStart - 1 ] != slash )
				--iDirStart;

			// remove everything from iDirStart to i+2
			sPath.replace( iDirStart, (i - iDirStart) + 3, "" );

			// start over
			i = 0;
		}
		else
		{
			++i;
		}
	}

	return sPath;
}


/** Returns the path to the current DLL or exe */
std::string GetThisModulePath()
{
	// gets the path of vrclient.dll itself
#ifdef WIN32
	HMODULE hmodule = NULL;

	::GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCTSTR>(GetThisModulePath), &hmodule);

	wchar_t *pwchPath = new wchar_t[MAX_UNICODE_PATH];
	char *pchPath = new char[ MAX_UNICODE_PATH_IN_UTF8 ];
	::GetModuleFileNameW( hmodule, pwchPath, MAX_UNICODE_PATH );
	WideCharToMultiByte( CP_UTF8, 0, pwchPath, -1, pchPath, MAX_UNICODE_PATH_IN_UTF8, NULL, NULL );
	delete[] pwchPath;

	std::string sPath = pchPath;
	delete [] pchPath;
	return sPath;

#elif defined( OSX ) || defined( LINUX )
	// get the addr of a function in vrclient.so and then ask the dlopen system about it
	Dl_info info;
	dladdr( (void *)GetThisModulePath, &info );
	return info.dli_fname;
#endif

}


/** returns true if the specified path exists and is a directory */
bool Path_IsDirectory( const std::string & sPath )
{
	std::string sFixedPath = Path_FixSlashes( sPath );

	// see if the specified path actually exists.
	struct	stat	buf;
	if ( stat ( sFixedPath.c_str(), &buf ) == -1)
	{
		return false;
	}

#if defined(LINUX)
	return S_ISDIR( buf.st_mode );
#else
	return ( buf.st_mode & _S_IFDIR ) != 0;
#endif
}

