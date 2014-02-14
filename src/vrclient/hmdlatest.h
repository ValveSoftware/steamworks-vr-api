//========= Copyright Valve Corporation ============//
#pragma once

#include "vrcommon/hmdplatform_private.h"
#include "steamvr.h"
#include "ihmddriver.h"
#include <string>

class CVRClient;

class CHmdLatest : public vr::IHmd
{
public:
	// Called whenever we reconnect to a client
	void Reset( CVRClient *pClient );

	// --- IHmd interface ---
	// Display Methods
	virtual void GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) OVERRIDE;
	virtual void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) OVERRIDE;
	virtual void GetEyeOutputViewport( vr::Hmd_Eye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) OVERRIDE;
	virtual vr::HmdMatrix44_t GetProjectionMatrix( vr::Hmd_Eye eEye, float fNearZ, float fFarZ, vr::GraphicsAPIConvention eProjType ) OVERRIDE;
	virtual void GetProjectionRaw( vr::Hmd_Eye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) OVERRIDE;
	virtual vr::DistortionCoordinates_t ComputeDistortion( vr::Hmd_Eye eEye, float fU, float fV ) OVERRIDE;
	virtual vr::HmdMatrix44_t GetEyeMatrix( vr::Hmd_Eye eEye ) OVERRIDE;
	virtual bool  GetViewMatrix( float fSecondsFromNow, vr::HmdMatrix44_t *pMatLeftView, vr::HmdMatrix44_t *pMatRightView, vr::HmdTrackingResult *peResult ) OVERRIDE;
	virtual int32_t GetD3D9AdapterIndex() OVERRIDE;
	virtual void GetDXGIOutputInfo( int32_t *pnAdapterIndex, int32_t *pnOutputIndex ) OVERRIDE;

	// Tracking Methods
	virtual bool GetWorldFromHeadPose( float fPredictedSecondsFromNow, vr::HmdMatrix34_t *pmPose, vr::HmdTrackingResult *peResult ) OVERRIDE;
	virtual bool GetLastWorldFromHeadPose( vr::HmdMatrix34_t *pmPose ) OVERRIDE;
	virtual bool WillDriftInYaw() OVERRIDE;
	virtual void ZeroTracker() OVERRIDE;

	// administrative methods
	virtual uint32_t GetDriverId( char *pchBuffer, uint32_t unBufferLen ) OVERRIDE;
	virtual uint32_t GetDisplayId( char *pchBuffer, uint32_t unBufferLen ) OVERRIDE;

private:
	int GetSDLDisplayIndex();

	CVRClient *m_pClient;

	vr::HmdMatrix34_t m_TrackerZeroFromTrackerOrigin;
	vr::HmdMatrix34_t m_LastPose;
	bool m_bZeroNextPose;
	bool m_bLastPoseIsValid;
};

