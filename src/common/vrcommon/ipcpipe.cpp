//========= Copyright Valve Corporation ============//
#include "ipcpipe.h"
#include <string>
#include "vrlog.h"
#include "hmdplatform_private.h"
#include "ipctools.h"
#include "stdio.h"

// -------------------------------------
// Platform independent pipe routines
// -------------------------------------

bool CIPCPipe::SendSimpleMessage( uint32_t unMessageType )
{
	// message type 0 is reserved for identifying timeouts
	if( unMessageType == 0 )
		return false;
	if( !IsValid() )
		return false;

	uint32_t runRead[2];
	runRead[0] = unMessageType;
	runRead[1] = 0;
	return SendMessageInternal( runRead, sizeof( runRead ) );
}


bool CIPCPipe::SendPackedMessage( uint32_t unMessageType, void *pvPayload, uint32_t unPayloadLength )
{
	// message type 0 is reserved for identifying timeouts
	if( unMessageType == 0 )
		return false;
	if( unPayloadLength == 0 )
		return SendSimpleMessage( unMessageType );
	uint32_t unTotalMessageLength = sizeof( uint32_t ) * 2 + unPayloadLength;
	uint32_t *runMsg = (uint32_t *)StackAlloc( unTotalMessageLength );
	runMsg[ 0 ] = unMessageType;
	runMsg[ 1 ] = unPayloadLength;
	memcpy( &runMsg[2], pvPayload, unPayloadLength );
	return SendMessageInternal( runMsg, unTotalMessageLength );
}

CIPCPipe::~CIPCPipe()
{
	ClosePipe();
}


// -------------------------------------
// Windows implementation of CIPCPipe
// -------------------------------------
#if defined(_WIN32)
#include <Windows.h>


CIPCPipe::CIPCPipe()
{
	m_hPipe = NULL;
	m_hEvent = NULL;
}


bool CIPCPipe::CreatePipe( const char *pchPipeName )
{
	if( m_hPipe )
		return false;

	m_sPipeName = pchPipeName;

	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL;

	m_hEvent = ::CreateEvent( NULL, TRUE, FALSE, NULL );

	std::string sFullName = "\\\\.\\pipe\\";
	sFullName += pchPipeName;

	m_hPipe = ::CreateNamedPipe( sFullName.c_str(),
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_WAIT,
		PIPE_UNLIMITED_INSTANCES,
		4096,
		4096,
		0,
		&saAttr );

	return m_hPipe != INVALID_HANDLE_VALUE;
}

bool CIPCPipe::WaitForConnection( uint32_t unTimeoutMs, CIPCPipe **ppNewConnection )
{
	*ppNewConnection = NULL;
	if( !m_hPipe )
		return false;

	// Wait for the client to connect to the pipe
	OVERLAPPED overlapped = {0};
	overlapped.hEvent = m_hEvent;
	DWORD res = ::ConnectNamedPipe( m_hPipe, &overlapped );
	if( res == 0 )
	{
		switch( GetLastError() )
		{
			case ERROR_PIPE_CONNECTED:
				// nothing to do here. Fall through to a successful connection
				break;

			case ERROR_IO_PENDING:
				switch( ::WaitForSingleObject( m_hEvent, unTimeoutMs ) )
				{
					// the event signaled
				case  WAIT_OBJECT_0:
					{
						DWORD dwIgnore;
						res = ::GetOverlappedResult( m_hPipe, &overlapped, &dwIgnore, FALSE );
					}

				case WAIT_TIMEOUT:
					{
						// we just timed out. Return true to indicate that it was a timeout
						// cancel the connect request. We'll start it again next time we're called
						::CancelIo( m_hPipe );
						return true;
					}

				default:
					{
						// some other error occurred
						::CancelIo( m_hPipe );
						ClosePipe();
						return false;
					}
				}
				break;
		}
	}

	// this pipe is now the connection, so we need to swap pipe handles with 
	// a new pipe object that represents the listener
	CIPCPipe *pNewPipe = new CIPCPipe();
	if( !pNewPipe->CreatePipe( m_sPipeName.c_str() ) )
	{
		Log( "Failed to relisten to pipe after connection\n" );
		delete pNewPipe;
		ClosePipe();
		return false;	
	}

	HANDLE hSwap = pNewPipe->m_hPipe;
	pNewPipe->m_hPipe = m_hPipe;
	m_hPipe = hSwap;

	// the two pipes can keep their own events. Those are 
	// only related to a specific pipe while we're waiting

	*ppNewConnection = pNewPipe;

	return true;
}


