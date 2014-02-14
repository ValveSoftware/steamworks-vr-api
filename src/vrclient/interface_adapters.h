//========= Copyright Valve Corporation ============//
#pragma once

namespace vr
{
	class IHmd;
}

extern void *FindInterface( const char *pchInterfaceName, vr::IHmd *pHmdLatest );
extern bool HasInterfaceAdapter( const char *pchInterfaceName );


