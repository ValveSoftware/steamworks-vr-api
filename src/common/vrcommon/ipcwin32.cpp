//========= Copyright Valve Corporation ============//
#include "ipctools.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#include <winbase.h>

#undef CreateMutex

#include <string>

#include "vrlog.h"
#include "hmdplatform_private.h"

namespace IPC
{
	struct CStaticEmptyDescriptor
	{
		SECURITY_ATTRIBUTES secAttr;
		char secDesc[ SECURITY_DESCRIPTOR_MIN_LENGTH ];

		//
		// helper function, return the SID that this PID is running as
		static void GetSidForProcess( HANDLE pid, PSID *pSid )
		{
			HANDLE hToken = NULL;

			// Open the access token associated with the calling process.
			if ( OpenProcessToken( pid, TOKEN_QUERY, &hToken)  == FALSE)
			{
				DWORD dwErrorCode = 0;
				dwErrorCode = GetLastError();
				Log( "OpenProcessToken failed. GetLastError returned: %d\n", dwErrorCode );
			}
			else
			{
				DWORD dwBufferSize = 0;

				// Retrieve the token information in a TOKEN_USER structure.
				GetTokenInformation( hToken, TokenUser,	NULL, 0, &dwBufferSize );

				PTOKEN_USER  pTokenUser = (PTOKEN_USER) new BYTE[dwBufferSize];
				memset( pTokenUser, 0, dwBufferSize );
				if ( GetTokenInformation(hToken, TokenUser, pTokenUser, dwBufferSize, &dwBufferSize ) == FALSE )
				{
					DWORD dwErrorCode = 0;
					dwErrorCode = GetLastError();
					Log( "GetTokenInformation failed. GetLastError returned: %d\n", dwErrorCode );
				}

				CloseHandle(hToken);

				if ( IsValidSid( pTokenUser->User.Sid ) == FALSE )
				{
					Log( "The owner SID is invalid.\n");
				}
				else
				{
					// stash off the sid of the user that is running this process
					DWORD nBytesSide = GetLengthSid( pTokenUser->User.Sid );
					*pSid = new BYTE[nBytesSide];
					CopySid( nBytesSide, *pSid, pTokenUser->User.Sid );
				}

				delete [] pTokenUser;
			}
		}

		//
		// helper func, return a registry uint value
		//
		static unsigned int GetRegistryUint( const char *pchKeyName, const char *pchValueName )
		{
			HKEY hKey = NULL;
			DWORD dwValue = 0;
			unsigned int uRet = ::RegOpenKey( HKEY_CURRENT_USER, pchKeyName, &hKey );
			if ( ERROR_SUCCESS == uRet )
			{
				DWORD dwType;
				DWORD dwLen = sizeof(dwValue);
				uRet = ::RegQueryValueEx( hKey, pchValueName, NULL, &dwType, (LPBYTE) &dwValue, (LPDWORD)&dwLen );
				RegCloseKey( hKey );
			}

			return dwValue;
		}

