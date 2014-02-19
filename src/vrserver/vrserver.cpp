//========= Copyright Valve Corporation ============//
#include "vr_controlpanel.h"
#include "vrcommon/pathtools.h"
#include "stdio.h"
#include "ihmddriver.h"
#include "ihmddriverprovider.h"

#include "SDL_loadso.h"
#include "SDL_filesystem.h"

#include "vrcommon/ipcpipe.h"
#include "vrcommon/threadtools.h"
#include "vrcommon/vripcconstants.h"
#include "vrcommon/dirtools.h"
#include "vrcommon/pathtools.h"
#include "vrcommon/strtools.h"
#include "vrcommon/hmdlog.h"
#include "vrcommon/hmdplatform_private.h"
#include "vrcommon/timeutils.h"
#include "vrcommon/envvartools.h"

#include "vr_messages.pb.h"

#include <vector>

using namespace vr;

class CConnectionThread;
class CListenThread;

class CVRClientContext
{
public:
	CVRClientContext() {}

};

class CVRServer : public vr::IPoseListener
{
	friend class CListenThread;
	friend class CConnectionThread;
public:
	CVRServer();
	~CVRServer();

	HmdError Init();
	int Run();
	void Cleanup();

	bool BIsShuttingDown() const { return m_bIsShuttingDown; }

	// Causes the server to keep running after the last connection drops
	void SetKeepAlive( bool bKeepAlive ) { m_bKeepAlive = bKeepAlive; }

	// IPoseListener interface
	virtual void PoseUpdated( IHmdDriver *pDriver, const DriverPose_t & newPose ) OVERRIDE;

protected:
	void NewConnection( CIPCPipe *pPipe );

	bool OnMsg_Connect( CVRClientContext * pContext, const CVRMsg_Connect & msg, CVRMsg_ConnectResponse & response );
	bool OnMsg_ComputeDistortion( CVRClientContext * pContext, const CVRMsg_ComputeDistortion & msg, CVRMsg_ComputeDistortionResponse & response );
	bool OnMsg_GetDriverInfo( CVRClientContext * pContext, const CVRMsg_GetDriverInfo & msg, CVRMsg_GetDriverInfoResponse & response );
	bool OnMsg_GetDisplayInfo( CVRClientContext * pContext, const CVRMsg_GetDisplayInfo & msg, CVRMsg_GetDisplayInfoResponse & response );
private:
	struct HmdDriver_t
	{
		std::string sName;
		void *pModule;
		IHmdDriverProvider *pDriverProvider;
	};

	bool RunFrame();
	HmdError SetHmdDriver( HmdDriver_t *pDriver, IHmdDriver *pHmdDriver );
	void ResetDriverSharedState();

	std::vector< HmdDriver_t > m_vecDrivers;

	std::string m_sUserConfigPath;

	/** Reads the user config file. If the file was not found all config
	* values will stay at their defaults */
	void ReadUserConfig();

	/** writes out all user config values. Returns false if the file couldn't
	* be written for some reason */
	bool BWriteUserConfig();

	void LoadDriver( const std::string & sFullDriverPath, const std::string & sLibSubPath, const std::string & sShortName );
	void CleanupDriver( HmdDriver_t *pDriver );

	HmdDriver_t *FindDriver( const std::string & sDriverId );

	bool m_bKeepAlive;
	bool m_bHasHadConnection;
	bool m_bIsShuttingDown;

	std::string m_sCurrentDriverId;
	std::string m_sCurrentHmdId;

	IHmdDriver *m_pCurrentDriver;

	std::vector<CConnectionThread *> m_vecConnections;
	CListenThread *m_pListener;

	CThreadMutex m_mutexConnection;

	CVRSharedState m_sharedState;
};


class CConnectionThread : public CThread
{
public:
	CConnectionThread( CIPCPipe *pPipe, CVRServer *pServer )
	{
		m_pPipe = pPipe;
		m_pServer = pServer;
		m_pContext = new CVRClientContext();
	}

	~CConnectionThread()
	{
		delete m_pPipe;
		m_pPipe = NULL;

		delete m_pContext;
	}

