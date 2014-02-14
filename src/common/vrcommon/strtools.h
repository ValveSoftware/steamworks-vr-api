//========= Copyright Valve Corporation ============//
#pragma once

#include <string>
#include <stdint.h>

/** returns true if the string has the prefix */
bool StringHasPrefix( const std::string & sString, const std::string & sPrefix );

/** converts a UTF-16 string to a UTF-8 string */
std::string UTF16to8(const wchar_t * in);

/** converts a UTF-8 string to a UTF-16 string */
std::wstring UTF8to16(const char * in);

/** safely copy a string into a buffer */
void strcpy_safe( char *pchBuffer, size_t unBufferSizeBytes, const char *pchSource );

// we stricmp (from WIN) but it isn't POSIX - OSX/LINUX have strcasecmp so just inline bridge to it
#if defined(OSX) || defined(LINUX)
#include <strings.h>
inline int stricmp(const char *pStr1, const char *pStr2) { return strcasecmp(pStr1,pStr2); }
#endif

/* Handles copying a std::string into a buffer as would be provided in an API */
uint32_t ReturnStdString( const std::string & sValue, char *pchBuffer, uint32_t unBufferLen );