		CStaticEmptyDescriptor()
		{
			m_pDACL = NULL;

			secAttr.nLength = sizeof(secAttr);
			secAttr.bInheritHandle = FALSE;
			secAttr.lpSecurityDescriptor = &secDesc;
			if ( InitializeSecurityDescriptor( secAttr.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION ) == TRUE )
			{
				PSID psidUserSid = NULL;
				PSID psidSteamSid = NULL;
				PSID psidAdministratorGroupSid = NULL;
				const int nSIDS = 3; // needs to match the number above

				// add the SID for this process (game or gameoverlayui or tenfoot)
				GetSidForProcess( GetCurrentProcess(), &psidUserSid );

				DWORD dwProcessId = GetRegistryUint( "Software\\Valve\\Steam\\ActiveProcess", "pid" );

				HANDLE hProcess = ::OpenProcess( PROCESS_QUERY_INFORMATION, false, dwProcessId );
				if ( hProcess )
				{
					// add the SID for the steam process user and implicitly the gameoverlayui user
					GetSidForProcess( hProcess, &psidSteamSid );
					CloseHandle( hProcess );
				}

				// lookup the group SID for administrators too, to allow games that run under accounts with admin rights
				DWORD cbSidSize = SECURITY_MAX_SID_SIZE;
				psidAdministratorGroupSid = new BYTE[cbSidSize];
				CreateWellKnownSid( WinBuiltinAdministratorsSid, NULL, psidAdministratorGroupSid, &cbSidSize );

				// Calculate the amount of memory that must be allocated for the DACL.
				int cbDacl = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE)*nSIDS;
				if ( psidUserSid )
					cbDacl += GetLengthSid(psidUserSid);
				if ( psidAdministratorGroupSid )
					cbDacl += GetLengthSid(psidAdministratorGroupSid);
				if ( psidSteamSid )
					cbDacl += GetLengthSid(psidSteamSid);

				// Create and initialize an ACL.
				// Not leak tracking because plumbing Validate into this internal detail is tough.
#ifdef _SERVER
				m_pDACL = (PACL) PvAllocNoLeakTracking( cbDacl );
#else
				m_pDACL = (PACL) new BYTE[ cbDacl ];
#endif
				if ( m_pDACL )
				{
					memset( m_pDACL, 0, cbDacl );

					if ( InitializeAcl( m_pDACL, cbDacl, ACL_REVISION	) == FALSE )
					{
						DWORD dwErrorCode = GetLastError();
						Log( "InitializeAcl failed. GetLastError returned: %d\n", dwErrorCode );

						delete [] m_pDACL;
						m_pDACL = NULL;
					}
					else
					{
						// Add access-allowed ACEs for the three trustees.
						if ( psidUserSid && AddAccessAllowedAce( m_pDACL,   ACL_REVISION,   GENERIC_ALL,  psidUserSid  ) == FALSE )
						{
							DWORD dwErrorCode = GetLastError();
							Log( "AddAccessAllowedAce failed for the process owner. GetLastError returned: %d\n", dwErrorCode );
						}

						if ( psidAdministratorGroupSid && AddAccessAllowedAce( m_pDACL,   ACL_REVISION,   GENERIC_ALL,  psidAdministratorGroupSid  ) == FALSE )
						{
							DWORD dwErrorCode = GetLastError();
							Log( "AddAccessAllowedAce failed for administrator. GetLastError returned: %d\n", dwErrorCode );
						}

						if ( psidSteamSid && AddAccessAllowedAce( m_pDACL,   ACL_REVISION,   GENERIC_ALL,  psidSteamSid  ) == FALSE )
						{
							DWORD dwErrorCode = GetLastError();
							Log( "AddAccessAllowedAce failed for steam. GetLastError returned: %d\n", dwErrorCode );
						}

						// now apply the dacl to the sd
						if ( SetSecurityDescriptorDacl( secAttr.lpSecurityDescriptor, TRUE, m_pDACL, FALSE ) == FALSE )
						{
							DWORD dwErrorCode = GetLastError();
							Log( "SetSecurityDescriptorDacl failed. GetLastError returned: %d\n", dwErrorCode );
						}
					}

					delete [] psidUserSid;
					delete [] psidAdministratorGroupSid;
					delete [] psidSteamSid;
				}
			}
			else // InitializeSecurityDescriptor failed
			{
				DWORD dwErrorCode = GetLastError();
				Log( "InitializeSecurityDescriptor failed. GetLastError returned: %d\n", dwErrorCode );				
			}
		}

		~CStaticEmptyDescriptor()
		{
			if( m_pDACL )
			{
#ifdef _SERVER
				FreePv( m_pDACL );
#else
				delete[] m_pDACL;
#endif
				m_pDACL = NULL;
			}
		}

