//========= Copyright Valve Corporation ============//
// --------------------------------------------------------------------------------------
// Test cases for the Oculus driver
// --------------------------------------------------------------------------------------


#include "gtest/gtest.h"

#include "steamvr.h"
#include "vrcommon/pathtools.h"
#include "vrcommon/hmdplatform_private.h"
#include "vrcommon/envvartools.h"
#include "SDL.h"

using namespace vr;

class OculusDriver : public ::testing::Test
{
public:
	virtual void SetUp()
	{
		HmdError err = HmdError_None;
		std::string sInstallPath = Path_MakeAbsolute( "../runtime", Path_StripFilename( Path_GetExecutablePath() ) );
		SetEnvironmentVariable( "VR_PLATFORM_INSTALL_PATH", sInstallPath.c_str() );
		m_pHmd = VR_Init( &err );
		ASSERT_EQ( HmdError_None, err );
		ASSERT_NE( (void *)NULL, (void*)m_pHmd );
	}

	virtual void TearDown()
	{
		VR_Shutdown();

		// wait for a bit to make sure the server can edit before the next test
		SDL_Delay( 100 );
	}

	IHmd *m_pHmd;
};

TEST_F( OculusDriver, WindowBounds )
{
	
	int32_t nX, nY;
	uint32_t nWidth, nHeight;
	m_pHmd->GetWindowBounds( &nX, &nY, &nWidth, &nHeight );
	EXPECT_EQ( 1280, nWidth );
	EXPECT_EQ( 800, nHeight );
	EXPECT_EQ( 2560, nX );
	EXPECT_EQ( 0, nY );
}


TEST_F( OculusDriver, EventuallyGetsPose )
{
	HmdMatrix34_t mat;
	HmdTrackingResult eResult;
	int nLoops = 0;
	while( !m_pHmd->GetWorldFromHeadPose( 0, &mat, &eResult ) && nLoops++ < 50 )
	{
		SDL_Delay( 100 );
	}

	ASSERT_EQ( eResult, TrackingResult_Running_OK );
	ASSERT_LT( nLoops, 50 );
}

TEST_F( OculusDriver, ProjMatrix )
{
	// no idea how to see if this is actually valid, but Neil says it looks right.
	HmdMatrix44_t projLeft = m_pHmd->GetProjectionMatrix( vr::Eye_Left, 0.1f, 1000.f, vr::API_DirectX );
	HmdMatrix44_t projRight = m_pHmd->GetProjectionMatrix( vr::Eye_Right, 0.1f, 1000.f, vr::API_DirectX );
	(void)projLeft;
	(void)projRight;
}


TEST_F( OculusDriver, RenderTargetSize )
{
	uint32_t nWidth, nHeight;
	m_pHmd->GetRecommendedRenderTargetSize( &nWidth, &nHeight );
}

TEST_F( OculusDriver, GetViewMatrix )
{
	HmdMatrix34_t mat;
	HmdTrackingResult eResult;
	int nLoops = 0;
	while( !m_pHmd->GetWorldFromHeadPose( 0, &mat, &eResult ) && nLoops++ < 50 )
	{
		SDL_Delay( 10 );
	}

	HmdMatrix44_t matLeft, matRight;
	ASSERT_TRUE( m_pHmd->GetViewMatrix( 0.f, &matLeft, &matRight, &eResult ) );
}

TEST_F( OculusDriver, GetAdapterIndex)
{
	int32_t nAdapter = m_pHmd->GetD3D9AdapterIndex();
	ASSERT_NE( nAdapter, -1 );
}

TEST_F( OculusDriver, GetDXGIInfo)
{
	int32_t nAdapterOutput;
	int32_t nAdapter;
	m_pHmd->GetDXGIOutputInfo( &nAdapter, &nAdapterOutput );
	ASSERT_NE( nAdapter, -1 );
	ASSERT_NE( nAdapterOutput, -1 );
}

TEST_F( OculusDriver, Zeroing )
{
	HmdMatrix34_t pose;
	HmdTrackingResult eResult;
	int nLoops = 0;
	while( !m_pHmd->GetWorldFromHeadPose( 0, &pose, &eResult ) && nLoops++ < 50 )
	{
		SDL_Delay( 10 );
	}
	ASSERT_TRUE( m_pHmd->GetWorldFromHeadPose( 0, &pose, &eResult ) );
	m_pHmd->ZeroTracker();
	HmdMatrix34_t pose2;
	ASSERT_TRUE( m_pHmd->GetWorldFromHeadPose( 0, &pose2, &eResult ) );
}