bool CIPCPipe::ConnectPipe( const char *pchPipeName )
{
	std::string sFullName = "\\\\.\\pipe\\";
	sFullName += pchPipeName;

	m_hPipe = ::CreateFile( sFullName.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL );

	if( m_hPipe == INVALID_HANDLE_VALUE )
		return false;

	return true;
}


void CIPCPipe::ClosePipe()
{
	if ( m_hPipe != INVALID_HANDLE_VALUE )
	{
		::CloseHandle( m_hPipe );
		m_hPipe = INVALID_HANDLE_VALUE;
	}
	if ( m_hEvent != INVALID_HANDLE_VALUE )
	{
		::CloseHandle( m_hEvent );
		m_hEvent = INVALID_HANDLE_VALUE;
	}
}

bool CIPCPipe::IsValid() const
{
	return m_hPipe != INVALID_HANDLE_VALUE;
}

bool CIPCPipe::GetNextMessage( uint32_t *punMessageType, uint32_t *punPayloadLength, uint32_t unTimeoutMs  )
{
	*punMessageType = 0;
	*punPayloadLength = 0;
	if( m_hPipe == INVALID_HANDLE_VALUE )
		return false;

	OVERLAPPED overlapped = {0};
	overlapped.hEvent = m_hEvent;

	uint32_t runRead[2];
	DWORD unBytesRead = 0;
	DWORD res = ::ReadFile( m_hPipe, runRead, sizeof( runRead ), &unBytesRead, &overlapped );
	if( !res )
	{
		DWORD err = ::GetLastError();
		switch( err )
		{
		case ERROR_IO_PENDING:
			{
				res = WaitForSingleObject( m_hEvent, unTimeoutMs );
				switch( res )
				{
				case WAIT_TIMEOUT:
					{
						// wait timed out. Just return true to let the caller know it was a timeout 
						// and not an error
						::CancelIo( m_hPipe );
						return true;
					}

				case WAIT_OBJECT_0:
					{
						// the event was signalled. We have data to read
						res = GetOverlappedResult( m_hPipe, &overlapped, &unBytesRead, FALSE );
						if( !res )
						{
							// failed somehow
							Log( "Failed to GetOverlappedResult after event was triggered\n" );
							ClosePipe();
							return false;
						}
					}
					break;

				default:
					{
						// some other error occurred
						::CancelIo( m_hPipe );
						err = GetLastError();
						Log( "waiting on event failed in GetNextMessage with res %d and error %d\n", res, err );
						ClosePipe();
						return false;

					}
				}

			}
			break;

		default:
			{
				// some other error occurred
				Log( "Pipe closed in GetNextMessage with error %d\n", err );
				ClosePipe();
				return false;
			}
		}

	}

	if( unBytesRead != sizeof( runRead ) )
	{
		Log( "Read %d bytes instead of %d bytes in GetNextMessage\n", unBytesRead, sizeof( runRead ) );
		ClosePipe();
		return false;
	}

	*punMessageType = runRead[0];
	*punPayloadLength = runRead[1];
	return true;
}


bool CIPCPipe::GetMessagePayload( void *pvPayloadBuffer, uint32_t unPayloadLength )
{
	if( m_hPipe == INVALID_HANDLE_VALUE )
		return false;

	// if somebody tries to read a 0 length payload, tell them it totally worked
	if( unPayloadLength == 0 )
		return true;

	DWORD unBytesRead = 0;
	if( !::ReadFile( m_hPipe, pvPayloadBuffer, unPayloadLength, &unBytesRead, NULL )
		|| unPayloadLength != unBytesRead )
	{
		ClosePipe();
		return false;
	}

	return true;
}


bool CIPCPipe::SendMessageInternal( void *pvMessage, uint32_t unTotalMessageLength )
{
	DWORD unBytesWritten = 0;
	if( !::WriteFile( m_hPipe, pvMessage, unTotalMessageLength, &unBytesWritten, NULL ) 
		|| unBytesWritten != unTotalMessageLength )
	{
		ClosePipe();
		return false;
	}

	return true;
}

#endif // _WIN32


// -------------------------------------
// Posix implementation of CIPCPipe
// -------------------------------------
#if defined(POSIX)
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

CIPCPipe::CIPCPipe()
{
	m_nSocket = -1;
	m_pSharedMem = NULL;
}

