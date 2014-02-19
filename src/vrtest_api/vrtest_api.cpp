//========= Copyright Valve Corporation ============//
#include "steamvr.h"

#include "steamvr.h"
#include "vr_controlpanel.h"
#include "ihmdsystem.h"

#include "vrcommon/hmdplatform_private.h"
#include "vrcommon/pathtools.h"
#include "vrcommon/sharedlibtools.h"
#include "vrcommon/envvartools.h"

namespace vr
{

static void *g_pVRModule = NULL;
static IHmdSystem *g_pHmdSystem = NULL;

typedef void* (*HmdCoreFactoryFn)(const char *pInterfaceName, int *pReturnCode);

/** Finds the active installation of vrclient.dll and initializes it */
IHmd *VR_Init( HmdError *peError )
{
	// figure out where we're going to look for vrclient.dll
	std::string sPath;
	std::string sExeDir = Path_StripFilename( Path_GetExecutablePath() );
	std::string sEnvVar = GetEnvironmentVariable( "VR_OVERRIDE" );
	std::string sPlatformEnvVar = GetEnvironmentVariable( "VR_PLATFORM_INSTALL_PATH" );
    if( !sEnvVar.empty() )
	{
		// The env var is used to override the install path for 
		// debugging the VR API itself, so it takes priority
		sPath = sEnvVar;
	}
	else if( !sPlatformEnvVar.empty() )
	{
		// this is the environment variable set by platforms like Steam
		// who want to point games they launch to the version of vrclient.dll 
		// that they manage
		sPath = sPlatformEnvVar;
	}
	else
	{
		// Then look in the path we were launched from for non-Steam games
		sPath = Path_MakeAbsolute( "./vr", Path_GetExecutablePath() );
	}

	// see if the specified path actually exists.
	if( !Path_IsDirectory( sPath ) )
	{
		if( peError)
			*peError = HmdError_Init_InstallationNotFound;
		return NULL;
	}

	std::string sLogPath = Path_MakeAbsolute( "log", sPath );
	std::string sConfigPath = Path_MakeAbsolute( "config", sPath );

	// Because we don't have a way to select debug vs. release yet we'll just
	// use debug if it's there
	std::string sTestPath = Path_Join( sPath, "bin" );
	if( Path_IsDirectory( sTestPath ) )
	{
		sPath = sTestPath;
	}
	else
	{
		if( peError )
			*peError = HmdError_Init_InstallationCorrupt;
		return NULL;
	}

#if defined( WIN64 )
	sPath = Path_Join( sPath, "vrclient_x64" DYNAMIC_LIB_EXT );
#else
	sPath = Path_Join( sPath, "vrclient" DYNAMIC_LIB_EXT );
#endif
    
	// only look in the override
	void *pMod = SharedLib_Load( sPath.c_str() );
	// nothing more to do if we can't load the DLL
	if( !pMod )
	{
		if( peError )
			*peError = HmdError_Init_VRClientDLLNotFound;
		return NULL;
	}

	HmdCoreFactoryFn fnFactory = ( HmdCoreFactoryFn )( SharedLib_GetFunction( pMod, "HmdSystemFactory" ) );
	if( !fnFactory )
	{
		SharedLib_Unload( pMod );
		if( peError )
			*peError = HmdError_Init_FactoryNotFound;
		return NULL;
	}

	int nReturnCode = 0;
	g_pHmdSystem = static_cast< IHmdSystem * > ( fnFactory( IHmdSystem_Version, &nReturnCode ) );
	if( !g_pHmdSystem )
	{
		SharedLib_Unload( pMod );
		if( peError )
		{
			if( nReturnCode != 0 )
				*peError = (HmdError)nReturnCode;
			else
				*peError = HmdError_Init_InterfaceNotFound;
		}
		return NULL;
	}


	HmdError err = g_pHmdSystem->Init( sLogPath.c_str(), sConfigPath.c_str() );
	if( err != HmdError_None )
	{
		SharedLib_Unload( pMod );
		g_pHmdSystem = NULL;
		if( peError )
		{
			*peError = err;
		}
		return NULL;
	}

	bool bInterfaceValid = g_pHmdSystem->IsInterfaceVersionValid( IHmd_Version ) == HmdError_None;
	if( !bInterfaceValid )
	{
		g_pHmdSystem->Cleanup();
		g_pHmdSystem = NULL;
		SharedLib_Unload( pMod );
		if( peError )
		{
			*peError = HmdError_Init_InterfaceNotFound;
		}

		return NULL;
	}

	IHmd *pHmd = static_cast< IHmd *> ( g_pHmdSystem->GetCurrentHmd( IHmd_Version ) );
	if( !pHmd )
	{
		g_pHmdSystem->Cleanup();
		g_pHmdSystem = NULL;
		SharedLib_Unload( pMod );

		if( peError )
		{
			*peError = HmdError_Init_HmdNotFound;
		}
		return NULL;
	}

	g_pVRModule = pMod;

	if( peError )
		*peError = HmdError_None;

	return pHmd;
}



/** unloads vrclient.dll. Any interface pointers from the interface are 
* invalid after this point */
void VR_Shutdown( )
{
	if( g_pHmdSystem )
	{
		g_pHmdSystem->Cleanup();
		g_pHmdSystem = NULL;
	}
	if (g_pVRModule)
	{
		SharedLib_Unload(g_pVRModule);
		g_pVRModule = NULL;
	}
}


void *VR_GetGenericInterface(const char *pchInterfaceVersion, HmdError *peError)
{
	if( !g_pHmdSystem )
	{
		if( peError )
			*peError = HmdError_Init_NotInitialized;
		return NULL;
	}

	return g_pHmdSystem->GetGenericInterface( pchInterfaceVersion, peError );
}

}