	private:
		PACL m_pDACL;
	};

	// Make sure that static object is initialized before access even at global startup
	static LPSECURITY_ATTRIBUTES GetEmptySecurityAttrs()
	{
		static CStaticEmptyDescriptor s_emptySecurity;
		return &s_emptySecurity.secAttr;
	}

	class Win32Mutex : public IMutex
	{
	public:
		Win32Mutex()
		{
			// Make sure GetOpaque() can just return the handle
			COMPILE_TIME_ASSERT( sizeof(HANDLE) <= sizeof(void*) );
			m_hMutex = NULL;
		}

		virtual bool Wait(uint32_t msWaitTime)
		{
			this->SetLastError(kSyncSuccess);

			DWORD dwResult = ::WaitForSingleObject( m_hMutex, msWaitTime );
			switch ( dwResult )
			{
			case WAIT_OBJECT_0:
			case WAIT_TIMEOUT:
				break;
			default:
				this->SetLastError(kSyncFail);
			}

			return ( dwResult == WAIT_OBJECT_0 );
		}

		virtual void Release()
		{
			this->SetLastError( ( ::ReleaseMutex( m_hMutex ) != 0 ) ? kSyncSuccess : kSyncFail );
		}

		virtual void Destroy()
		{
			this->SetLastError( ( ::CloseHandle( m_hMutex ) != 0 ) ? kSyncSuccess : kSyncFail );
		}
		
		virtual void* GetOpaque() { return (m_hMutex);	}

		bool Init( const char *pszName, bool bInitialOwner, bool *pbCreator )
		{
			if ( pbCreator )
				*pbCreator = false;

			std::string sNewName = pszName;
			sNewName += "-IPCWrapper";

			m_hMutex = ::CreateMutexA( GetEmptySecurityAttrs(), bInitialOwner, sNewName.c_str() );
			if( !m_hMutex )
			{
				Log( "Failed creating mutex %s (GLE: %u)\n", sNewName.c_str(), ::GetLastError() );
				return false;
			}

			if ( pbCreator && ( ::GetLastError() != ERROR_ALREADY_EXISTS ) )
				*pbCreator = true;

			return true;
		}

		virtual ~Win32Mutex()
		{
			Destroy();
		}

	private:
		HANDLE	m_hMutex;
	};

	class Win32Event : public IEvent
	{
	public:
		Win32Event()
		{
			m_hEvent = NULL;
		}

		virtual bool Wait(uint32_t msWaitTime)
		{
			this->SetLastError(kSyncSuccess);

			DWORD dwResult = ::WaitForSingleObject( m_hEvent, msWaitTime );
			switch ( dwResult )
			{
			case WAIT_OBJECT_0:
			case WAIT_TIMEOUT:
				break;
			default:
				this->SetLastError(kSyncFail);
			}

			return ( dwResult == WAIT_OBJECT_0 );
		}

		virtual void SetEvent()
		{
			this->SetLastError( ( ::SetEvent( m_hEvent ) != 0 ) ? kSyncSuccess : kSyncFail );
		}

		virtual void ResetEvent()
		{
			this->SetLastError( ( ::ResetEvent( m_hEvent ) != 0 ) ? kSyncSuccess : kSyncFail );
		}

		virtual void Destroy()
		{
			this->SetLastError( ( ::CloseHandle( m_hEvent ) != 0 ) ? kSyncSuccess : kSyncFail );
		}

		virtual void* GetOpaque() { return (m_hEvent);	}

		bool Init( const char *pszName, bool bManualReset, bool bInitiallySet, bool *pbCreator )
		{
			if ( pbCreator )
				*pbCreator = false;

			std::string sNewName = pszName;
			sNewName += "-IPCWrapper";


			m_hEvent = ::CreateEvent( GetEmptySecurityAttrs(), bManualReset, bInitiallySet, sNewName.c_str() );
			if( !m_hEvent )
			{
				Log( "Failed creating auto reset event %s (GLE: %u)\n", sNewName.c_str(), GetLastError() );
				return false;
			}

			if ( pbCreator && ( ::GetLastError() != ERROR_ALREADY_EXISTS ) )
				*pbCreator = true;

			return true;
		}

		virtual ~Win32Event()
		{
			Destroy();
		}

	private:
		HANDLE	m_hEvent;
	};

	class Win32SharedMemory : public ISharedMem
	{
	public:
		Win32SharedMemory()
		{
			m_hMapFile = NULL;
			m_pData = NULL;
		}

        virtual bool IsValid() const
        {
            return m_pData != NULL;
        }
		virtual void *Pointer() const
		{
			return m_pData;
		}

		virtual bool Destroy()
		{
			if ( m_pData )
				::UnmapViewOfFile( m_pData );

			if ( m_hMapFile )
				::CloseHandle( m_hMapFile );

			m_hMapFile = m_pData = NULL;
			return true;
		}

		virtual ~Win32SharedMemory()
		{
			Destroy();
		}

		bool Init( const char *pszName, uint32_t nSize, ISharedMem::Access access )
		{
			DWORD createAccess = 0;
			DWORD mapViewAccess = 0;

			std::string sNewName = pszName;
			sNewName += "-IPCWrapper";

			switch ( access )
			{
			case Read:
				createAccess = PAGE_READONLY;
				mapViewAccess = FILE_MAP_READ;
				break;
			case ReadWrite:
				createAccess = PAGE_READWRITE;
				mapViewAccess = FILE_MAP_ALL_ACCESS;
				break;
			default:
				Log( "SharedMem access invalid: %x\n", access );
				return false;
			}

			m_hMapFile = CreateFileMapping(	INVALID_HANDLE_VALUE, GetEmptySecurityAttrs(), createAccess, 0, nSize, sNewName.c_str() );
			if ( m_hMapFile == NULL || m_hMapFile == INVALID_HANDLE_VALUE )
			{
                m_hMapFile = NULL;
				Log( "Failed creating file mapping %s\n", sNewName.c_str() );
				return false;
			}

			// Map to the file
			m_pData = MapViewOfFile( m_hMapFile, mapViewAccess, 0, 0, nSize ); 
			if ( m_pData == NULL )
			{
                ::CloseHandle( m_hMapFile );
                m_hMapFile = NULL;
				Log( "Failed mapping view for %s\n", sNewName.c_str() );
				return false;
			}

			return true;
		}

	private:
		HANDLE m_hMapFile;
		void *m_pData;
	};

