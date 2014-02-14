//========= Copyright Valve Corporation ============//
#include "oculushmdlatest.h"
#include "vrcommon/hmdmatrixtools.h"

using namespace vr;
using OVR::Matrix4f;

bool COculusHmdLatest::Init( OVR::HMDDevice *pDevice )
{
	m_pPoseListener = NULL;

	m_bValidPose = false;

	m_pHMD = pDevice;
	m_pSensor = *m_pHMD->GetSensor();
	if( !m_pSensor )
	{
		return false; 
	}
	m_pSensor->GetDeviceInfo( &m_sensorInfo );

	// This will initialize HMDInfo with information about configured IPD,
	// screen size and other variables needed for correct projection.
	// We pass HMD DisplayDeviceName into the renderer to select the
	// correct monitor in full-screen mode.
	if ( !m_pHMD->GetDeviceInfo( &m_hmdInfo ) )
	{
		return false;
	}

	m_ActualDisplayWidth = m_hmdInfo.HResolution;
	m_ActualDisplayHeight = m_hmdInfo.VResolution;
	m_ActualDisplayX = m_hmdInfo.DesktopX;
	m_ActualDisplayY = m_hmdInfo.DesktopY;

	// Since the Oculus SDK can report the wrong size 
	// query the OS to find the actual size of the Rift display
	DetermineActualDisplaySize();

	OVR::Profile *pProfile = m_pHMD->GetProfile();
	if( pProfile )
	{
		m_fIpdMeters = pProfile->GetIPD();
	}
	
 	if( fabs( m_fIpdMeters - 0.0635f ) < 0.03 )
	{
		m_fIpdMeters = 0.0635f;
	}

	// We need to attach sensor to SensorFusion object for it to receive 
	// body frame messages and update orientation. SFusion.GetOrientation() 
	// is used in OnIdle() to orient the view.
	m_sensorFusion.AttachToSensor( m_pSensor );

	m_sensorFusion.SetDelegateMessageHandler( this );

	m_stereoConfig.SetHMDInfo( m_hmdInfo );

	// set the "grow for undistort" limit to be a bit smaller so we end up 
	// with better pixels in the render target.
	m_stereoConfig.SetDistortionFitPointVP( -0.6f, 0 );

	return true;
}


void COculusHmdLatest::Cleanup()
{
	RemoveHandlerFromDevices();
	m_pSensor.Clear();
	m_pHMD.Clear();
}

void COculusHmdLatest::DetermineActualDisplaySize()
{
#if defined(_WIN32)
	int nAdapter = 0;
	DISPLAY_DEVICE adapterInfo;
	adapterInfo.cb = sizeof( DISPLAY_DEVICE );
	while( EnumDisplayDevices( NULL, nAdapter, &adapterInfo, 0 ) )
	{
		DISPLAY_DEVICE displayInfo;
		displayInfo.cb = sizeof( DISPLAY_DEVICE );
		int nMonitor = 0;
		while( EnumDisplayDevices( adapterInfo.DeviceName, nMonitor, &displayInfo, 0 ) )
		{
			if( !strcmp( displayInfo.DeviceName, m_hmdInfo.DisplayDeviceName ) )
			{
				DEVMODE devMode;
				devMode.dmSize = sizeof(DEVMODE);
				if( EnumDisplaySettings( adapterInfo.DeviceName, ENUM_CURRENT_SETTINGS, &devMode ) )
				{
					m_ActualDisplayWidth = devMode.dmPelsWidth;
					m_ActualDisplayHeight = devMode.dmPelsHeight;
					break;
				}
			}

			nMonitor++;
		}

		nAdapter++;
	}
#endif
}


HmdError COculusHmdLatest::Activate(  vr::IPoseListener *pPoseListener )
{
	m_pPoseListener = pPoseListener;
	return HmdError_None;
}


void COculusHmdLatest::Deactivate() 
{
	m_pPoseListener = NULL;
}

const char *COculusHmdLatest::GetId()
{
	return m_sensorInfo.SerialNumber;
}

