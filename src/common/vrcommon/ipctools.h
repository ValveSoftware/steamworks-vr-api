//========= Copyright Valve Corporation ============//

#pragma once

#include "hmdplatform_private.h"

namespace IPC
{
	// Sync objects have some common properties, similar to Win32 IPC primitives.
	// All objects can be waited on for being 'signalled' in the Win32 sense.
	// All objects will be destroyed on delete, but their IPC state will depend on
	// the derived class (ie: mutex will be abandoned, but events will stay set if they are set)
	// Any ISyncObject can contribute to IPC::WaitMultiple.
	// Return codes are always logical outcomes, errors should be checked with ISyncObject::GetLastError().
	class ISyncObject
	{
	public:
		virtual bool Wait( uint32_t msWaitTime = UINT32_MAX) = 0; // thread re-entrant
		virtual void Destroy() = 0;
		virtual ~ISyncObject() {}

		enum ESyncErrorCode
		{
			kSyncSuccess		= 0,
			kSyncFail			= 1, // Unknown
			kSyncInvalidObject	= 2, // underlying primitive is bad
		};

		ESyncErrorCode GetLastError() { return m_eError; }

	protected:
		ISyncObject() { m_eError = kSyncSuccess; }
		void SetLastError( ESyncErrorCode eError ) { m_eError = eError; }

		friend int WaitMultiple( uint32_t msWaitTime, uint32_t nCount, ISyncObject *pFirst, ... );
		virtual void *GetOpaque() { return NULL; }
	private:
		ESyncErrorCode	m_eError;

#ifdef DBGFLAG_VALIDATE
	public:	virtual void Validate( CValidator &validator, const char *pchName ) {}
#endif
	};

	class IMutex : public ISyncObject
	{
	public:
		virtual void Release() = 0;
	};

	class IEvent : public ISyncObject
	{
	public:
		virtual void SetEvent() = 0;
		virtual void ResetEvent() = 0;
	};

	class ISharedMem
	{
	public:
		enum Access
		{
			Read		= 0x01,
			ReadWrite	= 0x02
		};
        virtual bool IsValid() const = 0;
		virtual void *Pointer() const = 0;

		virtual bool Destroy() = 0;
		virtual ~ISharedMem() {}
#ifdef DBGFLAG_VALIDATE
		virtual void Validate( CValidator &validator, const char *pchName ) {}
#endif

	};

	// Pass a time to wait (UINT_MAX for block) followed by a count of objects and then the
	// list of objects to wait on.  Return value is 0 for timeout, 1 for the first
	// item succeeded waiting: 2 for the second, etc...  Return is < 0 on fatal error
	// Only waits for 1 item to signal, no guarantee about starvation on repeated use.
	int WaitMultiple( uint32_t msWaitTime, uint32_t nCount, ISyncObject *pFirst, ... );

	// Look at Win32 CreateMutex for reference.  If return is non-null and *pbCreator is
	// false then an existing mutex was connected to - in which case bInitialOwner is ignored.
	IMutex *CreateMutex( const char *pszName, bool bInitialOwner, bool *pbCreator );

	// See Win32 CreateEvent.  If return is non-null and *pbCreator is false, then the bInitiallySet
	// parameter is ignored.
	IEvent *CreateEvent( const char *pszName, bool bManualReset, bool bInitiallySet, bool *pbCreator );

	// Name the memory segment, what size should it be, and what kind of access is required.
	ISharedMem *CreateSharedMem( const char *pszName, uint32_t nSize, ISharedMem::Access access );

    // On platforms that don't support automatic cleanup of shared objects
    // it may be necessary to explicitly request the IPC layer go clean
    // up shared objects that it created.
    // This can also come up with Posix where a game may exec itself into
    // an existing process and previous shared objects should be cleaned up
    // prior to that so that it doesn't look like the new execution,
    // which still has the same pid, has created those objects.
    void PurgeAllDead();
    void PurgeCurrentProcess();
};

