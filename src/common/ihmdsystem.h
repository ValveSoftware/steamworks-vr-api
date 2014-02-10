//========= Copyright Valve Corporation ============//
#pragma once

namespace vr
{

class IHmdSystem
{
public:
	/** Initializes the system */
	virtual HmdError Init() = 0;

	/** cleans up everything in vrclient.dll and prepares the DLL to be unloaded */
	virtual void Cleanup() = 0;

	/** checks to see if the specified interface/version is supported in this vrclient.dll */
	virtual HmdError IsInterfaceVersionValid( const char *pchInterfaceVersion ) = 0;

	/** Retrieves the specified version of the HmdCore interface. This is void because the 
	* class name inside vrclient.dll might not match the class name inside vrclient.lib. */
	virtual void *GetCurrentHmd( const char *pchCoreVersion ) = 0;

	/** Retrieves the control panel interface from vrclient.dll */
	virtual IVRControlPanel *GetControlPanel( const char *pchControlPanelVersion, HmdError *peError ) = 0;
};

static const char *IHmdSystem_Version = "IHmdSystem_001";

}
