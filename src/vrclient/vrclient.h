//========= Copyright Valve Corporation ============//
#pragma once

#include "vrcommon/ipcpipe.h"
#include "vrcommon/ipctools.h"
#include "vrcommon/vripcconstants.h"

struct VRSharedState_t;

/** handles the client endpoint of an IPC connection with
* the VR server */
class CVRClient
{
public:
	vr::HmdError Init();
	void Cleanup();
	
	CVRSharedState *GetSharedState() { return &m_sharedState; }

	/** Posts a protobuf message to the pipe. Returns false if the pipe closes before the
	* message is sent. A true return value doesn't guarantee that the message was ever
	* processed on the other end of the pipe.
	* Used by: Clients and Servers.
	*/
	template<typename Request, typename Response>
	bool SendProtobufMessageAndWaitForResponse( VRMsgType unMessageType, const Request & msg, VRMsgType unResponseMessageType, Response & response )
	{
		return m_pipe.SendProtobufMessageAndWaitForResponse( unMessageType, msg, unResponseMessageType, response, 1000 );
	}

	vr::DistortionCoordinates_t ComputeDistortion( vr::Hmd_Eye eEye, float fU, float fV );
protected:
	bool BStartVRServer();
	
private:
	CIPCPipe m_pipe;

	CVRSharedState m_sharedState;
};

