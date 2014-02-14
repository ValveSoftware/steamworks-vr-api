//========= Copyright Valve Corporation ============//
#include "vrcommon/hmdplatform_private.h"
#include "steamvr.h"
#include "vr_controlpanel.h"
#include "ihmdsystem.h"
#include "vrcommon/dirtools.h"
#include "vrcommon/pathtools.h"
#include "vrcommon/strtools.h"
#include "vrcommon/hmdlog.h"
#include "vrcommon/vrlog.h"

#include "vrclient.h"
#include "hmdlatest.h"
#include "notifications.h"
#include "SDL_loadso.h"
#include "SDL_log.h"
#include <vector>
#include "SDL_filesystem.h"
#include "interface_adapters.h"
#include "vr_messages.pb.h"

#if defined(LINUX)
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

using namespace vr;

static const char * const Old_IVRControlPanel_Version = "IOpenVRControlPanel_001";

class CHmdSystemLatest : public IHmdSystem, public IVRControlPanel
{
public:

	// -----------------------------------------------
	// ---------- From IHmdSystem --------------------
	// -----------------------------------------------

	/** Initializes the systems */
	virtual HmdError Init() OVERRIDE;

	/** cleans up everything in vrclient.dll and prepares the DLL to be unloaded */
	virtual void Cleanup() OVERRIDE;

	/** checks to see if the specified interface/version is supported in this vrclient.dll */
	virtual HmdError IsInterfaceVersionValid( const char *pchInterfaceVersion ) OVERRIDE;

	/** Retrieves the specified version of the HmdCore interface. This is void because the 
	* class name inside vrclient.dll might not match the class name inside vrclient.lib. */
	virtual void *GetCurrentHmd( const char *pchCoreVersion ) OVERRIDE;

	/** Retrieves the control panel interface from vrclient.dll */
	virtual IVRControlPanel *GetControlPanel( const char *pchControlPanelVersion, HmdError *peError ) OVERRIDE;

	// -----------------------------------------------
	// ---------- From IVRControlPanel -----------
	// -----------------------------------------------

	// Driver enumeration methods
	virtual uint32_t GetDriverCount() OVERRIDE;
	virtual uint32_t GetDriverId( uint32_t unDriverIndex, char *pchBuffer, uint32_t unBufferLen ) OVERRIDE;

	// Display Enumeration Methods
	virtual uint32_t GetDriverDisplayCount( const char *pchDriverId ) OVERRIDE;
	virtual uint32_t GetDriverDisplayId( const char *pchDriverId, uint32_t unDisplayIndex, char *pchBuffer, uint32_t unBufferLen ) OVERRIDE;

	// Display Detail Methods
	virtual uint32_t GetDriverDisplayModelNumber( const char *pchDriverId, const char *pchDisplayId, char *pchBuffer, uint32_t unBufferLen ) OVERRIDE;
	virtual uint32_t GetDriverDisplaySerialNumber( const char *pchDriverId, const char *pchDisplayId, char *pchBuffer, uint32_t unBufferLen ) OVERRIDE;

	virtual IHmd *GetCurrentDisplayInterface( const char *pchHmdInterfaceVersion ) OVERRIDE;

	// Shared Resource Methods
	virtual uint32_t LoadSharedResource( const char *pchResourceName, char *pchBuffer, uint32_t unBufferLen ) OVERRIDE;

private:
	CHmdLatest m_hmd;
	CVRClient m_client;
};

CHmdSystemLatest g_hmdSystem;


std::string GetResourceBaseDir()
{
	static const char *k_pchResourceRelativePath = "..\\resources";

	std::string sModulePath = Path_StripFilename( GetThisModulePath() );

	return Path_MakeAbsolute( k_pchResourceRelativePath, sModulePath );
}


/** Initializes the system */
HmdError CHmdSystemLatest::Init() 
{
	char *pchUserConfigPath = SDL_GetPrefPath( "", "steamvr" );
	if( !pchUserConfigPath )
	{
		return HmdError_Init_UserConfigDirectoryInvalid;
	}

	HmdError err = m_client.Init();
	if( err != HmdError_None )
	{
		return err;
	}

	m_hmd.Reset( &m_client );

	NotifyVR_InitSuccess();

	return HmdError_None;
}


