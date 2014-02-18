#pragma once

#include "steamvr.h"

namespace vr
{

struct DriverPoseQuaternion_t
{
	double w, x, y, z;
};

struct DriverPose_t
{
	/* Time offset of this pose, in seconds from the actual time of the pose,
	 * relative to the time of the PoseUpdated() call made by the driver.
	 */
	double poseTimeOffset;

	/* Default prediction time to use if not specified by app.
	 */
	double defaultPredictionTime;

	/* Generally, the pose maintained by a driver
	 * is in an inertial coordinate system different
	 * from the world system of x+ right, y+ up, z+ back.
	 * Also, the driver is not usually tracking the "head" position,
	 * but instead an internal IMU or another reference point in the HMD.
	 * The following two transforms transform positions and orientations
	 * to app world space from driver world space,
	 * and to HMD head space from driver local body space. 
	 *
	 * We maintain the driver pose state in its internal coordinate system,
	 * so we can do the pose prediction math without having to
	 * use angular acceleration.  A driver's angular acceleration is generally not measured,
	 * and is instead calculated from successive samples of angular velocity.
	 * This leads to a noisy angular acceleration values, which are also
	 * lagged due to the filtering required to reduce noise to an acceptable level.
	 */
	DriverPoseQuaternion_t qWorldFromDriverRotation;
	double vecWorldFromDriverTranslation[ 3 ];

	DriverPoseQuaternion_t qDriverFromHeadRotation;
	double vecDriverFromHeadTranslation[ 3 ];

	/* State of driver pose, in meters and radians. */
	/* Position of the driver tracking reference in driver world space
	* +[0] (x) is right
	* +[1] (y) is up
	* -[2] (z) is forward
	*/
	double vecPosition[ 3 ];

	/* Velocity of the pose in meters/second */
	double vecVelocity[ 3 ];

	/* Acceleration of the pose in meters/second */
	double vecAcceleration[ 3 ];

	/* Orientation of the tracker, represented as a quaternion */
	DriverPoseQuaternion_t qRotation;

	/* Angular velocity of the pose in axis-angle 
	* representation. The direction is the angle of
	* rotation and the magnitude is the angle around
	* that axis in radians/second. */
	double vecAngularVelocity[ 3 ];

	/* Angular acceleration of the pose in axis-angle 
	* representation. The direction is the angle of
	* rotation and the magnitude is the angle around
	* that axis in radians/second^2. */
	double vecAngularAcceleration[ 3 ];

	HmdTrackingResult result;

	bool poseIsValid;
	bool willDriftInYaw;
	bool shouldApplyHeadModel;
};

class IHmdDriver;

class IPoseListener
{
public:
	virtual void PoseUpdated( IHmdDriver *pDriver, const DriverPose_t & newPose ) = 0;
};

class IHmdDriver
{
public:

	// ------------------------------------
	// Management Methods
	// ------------------------------------
	/** This is called before an HMD is returned to the application. It will always be
	* called before any display or tracking methods. Memory and processor use by the
	* IHmdDriver object should be kept to a minimum until it is activated. 
	* The pose listener is guaranteed to be valid until Deactivate is called, but 
	* should not be used after that point. */
	virtual HmdError Activate( IPoseListener *pPoseListener ) = 0;

	/** This is called when The VR system is switching from this Hmd being the active display
	* to another Hmd being the active display. The driver should clean whatever memory
	* and thread use it can when it is deactivated */
	virtual void Deactivate() = 0;

	/** returns the ID of this particular HMD. This value is opaque to the VR system itself,
	* but should be unique within the driver because it will be passed back in via FindHmd */
	virtual const char *GetId() = 0;

	// ------------------------------------
	// Display Methods
	// ------------------------------------

	/** Size and position that the window needs to be on the VR display. */
	virtual void GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) = 0;

	/** Suggested size for the intermediate render target that the distortion pulls from. */
	virtual void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) = 0;

	/** Gets the viewport in the frame buffer to draw the output of the disortion into */
	virtual void GetEyeOutputViewport( Hmd_Eye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) = 0;

	/** The components necessary to build your own projection matrix in case your
	* application is doing something fancy like infinite Z */
	virtual void GetProjectionRaw( Hmd_Eye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) = 0;

	/** Returns the transform between the view space and eye space. Eye space is the per-eye flavor of view
	* space that provides stereo disparity. Instead of Model * View * Projection the model is Model * View * Eye * Projection. 
	* Normally View and Eye will be multiplied together and treated as View in your application. 
	*/
	virtual HmdMatrix44_t GetEyeMatrix( Hmd_Eye eEye ) = 0;

	/** Returns the result of the distortion function for the specified eye and input UVs. UVs go from 0,0 in 
	* the upper left of that eye's viewport and 1,1 in the lower right of that eye's viewport. */
	virtual DistortionCoordinates_t ComputeDistortion( Hmd_Eye eEye, float fU, float fV ) = 0;

	// -----------------------------------
	// Administrative Methods
	// -----------------------------------

	/** Returns the model number of this HMD */
	virtual const char *GetModelNumber() = 0;

	/** Returns the serial number of this HMD */
	virtual const char *GetSerialNumber() = 0;
};



static const char *IHmdDriver_Version = "IHmdDriver_001";

}