	template< typename msg_t, typename response_t>
	bool HandleMessage( uint32_t unMessagePayloadLength, VRMsgType responseType, bool (CVRServer::*pFunc) ( CVRClientContext *, const msg_t &, response_t & ) )
	{
		msg_t msg;
		response_t response;
		if( !m_pPipe->GetProtobufPayload( unMessagePayloadLength, msg ) )
			return false;

		if( !(m_pServer->*pFunc)( m_pContext, msg, response ) )
			return false;

		return m_pPipe->SendProtobufMessage( responseType, response );
	}

	virtual int Run() OVERRIDE
	{
		uint32_t unMessageType, unMessagePayloadLength;
		bool bContinue = true;
		while( m_pPipe 
			&& m_pPipe->GetNextMessage( &unMessageType, &unMessagePayloadLength, 100 ) 
			&& bContinue 
			&& !m_pServer->BIsShuttingDown() )
		{
			switch( unMessageType )
			{
			case VRMsg_Connect:
				{
					bContinue = HandleMessage<CVRMsg_Connect, CVRMsg_ConnectResponse>( unMessagePayloadLength, VRMsg_ConnectResponse, &CVRServer::OnMsg_Connect );
				}
				break;

			case VRMsg_ComputeDistortion:
				{
					bContinue = HandleMessage<CVRMsg_ComputeDistortion, CVRMsg_ComputeDistortionResponse>( unMessagePayloadLength, VRMsg_ComputeDistortionResponse, &CVRServer::OnMsg_ComputeDistortion );
				}
				break;

			case VRMsg_GetDriverInfo:
				{
					bContinue = HandleMessage<CVRMsg_GetDriverInfo, CVRMsg_GetDriverInfoResponse>( unMessagePayloadLength, VRMsg_GetDriverInfoResponse, &CVRServer::OnMsg_GetDriverInfo );
				}
				break;

			case VRMsg_GetDisplayInfo:
				{
					bContinue = HandleMessage<CVRMsg_GetDisplayInfo, CVRMsg_GetDisplayInfoResponse>( unMessagePayloadLength, VRMsg_GetDisplayInfoResponse, &CVRServer::OnMsg_GetDisplayInfo );
				}
				break;

			case 0:
				// message type 0 means we timed out. That's ok.
				break;

			default:
				// read the payload just so the stream doesn't get corrupted
				if( unMessagePayloadLength )
				{
					void *pvPayload = malloc( unMessagePayloadLength );
					m_pPipe->GetMessagePayload( pvPayload, unMessagePayloadLength );
					free( pvPayload );
				}
				Log( "Unknown message type %d (%d byte payload)\n", unMessageType, unMessagePayloadLength );
			}
		}

		Log( "Lost IPC connection\n" );

		return 0;
	}
private:
	CIPCPipe *m_pPipe;
	CVRServer *m_pServer;
	CVRClientContext *m_pContext;
};


class CListenThread : public CThread
{
public:
	CListenThread( CVRServer *pServer )
	{
		m_pPipe = NULL;
		m_pServer = pServer;
	}
	~CListenThread()
	{
		delete m_pPipe;
		m_pPipe = NULL;
	}

	virtual bool Init() OVERRIDE
	{
		m_pPipe = new CIPCPipe();
		return 		m_pPipe->CreatePipe( "VR_Pipe" );
	}
	
	virtual int Run() OVERRIDE
	{
		// wait for a connection and then spin up another thread to handle it
		CIPCPipe *pNewConnection;
		while( m_pPipe 
			&& m_pPipe->WaitForConnection( 100, &pNewConnection ) 
			&& !m_pServer->BIsShuttingDown() )
		{
			// if we didn't get a new connection it just means we timed out.
			// go ahead and loop again in that case
			if( pNewConnection )
			{
				m_pServer->NewConnection( pNewConnection );
			}
		}

		Log( "Listener thread ending\n" );

		return 0;
	}

	virtual void OnExit() OVERRIDE
	{

	}

private:
	CIPCPipe *m_pPipe;
	CVRServer *m_pServer;
};


CVRServer::CVRServer()
{
	m_pCurrentDriver = NULL;
	m_bKeepAlive = false;
	m_bHasHadConnection = false;
	m_bIsShuttingDown = false;
}

CVRServer::~CVRServer()
{

}

