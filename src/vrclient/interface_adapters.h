//========= Copyright Valve Corporation ============//
#pragma once

namespace vr
{
	class IHmd;
	class IHmdSystem;
}

/** Register an interface that is not an adapter */
extern void RegisterInterface( const char *pchInterfaceName, void * );

extern void *FindInterface( const char *pchInterfaceName, vr::IHmd *pHmdLatest, vr::IHmdSystem *pSystemLatest );
extern bool HasInterfaceAdapter( const char *pchInterfaceName );


