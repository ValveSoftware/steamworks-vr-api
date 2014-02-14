//========= Copyright Valve Corporation ============//
#pragma once

#include "vrcommon/hmdplatform_private.h"
#include "ihmddriver.h"
#include "OVR.h"

class COculusHmdLatest : public vr::IHmdDriver, public OVR::MessageHandler
{
public:
	// internal interface
	bool Init( OVR::HMDDevice *pDevice );
	void Cleanup();
	void UpdateValidPose( OVR::Quatf *pQuat = NULL );
	void DetermineActualDisplaySize();

	// IHmdDriver interface
	virtual vr::HmdError Activate( vr::IPoseListener *pPoseListener ) OVERRIDE;
	virtual void Deactivate() OVERRIDE;
	virtual const char *GetId() OVERRIDE;
	virtual void GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) OVERRIDE;
	virtual void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) OVERRIDE;
	virtual void GetEyeOutputViewport( vr::Hmd_Eye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) OVERRIDE;
	virtual void GetProjectionRaw( vr::Hmd_Eye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) OVERRIDE;
	virtual vr::HmdMatrix44_t GetEyeMatrix( vr::Hmd_Eye eEye ) OVERRIDE;
	virtual vr::DistortionCoordinates_t ComputeDistortion( vr::Hmd_Eye eEye, float fU, float fV ) OVERRIDE;
	virtual const char *GetModelNumber() OVERRIDE;
	virtual const char *GetSerialNumber() OVERRIDE;

	// OVR::MessageHandler interface
	virtual void OnMessage( const OVR::Message& ) OVERRIDE;

private:
	vr::IPoseListener *m_pPoseListener;

	OVR::Ptr<OVR::SensorDevice>		m_pSensor;
	OVR::Ptr<OVR::HMDDevice>		m_pHMD;
	OVR::SensorFusion				m_sensorFusion;
	OVR::HMDInfo					m_hmdInfo;
	OVR::SensorInfo					m_sensorInfo;
	OVR::Util::Render::StereoConfig	m_stereoConfig;

	uint32_t						m_ActualDisplayWidth;
	uint32_t						m_ActualDisplayHeight;
	int32_t							m_ActualDisplayX;
	int32_t							m_ActualDisplayY;

	bool							m_bValidPose;

	float							m_fIpdMeters;
};