void COculusHmdLatest::GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnX = m_ActualDisplayX;
	*pnY = m_ActualDisplayY;
	*pnWidth = m_ActualDisplayWidth;
	*pnHeight = m_ActualDisplayHeight;
}

void COculusHmdLatest::GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight )
{
	float fScale = m_stereoConfig.GetDistortionScale();
//	fScale = 2.f;
	*pnWidth = (uint32_t)((float)m_ActualDisplayWidth/2 * fScale );
	*pnHeight = (uint32_t)((float)m_ActualDisplayHeight * fScale );
}


void COculusHmdLatest::GetEyeOutputViewport( vr::Hmd_Eye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnY = 0;
	*pnWidth = m_ActualDisplayWidth/2;
	*pnHeight = m_ActualDisplayHeight;

	if( eEye == vr::Eye_Left )
		*pnX = 0;
	else
		*pnX = *pnWidth;
}

// Decompose 4x4 projection transform into eye projection parameters and, optionally, zNear and zFar.
inline static void DecomposeProjectionTransform(const Matrix4f & mat, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom )
{
	const float (*p)[4] = mat.M;

	float dx  = 2.0f / p[0][0];
	float sx  = p[0][2] * dx;
	*pfRight  = (sx + dx) * 0.5f;
	*pfLeft   = sx - *pfRight;

	float dy   = 2.0f / p[1][1];
	float sy   = p[1][2] * dy;
	*pfBottom  = (sy + dy) * 0.5f;
	*pfTop     = sy - *pfBottom;
}

void COculusHmdLatest::GetProjectionRaw( Hmd_Eye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom )
{
	// Projection matrix for the center eye, which the left/right matrices are based on.
	Matrix4f projCenter = Matrix4f::PerspectiveRH(m_stereoConfig.GetYFOVRadians(), m_stereoConfig.GetAspect(), 0.1f, 100.f );
	Matrix4f projOffset;
	switch( eEye )
	{
	default:
	case vr::Eye_Left:
		projOffset = Matrix4f::Translation( m_stereoConfig.GetProjectionCenterOffset(), 0, 0);
		break;
	case vr::Eye_Right:
		projOffset = Matrix4f::Translation(-m_stereoConfig.GetProjectionCenterOffset(), 0, 0);
		break;

	}

	Matrix4f projFinal = projOffset * projCenter;

	DecomposeProjectionTransform( projFinal, pfLeft, pfRight, pfTop, pfBottom );
}


/** returns the transform between the mideye and each eye with the given IPD in meters */
HmdMatrix44_t COculusHmdLatest::GetEyeMatrix( Hmd_Eye eEye )
{
	HmdMatrix44_t mat;
	HmdMatrix_SetIdentity( &mat );
	if( eEye == vr::Eye_Left )
		mat.m[0][3] = -m_fIpdMeters/2.f;
	else
		mat.m[0][3] = m_fIpdMeters/2.f;
	return mat;
}


//"   float2 theta = (oTexCoord - LensCenter) * ScaleIn;\n" // Scales to [-1, 1]
//	"   float  rSq= theta.x * theta.x + theta.y * theta.y;\n"
//	"   float2 theta1 = theta * (HmdWarpParam.x + HmdWarpParam.y * rSq + "
//	"                   HmdWarpParam.z * rSq * rSq + HmdWarpParam.w * rSq * rSq * rSq);\n"
//	"   \n"
//	"   // Detect whether blue texture coordinates are out of range since these will scaled out the furthest.\n"
//	"   float2 thetaBlue = theta1 * (ChromAbParam.z + ChromAbParam.w * rSq);\n"
//	"   float2 tcBlue = LensCenter + Scale * thetaBlue;\n"
//	"   if (any(clamp(tcBlue, ScreenCenter-float2(0.25,0.5), ScreenCenter+float2(0.25, 0.5)) - tcBlue))\n"
//	"       return 0;\n"
//	"   \n"
//	"   // Now do blue texture lookup.\n"
//	"   float  blue = Texture.Sample(Linear, tcBlue).b;\n"
//	"   \n"
//	"   // Do green lookup (no scaling).\n"
//	"   float2 tcGreen = LensCenter + Scale * theta1;\n"
//	"   float  green = Texture.Sample(Linear, tcGreen).g;\n"
//	"   \n"
//	"   // Do red scale and lookup.\n"
//	"   float2 thetaRed = theta1 * (ChromAbParam.x + ChromAbParam.y * rSq);\n"
//	"   float2 tcRed = LensCenter + Scale * thetaRed;\n"
//	"   float  red = Texture.Sample(Linear, tcRed).r;\n"

