//========= Copyright Valve Corporation ============//
#include "ihmddriverprovider.h"
#include "ihmddriver.h"
#include "steamvr.h"

#include "oculushmdlatest.h"

#include "OVR.h"

#include "vrcommon/strtools.h"
#include "vrcommon/hmdplatform_private.h"

#include <vector>

using namespace vr;
using OVR::DeviceManager;
using OVR::HMDDevice;

// The Oculus SDK depends on winmm.lib
#pragma comment(lib, "winmm.lib")


class CDriver_Oculus : public IHmdDriverProvider
{
public:
	virtual HmdError Init( const char *pchUserConfigDir, const char *pchDriverInstallDir ) OVERRIDE;
	virtual void Cleanup() OVERRIDE;
	virtual uint32_t GetHmdCount() OVERRIDE;
	virtual IHmdDriver *GetHmd( uint32_t unWhich ) OVERRIDE;
	virtual IHmdDriver* FindHmd( const char *pchId ) OVERRIDE;

private:
	OVR::Ptr<OVR::DeviceManager>	m_pDeviceManager;
	std::vector< COculusHmdLatest *> m_vecHmds;
};

CDriver_Oculus g_driverOculus;


HmdError CDriver_Oculus::Init( const char *pchUserConfigDir, const char *pchDriverInstallDir ) 
{
	OVR::System::Init();

	m_pDeviceManager = *OVR::DeviceManager::Create();	
	if( !m_pDeviceManager )
		return HmdError_Driver_Failed;

	OVR::DeviceEnumerator<HMDDevice> enumerator = m_pDeviceManager->EnumerateDevices<HMDDevice>();
	do 
	{
		OVR::Ptr< OVR::HMDDevice > pHmdDevice = *enumerator.CreateDevice();
		if( pHmdDevice )
		{
			COculusHmdLatest *pHmd = new COculusHmdLatest;
			if( pHmd->Init( pHmdDevice ) )
			{
				m_vecHmds.push_back( pHmd );
			}
			else
			{
				delete pHmd;
			}
		}
	} while ( enumerator.Next() );

	return HmdError_None;
}

void CDriver_Oculus::Cleanup() 
{
	for( std::vector< COculusHmdLatest * >::iterator i = m_vecHmds.begin(); i != m_vecHmds.end(); i++ )
	{
		(*i)->Cleanup();
		delete (*i);
	}
	m_vecHmds.clear();
	m_pDeviceManager.Clear();

	OVR::System::Destroy();
}


/** returns the number of Hmds that are currently connected */
uint32_t CDriver_Oculus::GetHmdCount()
{
	return m_vecHmds.size();
}

/** returns the Nth Hmd */
IHmdDriver *CDriver_Oculus::GetHmd( uint32_t unWhich )
{
	if( unWhich >= m_vecHmds.size() )
		return NULL;
	return m_vecHmds[ unWhich ];
}


IHmdDriver* CDriver_Oculus::FindHmd( const char *pchId )
{
	for( std::vector< COculusHmdLatest *>::iterator i = m_vecHmds.begin(); i != m_vecHmds.end(); i++ )
	{
		const char *pchHmdId = (*i)->GetId();
		if( !stricmp( pchHmdId, pchId ) )
			return *i;
	}

	return NULL;
}


static const char *IHmdDriverProvider_Prefix = "IHmdDriverProvider_";

HMD_DLL_EXPORT void *HmdDriverFactory( const char *pInterfaceName, int *pReturnCode )
{
	if( !StringHasPrefix( pInterfaceName, IHmdDriverProvider_Prefix ) )
	{
		*pReturnCode = HmdError_Init_InvalidInterface;
		return NULL;
	}

	if( 0 != strcmp( IHmdDriverProvider_Version, pInterfaceName ) )
	{
		if( pReturnCode )
			*pReturnCode = HmdError_Init_InterfaceNotFound;
		return NULL;
	}

	return &g_driverOculus;
}
