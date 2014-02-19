//========= Copyright Valve Corporation ============//
#include "vr_controlpanel.h"
#include "ihmdsystem.h"
#include "steamvr.h"

#include <string>
#include "vrcommon/hmdplatform_private.h"

using namespace vr;

class InterfaceRegistrationBase
{
public:
	InterfaceRegistrationBase( const char *pchInterfaceName )
	{
		m_sName = pchInterfaceName;
		m_pNext = s_pFirst;
		s_pFirst = this;
	}
	virtual ~InterfaceRegistrationBase() {}

	static InterfaceRegistrationBase *Find( const char *pchInterfaceName )
	{
		for( InterfaceRegistrationBase *pCurr = s_pFirst; pCurr != NULL; pCurr = pCurr->m_pNext )
		{
			if( pCurr->m_sName == pchInterfaceName )
			{
				return pCurr;
			}
		}
		return NULL;
	}

	virtual void *GetInterface( IHmd *pHmdLatest, IHmdSystem *pSystemLatest ) = 0;

private:
	static InterfaceRegistrationBase *s_pFirst;
	InterfaceRegistrationBase *m_pNext;
	std::string m_sName;
};

InterfaceRegistrationBase *InterfaceRegistrationBase::s_pFirst = NULL;

class GenericInterfaceRegistration : public InterfaceRegistrationBase
{
public:
	GenericInterfaceRegistration( const char *pchInterfaceName, void *pInterface ) 
		: InterfaceRegistrationBase( pchInterfaceName ), m_pInterface( pInterface )
	{

	}

	virtual void *GetInterface( IHmd *pHmdLatest, IHmdSystem *pSystemLatest ) OVERRIDE
	{
		return m_pInterface;
	}

private:
	void *m_pInterface;
};

void RegisterInterface( const char *pchInterfaceName, void *pInterface )
{
	// for now, just leak this guy. He'll register in the global list on construction
	GenericInterfaceRegistration *pReg = new GenericInterfaceRegistration( pchInterfaceName, pInterface );
	(void)pReg;
}

void *FindInterface( const char *pchInterfaceName, vr::IHmd *pHmdLatest, IHmdSystem *pSystemLatest )
{
	InterfaceRegistrationBase *pReg = InterfaceRegistrationBase::Find( pchInterfaceName );
	if( !pReg )
		return NULL;

	return pReg->GetInterface( pHmdLatest, pSystemLatest );
}

bool HasInterfaceAdapter( const char *pchInterfaceName )
{
	return InterfaceRegistrationBase::Find( pchInterfaceName ) != NULL;
}


template< typename t >
class HmdInterfaceRegistration : public InterfaceRegistrationBase
{
public:
	HmdInterfaceRegistration( const char *pchInterfaceName ) 
		: InterfaceRegistrationBase( pchInterfaceName )
	{

	}

	virtual void *GetInterface( IHmd *pHmdLatest, IHmdSystem *pSystemLatest ) OVERRIDE
	{
		m_Interface.m_pHmdLatest = pHmdLatest;
		return &m_Interface;
	}

private:
	t m_Interface;
};

class CHmd_001
{
public:
	virtual bool GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) { m_pHmdLatest->GetWindowBounds( pnX, pnY, pnWidth, pnHeight ); return true; }
	virtual void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) { m_pHmdLatest->GetRecommendedRenderTargetSize( pnWidth, pnHeight ); }
	virtual void GetEyeOutputViewport( Hmd_Eye eEye, GraphicsAPIConvention eAPIType, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) { m_pHmdLatest->GetEyeOutputViewport( eEye, pnX, pnY, pnWidth, pnHeight ); }
	virtual HmdMatrix44_t GetProjectionMatrix( Hmd_Eye eEye, float fNearZ, float fFarZ, GraphicsAPIConvention eProjType ) { return m_pHmdLatest->GetProjectionMatrix( eEye, fNearZ, fFarZ, eProjType ); }
	virtual void GetProjectionRaw( Hmd_Eye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) { m_pHmdLatest->GetProjectionRaw( eEye, pfLeft, pfRight, pfTop, pfBottom ); }
	virtual DistortionCoordinates_t ComputeDistortion( Hmd_Eye eEye, float fU, float fV ) { return m_pHmdLatest->ComputeDistortion( eEye, fU, fV ); }
	virtual HmdMatrix44_t GetEyeMatrix( Hmd_Eye eEye ) { return m_pHmdLatest->GetEyeMatrix( eEye ); }
	virtual bool GetViewMatrix( float fSecondsFromNow, HmdMatrix44_t *pMatLeftView, HmdMatrix44_t *pMatRightView, HmdTrackingResult *peResult ) { return m_pHmdLatest->GetViewMatrix( fSecondsFromNow, pMatLeftView, pMatRightView, peResult ); }
	virtual int32_t GetD3D9AdapterIndex() { return m_pHmdLatest->GetD3D9AdapterIndex(); }
	virtual bool GetWorldFromHeadPose( float fPredictedSecondsFromNow, HmdMatrix34_t *pmPose, HmdTrackingResult *peResult ) { return m_pHmdLatest->GetWorldFromHeadPose( fPredictedSecondsFromNow, pmPose, peResult ); }
	virtual bool WillDriftInYaw() { return m_pHmdLatest->WillDriftInYaw(); }
	virtual uint32_t GetDriverId( char *pchBuffer, uint32_t unBufferLen ) { return m_pHmdLatest->GetDriverId( pchBuffer, unBufferLen ); }
	virtual uint32_t GetDisplayId( char *pchBuffer, uint32_t unBufferLen ) { return m_pHmdLatest->GetDisplayId( pchBuffer, unBufferLen ); }

	IHmd *m_pHmdLatest;
};

