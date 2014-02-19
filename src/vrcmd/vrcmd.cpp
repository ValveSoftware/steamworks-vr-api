//========= Copyright Valve Corporation ============//
#include "vrcommon/hmdplatform_private.h"
#include "vr_controlpanel.h"

#include <stdio.h>
#include <vector>

#include "SDL_timer.h"

#include "vrcommon/ipcpipe.h"
#include "vrcommon/threadtools.h"
#include "vrcommon/processtools.h"
#include "vrcommon/envvartools.h"
#include "vrcommon/pathtools.h"

using namespace vr;

bool PrintControlPanelInfo( IHmd *pHmd, IVRControlPanel *pControlPanel )
{
	std::string sCurrentDriverId, sCurrentDisplayId;
	char buf[1024];
	uint32_t unIdLen = pHmd->GetDriverId( buf, sizeof(buf) );
	if( unIdLen + 1 > 128 )
	{
		printf( "Error: Current driver ID %s is %d characters, which breaks the promise in the interface documentation.\n", buf, unIdLen );
		if( unIdLen + 1 > sizeof( buf ) )
			return false;
	}
	sCurrentDriverId = buf;

	unIdLen = pHmd->GetDisplayId( buf, sizeof(buf) );
	if( unIdLen + 1 > 128 )
	{
		printf( "Error: Current display ID %s is %d characters, which breaks the promise in the interface documentation.\n", buf, unIdLen );
		if( unIdLen + 1 > sizeof( buf ) )
			return false;
	}
	sCurrentDisplayId = buf;


	uint32_t unDriverCount = pControlPanel->GetDriverCount();
	for( uint32_t unDriverIndex = 0; unDriverIndex < unDriverCount; unDriverIndex++ )
	{
		unIdLen = pControlPanel->GetDriverId( unDriverIndex, buf, sizeof(buf) );
		if( unIdLen + 1 > 128 )
		{
			printf( "Error: Driver ID %s for %d is %d characters, which breaks the promise in the interface documentation.\n", buf, unDriverIndex, unIdLen );
			if( unIdLen + 1 > sizeof( buf ) )
				return false;
		}
		std::string sDriverId = buf;

		uint32_t unDisplayCount = pControlPanel->GetDriverDisplayCount( sDriverId.c_str() );
		printf( "Driver %s : %d displays\n", sDriverId.c_str(), unDisplayCount );
		for( uint32_t unDisplayIndex = 0; unDisplayIndex < unDisplayCount; unDisplayIndex++ )
		{
			unIdLen = pControlPanel->GetDriverDisplayId( sDriverId.c_str(), unDisplayIndex, buf, sizeof(buf) );
			if( unIdLen + 1 > 128 )
			{
				printf( "Error: Display ID %s for %d on driver %s is %d characters, which breaks the promise in the interface documentation.\n", buf, unDriverIndex, sDriverId.c_str(), unIdLen );
				if( unIdLen + 1 > sizeof( buf ) )
					return false;
			}
			std::string sDisplayId = buf;

			unIdLen = pControlPanel->GetDriverDisplayModelNumber( sDriverId.c_str(), sDisplayId.c_str(), buf, sizeof(buf) );
			if( unIdLen + 1 > 128 )
			{
				printf( "Error: Model Number %s for %s:%s is %d characters, which breaks the promise in the interface documentation.\n", buf, sDriverId.c_str(), sDisplayId.c_str(), unIdLen );
				if( unIdLen + 1 > sizeof( buf ) )
					return false;
			}
			std::string sModelNumber = buf;

			unIdLen = pControlPanel->GetDriverDisplaySerialNumber( sDriverId.c_str(), sDisplayId.c_str(), buf, sizeof(buf) );
			if( unIdLen + 1 > 128 )
			{
				printf( "Error: Serial Number %s for %s:%s is %d characters, which breaks the promise in the interface documentation.\n", buf, sDriverId.c_str(), sDisplayId.c_str(), unIdLen );
				if( unIdLen + 1 > sizeof( buf ) )
					return false;
			}
			std::string sSerialNumber = buf;

			printf( "\t%s (Serial number %s)\n", sModelNumber.c_str(), sSerialNumber.c_str() );

			if( sDisplayId == sCurrentDisplayId && sDriverId == sCurrentDriverId )
			{
				// for the current display print some extra stuff

				if( pHmd->WillDriftInYaw() )
					printf( "\t\tWill Drift In Yaw\n" );

				int32_t windowX, windowY;
				uint32_t windowWidth, windowHeight;
				pHmd->GetWindowBounds( &windowX, &windowY, &windowWidth, &windowHeight );
				printf("\t\tWindowBounds:   %4d, %4d, %4d, %4d\n", windowX, windowY, windowWidth, windowHeight );

#ifdef WIN32
				printf( "\t\tD3DAdapterIndex:   %d\n", pHmd->GetD3D9AdapterIndex() );
#endif
				uint32_t viewportX, viewportY, viewportWidth, viewportHeight;
				pHmd->GetEyeOutputViewport( Eye_Left, &viewportX, &viewportY, &viewportWidth, &viewportHeight );
				printf( "\t\t Left Viewport: %4d, %4d, %4d, %4d\n", viewportX, viewportY, viewportWidth, viewportHeight );
				pHmd->GetEyeOutputViewport( Eye_Right, &viewportX, &viewportY, &viewportWidth, &viewportHeight );
				printf( "\t\tRight Viewport: %4d, %4d, %4d, %4d\n", viewportX, viewportY, viewportWidth, viewportHeight );

				float projLeft, projRight, projTop, projBottom;
				pHmd->GetProjectionRaw( Eye_Left, &projLeft, &projRight, &projTop, &projBottom );
				printf( "\t\t Left Projection: left=%f, right=%f, top=%f, bottom=%f\n", projLeft, projRight, projTop, projBottom );
				pHmd->GetProjectionRaw( Eye_Right, &projLeft, &projRight, &projTop, &projBottom );
				printf( "\t\tRight Projection: left=%f, right=%f, top=%f, bottom=%f\n", projLeft, projRight, projTop, projBottom );

			}
		}
	}

	return true;
}

