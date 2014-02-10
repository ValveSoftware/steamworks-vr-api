//========= Copyright Valve Corporation ============//
#include "vrclient.h"

#include "vrcommon/pathtools.h"
#include "vrcommon/vripcconstants.h"
#include "vrcommon/vrlog.h"
#include "vrcommon/threadtools.h"
#include "vrcommon/processtools.h"
#include "vr_messages.pb.h"
#include "SDL_timer.h"
#include "SDL_filesystem.h"

using namespace vr;

bool CVRClient::BInit()
{
	// log files go in prefpath/logs
	char *pchUserConfigPath = SDL_GetPrefPath( "", "steamvr" );
	if( !pchUserConfigPath )
	{
		return false;
	}
	std::string sConfigPath = pchUserConfigPath;
	SDL_free( pchUserConfigPath );

	std::string sLogDir = Path_Join( sConfigPath, "logs" );

	InitLog( sLogDir.c_str(), "client" );

	int nConnectAttempt = 0;
	uint32_t unPid = ThreadGetCurrentProcessId();
	bool bStartedServer = false;

	while( nConnectAttempt++ < 10 )
	{
		Log( "PID %u connection attempt %d...\n", unPid, nConnectAttempt );

		// try to connect to the pipe
		if( !m_pipe.ConnectPipe( k_PipeName ) )
		{
			if( !bStartedServer )
			{
				Log( "Unable to connect to VR pipe %s. Attempting to start vrserver\n", k_PipeName );
				if( !BStartVRServer() )
				{
					Log( "Failed to start vrserver. Giving up\n" );
					return false;
				}
				bStartedServer = true;
			}
			else
			{
				Log( "Unable to connect to pipe. Already started server. Waiting for a bit and trying connection again\n" );
			}
		}
		else
		{
			CVRMsg_Connect msgConnect;
			msgConnect.set_hmd_interface_version( IHmd_Version );
			msgConnect.set_pid( unPid );

			CVRMsg_ConnectResponse msgConnectResponse;

			if( !m_pipe.SendProtobufMessageAndWaitForResponse( VRMsg_Connect, msgConnect, VRMsg_ConnectResponse, msgConnectResponse, 100 ) )
			{
				// FAILED! Retry
				Log( "Invalid response to connect message. Connect failed\n" );
				m_pipe.ClosePipe();
			}
			else if( msgConnectResponse.result() != HmdError_None )
			{
				// FAILED! Don't retry
				Log( "Received connect response %d. Giving up.\n", msgConnectResponse.result() );
				return false;
			}
			else
			{
				// SUCCESS!
				Log( "Received success response from connect\n" );

				// hook up our shared state to the shared mem
				return m_sharedState.BInit( CVRSharedState::Client );
			}

		}

		SDL_Delay( 100 );
	}

	Log( "Giving up server connection after %d attempts\n", nConnectAttempt - 1 );

	return false;
}


bool CVRClient::BStartVRServer()
{
	std::string sPath = Path_StripFilename( Path_GetModulePath() );

	if( sPath.empty() )
	{
		Log( "Unable to find path to current module, so vrserver can't be started\n" );
		return false;
	}

#if defined( _WIN32 )
	static const char *pchSrvExeName = "vrserver.exe";
#elif defined( LINUX )
	static const char *pchSrvExeName = "vrserver_linux";
#elif defined( OSX )
	static const char *pchSrvExeName = "vrserver_osx";
#else
#error "Unknown platform"
#endif
	std::string sSrvPath = Path_Join( sPath, pchSrvExeName );

	Log( "Starting vrserver process: %s\n", sSrvPath.c_str() );
	
	const char *argv[] =
	{
		sSrvPath.c_str(),
		NULL
	};

	return BCreateProcess( sPath.c_str(), argv );
}



void CVRClient::Cleanup()
{
	m_pipe.ClosePipe();

	m_sharedState.Cleanup();

	CleanupLog();
}

DistortionCoordinates_t CVRClient::ComputeDistortion( vr::Hmd_Eye eEye, float fU, float fV )
{
	CVRMsg_ComputeDistortion msg;
	msg.set_eye( eEye );
	msg.set_u( fU );
	msg.set_v( fV );
	CVRMsg_ComputeDistortionResponse msgResponse;
	if( m_pipe.SendProtobufMessageAndWaitForResponse( VRMsg_ComputeDistortion, msg, VRMsg_ComputeDistortionResponse, msgResponse, 100 ) )
	{
		DistortionCoordinates_t coords;
		coords.rfRed[0] = msgResponse.red_u();
		coords.rfRed[1] = msgResponse.red_v();
		coords.rfGreen[0] = msgResponse.green_u();
		coords.rfGreen[1] = msgResponse.green_v();
		coords.rfBlue[0] = msgResponse.blue_u();
		coords.rfBlue[1] = msgResponse.blue_v();
		return coords;
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

