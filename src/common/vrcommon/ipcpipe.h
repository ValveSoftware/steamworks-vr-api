//========= Copyright Valve Corporation ============//
#pragma once

#include "hmdplatform_private.h"

#pragma warning( push )
#pragma warning( disable : 4244 )
#include "google/protobuf/message.h"
#pragma warning( push )
#include "vrlog.h"

namespace IPC
{
	class ISharedMem;
}

class CIPCPipe
{
public:
	CIPCPipe();
	~CIPCPipe();

	/** Creates the named pipe. 
	* Returns false if and error occurred with the pipe and no connection was established. 
	* Used by: Servers 
	*/
	bool CreatePipe( const char *pchPipeName );

	/** Waits for an incoming connection.
	* Returns NULL if an error occurred with the pipe and no connection was established. 
	* Otherwise it returns a new CIPCPipe object that represents the new connection. THe 
	* caller is responsible for freeing that new pipe object 
	* Used by: Servers 
	*/
	bool WaitForConnection( uint32_t unTimeoutMs, CIPCPipe **ppNewConnection );

	/** Connects to an existing named pipe. 
	* Used by: Clients
	*/
	bool ConnectPipe( const char *pchPipeName );

	/** Closes the named pipe.
	* Used by: Clients and Servers
	*/
	void ClosePipe();

	/** Returns true if this object contains a valid pipe */
	bool IsValid() const;

	/** Blocks waiting for a message from the pipe. Return false if the pipe closed before
	* a message arrived.
	* Used by: Clients and Servers
	*/
	bool GetNextMessage( uint32_t *punMessageType, uint32_t *punPayloadLength, uint32_t unTimeoutMs );

	/** Blocks waiting for the payload of a message from the pipe. Returns false if the 
	* pipe closes before that body arrives. The buffer provided must have at least enough space
	* for the payload.
	* Used by: Clients and Servers
	*/
	bool GetMessagePayload( void *pvPayloadBuffer, uint32_t unPayloadLength );

	/** Posts a protobuf message to the pipe. Returns false if the pipe closes before the
	* message is sent. A true return value doesn't guarantee that the message was ever
	* processed on the other end of the pipe.
	* Used by: Clients and Servers.
	*/
	template<typename T>
	bool SendProtobufMessage( uint32_t unMessageType, const T & payload );

	/** Waits for a particular protobuf message from the pipe. Returns false if the pipe closes before the
	* message is sent or of the wrong kind of message is received. 
	* Used by: Clients
	*/
	template<typename T>
	bool ReceiveProtobufMessage( uint32_t unMessageType, T & payload, uint32_t unTimeoutMs );

	/** Posts a protobuf message to the pipe. Returns false if the pipe closes before the
	* message is sent. A true return value doesn't guarantee that the message was ever
	* processed on the other end of the pipe.
	* Used by: Clients and Servers.
	*/
	template<typename Request, typename Response>
	bool SendProtobufMessageAndWaitForResponse( uint32_t unMessageType, const Request & requestPayload, uint32_t unResponseMessageType, Response & responsePayload, uint32_t unTimeoutMs );

	/** Reads payload and parses it into a protobuf message
	* Returns false if the payload couldn't be read or the parse fails 
	* Used by: Clients and Servers
	*/
	template< typename T >
	bool GetProtobufPayload( uint32_t unMessagePayloadLength, T & msg );

	/** Sends a message with no payload. Returns false if the pipe closes before the
	* message is sent. A true return value doesn't guarantee that the message was ever
	* processed on the other end of the pipe.
	* Used by: Clients and Servers.
	*/
	bool SendSimpleMessage( uint32_t unMessageType );

	/** Sends a message with an already packed payload. In general, the Protobuf version
	* of this method is preferred because it avoids a memcpy. Returns false if the pipe closes before the
	* message is sent. A true return value doesn't guarantee that the message was ever
	* processed on the other end of the pipe.
	* Used by: Clients and Servers.
	*/
	bool SendPackedMessage( uint32_t unMessageType, void *pvPayload, uint32_t unPayloadLength );


private:

	bool SendMessageInternal( void *pvMessage, uint32_t unTotalMessageLength );

	std::string m_sPipeName;

#if defined( _WIN32 )
	void *m_hPipe;
	void *m_hEvent;
#elif defined( POSIX )
	int m_nSocket;
	IPC::ISharedMem *m_pSharedMem;
#endif
};


template<typename T>
bool CIPCPipe::SendProtobufMessage( uint32_t unMessageType, const T & payload )
{
	const google::protobuf::MessageLite & msgPayload = payload;
	uint32_t unSize = msgPayload.ByteSize();
	void *pvSerialized = malloc( unSize );
	msgPayload.SerializeWithCachedSizesToArray( (uint8_t *)pvSerialized );
	bool bRetVal = SendPackedMessage( unMessageType, pvSerialized, unSize );
	free( pvSerialized );
	return bRetVal;
}

template< typename T >
bool CIPCPipe::GetProtobufPayload( uint32_t unMessagePayloadLength, T & payload )
{
	bool bRetVal = true;
	if( unMessagePayloadLength )
	{
		google::protobuf::MessageLite & msgPayload = payload;
		void *pvPayload = malloc( unMessagePayloadLength );
		if( GetMessagePayload( pvPayload, unMessagePayloadLength ) )
		{
			msgPayload.ParseFromArray( pvPayload, unMessagePayloadLength );
		}
		else
		{
			Log( "Attempted to read payload of %d bytes from pipe but failed\n", unMessagePayloadLength );
			bRetVal = false;
		}
		free( pvPayload );
	}
	return bRetVal;
}


template<typename T>
bool CIPCPipe::ReceiveProtobufMessage( uint32_t unMessageType, T & payload, uint32_t unTimeoutMs )
{
	uint32_t unReceivedMessageType, unReceivedPayloadLength;
	if( !GetNextMessage( &unReceivedMessageType, &unReceivedPayloadLength, unTimeoutMs ) )
		return false;

	// timeouts count as failures too
	if( unReceivedMessageType == 0 )
		return false;

	// get the payload first so that we don't corrupt the stream on mismatched types
	bool bRetVal = GetProtobufPayload( unReceivedPayloadLength, payload );
	if( unMessageType != unReceivedMessageType )
	{
		Log( "Received message of type %d when %d was expected\n", unReceivedMessageType, unMessageType );
		bRetVal = false;
	}

	return bRetVal;
}


template<typename Request, typename Response>
bool CIPCPipe::SendProtobufMessageAndWaitForResponse( uint32_t unMessageType, const Request & requestPayload, uint32_t unResponseMessageType, Response & responsePayload, uint32_t unTimeoutMs )
{
	return SendProtobufMessage( unMessageType, requestPayload ) 
		&& ReceiveProtobufMessage( unResponseMessageType, responsePayload, unTimeoutMs );
}
