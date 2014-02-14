//========= Copyright Valve Corporation ============//
// --------------------------------------------------------------------------------------
// Test cases for the core VR implementation
// --------------------------------------------------------------------------------------

#include "gtest/gtest.h"

#include "steamvr.h"
#include "vrcommon/pathtools.h"
#include "vrcommon/envvartools.h"
#include "vrcommon/hmdplatform_private.h"
#include "SDL.h"

using namespace vr;

IHmd *VR_Init_Test( const std::string & sPath, HmdError *peError )
{
	std::string sInstallPath = Path_MakeAbsolute( sPath, Path_StripFilename( Path_GetExecutablePath() ) );
	SetEnvironmentVariable( "VR_PLATFORM_INSTALL_PATH", sInstallPath.c_str() );
	IHmd *pHmd = VR_Init( peError );
	SetEnvironmentVariable("VR_PLATFORM_INSTALL_PATH", NULL );
	return pHmd;
}

TEST( vrcore, InitBogusOverridePath)
{
	HmdError err = HmdError_None;
	IHmd *pHmd = VR_Init_Test( "thisisabrokenpath", &err );
	ASSERT_EQ( NULL, pHmd );
	ASSERT_EQ( HmdError_Init_InstallationNotFound, err );
}


TEST( vrcore, InitEmptyDll)
{
	HmdError err = HmdError_None;
	IHmd *pHmd = VR_Init_Test( "emptydll", &err );
	ASSERT_EQ( NULL, pHmd );
	ASSERT_EQ( HmdError_Init_FactoryNotFound, err );
}


TEST( vrcore, InitNoInterfaceDll)
{
	HmdError err = HmdError_None;
	IHmd *pHmd = VR_Init_Test( "nointerfacedll", &err );
	ASSERT_EQ( NULL, pHmd );
	ASSERT_EQ( HmdError_Init_InterfaceNotFound, err );
}


TEST( vrcore, ActualDll)
{
	HmdError err = HmdError_None;
	IHmd *pHmd = VR_Init_Test( "../runtime", &err );
	ASSERT_NE( (IHmd*)NULL, pHmd );
	VR_Shutdown();

	// wait for a bit to make sure the server can edit before the next test
	SDL_Delay( 100 );
}

TEST( vrcore, ActualDLLWithConVar)
{
	HmdError err = HmdError_None;
	IHmd *pHmd = VR_Init_Test( "../runtime", &err );
	ASSERT_NE( (IHmd*)NULL, pHmd );
	VR_Shutdown();

	// wait for a bit to make sure the server can edit before the next test
	SDL_Delay( 100 );
}

TEST( vrcore, OverrideConVar )
{
	HmdError err = HmdError_None;
	std::string sInstallPath = Path_MakeAbsolute( "../runtime", Path_StripFilename( Path_GetExecutablePath() ) );
	EXPECT_TRUE( SetEnvironmentVariable( "VR_OVERRIDE", sInstallPath.c_str() ) );
	IHmd *pHmd = VR_Init_Test( "../runtime", &err );
	ASSERT_NE( (IHmd*)NULL, pHmd );
	VR_Shutdown();
	EXPECT_TRUE( SetEnvironmentVariable( "VR_OVERRIDE", NULL ) );
}
