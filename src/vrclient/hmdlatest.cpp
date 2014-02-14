//========= Copyright Valve Corporation ============//
#include "hmdlatest.h"
#include "vrcommon/hmdplatform_private.h"
#include "vrcommon/strtools.h"
#include "vrcommon/hmdmatrixtools.h"
#include "vrcommon/vripcconstants.h"
#include "vrcommon/timeutils.h"
#include "SDL.h"
#include "vrclient.h"

#include "vr_messages.pb.h"

using namespace vr;

// ---------------------------
// Construction
// ---------------------------

// Called whenever we reconnect to a client
void CHmdLatest::Reset( CVRClient *pClient )
{
	m_pClient = pClient;
	HmdMatrix_SetIdentity( &m_TrackerZeroFromTrackerOrigin );
	m_bZeroNextPose = false;
	m_bLastPoseIsValid = false;
}

void CHmdLatest::GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	CVRSharedStatePtr data( m_pClient->GetSharedState() );
	*pnX = data->bounds.x;
	*pnY = data->bounds.y;
	*pnWidth = data->bounds.w;
	*pnHeight = data->bounds.h;
}



void CHmdLatest::GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight )
{
	CVRSharedStatePtr data( m_pClient->GetSharedState() );
	*pnWidth = data->renderTargetSize.w;
	*pnHeight = data->renderTargetSize.h;
}

void CHmdLatest::GetProjectionRaw( Hmd_Eye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom )
{
	if( eEye < 0 || eEye > 1 )
	{
		eEye = Eye_Left;
	}

	CVRSharedStatePtr data( m_pClient->GetSharedState() );
	*pfLeft = data->eye[ eEye ].projection.left;
	*pfRight = data->eye[ eEye ].projection.right;
	*pfTop = data->eye[ eEye ].projection.top;
	*pfBottom = data->eye[ eEye ].projection.bottom;
}


void CHmdLatest::GetEyeOutputViewport( vr::Hmd_Eye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	if( eEye < 0 || eEye > 1 )
	{
		eEye = Eye_Left;
	}

	CVRSharedStatePtr data( m_pClient->GetSharedState() );
	*pnX = data->eye[ eEye ].viewport.x;
	*pnY = data->eye[ eEye ].viewport.y;
	*pnWidth = data->eye[ eEye ].viewport.w;
	*pnHeight = data->eye[ eEye ].viewport.h;
}


// Create a 4x4 projection transform from eye projection and distortion parameters
inline static void ComposeProjectionTransform(float fLeft, float fRight, float fTop, float fBottom, float zNear, float zFar,  HmdMatrix44_t *pmProj )
{
	float idx = 1.0f / (fRight - fLeft);
	float idy = 1.0f / (fBottom - fTop);
	float idz = 1.0f / (zFar - zNear);
	float sx = fRight + fLeft;
	float sy = fBottom + fTop;

	float (*p)[4] = pmProj->m;
	p[0][0] = 2*idx; p[0][1] = 0;     p[0][2] = sx*idx;    p[0][3] = 0;
	p[1][0] = 0;     p[1][1] = 2*idy; p[1][2] = sy*idy;    p[1][3] = 0;
	p[2][0] = 0;     p[2][1] = 0;     p[2][2] = -zFar*idz; p[2][3] = -zFar*zNear*idz;
	p[3][0] = 0;     p[3][1] = 0;     p[3][2] = -1.0f;     p[3][3] = 0;
}


HmdMatrix44_t CHmdLatest::GetProjectionMatrix( Hmd_Eye eEye, float fNearZ, float fFarZ, vr::GraphicsAPIConvention eProjType )
{
	float fLeft, fRight, fTop, fBottom;
	GetProjectionRaw( eEye, &fLeft, &fRight, &fTop, &fBottom );

	HmdMatrix44_t matProj;
	ComposeProjectionTransform( fLeft, fRight, fTop, fBottom, fNearZ, fFarZ, &matProj );
	return matProj;
}

