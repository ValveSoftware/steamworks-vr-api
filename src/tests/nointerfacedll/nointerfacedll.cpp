//========= Copyright Valve Corporation ============//
#include "steamvr.h"

VR_INTERFACE void *HmdSystemFactory( const char *pInterfaceName, int *pReturnCode )
{
	(void)pInterfaceName;
	(void)pReturnCode;
	return 0;
}
