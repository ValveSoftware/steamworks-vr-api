#pragma once

#include "steamvr.h"

namespace vr
{

class IHmdDriver;

class IHmdDriverProvider
{
public:
	/** initializes the driver. This will be called before any other methods are called.
	* If Init returns anything other than HmdError_None the driver DLL will be unloaded.
	*
	* pchUserConfigDir - The absolute path of the directory where the driver should store user
	*	config files.
	* pchDriverInstallDir - The absolute path of the root directory for the driver.
	*/
	virtual HmdError Init( const char *pchUserConfigDir, const char *pchDriverInstallDir ) = 0;

	/** cleans up the driver right before it is unloaded */
	virtual void Cleanup() = 0;

	/** returns the number of HMDs that this driver manages that are physically connected. */
	virtual uint32_t GetHmdCount() = 0;

	/** returns a single HMD */
	virtual IHmdDriver *GetHmd( uint32_t unWhich ) = 0;

	/** returns a single HMD by ID */
	virtual IHmdDriver* FindHmd( const char *pchId ) = 0;

};



static const char *IHmdDriverProvider_Version = "IHmdDriverProvider_001";

}