/** Returns the result of the distortion function for the specified eye and input UVs. UVs go from 0,0 in 
* the upper left of that eye's viewport and 1,1 in the lower right of that eye's viewport. */
vr::DistortionCoordinates_t COculusHmdLatest::ComputeDistortion( vr::Hmd_Eye eEye, float fU, float fV )
{
	OVR::Util::Render::DistortionConfig distConfig = m_stereoConfig.GetDistortionConfig();
	OVR::Util::Render::Viewport vp = m_stereoConfig.GetFullViewport();
	
	vp.w = m_hmdInfo.HResolution/2;

	float fXCenterOffset = distConfig.XCenterOffset;
	float fUOutOffset = 0;
	if( eEye == vr::Eye_Right )
	{
		fXCenterOffset = -fXCenterOffset;

		vp.x = m_hmdInfo.HResolution/2;
		fUOutOffset = -0.5f;
	}

	float x = (float)vp.x / (float)m_hmdInfo.HResolution;
	float y = (float)vp.y / (float)m_hmdInfo.VResolution;
	float w = (float)vp.w / (float)m_hmdInfo.HResolution;
	float h = (float)vp.h / (float)m_hmdInfo.VResolution;

	// pre-munge the UVs the way that Oculus does in their vertex shader
	float fMungedU = fU * w + x;
	float fMungedV = fV * h + y;

	float as = (float)vp.w / (float)vp.h;

	float fLensCenterX = x + (w + fXCenterOffset * 0.5f)*0.5f;
	float fLensCenterY = y + h *0.5f;

	float scaleFactor = 1.0f / distConfig.Scale;

	float scaleU = (w/2)*scaleFactor; 
	float scaleV = (h/2)*scaleFactor * as;

	float scaleInU = 2/w;
	float scaleInV = (2/h)/as;

	DistortionCoordinates_t coords;
	float thetaU = ( fMungedU - fLensCenterX ) *scaleInU; // Scales to [-1, 1]
	float thetaV = ( fMungedV - fLensCenterY ) * scaleInV; // Scales to [-1, 1]

	float  rSq= thetaU * thetaU + thetaV * thetaV;
	float theta1U = thetaU * (distConfig.K[0]+ distConfig.K[1] * rSq +
		distConfig.K[2] * rSq * rSq + distConfig.K[3] * rSq * rSq * rSq);
	float theta1V = thetaV * (distConfig.K[0]+ distConfig.K[1] * rSq +
		distConfig.K[2] * rSq * rSq + distConfig.K[3] * rSq * rSq * rSq);

	// The x2 on U scale of the output coords is because the input texture in the VR API is 
	// single eye instead of two-eye like is standard in the Rift samples.

	// Do blue scale and lookup
	float thetaBlueU = theta1U * (distConfig.ChromaticAberration[2] + distConfig.ChromaticAberration[3] * rSq);
	float thetaBlueV = theta1V * (distConfig.ChromaticAberration[2] + distConfig.ChromaticAberration[3] * rSq);
	coords.rfBlue[0] = 2.f * (fLensCenterX + scaleU * thetaBlueU + fUOutOffset );
	coords.rfBlue[1] = fLensCenterY + scaleV * thetaBlueV;

	// Do green lookup (no scaling).
	coords.rfGreen[0] = 2.f * (fLensCenterX + scaleU * theta1U + fUOutOffset);
	coords.rfGreen[1] = fLensCenterY + scaleV * theta1V;

	// Do red scale and lookup.
	float thetaRedU = theta1U * (distConfig.ChromaticAberration[0] + distConfig.ChromaticAberration[1] * rSq);
	float thetaRedV = theta1V * (distConfig.ChromaticAberration[0] + distConfig.ChromaticAberration[1] * rSq);
	coords.rfRed[0] = 2.f * (fLensCenterX + scaleU * thetaRedU + fUOutOffset);
	coords.rfRed[1] = fLensCenterY + scaleV * thetaRedV;

	//float r = sqrtf( rSq );
	//if( r > 0.19f && r < 0.2f )
	//{
	//	memset( &coords, 0, sizeof(coords) );
	//}

	return coords;
}


