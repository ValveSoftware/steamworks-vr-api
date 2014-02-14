//========= Copyright Valve Corporation ============//
#include "vripcconstants.h"
#include "ipctools.h"
#include "vrlog.h"


bool CVRSharedState::BInit( SharedStateRole eRole )
{
	IPC::ISharedMem::Access eAccess = ( eRole == CVRSharedState::Server ? IPC::ISharedMem::ReadWrite : IPC::ISharedMem::Read );
	m_pSharedStateMem = IPC::CreateSharedMem( k_VRSharedMemName, sizeof( VRSharedState_t ), eAccess );
	if( !m_pSharedStateMem )
	{
		Log( "Unable to create shared memory for IPC client\n" );
		return false;
	}

	m_pSharedMutex = IPC::CreateMutex( k_VRSharedMutexName, false, NULL );
	if( !m_pSharedMutex )
	{
		Log( "Unable to create shared mutex for IPC client\n" );
		return false;
	}

	return true;
}


void CVRSharedState::Cleanup()
{
	if( m_pSharedStateMem && m_pSharedStateMem->IsValid() )
	{
		m_pSharedStateMem->Destroy();
		m_pSharedStateMem = NULL;
	}

	if( m_pSharedMutex )
	{
		m_pSharedMutex->Destroy();
		m_pSharedMutex = NULL;
	}
}


void CVRSharedState::LockSharedMem()
{
	m_pSharedMutex->Wait();
}
void CVRSharedState::UnlockSharedMem()
{
	m_pSharedMutex->Release();
}
const VRSharedState_t *CVRSharedState::GetSharedState()
{
	return (VRSharedState_t *)m_pSharedStateMem->Pointer();
}

VRSharedState_t *CVRSharedState::GetWritableSharedState()
{
	return (VRSharedState_t *)m_pSharedStateMem->Pointer();
}


CVRSharedStatePtrBase::CVRSharedStatePtrBase( CVRSharedState *pSharedState )
{
	m_pSharedState = pSharedState;
	m_pSharedState->LockSharedMem();
}

CVRSharedStatePtrBase::~CVRSharedStatePtrBase()
{
	m_pSharedState->UnlockSharedMem();
}

const VRSharedState_t *CVRSharedStatePtr::operator ->()
{
	return m_pSharedState->GetSharedState();
}

VRSharedState_t *CVRSharedStateWritablePtr::operator ->()
{
	return m_pSharedState->GetWritableSharedState();
}