std::string GetDriverBaseDir( )
{
	static const char *k_pchDriverRelativePath = "..\\drivers";

	std::string sModulePath = Path_StripFilename( GetThisModulePath() );

	return Path_MakeAbsolute( k_pchDriverRelativePath, sModulePath );
}



HmdError CVRServer::Init()
{
	m_sUserConfigPath = GetEnvironmentVariable( "VR_CONFIG_PATH" );
	if( m_sUserConfigPath.empty() )
	{
		m_sUserConfigPath = Path_MakeAbsolute( "../config", Path_StripFilename( Path_GetExecutablePath() ) );
	}

	std::string sLogPath = GetEnvironmentVariable( "VR_LOG_PATH" );
	if( sLogPath.empty() )
	{
		sLogPath = Path_MakeAbsolute( "../logs", Path_StripFilename( Path_GetExecutablePath() ) );
	}

	InitLog( sLogPath.c_str(), "vrserver" );
	Log( "VR server starting up with config=%s\n", m_sUserConfigPath.c_str() );

	std::string sDriverBaseDir = GetDriverBaseDir();

	if( !BCreateDirectoryRecursive( m_sUserConfigPath.c_str() ) )
	{
		Log( "Unable to create user pref directory %s\n", m_sUserConfigPath.c_str() );
		return HmdError_Init_UserConfigDirectoryInvalid;
	}

	ReadUserConfig();

	CDirIterator dirIter( sDriverBaseDir.c_str(), "*" );

	while( dirIter.BNextFile() )
	{
		if( dirIter.BCurrentIsDir() )
		{
			std::string sFullDriverPath = Path_MakeAbsolute( dirIter.CurrentFileName(), sDriverBaseDir );

			char rchRelativeDriverPath[ MAX_PATH ];
#if defined(_WIN32)
			sprintf_s( rchRelativeDriverPath, MAX_PATH, "bin/driver_%s%s", 
				dirIter.CurrentFileName().c_str(), HMD_DLL_EXT_STRING );
#else
			sprintf( rchRelativeDriverPath, "bin/driver_%s%s", 
				dirIter.CurrentFileName().c_str(), HMD_DLL_EXT_STRING );
#endif
			LoadDriver( sFullDriverPath, rchRelativeDriverPath, dirIter.CurrentFileName() );
		}
	}

	if( !m_sharedState.BInit( CVRSharedState::Server ) )
	{
		return HmdError_IPC_SharedStateInitFailed;
	}

	m_pListener = new CListenThread( this );
	if( !m_pListener->Start() )
	{
		Log( "Failed to start listener\n" );
		return HmdError_IPC_ServerInitFailed;
	}

	return HmdError_None;
}

typedef void* (*HmdDriverFactoryFn)(const char *pInterfaceName, int *pReturnCode);