const char *COculusHmdLatest::GetModelNumber()
{
	return m_hmdInfo.ProductName;
}


const char *COculusHmdLatest::GetSerialNumber()
{
	return m_sensorInfo.SerialNumber;
}

void COculusHmdLatest::OnMessage( const OVR::Message & msg ) 
{
	// the only messages we get are for the sensor fusion object.
	// we just intercept them here to force the pose in vrserver to update
	// whenever one of these messages arrives
	if( msg.Type == OVR::Message_BodyFrame )
	{
		// figure out what pose to pass to the listener
		if( m_pPoseListener )
		{
			vr::DriverPose_t pose;

			// Since prediction is currently handled by Oculus SDK
			// so clear time derivatives and offsets so that
			// vrclient won't do any prediction.
			// TODO: Do we want to change this?
			pose.poseTimeOffset = 0;
			pose.defaultPredictionTime = 0;

			// Set WorldFromDriver and DriverFromHead poses to identity
			for (int i = 0; i < 3; i++)
			{
				pose.vecWorldFromDriverTranslation[ i ] = 0.0;
				pose.vecDriverFromHeadTranslation[ i ] = 0.0;
			}

			pose.qWorldFromDriverRotation.w = 1;
			pose.qWorldFromDriverRotation.x = 0;
			pose.qWorldFromDriverRotation.y = 0;
			pose.qWorldFromDriverRotation.z = 0;

			pose.qDriverFromHeadRotation.w = 1;
			pose.qDriverFromHeadRotation.x = 0;
			pose.qDriverFromHeadRotation.y = 0;
			pose.qDriverFromHeadRotation.z = 0;

			// some things are always true
			pose.shouldApplyHeadModel = true;
			pose.willDriftInYaw = true;

			// we don't do position, so these are easy
			for( int i=0; i < 3; i++ )
			{
				pose.vecPosition[ i ] = 0.0;
				pose.vecVelocity[ i ] = 0.0;
				pose.vecAcceleration[ i ] = 0.0;

				// we also don't know the angular velocity or acceleration
				pose.vecAngularVelocity[ i ] = 0.0;
				pose.vecAngularAcceleration[ i ] = 0.0;

			}

			// now get the rotation and turn it into axis-angle format
			OVR::Quatf qOculus = m_sensorFusion.GetPredictedOrientation();
			if( qOculus.w == 0 && qOculus.x == 0 && qOculus.y == 0 && qOculus.z == 0 )
			{
				pose.qRotation.w = 1;
				pose.qRotation.x = 0;
				pose.qRotation.y = 0;
				pose.qRotation.z = 0;
				pose.poseIsValid = false;
				pose.result = TrackingResult_Uninitialized;
			}
			else
			{
				pose.qRotation.w = qOculus.w;
				pose.qRotation.x = qOculus.x;
				pose.qRotation.y = qOculus.y;
				pose.qRotation.z = qOculus.z;

				//TODO: Fold in msgBodyFrame.RotationRate for vecAngularVelocity
				//NOTE: If you do this in order to perform prediction in vrclient,
				// then use GetOrientation above instead of GetPredictedOrientation.

				pose.poseIsValid = true;
				pose.result = TrackingResult_Running_OK;
			}
			m_pPoseListener->PoseUpdated( this, pose );
		}
	}
}