class CServerTestThread : public CThread
{
public:

	CServerTestThread( const std::string sPipeName ) 
	{ 
		m_sPipeName = sPipeName; 
		m_bShutdown = false;
	}

	void Shutdown() { m_bShutdown = true; }

	virtual int Run() 
	{
		CIPCPipe listenPipe;
		if( !listenPipe.CreatePipe( m_sPipeName.c_str() ) )
		{
			printf("Unable to open pipe %s\n", m_sPipeName.c_str() );
			return 123;
		}

		while( !m_bShutdown )
		{
			CIPCPipe *connectionPipe;
			if( !listenPipe.WaitForConnection( 100, &connectionPipe ) )
			{
				printf("Lost listen pipe %s\n", m_sPipeName.c_str() );
				return 124;
			}

			// if we didn't get a connection but we did get a true
			// return value, that's a timeout. Loop again.
			if( !connectionPipe )
				continue;

			//printf( "Accepted new connection\n" );

			while( ProcessPipeMessage( *connectionPipe )  && !m_bShutdown )
			{
			}
			
			//printf( "Lost connection\n" );
			delete connectionPipe;

		}

		return 0;
	}

	bool ProcessPipeMessage( CIPCPipe & pipe )
	{
		uint32_t unMessageType, unPayloadLength;
		if( !pipe.GetNextMessage( &unMessageType, &unPayloadLength, 100 ) )
			return false;

		// check to see if the timeout just expired
		if( unMessageType == 0 )
			return true;

		//if( unPayloadLength > 1000 )
		//printf( "Received message %u with %u bytes\n", unMessageType, unPayloadLength );
		if( unPayloadLength > 0 && unPayloadLength < 1000000 )
		{
			void *pvBuffer = StackAlloc( unPayloadLength);
			if( !pipe.GetMessagePayload( pvBuffer, unPayloadLength) )
			{
				printf( "Unable to get %u byte payload\n", unPayloadLength );
				return false;
			}

			// reply with message type +1
			pipe.SendPackedMessage( unMessageType + 1, pvBuffer, unPayloadLength );
		}
		else
		{
			// reply with message type +1
			pipe.SendSimpleMessage( unMessageType + 1 );
		}
		return true;
	}


private:
	std::string m_sPipeName;
	bool m_bShutdown;
};


