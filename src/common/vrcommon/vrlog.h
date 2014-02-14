//========= Copyright Valve Corporation ============//

#pragma once

extern void Log( const char *pchFormat, ... );

extern bool InitLog( const char *pchLogDir, const char *pchLogFilePrefix );
extern void CleanupLog();