static bool SetSocketKeepAlive( int nSocket )
{
	if( nSocket == -1 )
		return false;

	int optval = 1;
	socklen_t optlen = sizeof(optval);
	
	int res = setsockopt( nSocket, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen );
	if( res != 0 )
		return false;

#if defined( LINUX )
	// seconds the connection must be idle before keepalives start
	optval = 10;
	res = setsockopt( nSocket, SOL_TCP, TCP_KEEPIDLE, &optval, optlen );
	if( res != 0 )
		return false;

	// number of outstanding keepalive probes before the connection is killed
	optval = 5;
	res = setsockopt( nSocket, SOL_TCP, TCP_KEEPCNT, &optval, optlen );
	if( res != 0 )
		return false;

	// seconds between keepalive probes
	optval = 2;
	res = setsockopt( nSocket, SOL_TCP, TCP_KEEPINTVL, &optval, optlen );
	if( res != 0 )
		return false;
#endif

	return true;
}



bool CIPCPipe::CreatePipe( const char *pchPipeName )
{
	if( m_nSocket != -1 )
		return false;

	m_nSocket = socket( AF_INET, SOCK_STREAM, 0 );
	if( m_nSocket == -1 )
	{
		Log( "Unable to create server socket errno=%d\n", errno );
		return false;
	}
	
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	addr.sin_port = 0; // let the system pick a port for us. We'll publish it as shared mem
	int res = bind( m_nSocket, (const sockaddr *)&addr, sizeof(addr) );
	if( res != 0 )
	{
		Log( "Unable to bind server socket errno=%d\n", errno );
		ClosePipe();
		return false;
	}

	res = listen( m_nSocket, 5 );
	if( res != 0 )
	{
		Log( "Unable to listen on server socket. errno=%d\n", errno );
		ClosePipe();
		return false;
	}

	// figure out what port we actually bound to
	socklen_t addrlen = sizeof( addr );
	res = getsockname( m_nSocket, (sockaddr *)&addr, &addrlen );
	if( res == -1 || addrlen != sizeof( addr ) )
	{
		Log( "Unable to get address from server socket. errno=%d\n", errno );
		ClosePipe();
		return false;
	}

	m_pSharedMem = IPC::CreateSharedMem( pchPipeName, sizeof( uint16_t ), IPC::ISharedMem::ReadWrite );
	if( !m_pSharedMem )
	{
		Log( "Failed to create shared mem %s to share port number with clients\n", pchPipeName );
		ClosePipe();
		return false;
	}
	*((uint16_t *)m_pSharedMem->Pointer() ) = ntohs( addr.sin_port );

	//printf( "Bound to port %d\n", ntohs( addr.sin_port ) );

	return true;
}


bool CIPCPipe::WaitForConnection( uint32_t unTimeoutMs, CIPCPipe **ppNewConnection )
{
	*ppNewConnection = NULL;
	if( m_nSocket == -1  )
		return false;

	// Wait for the client to connect to the socket
	fd_set listenSet;
	FD_ZERO( &listenSet );
	FD_SET( m_nSocket, &listenSet );
	timeval timeout;
	timeout.tv_sec = unTimeoutMs / 1000;
	timeout.tv_usec = ( unTimeoutMs % 1000 ) * 1000;

	int res = select( m_nSocket + 1, &listenSet, NULL, NULL, &timeout );
	if( res == -1 )
	{
		Log( "select failed on listening socket: errno=%d\n", errno );
		return false;
	}

	// if the listen socket isn't set there is no connection waiting. Return
	// true so the caller can have time to work
	if( !FD_ISSET( m_nSocket, &listenSet ) )
		return true;

	sockaddr_in addr;
	socklen_t addrLen = sizeof( addr );
	res = accept( m_nSocket, (sockaddr *)&addr, &addrLen );
	if( res < 0 )
	{
		Log( "accept failed with error %d\n", errno );
		ClosePipe();
		return false;
	}

	// set keepalive on the socket so our reads will fail if the client goes away
	if( !SetSocketKeepAlive( m_nSocket ) )
	{
		Log( "Failed to set SO_KEEPALIVE on new connection: errno=%d\n", errno );
		ClosePipe();
		return false;
	}

	// res is the socket of the new connection. Return a pipe that uses it
	CIPCPipe *pNewPipe = new CIPCPipe();
	pNewPipe->m_nSocket = res;
	pNewPipe->m_sPipeName = m_sPipeName;
	*ppNewConnection = pNewPipe;

	return true;
}