struct Histogram_t
{
	float lowerbound;
	uint32_t count;
};

void RunPipeTest( const char *pchPipeName, int nIterations, int nDataSize, const char *pchLabel, std::vector<Histogram_t> & vecHistogram )
{
	CIPCPipe pipe;
	if( !pipe.ConnectPipe( pchPipeName ) )
	{
		printf( "%s: Unable to connect to pipe\n", pchLabel );
		return;
	}

	char *pchPayload = NULL;
	char *pchPayloadReturn = NULL;
	if( nDataSize > 0 )
	{
		pchPayload = (char *)malloc( nDataSize );
		pchPayloadReturn = (char *)malloc( nDataSize );
		for( int i=0; i<nDataSize; i++ )
		{
			pchPayload[i] = rand() % 256;
		}
	}

	uint64_t freq = SDL_GetPerformanceFrequency();

	uint64_t delayTime = freq / 100;

	std::vector< uint64_t > vecSamples;
	vecSamples.reserve( nIterations );
	uint64_t worst = 0;
	uint64_t best = UINT64_MAX;
	uint64_t total = 0;
	uint64_t reportThreshold = freq / 10000;
	int nErrors = 0;
	vecSamples.reserve( nIterations );
	for( int i=0; i<nIterations; i++ )
	{
		uint64_t startTime = SDL_GetPerformanceCounter();
		uint64_t endTime;
		uint32_t unMsgSent = ( rand()%1000000 ) + 1;

		pipe.SendPackedMessage( unMsgSent, pchPayload, nDataSize );

		uint32_t unMessageType, unPayloadLength;

		if( !pipe.GetNextMessage( &unMessageType, &unPayloadLength, 1000 ) )
		{
			nErrors++;
			break;
		}

		if( unMessageType != unMsgSent+1 || unPayloadLength != (uint32_t)nDataSize )
		{
			nErrors++;
			break;
		}

		if( !pipe.GetMessagePayload( pchPayloadReturn, unPayloadLength ) )
		{
			nErrors++;
			break;
		}

		endTime = SDL_GetPerformanceCounter();

		if( 0 != memcmp( pchPayload, pchPayloadReturn, nDataSize ) )
		{
			nErrors++;
		}

		uint64_t diff = endTime - startTime;
		if( diff > worst )
		{
			worst = diff;
		}
		if( diff < best )
			best = diff;
		total += diff;

		if( diff > reportThreshold )
		{
			//printf("Bad time %f at %f\n", (double)diff * 1000.0 / (double)freq, (double)endTime * 1000.0 / (double)freq );
		
		}
		vecSamples.push_back( diff );

		endTime = SDL_GetPerformanceCounter();
		while( ( SDL_GetPerformanceCounter()  - endTime ) < delayTime )
		{
		}
	}

	free( pchPayload );
	pipe.ClosePipe();

	double worstMS = (double)worst * 1000.0 / (double)freq;
	double bestMS = (double)best * 1000.0 / (double)freq;
	double averageMS = (double)total * 1000.0 / ((double)freq* (double)nIterations);

	printf( "%-11s%10d%10d%15lf%15lf%15lf%15d\n",
		pchLabel, nDataSize, nIterations, worstMS, bestMS, averageMS, nErrors );

	double chunkSize = (worstMS - bestMS) / 10.0;
	vecHistogram.clear();
	vecHistogram.reserve( 10 );
	for( int i=0; i<10; i++ )
	{
		Histogram_t hist;
		hist.lowerbound = (float)(bestMS + (double)i * chunkSize);
		hist.count = 0;
		vecHistogram.push_back( hist );
	}

	for( std::vector<uint64_t>::iterator i = vecSamples.begin(); i != vecSamples.end(); i++ )
	{
		double sampleMS = (double)(*i) * 1000.0 / (double)freq;

		uint32_t unBucket = (uint32_t) ( (sampleMS - bestMS) / chunkSize );
		if( unBucket >= 10 )
			unBucket = 9;
		vecHistogram[ unBucket ].count++;
	}

	SDL_Delay( 100 );
}