/** returns the transform between the mideye and each eye with the given IPD in meters */
HmdMatrix44_t CHmdLatest::GetEyeMatrix( Hmd_Eye eEye ) 
{
	if( eEye < 0 || eEye > 1 )
	{
		eEye = Eye_Left;
	}

	CVRSharedStatePtr data( m_pClient->GetSharedState() );
	return data->eye[ eEye ].matrix;
}


bool CHmdLatest::GetViewMatrix( float fSecondsFromNow, HmdMatrix44_t *pMatLeftView, HmdMatrix44_t *pMatRightView, HmdTrackingResult *peResult ) 
{
	HmdMatrix34_t matWorldFromHead;
	if( !GetWorldFromHeadPose( fSecondsFromNow, &matWorldFromHead, peResult ) )
	{
		HmdMatrix_SetIdentity( pMatLeftView );
		HmdMatrix_SetIdentity( pMatRightView );
		return false;
	}

	HmdMatrix44_t matHeadFromWorld = HmdMatrix_34to44( HmdMatrix_InvertTR( matWorldFromHead ) );
	*pMatLeftView = HmdMatrix_Multiply( GetEyeMatrix( Eye_Left ), matHeadFromWorld );
	*pMatRightView = HmdMatrix_Multiply( GetEyeMatrix( Eye_Right ), matHeadFromWorld );

	return true;
}


/** Returns the result of the distortion function for the specified eye and input UVs. UVs go from 0,0 in 
* the upper left of that eye's viewport and 1,1 in the lower right of that eye's viewport. */
DistortionCoordinates_t CHmdLatest::ComputeDistortion( Hmd_Eye eEye, float fU, float fV ) 
{
	if( m_pClient )
	{
		return m_pClient->ComputeDistortion( eEye, fU, fV );
	}
	else
	{
		DistortionCoordinates_t coords;
		coords.rfRed[0] = fU;
		coords.rfRed[1] = fV;
		coords.rfGreen[0] = fU;
		coords.rfGreen[1] = fV;
		coords.rfBlue[0] = fU;
		coords.rfBlue[1] = fV;
		return coords;
	}
}

int CHmdLatest::GetSDLDisplayIndex()
{
	int32_t x, y;
	uint32_t width, height;

	int displayIndex = -1;

	GetWindowBounds( &x, &y, &width, &height );
	for( int i=0; i<SDL_GetNumVideoDisplays(); i++ )
	{
		SDL_Rect bounds;
		SDL_GetDisplayBounds( i, &bounds );
		if( bounds.x == x && bounds.y == y 
			&& bounds.w == (int)width && bounds.h == (int)height )
		{
			displayIndex = i;
			break;
		}
	}

	return displayIndex;
}

int32_t CHmdLatest::GetD3D9AdapterIndex()
{
#if defined(_WIN32)
	// Init the video bits of SDL if we they aren't already active
	bool bWasInit = 0 != SDL_WasInit( SDL_INIT_VIDEO );
	if( !bWasInit )
	{
		SDL_Init( SDL_INIT_VIDEO );
	}

	int displayIndex = GetSDLDisplayIndex();
	int32_t adapterIndex = -1;
	if( displayIndex != -1 )
		adapterIndex = SDL_Direct3D9GetAdapterIndex( displayIndex );

	if( !bWasInit )
	{
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
	}

	return adapterIndex;
#else
    return 0;
#endif
}

void CHmdLatest::GetDXGIOutputInfo( int32_t *pnAdapterIndex, int32_t *pnAdapterOutputIndex )
{
	if( pnAdapterIndex )
		*pnAdapterIndex = -1;
	if( pnAdapterOutputIndex )
		*pnAdapterOutputIndex = -1;
	if( !pnAdapterIndex || !pnAdapterOutputIndex )
		return;
#if defined(_WIN32)
	// Init the video bits of SDL if we they aren't already active
	bool bWasInit = 0 != SDL_WasInit( SDL_INIT_VIDEO );
	if( !bWasInit )
	{
		SDL_Init( SDL_INIT_VIDEO );
	}

	int displayIndex = GetSDLDisplayIndex();
	if( displayIndex != -1 )
	{
		int adapter, output;
		SDL_DXGIGetOutputInfo( displayIndex, &adapter, &output );
		*pnAdapterIndex = adapter;
		*pnAdapterOutputIndex = output;
	}

	if( !bWasInit )
	{
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
	}

#endif
}