/** Loads a single driver and adds it to the list */
void CVRServer::LoadDriver( const std::string & sFullDriverPath, const std::string & sLibSubPath, const std::string & sShortName )
{
	std::string sLibFullPath = Path_FixSlashes( Path_Join( sFullDriverPath, sLibSubPath ) );
	std::string sUserConfigFullPath = Path_FixSlashes( Path_Join( m_sUserConfigPath, sShortName ) );
	if( !BCreateDirectoryRecursive( sUserConfigFullPath.c_str() ) )
	{
		// if we can't create a config directory for this driver something is seriously wrong, 
		// so there isn't much point in actually loading the DLL
		SDL_LogWarn( HMD_LOG_DRIVER, "Unable to create user config dir %s for driver %s.\n", sUserConfigFullPath.c_str(), sShortName.c_str() );
		return;
	}

	HmdDriver_t driver;
	driver.sName = sShortName;
	std::string sOldCwd = Path_GetWorkingDirectory();
	Path_SetWorkingDirectory( Path_StripFilename( sLibFullPath ) );
	driver.pModule = SDL_LoadObject( sLibFullPath.c_str() );
	Path_SetWorkingDirectory( sOldCwd );
	if( !driver.pModule )
	{
		SDL_LogWarn( HMD_LOG_DRIVER, "Unable to load driver %s from %s.\n", sShortName.c_str(), sLibFullPath.c_str() );
		return;
	}

	HmdDriverFactoryFn fnFactory = (HmdDriverFactoryFn)( SDL_LoadFunction( driver.pModule, "HmdDriverFactory" ) );

	if( !fnFactory )
	{
		SDL_UnloadObject( driver.pModule );
		SDL_LogWarn( HMD_LOG_DRIVER, "Could not find factor function in driver %s from %s.\n", sShortName.c_str(), sLibFullPath.c_str() );
		return;
	}

	int nReturnCode = 0;
	driver.pDriverProvider = static_cast<IHmdDriverProvider *>( (void*) fnFactory( IHmdDriverProvider_Version, &nReturnCode ) );
	if( !driver.pDriverProvider || nReturnCode != HmdError_None )
	{
		SDL_UnloadObject( driver.pModule );
		SDL_LogWarn( HMD_LOG_DRIVER, "Could not create interface in driver %s from %s.\n", sShortName.c_str(), sLibFullPath.c_str() );
		return;
	}

	HmdError err = driver.pDriverProvider->Init( sUserConfigFullPath.c_str(), sFullDriverPath.c_str() );
	if( err != HmdError_None )
	{
		SDL_UnloadObject( driver.pModule );
		SDL_LogWarn( HMD_LOG_DRIVER, "error %d when initing driver %s from %s.\n", sShortName.c_str(), sLibFullPath.c_str() );
		return;
	}

	m_vecDrivers.push_back( driver );
}



void CVRServer::NewConnection( CIPCPipe *pPipe )
{
	CAutoLock lock( m_mutexConnection );

	CConnectionThread *pThread = new CConnectionThread( pPipe, this );
	if( !pThread->Start() )
	{
		Log( "Unable to start thread for connection\n" );
	}
	else
	{
		m_vecConnections.push_back( pThread );

		m_bHasHadConnection = true;
	}
}



int CVRServer::Run()
{
	while( RunFrame() )
	{
		ThreadSleep( 10 );
	}

	Log( "VR server shutting down\n" );
	m_bIsShuttingDown = true;

	// wait for the other threads
	if( m_pListener )
	{
		m_pListener->Join( 5000 );
		Log( "Listener thread joined\n" );
	}

	delete m_pListener;
	m_pListener = NULL;
	
	{
		CAutoLock lock( m_mutexConnection );
		for( std::vector< CConnectionThread *>::iterator i = m_vecConnections.begin(); i != m_vecConnections.end(); i++ )
		{
			// wait for this connection thread to exit
			(*i)->Join( 1000 );

			delete (*i);
		}
		m_vecConnections.clear();
	}

	return 0;
}

bool CVRServer::RunFrame()
{
	// if we lose the listener, quit
	if( !m_pListener || !m_pListener->IsAlive() )
	{
		return false;
	}

	// clean up any dead connections
	{
		CAutoLock lock( m_mutexConnection );
		for( std::vector< CConnectionThread *>::iterator i = m_vecConnections.begin(); i != m_vecConnections.end(); i++ )
		{
			// the thread ends when the connection drops
			if( !(*i)->IsAlive() )
			{
				m_vecConnections.erase( i );
				break; // we'll get other disconnections next frame
			}
		}
	}

	return m_vecConnections.size() > 0  || !m_bHasHadConnection || m_bKeepAlive;
}


/** cleans up and unloads a single driver */
void CVRServer::CleanupDriver( HmdDriver_t *pDriver )
{
	if( pDriver->pDriverProvider )
	{
		pDriver->pDriverProvider->Cleanup();
		pDriver->pDriverProvider = NULL;
	}
	if( pDriver->pModule )
	{
		SDL_UnloadObject( pDriver->pModule );
		pDriver->pModule = NULL;
	}
}



void CVRServer::Cleanup()
{
	if( m_pListener )
		m_pListener->Stop();

	for( std::vector< HmdDriver_t >::iterator iDriver = m_vecDrivers.begin(); iDriver != m_vecDrivers.end(); iDriver++ )
	{
		CleanupDriver( &(*iDriver) );
	}
	m_vecDrivers.clear();

	m_sharedState.Cleanup();

	CleanupLog();
}


const char *k_pchVRUserConfigFileName = "steamvr.cfg";

