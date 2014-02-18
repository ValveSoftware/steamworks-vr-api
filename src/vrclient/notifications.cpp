//========= Copyright Valve Corporation ============//
#include "SDL_loadso.h"
#include "notifications.h"
#include "vrcommon/pathtools.h"
#if defined( _WIN32 )
#include <Windows.h>
#endif

typedef void (*NoArgNotificationFn_t)();

/** Calls a function with no args and no return value in the specified DLL */
void CallNoArgNotification( const char *pchModuleName, const char *pchProcName )
{
#if defined(_WIN32)
	void * pMod = (void *)GetModuleHandle( pchModuleName );
	if( pMod )
	{
		NoArgNotificationFn_t fn = (NoArgNotificationFn_t)SDL_LoadFunction( pMod, pchProcName );
#elif defined(POSIX)
        // on POSIX there is no reasonable way to get the handle of a module without the
        // full path name. Since we don't know that, just assume that the global symbol
        // name is only defined in the right module
        NoArgNotificationFn_t fn = (NoArgNotificationFn_t)SDL_LoadFunction( NULL, pchProcName );
#endif
		if( fn )
		{
			// actually call the function
			fn();
		}

#if defined(_WIN32)
	}
#endif

}


/** Called when VR_Init is completed successfully */
void NotifyVR_InitSuccess()
{
	// If it is connected, tell the Steam Overlay that the VR API has started up. If the
	// Steam Overlay is not present this does nothing.
	CallNoArgNotification("gameoverlayrenderer" DYNAMIC_LIB_EXT, "NotifyOpenVRInit");
	CallNoArgNotification("gameoverlayrenderer" DYNAMIC_LIB_EXT, "NotifyVRInit");

	// If there are other system-level things that need notifactions about VR mode this
	// would be a good place to add them.
}


/** called when VR_Shutdown is called before it does any cleaning up */
void NotifyVR_Shutdown( )
{
	CallNoArgNotification( "gameoverlayrenderer" DYNAMIC_LIB_EXT, "NotifyOpenVRCleanup" );
	CallNoArgNotification( "gameoverlayrenderer" DYNAMIC_LIB_EXT, "NotifyVRShutdown");
	CallNoArgNotification( "gameoverlayrenderer" DYNAMIC_LIB_EXT, "NotifyVRCleanup" );
}