/** cleans up everything in vrclient.dll and prepares the DLL to be unloaded */
void CHmdSystemLatest::Cleanup() 
{
	m_client.Cleanup();

	NotifyVR_Shutdown( );
}


/** checks to see if the specified interface/version is supported in this vrclient.dll */
HmdError CHmdSystemLatest::IsInterfaceVersionValid( const char *pchInterfaceVersion ) 
{
	if( !stricmp( pchInterfaceVersion, IHmd_Version ) 
		|| HasInterfaceAdapter( pchInterfaceVersion ) )
	{
		return HmdError_None;
	}
	else
	{
		return HmdError_Init_InterfaceNotFound;
	}
}



/** Retrieves the specified version of the IHmd interface */
void *CHmdSystemLatest::GetCurrentHmd( const char *pchHmdVersion ) 
{
	if( 0 == strcmp( pchHmdVersion, IHmd_Version ) )
	{
		return &m_hmd;
	}
	else
	{
		// maybe we have an adapter from an old version
		return (IHmd*)FindInterface( pchHmdVersion, &m_hmd );
	}
}

/** Retrieves the control panel interface from vrclient.dll */
IVRControlPanel *CHmdSystemLatest::GetControlPanel( const char *pchControlPanelVersion, HmdError *peError )
{
	if (!stricmp(pchControlPanelVersion, IVRControlPanel_Version) || !stricmp(pchControlPanelVersion, Old_IVRControlPanel_Version ) )
	{
		if( peError )
			*peError = HmdError_None;
		return this;
	}
	else
	{
		if( peError )
			*peError = HmdError_Init_InterfaceNotFound;
		return NULL;
	}
}

/** returns the number of drivers */
uint32_t CHmdSystemLatest::GetDriverCount() 
{
	CVRMsg_GetDriverInfo msg;
	CVRMsg_GetDriverInfoResponse response;
	if( m_client.SendProtobufMessageAndWaitForResponse( VRMsg_GetDriverInfo, msg, VRMsg_GetDriverInfoResponse, response ) )
	{
		return response.driver_size();
	}
	else
	{
		return 0;
	}
}

/** returns the ID of a particular driver */
uint32_t CHmdSystemLatest::GetDriverId( uint32_t unDriverIndex, char *pchBuffer, uint32_t unBufferLen ) 
{
	CVRMsg_GetDriverInfo msg;
	CVRMsg_GetDriverInfoResponse response;
	if( m_client.SendProtobufMessageAndWaitForResponse( VRMsg_GetDriverInfo, msg, VRMsg_GetDriverInfoResponse, response ) )
	{
		if( unDriverIndex >= (uint32_t)response.driver_size() )
			return 0;

		return ReturnStdString( response.driver( unDriverIndex ).driver_id(), pchBuffer, unBufferLen );
	}
	else
	{
		return 0;
	}
}

// Display Enumeration Methods
uint32_t CHmdSystemLatest::GetDriverDisplayCount( const char *pchDriverId ) 
{
	CVRMsg_GetDisplayInfo msg;
	msg.set_driver_id( pchDriverId );
	CVRMsg_GetDisplayInfoResponse response;
	if( m_client.SendProtobufMessageAndWaitForResponse( VRMsg_GetDisplayInfo, msg, VRMsg_GetDisplayInfoResponse, response ) )
	{
		return response.display_size();
	}
	else
	{
		return 0;
	}
}