void CVRServer::ReadUserConfig()
{
	std::string sFile = Path_Join( m_sUserConfigPath, k_pchVRUserConfigFileName);
	FILE *f = fopen( sFile.c_str(), "rt" );
	if( !f )
		return;

	char buf[1024];

	while( fgets( buf, sizeof(buf), f ) )
	{
		// get rid of the trailing \n
		int len = strlen( buf );
		if( len > 0 && buf[ len - 1 ] == '\n' )
		{
			buf[len-1] = '\0';
			len--;
		}

		char *token = buf;
		char *next = strpbrk( buf, "=" );
		if( !next )
			continue;
		*next = '\0';
		next++;

		if( !stricmp( token, "CurrentHmd" ) )
		{
			char *id = strpbrk( next, ":" );
			if( id )
			{
				*id = '\0';
				id++;
				m_sCurrentHmdId = id;
				m_sCurrentDriverId = next;
			}
		}
	}

	fclose( f );
}

/** writes out all user config values. Returns false if the file couldn't
* be written for some reason */
bool CVRServer::BWriteUserConfig()
{
	std::string sFile = Path_Join( m_sUserConfigPath, k_pchVRUserConfigFileName );
	FILE *f = fopen( sFile.c_str(), "wt" );
	if( !f )
		return false;

	fprintf( f, "CurrentHmd=%s:%s\n", m_sCurrentDriverId.c_str(), m_sCurrentHmdId.c_str() );

	fclose( f );

	return true;
}


/** finds the driver by ID or returns NULL if it wasn't found */
CVRServer::HmdDriver_t *CVRServer::FindDriver( const std::string & sDriverId )
{
	for( std::vector< HmdDriver_t >::iterator iDriver = m_vecDrivers.begin(); iDriver != m_vecDrivers.end(); iDriver++ )
	{
		if( iDriver->sName == sDriverId )
			return &(*iDriver );
	}

	return NULL;
}


// -----------------------------------------------------------------------------------------
//                                    MESSAGE HANDLERS
// -----------------------------------------------------------------------------------------
bool CVRServer::OnMsg_Connect( CVRClientContext * pContext, const CVRMsg_Connect & msg, CVRMsg_ConnectResponse & response )
{
	// if we haven't picked a HmdDriver yet, do that now
	if( !m_pCurrentDriver )
	{
		// if we have a configured, driver, pick that one
		if( !m_sCurrentDriverId.empty() && !m_sCurrentHmdId.empty() )
		{
			// look for the right driver
			for( std::vector< HmdDriver_t >::iterator i = m_vecDrivers.begin(); i != m_vecDrivers.end(); i++ )
			{
				if( i->sName != m_sCurrentDriverId )
					continue;

				if( i->pDriverProvider )
				{
					IHmdDriver *pHmdDriver = i->pDriverProvider->FindHmd( m_sCurrentHmdId.c_str() );
					if( pHmdDriver )
					{
						HmdError err = SetHmdDriver( &*(i), pHmdDriver );
						if( err != HmdError_None )
						{
							Log( "Unable to set %s.%s as the driver. Err=%d\n", i->sName.c_str(), pHmdDriver->GetId(), err );
						}
					}
					else
					{
						Log( "Unable to set %s.%s as the display because it was not found\n", m_sCurrentDriverId.c_str(), m_sCurrentHmdId.c_str() );
					}
					break;
				}
			}
		}

		// see if we didn't find our current value
		if( !m_pCurrentDriver )
		{
			// otherwise, for now, just pick the first display in the first driver that actually activates
			for( std::vector< HmdDriver_t >::iterator i = m_vecDrivers.begin(); i != m_vecDrivers.end(); i++ )
			{
				if( i->pDriverProvider && i->pDriverProvider->GetHmdCount() > 0 )
				{
					IHmdDriver *pHmdDriver = i->pDriverProvider->GetHmd( 0 );
					if( pHmdDriver )
					{
						HmdError err = SetHmdDriver( &(*i), pHmdDriver );
						if( err == HmdError_None )
						{
							// remember the Hmd we found
							m_sCurrentDriverId = i->sName;
							m_sCurrentHmdId = pHmdDriver->GetId();
							BWriteUserConfig();
						}
						else
						{
							Log( "Unable to set %s.%s as the driver. Err=%d\n", i->sName.c_str(), pHmdDriver->GetId(), err );
						}

						break;
					}
				}
			}
		}
	}

	// if we found an Hmd, there's no error
	if( m_pCurrentDriver )
	{
		response.set_result( HmdError_None );
	}
	else
	{
		response.set_result( HmdError_Init_HmdNotFound );
	}

	return true;
}

