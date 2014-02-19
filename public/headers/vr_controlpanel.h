#pragma once

#include "steamvr.h"

namespace vr
{

class IVRControlPanel
{
public:

	// ------------------------------------
	// Driver enumeration methods
	// ------------------------------------

	/** the number of active drivers */
	virtual uint32_t GetDriverCount() = 0;

	/** The ID of the specified driver as a UTF-8 string. Returns the length of the ID in bytes. If 
	* the buffer is not large enough to fit the ID an empty string will be returned. In general, 128 bytes
	* will be enough to fit any ID. */
	virtual uint32_t GetDriverId( uint32_t unDriverIndex, char *pchBuffer, uint32_t unBufferLen ) = 0;

	// ------------------------------------
	// Display Enumeration Methods
	// ------------------------------------

	/** the number of active displays on the specified driver */
	virtual uint32_t GetDriverDisplayCount( const char *pchDriverId ) = 0;

	/** The ID of the specified display in the specified driver as a UTF-8 string. Returns the 
	* length of the ID in bytes. If the buffer is not large enough to fit the ID an empty
	* string will be returned. In general, 128 bytes will be enough to fit any ID. */
	virtual uint32_t GetDriverDisplayId( const char *pchDriverId, uint32_t unDisplayIndex, char *pchBuffer, uint32_t unBufferLen ) = 0;

	// ------------------------------------
	// Display Detail Methods
	// ------------------------------------

	/** The model name of the specified driver in the specified driver as a UTF-8 string. Returns the 
	* length of the model name in bytes. If the buffer is not large enough to fit the model name an empty
	* string will be returned. In general, 128 bytes will be enough to fit any model name. Returns 0 if
	* the display or driver was not found. */
	virtual uint32_t GetDriverDisplayModelNumber( const char *pchDriverId, const char *pchDisplayId, char *pchBuffer, uint32_t unBufferLen ) = 0;

	/** The serial number of the specified driver in the specified driver as a UTF-8 string. Returns the 
	* length of the serial number in bytes. If the buffer is not large enough to fit the serial number an empty
	* string will be returned. In general, 128 bytes will be enough to fit any model name. Returns 0 if
	* the display or driver was not found. */
	virtual uint32_t GetDriverDisplaySerialNumber( const char *pchDriverId, const char *pchDisplayId, char *pchBuffer, uint32_t unBufferLen ) = 0;

	/** Returns the IHmd interface for the current display that matches the specified version number. 
	* This is usually unnecessary and the return value of VR_Init can be used without calling this method. */
	virtual IHmd *GetCurrentDisplayInterface( const char *pchHmdInterfaceVersion ) = 0;

	// ------------------------------------
	// Shared Resource Methods
	// ------------------------------------

	/** Loads the specified resource into the provided buffer if large enough.
	* Returns the size in bytes of the buffer required to hold the specified resource. */
	virtual uint32_t LoadSharedResource( const char *pchResourceName, char *pchBuffer, uint32_t unBufferLen ) = 0;
};

static const char * const IVRControlPanel_Version = "IVRControlPanel_001";


/** Returns the interface of the specified version. This method must be called after VR_Init. The
* pointer returned is valid until VR_Shutdown is called.
*/
VR_INTERFACE void *VR_GetGenericInterface( const char *pchInterfaceVersion, HmdError *peError );

}