static HmdInterfaceRegistration<CHmd_001> hmd001( "IHmd_001" );

class CHmd_002
{
public:
	virtual bool GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) { m_pHmdLatest->GetWindowBounds( pnX, pnY, pnWidth, pnHeight ); return true; }
	virtual void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) { m_pHmdLatest->GetRecommendedRenderTargetSize( pnWidth, pnHeight ); }
	virtual void GetEyeOutputViewport( Hmd_Eye eEye, GraphicsAPIConvention eAPIType, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) { m_pHmdLatest->GetEyeOutputViewport( eEye, pnX, pnY, pnWidth, pnHeight ); }
	virtual HmdMatrix44_t GetProjectionMatrix( Hmd_Eye eEye, float fNearZ, float fFarZ, GraphicsAPIConvention eProjType ) { return m_pHmdLatest->GetProjectionMatrix( eEye, fNearZ, fFarZ, eProjType ); }
	virtual void GetProjectionRaw( Hmd_Eye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) { m_pHmdLatest->GetProjectionRaw( eEye, pfLeft, pfRight, pfTop, pfBottom ); }
	virtual DistortionCoordinates_t ComputeDistortion( Hmd_Eye eEye, float fU, float fV ) { return m_pHmdLatest->ComputeDistortion( eEye, fU, fV ); }
	virtual HmdMatrix44_t GetEyeMatrix( Hmd_Eye eEye ) { return m_pHmdLatest->GetEyeMatrix( eEye ); }
	virtual bool GetViewMatrix( float fSecondsFromNow, HmdMatrix44_t *pMatLeftView, HmdMatrix44_t *pMatRightView, HmdTrackingResult *peResult ) { return m_pHmdLatest->GetViewMatrix( fSecondsFromNow, pMatLeftView, pMatRightView, peResult ); }
	virtual int32_t GetD3D9AdapterIndex()  { return m_pHmdLatest->GetD3D9AdapterIndex(); }
	virtual bool GetWorldFromHeadPose( float fPredictedSecondsFromNow, HmdMatrix34_t *pmPose, HmdTrackingResult *peResult ) { return m_pHmdLatest->GetWorldFromHeadPose( fPredictedSecondsFromNow, pmPose, peResult ); }
	virtual bool GetLastWorldFromHeadPose( HmdMatrix34_t *pmPose ) { return m_pHmdLatest->GetLastWorldFromHeadPose( pmPose ); }
	virtual bool WillDriftInYaw() { return m_pHmdLatest->WillDriftInYaw(); }
	virtual void ZeroTracker() { m_pHmdLatest->ZeroTracker(); }
	virtual uint32_t GetDriverId( char *pchBuffer, uint32_t unBufferLen ) { return m_pHmdLatest->GetDriverId( pchBuffer, unBufferLen ); }
	virtual uint32_t GetDisplayId( char *pchBuffer, uint32_t unBufferLen )  { return m_pHmdLatest->GetDisplayId( pchBuffer, unBufferLen ); }

	IHmd *m_pHmdLatest;
};

static HmdInterfaceRegistration<CHmd_002> hmd002( "IHmd_002" );


template< typename T >
class SystemInterfaceRegistration : public InterfaceRegistrationBase
{
public:
	SystemInterfaceRegistration( const char *pchInterfaceName ) 
		: InterfaceRegistrationBase( pchInterfaceName )
	{

	}

	virtual void *GetInterface( IHmd *pHmdLatest, IHmdSystem *pSystemLatest ) OVERRIDE
	{
		m_Interface.m_pSystemLatest = pSystemLatest;
		return &m_Interface;
	}

private:
	T m_Interface;
};

class CHmdSystem_001
{
public:
	virtual HmdError Init() { return m_pSystemLatest->Init( NULL, NULL ); }
	virtual void Cleanup() { m_pSystemLatest->Cleanup(); }
	virtual HmdError IsInterfaceVersionValid( const char *pchInterfaceVersion ) { return m_pSystemLatest->IsInterfaceVersionValid( pchInterfaceVersion ); }
	virtual void *GetCurrentHmd( const char *pchCoreVersion ) { return m_pSystemLatest->GetCurrentHmd( pchCoreVersion ); }
	virtual IVRControlPanel *GetControlPanel( const char *pchControlPanelVersion, HmdError *peError ) { return (IVRControlPanel *)m_pSystemLatest->GetGenericInterface( pchControlPanelVersion, peError ); }

	IHmdSystem *m_pSystemLatest;
};

static SystemInterfaceRegistration<CHmdSystem_001> system001( "IHmdSystem_001" );