bool CVRServer::OnMsg_ComputeDistortion( CVRClientContext * pContext, const CVRMsg_ComputeDistortion & msg, CVRMsg_ComputeDistortionResponse & response )
{
	if( m_pCurrentDriver )
	{
		Hmd_Eye eEye = (Hmd_Eye)msg.eye();
		if( eEye != Eye_Left && eEye !=  Eye_Right )
		{
			Log( "Invalid eye %d in ComputeDistortion\n", msg.eye() );
			return false;
		}

		DistortionCoordinates_t coords = m_pCurrentDriver->ComputeDistortion( eEye, msg.u(), msg.v() );

		response.set_red_u( coords.rfRed[0] );
		response.set_red_v( coords.rfRed[1] );
		response.set_green_u( coords.rfGreen[0] );
		response.set_green_v( coords.rfGreen[1] );
		response.set_blue_u( coords.rfBlue[0] );
		response.set_blue_v( coords.rfBlue[1] );

		return true;
	}
	else
	{
		Log( "ComputeDistortion called on a server with no current Hmd\n" );
		return false;
	}
}


bool CVRServer::OnMsg_GetDriverInfo( CVRClientContext * pContext, const CVRMsg_GetDriverInfo & msg, CVRMsg_GetDriverInfoResponse & response )
{
	for( std::vector< HmdDriver_t >::iterator i = m_vecDrivers.begin(); i != m_vecDrivers.end(); i++ )
	{
		CVRMsg_GetDriverInfoResponse_DriverInfo *pDriverInfo = response.add_driver();
		pDriverInfo->set_driver_id( i->sName );
	}

	return true;
}

bool CVRServer::OnMsg_GetDisplayInfo( CVRClientContext * pContext, const CVRMsg_GetDisplayInfo & msg, CVRMsg_GetDisplayInfoResponse & response )
{
	HmdDriver_t *pDriver = FindDriver( msg.driver_id() );
	if( pDriver && pDriver->pDriverProvider )
	{
		uint32_t count = pDriver->pDriverProvider->GetHmdCount();
		for( uint32_t i=0; i<count; i++ )
		{
			IHmdDriver *pHmd = pDriver->pDriverProvider->GetHmd( i );
			CVRMsg_GetDisplayInfoResponse_DisplayInfo *pDisplayInfo = response.add_display();
			pDisplayInfo->set_display_id( pHmd->GetId() );
			pDisplayInfo->set_model_number( pHmd->GetModelNumber() );
			pDisplayInfo->set_serial_number( pHmd->GetSerialNumber() );
		}
	}
	return true;
}

// --------------------------------------------------------------------------
// Deactivates the old HmdDriver (if any), and activates the new HMD driver.
// --------------------------------------------------------------------------
HmdError CVRServer::SetHmdDriver( HmdDriver_t *pDriver, IHmdDriver *pHmdDriver )
{
	if( m_pCurrentDriver )
	{
		m_pCurrentDriver->Deactivate();
		m_pCurrentDriver = NULL;
	}

	if( pHmdDriver )
	{
		m_sCurrentDriverId = pDriver->sName;

		HmdError err = pHmdDriver->Activate( this );
		if( err != HmdError_None )
			return err;

		m_pCurrentDriver = pHmdDriver;
		ResetDriverSharedState();
	}

	return HmdError_None;
}