#define CREATE_OBJECT( TYPE, ... )		\
	TYPE *pObj = new TYPE();			\
	if ( !pObj->Init( __VA_ARGS__ ) )	\
	{									\
		delete pObj;					\
		return NULL;					\
	}									\
	return pObj;

	ISharedMem *CreateSharedMem( const char *pszName, uint32_t nSize, ISharedMem::Access access )
	{
		CREATE_OBJECT( Win32SharedMemory, pszName, nSize, access );
	}

	// Look at Win32 CreateMutex for reference.  If return is non-null and *pbCreator is
	// false then an existing mutex was connected to - in which case ownership is not granted
	// even if it not currently locked.
	IMutex *CreateMutex( const char *pszName, bool bInitialOwner, bool *pbCreator )
	{
		CREATE_OBJECT( Win32Mutex, pszName, bInitialOwner, pbCreator );
	}

	// See Win32 CreateEvent.  If return is non-null and *pbCreator is false, then the bInitiallySet
	// parameter is ignored.  If bInitially set is true, the event is signalled for a Wait().
	IEvent *CreateEvent( const char *pszName, bool bManualReset, bool bInitiallySet, bool *pbCreator )
	{
		CREATE_OBJECT( Win32Event, pszName, bManualReset, bInitiallySet, pbCreator );
	}

	// Pass a time to wait (UINT_MAX for block) followed by a count of objects and then the
	// list of objects to wait on.  Return value is 0 for timeout, 1 for the first
	// item succeeded waiting: 2 for the second, etc...  Return is < 0 on fatal error
	// Only waits for 1 item to signal, no guarantee about starvation on repeated use.
	int WaitMultiple( uint32_t msWaitTime, uint32_t nCount, ISyncObject *pFirst, ... )
	{
		HANDLE handles[8];
		DWORD nHandleCount = 0;

		if ( pFirst == NULL || nCount >= 8 )
			return -1;

		va_list ap;
		va_start(ap, pFirst);
		ISyncObject *pNext = pFirst;
		for ( uint32_t i = 0 ; i < nCount; i++, pNext = va_arg(ap, ISyncObject *) )
		{
			handles[nHandleCount++] = (HANDLE)pNext->GetOpaque();
		}

		va_end(ap);

		DWORD dwResult = ::WaitForMultipleObjects( nHandleCount, handles, false, msWaitTime );

		// Timeout
		if ( dwResult == WAIT_TIMEOUT )
			return 0;

		// someone signalled
		if ( ( dwResult >= WAIT_OBJECT_0 ) && (dwResult < (WAIT_OBJECT_0 + nHandleCount) ) )
			return dwResult - WAIT_OBJECT_0 + 1;

		// internal fail
		if ( dwResult == WAIT_FAILED )
			return -2;

		// wait_abandoned
		return -3;
	}

    // On platforms that don't support automatic cleanup of shared objects
    // it may be necessary to explicitly request the IPC layer go clean
    // up shared objects that it created.
    // This can also come up with Posix where a game may exec itself into
    // an existing process and previous shared objects should be cleaned up
    // prior to that so that it doesn't look like the new execution,
    // which still has the same pid, has created those objects.
    void PurgeAllDead()
    {
        // No need for explicit tracking of shared objects on Win32.
    }
    void PurgeCurrentProcess()
    {
        // No need for explicit tracking of shared objects on Win32.
    }
}