bool CIPCPipe::ConnectPipe( const char *pchPipeName )
{
	m_pSharedMem = IPC::CreateSharedMem( pchPipeName, sizeof( uint16_t ), IPC::ISharedMem::Read );
	if( !m_pSharedMem )
	{
		Log( "Unable to create shared mem to get port number for pipe %s.\n", pchPipeName );
		return false;
	}

	m_nSocket = socket( AF_INET, SOCK_STREAM, 0 );
	if( m_nSocket == -1 )
	{
		Log( "Unable to create server socket errno=%d\n", errno );
		ClosePipe();
		return false;
	}

	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	addr.sin_port = htons( *((uint16_t *)m_pSharedMem->Pointer() ) );

	int res = connect( m_nSocket, (const sockaddr *)&addr, sizeof( addr ) );
	if( res == -1 )
	{
		Log( "Unable to connect to pipe errno=%d\n", errno );
		ClosePipe();
		return false;
	}

	// set keepalive on the socket so our reads will fail if the server goes away
	if( !SetSocketKeepAlive( m_nSocket ) )
	{
		Log( "Failed to set SO_KEEPALIVE on outgoing connection: errno=%d\n", errno );
		ClosePipe();
		return false;
	}

	return true;
}


void CIPCPipe::ClosePipe()
{
	if( m_nSocket != -1 )
	{
		shutdown( m_nSocket, SHUT_WR );
		close( m_nSocket );
		m_nSocket = -1;
	}

	if( m_pSharedMem != NULL )
	{
		m_pSharedMem->Destroy();
		m_pSharedMem = NULL;
	}
}

bool CIPCPipe::IsValid() const
{
	return m_nSocket != -1;
}

bool CIPCPipe::GetNextMessage( uint32_t *punMessageType, uint32_t *punPayloadLength, uint32_t unTimeoutMs )
{
	*punMessageType = *punPayloadLength = 0;
	if( m_nSocket == -1 )
		return false;

	// Wait for the other end to send a message
	fd_set readSet;
	FD_ZERO( &readSet );
	FD_SET( m_nSocket, &readSet );
	timeval timeout;
	timeout.tv_sec = unTimeoutMs / 1000;
	timeout.tv_usec = ( unTimeoutMs % 1000 ) * 1000;

	int res = select( m_nSocket + 1, &readSet, NULL, NULL, &timeout );
	if( res == -1 )
	{
		Log( "select failed on reading socket: errno=%d\n", errno );
		return false;
	}

	// if the listen socket isn't set there is no connection waiting. Return
	// true so the caller can have time to work
	if( !FD_ISSET( m_nSocket, &readSet ) )
		return true;

	uint32_t runRead[2];
	ssize_t sizeReadTotal = 0;
	ssize_t sizeRead;

	while( sizeReadTotal < sizeof( runRead ) )
	{
		sizeRead = read( m_nSocket, ((char *)runRead) + sizeReadTotal, sizeof( runRead ) - sizeReadTotal );
		if( sizeRead == 0 )
		{
			Log( "Socket closed\n" );
			break;
		}
		if( sizeRead == -1 )
		{
			Log( "Error reading from socket\n" );
			break;
		}
		sizeReadTotal += sizeRead;
	}
		
	if( sizeReadTotal != sizeof( runRead ) )
	{
		Log( "Unable to read message from socket\n", errno );
		ClosePipe();
		return false;
	}

	*punMessageType = runRead[0];
	*punPayloadLength = runRead[1];
	return true;
}


bool CIPCPipe::GetMessagePayload( void *pvPayloadBuffer, uint32_t unPayloadLength )
{
	if( m_nSocket == -1 )
		return false;

	// if somebody tries to read a 0 length payload, tell them it totally worked
	if( unPayloadLength == 0 )
		return true;

	ssize_t sizeReadTotal = 0;
	ssize_t sizeRead;
	while( sizeReadTotal < unPayloadLength )
	{
		sizeRead = read( m_nSocket, ((char *)pvPayloadBuffer) + sizeReadTotal, unPayloadLength - sizeReadTotal );
		if( sizeRead == -1 )
		{
			Log( "Failed when reading payload of length %u: errno=%d\n", unPayloadLength, errno );
			break;
		}
		sizeReadTotal += sizeRead;
	}
		
	if( sizeReadTotal != unPayloadLength )
	{
		Log( "Unable to read message from socket\n" );
		ClosePipe();
		return false;
	}

	return true;
}


bool CIPCPipe::SendMessageInternal( void *pvMessage, uint32_t unTotalMessageLength )
{
	ssize_t totalSizeWritten = 0;
	while( totalSizeWritten < unTotalMessageLength )
	{
		ssize_t sizeToTry = unTotalMessageLength - totalSizeWritten;
		ssize_t sizeWritten = write( m_nSocket, ((const char *)pvMessage) + totalSizeWritten, sizeToTry );
		if( sizeWritten == -1 )
		{
			ClosePipe();
			return false;
		}
		if( sizeWritten == 0 )
		{
			Log( "Attempted to write %u bytes and ended up writing 0. Closing the pipe\n", sizeToTry );
			ClosePipe();
			return false;
		}
		totalSizeWritten += sizeWritten;
	}

	return true;
}

#endif // posix


