//========= Copyright Valve Corporation ============//
#pragma once

namespace vr
{

class IHmdSystem
{
public:
	/** Initializes the system */
	virtual HmdError Init( const char *pchLogPath, const char *pchConfigPath ) = 0;

	/** cleans up everything in vrclient.dll and prepares the DLL to be unloaded */
	virtual void Cleanup() = 0;

	/** checks to see if the specified interface/version is supported in this vrclient.dll */
	virtual HmdError IsInterfaceVersionValid( const char *pchInterfaceVersion ) = 0;

	/** Retrieves the specified version of the HmdCore interface. This is void because the 
	* class name inside vrclient.dll might not match the class name inside vrclient.lib. */
	virtual void *GetCurrentHmd( const char *pchCoreVersion ) = 0;

	/** Retrieves any interface from vrclient.dll */
	virtual void *GetGenericInterface( const char *pchNameAndVersion, HmdError *peError ) = 0;
};

static const char * const IHmdSystem_Version = "IHmdSystem_002";

}