void CVRServer::ResetDriverSharedState()
{
	CVRSharedStateWritablePtr data( &m_sharedState );

	m_pCurrentDriver->GetWindowBounds(
		&data->bounds.x,
		&data->bounds.y,
		&data->bounds.w,
		&data->bounds.h
		);

	strcpy( data->hmd.driverId, m_sCurrentDriverId.c_str() );
	strcpy( data->hmd.displayId, m_sCurrentHmdId.c_str() );

	m_pCurrentDriver->GetRecommendedRenderTargetSize( &data->renderTargetSize.w, &data->renderTargetSize.h );

	for( int nEye = 0; nEye < 2; nEye++ )
	{
		data->eye[ nEye ].matrix = m_pCurrentDriver->GetEyeMatrix( (Hmd_Eye)nEye );

		m_pCurrentDriver->GetProjectionRaw( (Hmd_Eye)nEye, 
			&data->eye[ nEye ].projection.left,
			&data->eye[ nEye ].projection.right,
			&data->eye[ nEye ].projection.top,
			&data->eye[ nEye ].projection.bottom
			);

		m_pCurrentDriver->GetEyeOutputViewport( (Hmd_Eye) nEye,
			&data->eye[ nEye ].viewport.x,
			&data->eye[ nEye ].viewport.y,
			&data->eye[ nEye ].viewport.w,
			&data->eye[ nEye ].viewport.h
			);
	}

	data->pose.poseIsValid = false;
	data->pose.shouldApplyHeadModel = false;
	data->pose.willDriftInYaw = false;
	data->pose.poseTimeInTicks = 0;
	data->pose.poseTimeOffset = 0;
	data->pose.defaultPredictionTime = 0;
}


void CVRServer::PoseUpdated( IHmdDriver *pDriver, const DriverPose_t & newPose )
{
	// convert the pose to our internal format
	VRSharedState_Pose_t pose;

	pose.poseTimeInTicks = GetSystemTimeInTicks();

	pose.poseTimeOffset = newPose.poseTimeOffset;
	pose.defaultPredictionTime = newPose.defaultPredictionTime;

	pose.qWorldFromDriverRotation.w = newPose.qWorldFromDriverRotation.w;
	pose.qWorldFromDriverRotation.x = newPose.qWorldFromDriverRotation.x;
	pose.qWorldFromDriverRotation.y = newPose.qWorldFromDriverRotation.y;
	pose.qWorldFromDriverRotation.z = newPose.qWorldFromDriverRotation.z;

	pose.qDriverFromHeadRotation.w = newPose.qDriverFromHeadRotation.w;
	pose.qDriverFromHeadRotation.x = newPose.qDriverFromHeadRotation.x;
	pose.qDriverFromHeadRotation.y = newPose.qDriverFromHeadRotation.y;
	pose.qDriverFromHeadRotation.z = newPose.qDriverFromHeadRotation.z;

	pose.poseIsValid = newPose.poseIsValid;
	pose.shouldApplyHeadModel = newPose.shouldApplyHeadModel;
	pose.willDriftInYaw = newPose.willDriftInYaw;

	pose.result = newPose.result;

	for( int i=0; i < 3; i++ )
	{
		pose.vWorldFromDriverTranslation.v[i] = newPose.vecWorldFromDriverTranslation[i];
		pose.vDriverFromHeadTranslation.v[i] = newPose.vecDriverFromHeadTranslation[i];

		pose.vPosition.v[i] = newPose.vecPosition[i];
		pose.vVelocity.v[i] = newPose.vecVelocity[i];
		pose.vAcceleration.v[i] = newPose.vecAcceleration[i];

		pose.vAngularVelocity.v[i] = newPose.vecAngularVelocity[i];
		pose.vAngularAcceleration.v[i] = newPose.vecAngularAcceleration[i];
	}

	pose.qRotation.w = newPose.qRotation.w;
	pose.qRotation.x = newPose.qRotation.x;
	pose.qRotation.y = newPose.qRotation.y;
	pose.qRotation.z = newPose.qRotation.z;

	CVRSharedStateWritablePtr data( &m_sharedState );

	data->pose = pose;
}

int main( int argc, char **argv )
{
	CVRServer server;
	HmdError err = server.Init();
	if( argc == 2 && !stricmp( argv[1], "--keepalive" ) )
	{
		Log( "Setting keepalive from command line\n" );
		server.SetKeepAlive( true );
	}
	if( HmdError_None != err )
	{
		Log( "Failed to start server with error %d\n", err );
		return err;
	}

	int retVal = server.Run();
	server.Cleanup();

	return retVal;
}