uint32_t CHmdSystemLatest::GetDriverDisplayId( const char *pchDriverId, uint32_t unDisplayIndex, char *pchBuffer, uint32_t unBufferLen ) 
{
	CVRMsg_GetDisplayInfo msg;
	msg.set_driver_id( pchDriverId );
	CVRMsg_GetDisplayInfoResponse response;
	if( m_client.SendProtobufMessageAndWaitForResponse( VRMsg_GetDisplayInfo, msg, VRMsg_GetDisplayInfoResponse, response ) )
	{
		if( unDisplayIndex >= (uint32_t)response.display_size() )
			return 0;
		return ReturnStdString( response.display( unDisplayIndex ).display_id(), pchBuffer, unBufferLen );
	}
	else
	{
		return 0;
	}
}

// Display Detail Methods
uint32_t CHmdSystemLatest::GetDriverDisplayModelNumber( const char *pchDriverId, const char *pchDisplayId, char *pchBuffer, uint32_t unBufferLen ) 
{
	CVRMsg_GetDisplayInfo msg;
	msg.set_driver_id( pchDriverId );
	CVRMsg_GetDisplayInfoResponse response;
	if( m_client.SendProtobufMessageAndWaitForResponse( VRMsg_GetDisplayInfo, msg, VRMsg_GetDisplayInfoResponse, response ) )
	{
		for( int i=0; i<response.display_size(); i++ )
		{
			if( !stricmp( pchDisplayId, response.display(i).display_id().c_str() ) )
			{
				return ReturnStdString( response.display( i ).model_number(), pchBuffer, unBufferLen );
			}
		}

	}
	return 0;
}

uint32_t CHmdSystemLatest::GetDriverDisplaySerialNumber( const char *pchDriverId, const char *pchDisplayId, char *pchBuffer, uint32_t unBufferLen ) 
{
	CVRMsg_GetDisplayInfo msg;
	msg.set_driver_id( pchDriverId );
	CVRMsg_GetDisplayInfoResponse response;
	if( m_client.SendProtobufMessageAndWaitForResponse( VRMsg_GetDisplayInfo, msg, VRMsg_GetDisplayInfoResponse, response ) )
	{
		for( int i=0; i<response.display_size(); i++ )
		{
			if( !stricmp( pchDisplayId, response.display(i).display_id().c_str() ) )
			{
				return ReturnStdString( response.display( i ).serial_number(), pchBuffer, unBufferLen );
			}
		}
	}
	return 0;
}

IHmd *CHmdSystemLatest::GetCurrentDisplayInterface( const char *pchHmdInterfaceVersion ) 
{
	return (IHmd *)GetCurrentHmd( pchHmdInterfaceVersion );
}

uint32_t CHmdSystemLatest::LoadSharedResource( const char *pchResourceName, char *pchBuffer, uint32_t unBufferLen )
{
	// disallow relative paths
	if( strstr( pchResourceName, ".." ) != NULL )
		return 0;

	std::string sResourceBaseDir = GetResourceBaseDir();
	std::string sFullResourcePath = Path_MakeAbsolute( pchResourceName, sResourceBaseDir );

	FILE *f = fopen( sFullResourcePath.c_str(), "rb" );
	if( !f )
		return 0;

	uint32_t unSize = 0;

	fseek( f, 0, SEEK_END );
	unSize = ftell( f );
	fseek( f, 0, SEEK_SET );

	if( unBufferLen >= unSize )
	{
		uint32_t numRead = 0;
		while ( numRead != unSize )
			numRead = fread( &pchBuffer[numRead], 1, unSize, f );
	}

	fclose( f );

	return unSize;
}

static const char *IHmdSystem_Prefix = "IHmdSystem_";

// ----------------------------------------------------------------------------------
// Creates and returns the system object
// ----------------------------------------------------------------------------------
HMD_DLL_EXPORT void *HmdSystemFactory( const char *pInterfaceName, int *pReturnCode )
{
	if( !StringHasPrefix( pInterfaceName, IHmdSystem_Prefix ) )
	{
		*pReturnCode = HmdError_Init_InvalidInterface;
		return NULL;
	}

	// right now we only support one version
	if( 0 != stricmp( pInterfaceName, IHmdSystem_Version ) )
	{
		*pReturnCode = HmdError_Init_InterfaceNotFound;
		return NULL;
	}

	return &g_hmdSystem;
}