void PrintHistogram( int nDataSize, const std::vector<Histogram_t> & vecHistogram )
{

	printf( "Histogram for");
	for( std::vector<Histogram_t>::const_iterator i = vecHistogram.begin(); i != vecHistogram.end(); i++ )
	{
		printf("  %7.3f", i->lowerbound );
	}
	printf("\n");

	printf( "%7d bytes", nDataSize );
	for( std::vector<Histogram_t>::const_iterator i = vecHistogram.begin(); i != vecHistogram.end(); i++ )
	{
		printf("  %7d", i->count);
	}
	printf("\n\n");

}


int TestPipe()
{
	char rchPipeName[128];
	sprintf( rchPipeName, "testpipe%d", SDL_GetTicks() );

	CServerTestThread serverThread( rchPipeName );
	serverThread.Start();

	SDL_Delay(10);

	printf( "%-11s%10s%10s%15s%15s%15s%15s\n",
		rchPipeName,
		"Payload",
		"Iter",
		"Worst (ms)",
		"Best (ms)",
		"Average (ms)",
		"Errors" );
	printf( "%-11s%10s%10s%15s%15s%15s%15s\n",
		"",
		"--------",
		"--------",
		"------------",
		"------------",
		"------------",
		"------------" );


	std::vector<Histogram_t> vecHist0, vecHist10, vecHist100, vecHist1000, vecHist100000;
	RunPipeTest( rchPipeName, 100, 0, "No Data", vecHist0 );
	RunPipeTest( rchPipeName, 100, 10, "Small Data", vecHist10 );
	RunPipeTest( rchPipeName, 100, 100, "Medium Data", vecHist100 );
	RunPipeTest( rchPipeName, 100, 1000, "Large Data", vecHist1000 );
	RunPipeTest( rchPipeName, 100, 100000, "Huge Data", vecHist100000 );

	serverThread.Shutdown();
	
	printf("\n\n");

	PrintHistogram( 0, vecHist0 );
	PrintHistogram( 10, vecHist10 );
	PrintHistogram( 100, vecHist100 );
	PrintHistogram( 1000, vecHist1000 );
	PrintHistogram( 1000000, vecHist100000 );

	return 0;
}


int main( int argc, char **argv )
{
	if( argc == 2 && !strcmp( argv[1], "--testpipe" ) )
		return TestPipe();

	std::string sExeDir = Path_StripFilename( Path_GetExecutablePath() );
	std::string sVRDir = Path_MakeAbsolute( "..", sExeDir );

	// for now just dump everything in the control panel interface
	HmdError error;
	SetEnvironmentVariable( "VR_PLATFORM_INSTALL_PATH", sVRDir.c_str() );
	IHmd *pHmd = VR_Init( &error );
	SetEnvironmentVariable( "VR_PLATFORM_INSTALL_PATH", NULL );
	if( !pHmd )
	{
		printf( "VR_Init failed with error %d for path %s\n", error, sVRDir.c_str( ) );
		return -1;
	}

	IVRControlPanel *pControlPanel = (IVRControlPanel *)VR_GetGenericInterface( IVRControlPanel_Version, &error );
	bool bRetVal = false;
	if( pControlPanel )
	{
		bRetVal = PrintControlPanelInfo( pHmd, pControlPanel );
	}
	else
	{
		printf( "Unable to get control panel interface: error code %d\n", error );
	}

	VR_Shutdown();

	return bRetVal ? 0 : 1;
}