// Tracking Methods
bool CHmdLatest::GetWorldFromHeadPose( float fSecondsFromNow, HmdMatrix34_t *pmPose, HmdTrackingResult *peResult )
{
	VRSharedState_Pose_t rawPose;
	{
		CVRSharedStatePtr data( m_pClient->GetSharedState() );

		if( peResult )
			*peResult = data->pose.result;
		if( !data->pose.poseIsValid )
		{
			HmdMatrix_SetIdentity( pmPose );
			return false;
		}
		rawPose = data->pose;
	}

	// Get pose time stamp and convert to seconds
	double poseTime = GetSystemTimeFromTicks(rawPose.poseTimeInTicks) + rawPose.poseTimeOffset;

	if (fSecondsFromNow == 0)
		fSecondsFromNow = rawPose.defaultPredictionTime;

	// Update current pose by time elapsed since it was stored in shared memory by the server.
	// We integrate over the prediction time interval dt, assuming constant linear/angular velocity and acceleration.

	double now = GetSystemTime();

	double dt = (now + fSecondsFromNow) - poseTime;

	// Ensure a sane prediction time

	static const double minPredictionTime = -0.100;
	static const double maxPredictionTime =  0.100;
	dt = Clamp( dt, minPredictionTime, maxPredictionTime );

	// driver pos = pos + (vel + acc * dt/2)*dt;
	HmdVector3_t vVel = HmdVector_Add( rawPose.vVelocity,  HmdVector_ScalarMultiply( rawPose.vAcceleration, dt * 0.5 ) );
	HmdVector3_t vPredictedDriverPos = HmdVector_Add( rawPose.vPosition, HmdVector_ScalarMultiply( vVel, dt ) );

	// driver orientation = orientation * FromAxisAngleVector( (omega + omegaDot * dt/2)*dt);
	HmdQuaternion_t qOrientation = HmdQuaternion_Init( rawPose.qRotation.w, rawPose.qRotation.x, rawPose.qRotation.y, rawPose.qRotation.z );

	HmdVector3_t vOmega = HmdVector_Add( rawPose.vAngularVelocity, HmdVector_ScalarMultiply( rawPose.vAngularAcceleration, dt * 0.5 ) );
	HmdVector3_t vOmegaDt = HmdVector_ScalarMultiply(vOmega, dt);
	HmdQuaternion_t qPredictedDriverOrientation = HmdQuaternion_Multiply( qOrientation, HmdQuaternion_FromAxisAngleVector( vOmegaDt ) );

	// Now map from head space to driver's local origin, and from driver's world space to our world space.
	//
	// predictedPose = WorldFromDriver * predictedDriverPose * DriverFromHead
	// 
	// Given two poses, (aRot,aTrans) and (bRot,bTrans), c = a * b ==
	//
	//    cRot = aRot * bRot;
	//    cTrans = aRot * bTrans + aTrans
	//

	// temp =  (driverPose * DriverFromHead)
	HmdQuaternion_t qTemp = HmdQuaternion_Multiply(qPredictedDriverOrientation, rawPose.qDriverFromHeadRotation);
	HmdVector3_t vTemp = HmdQuaternion_RotateVector(qPredictedDriverOrientation, rawPose.vDriverFromHeadTranslation);
	vTemp = HmdVector_Add(vTemp, vPredictedDriverPos);

	// predicted = WorldFromDriver * temp = WorldFromDriver * (driverPose * DriverFromHead)
	HmdQuaternion_t qPredictedOrientation = HmdQuaternion_Multiply(rawPose.qWorldFromDriverRotation, qTemp);
	HmdVector3_t vPredictedPos = HmdQuaternion_RotateVector(rawPose.qWorldFromDriverRotation, vTemp);
	vPredictedPos = HmdVector_Add(vPredictedPos, rawPose.vWorldFromDriverTranslation);

	// Convert rotation,translation to 4x4 pose matrix
	HmdMatrix34_t pose = HmdQuaternion_ToRotationMatrix( qPredictedOrientation );
	HmdMatrix_SetTranslationInline( &pose, vPredictedPos );

	// zero if we need to
	if( m_bZeroNextPose )
	{
		// Normally a tracker aligns its vertical world axis to gravity
		// i.e., its world up vector is (0,1,0).  
		//
		HmdVector3_t up = HmdVector_Init( 0, 1, 0);

		// Create orthonormal basis from the direction we're looking and the up vector
		HmdVector3_t lookBack = HmdMatrix_GetBack( pose );

		// If he is looking straight up or down,
		// we don't really know where he's looking,
		// and we'll either point him in a random 
		// direction or even a divide by zero.
		// So we'll use the z axis as the look vector.
		static const double cos5degrees = 0.9962;
		if (fabs( HmdVector_Dot( lookBack, up ) ) >= cos5degrees)
			lookBack = HmdVector_Init(0, 0, 1);

		HmdVector3_t right = HmdVector_Normalized( HmdVector_Cross( up, lookBack) );	// y = cross(z, x)
		HmdVector3_t back = HmdVector_Cross( right, up);

		HmdMatrix34_t worldFromZero = HmdMatrix34_Init( right, up, back, HmdMatrix_GetTranslation( pose ) );

		m_TrackerZeroFromTrackerOrigin = HmdMatrix_InvertTR( worldFromZero );

		m_bZeroNextPose = false;
	}

	// transform the pose into the zero space that the app picked
	*pmPose = HmdMatrix_Multiply( m_TrackerZeroFromTrackerOrigin, pose );

	// Handle head and neck model
	if( rawPose.shouldApplyHeadModel )
	{
		static const float headBaseToEyeHeight     = 0.15f;  // Vertical height of eye from base of head
		static const float headBaseToEyeProtrusion = 0.09f;  // Distance forward of eye from base of head

		HmdVector3_t rotationPointToEyeZero = HmdVector_Init( 0, headBaseToEyeHeight, -headBaseToEyeProtrusion );
		HmdVector3_t eyeZeroToRotationPoint = HmdVector_Init( 0, -headBaseToEyeHeight, headBaseToEyeProtrusion );

		HmdVector3_t rotationPointToEyeZeroCurrent = HmdMatrix_Transform( *pmPose, rotationPointToEyeZero );

		HmdVector3_t eyeOffset = HmdVector_Add( eyeZeroToRotationPoint, rotationPointToEyeZeroCurrent );

		HmdMatrix_SetTranslationInline( pmPose, eyeOffset );
	}

	m_bLastPoseIsValid = true;
	m_LastPose = *pmPose;

	return true;
}

bool CHmdLatest::GetLastWorldFromHeadPose( HmdMatrix34_t *pmPose ) 
{
	if( m_bLastPoseIsValid )
	{
		*pmPose = m_LastPose;
		return true;
	}
	else
	{
		HmdMatrix_SetIdentity( pmPose );
		return false;
	}
}


bool CHmdLatest::WillDriftInYaw()
{
	CVRSharedStatePtr data( m_pClient->GetSharedState() );
	return data->pose.willDriftInYaw;
}

void CHmdLatest::ZeroTracker()
{
	m_bZeroNextPose = true;
}


uint32_t CHmdLatest::GetDriverId( char *pchBuffer, uint32_t unBufferLen ) 
{
	CVRSharedStatePtr data( m_pClient->GetSharedState() );
	return ReturnStdString( data->hmd.driverId, pchBuffer, unBufferLen );
}

uint32_t CHmdLatest::GetDisplayId( char *pchBuffer, uint32_t unBufferLen )
{
	CVRSharedStatePtr data( m_pClient->GetSharedState() );
	return ReturnStdString( data->hmd.displayId, pchBuffer, unBufferLen );
}
