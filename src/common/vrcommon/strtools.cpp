//========= Copyright Valve Corporation ============//
#include "strtools.h"
#include <string.h>

bool StringHasPrefix( const std::string & sString, const std::string & sPrefix )
{
	return 0 == strncmp( sString.c_str(), sPrefix.c_str(), sPrefix.length() );
}


std::string UTF16to8(const wchar_t * in)
{
	std::string out;
	unsigned int codepoint = 0;
	for (in;  *in != 0;  ++in)
	{
		if (*in >= 0xd800 && *in <= 0xdbff)
			codepoint = ((*in - 0xd800) << 10) + 0x10000;
		else
		{
			if (*in >= 0xdc00 && *in <= 0xdfff)
				codepoint |= *in - 0xdc00;
			else
				codepoint = *in;

			if (codepoint <= 0x7f)
				out.append(1, static_cast<char>(codepoint));
			else if (codepoint <= 0x7ff)
			{
				out.append(1, static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f)));
				out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
			}
			else if (codepoint <= 0xffff)
			{
				out.append(1, static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f)));
				out.append(1, static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
				out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
			}
			else
			{
				out.append(1, static_cast<char>(0xf0 | ((codepoint >> 18) & 0x07)));
				out.append(1, static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
				out.append(1, static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
				out.append(1, static_cast<char>(0x80 | (codepoint & 0x3f)));
			}
			codepoint = 0;
		}
	}
	return out;
}

std::wstring UTF8to16(const char * in)
{
	std::wstring out;
	unsigned int codepoint = 0;
	int following = 0;
	for (in;  *in != 0;  ++in)
	{
		unsigned char ch = *in;
		if (ch <= 0x7f)
		{
			codepoint = ch;
			following = 0;
		}
		else if (ch <= 0xbf)
		{
			if (following > 0)
			{
				codepoint = (codepoint << 6) | (ch & 0x3f);
				--following;
			}
		}
		else if (ch <= 0xdf)
		{
			codepoint = ch & 0x1f;
			following = 1;
		}
		else if (ch <= 0xef)
		{
			codepoint = ch & 0x0f;
			following = 2;
		}
		else
		{
			codepoint = ch & 0x07;
			following = 3;
		}
		if (following == 0)
		{
			if (codepoint > 0xffff)
			{
				out.append(1, static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
				out.append(1, static_cast<wchar_t>(0xdc00 + (codepoint & 0x03ff)));
			}
			else
				out.append(1, static_cast<wchar_t>(codepoint));
			codepoint = 0;
		}
	}
	return out;
}


void strcpy_safe( char *pchBuffer, size_t unBufferSizeBytes, const char *pchSource )
{
	pchBuffer[ unBufferSizeBytes - 1 ] = '\0';
	strncpy( pchBuffer, pchSource, unBufferSizeBytes - 1 );
}


uint32_t ReturnStdString( const std::string & sValue, char *pchBuffer, uint32_t unBufferLen )
{
	uint32_t unLen = (uint32_t)sValue.length() + 1;
	if( !pchBuffer || !unBufferLen )
		return unLen;

	if( unBufferLen < unLen )
	{
		pchBuffer[0] = '\0';
	}
	else
	{
		memcpy( pchBuffer, sValue.c_str(), unLen );
	}

	return unLen;
}


