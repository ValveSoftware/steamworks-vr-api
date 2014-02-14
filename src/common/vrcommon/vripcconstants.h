//========= Copyright Valve Corporation ============//
#pragma once

#include "hmdplatform_private.h"
#include "steamvr.h"
#include "hmdmatrixtools.h"


#if defined(__linux__) || defined(__APPLE__) 
// The 32-bit version of gcc has the alignment requirement for uint64 and double set to
// 4 meaning that even with #pragma pack(8) these types will only be four-byte aligned.
// The 64-bit version of gcc has the alignment requirement for these types set to
// 8 meaning that unless we use #pragma pack(4) our structures will get bigger.
// The 64-bit structure packing has to match the 32-bit structure packing for each platform.
#pragma pack( push, 4 )
#else
#pragma pack( push, 8 )
#endif

static const char *k_PipeName			= "VR_Pipe";
static const char *k_VRSharedMemName = "VR_SharedState";
static const char *k_VRSharedMutexName = "VR_SharedMutex";


enum VRMsgType
{
	VRMsg_Connect					= 100,
	VRMsg_ConnectResponse			= 101,

	VRMsg_ComputeDistortion			= 200,
	VRMsg_ComputeDistortionResponse	= 201,

	VRMsg_GetDriverInfo				= 300,
	VRMsg_GetDriverInfoResponse		= 301,
	VRMsg_GetDisplayInfo			= 302,
	VRMsg_GetDisplayInfoResponse	= 303,
};




struct VRSharedState_Bounds_t
{
	int32_t x, y;
	uint32_t w, h;
};

struct VRSharedState_RenderTargetSize_t
{
	uint32_t w, h;
};

struct VRSharedState_Viewport_t
{
	uint32_t x, y;
	uint32_t w, h;
};

struct VRSharedState_Projection_t
{
	float left, right, top, bottom;
};

struct VRSharedState_Eye_t
{
	VRSharedState_Viewport_t viewport;
	VRSharedState_Projection_t projection;
	vr::HmdMatrix44_t matrix;	
};

struct VRSharedState_Pose_t
{
	uint64_t poseTimeInTicks;
	double poseTimeOffset;
	double defaultPredictionTime;

	HmdQuaternion_t qWorldFromDriverRotation;
	HmdVector3_t vWorldFromDriverTranslation;

	HmdQuaternion_t qDriverFromHeadRotation;
	HmdVector3_t vDriverFromHeadTranslation;

	HmdVector3_t vPosition;
	HmdVector3_t vVelocity;
	HmdVector3_t vAcceleration;
	HmdQuaternion_t qRotation;
	HmdVector3_t vAngularVelocity;
	HmdVector3_t vAngularAcceleration;

	vr::HmdTrackingResult result;

	bool poseIsValid;
	bool willDriftInYaw;
	bool shouldApplyHeadModel;
};


const uint32_t k_VRIdMaxLength = 128;

struct VRSharedState_CurrentHmd_t
{
	char driverId[k_VRIdMaxLength];
	char displayId[k_VRIdMaxLength];
};

struct VRSharedState_t
{
	VRSharedState_CurrentHmd_t hmd;
	VRSharedState_Bounds_t bounds;
	VRSharedState_RenderTargetSize_t renderTargetSize;
	VRSharedState_Eye_t eye[2];
	VRSharedState_Pose_t pose;
};

namespace IPC
{
	class ISharedMem;
	class IMutex;
}

class CVRSharedState
{
	friend class CVRSharedStatePtrBase;
	friend class CVRSharedStatePtr;
	friend class CVRSharedStateWritablePtr;
public:
	enum SharedStateRole
	{
		Client,
		Server
	};

	bool BInit( SharedStateRole eRole );
	void Cleanup();

protected:
	void LockSharedMem();
	void UnlockSharedMem();
	const VRSharedState_t *GetSharedState();
	VRSharedState_t *GetWritableSharedState();

private:
	IPC::ISharedMem *m_pSharedStateMem;
	IPC::IMutex *m_pSharedMutex;
};

class CVRSharedStatePtrBase
{
public:
	CVRSharedStatePtrBase(CVRSharedState *pSharedState);
	~CVRSharedStatePtrBase();

protected:
	CVRSharedState *m_pSharedState;
};

class CVRSharedStatePtr : public CVRSharedStatePtrBase
{
public:
	CVRSharedStatePtr(CVRSharedState *pSharedState) : CVRSharedStatePtrBase(pSharedState) {}
	const VRSharedState_t *operator ->();
};

class CVRSharedStateWritablePtr : public CVRSharedStatePtrBase
{
public:
	CVRSharedStateWritablePtr(CVRSharedState *pSharedState) : CVRSharedStatePtrBase(pSharedState) {}
	VRSharedState_t *operator ->();
};

#pragma pack(pop)
