//========= Copyright Valve Corporation ============//

#include "ipctools.h"

#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <assert.h>
#include <SDL_timer.h>
#include <limits.h>

#include "vrlog.h"

#define Q_ARRAYSIZE( x ) (sizeof(x) / sizeof(x[0]) )

#if defined(LINUX)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/time.h>
#include <linux/futex.h>
#include <syscall.h>

#define SelfTid syscall( SYS_gettid )

#define _ReadWriteBarrier() asm volatile("" ::: "memory")

#define ThreadPause() __asm __volatile("pause")

// We can have a mix of 32-bit and 64-bit processes using our IPC
// primitives, such as a 64-bit gameoverlayrenderer communicating with
// a 32-bit gameoverlayui.  pthread synchronization primitives
// vary with the pointer size and so cannot be shared between different
// bitness, thus we build our own based on raw Linux futexes.
// A futex is always a 32-bit value so we can safely share them
// between 32-bit and 64-bit processes.
//
// TODO - We could implement robustness here by following the
// robust_list specification from https://www.kernel.org/doc/Documentation/robust-futexes.txt,
// but we have relatively good PID-based purging support so we don't bother.
//
// References: 
// http://http://man7.org/linux/man-pages/man2/futex.2.html
// http://www.akkadia.org/drepper/futex.pdf
// http://locklessinc.com/articles/mutex_cv_futex

int linux_futex( volatile int *uaddr, int op, int val, const struct timespec *timeout,
                 volatile int *uaddr2, int val3 )
{
    return syscall( SYS_futex, uaddr, op, val, timeout, uaddr2, val3 );
}

// See TODO above, no robustness support.
#define TRUST_ROBUST_MUTEX 0

// Leave mutex tracking off by default.  Making it DEBUG means
// that debug and retail code cannot interoperate, so don't
// do that either.
// Be sure to recompile all code, 32-bit and 64-bit, when changing this define.
#if 0
#define TRACK_MUTEX_OWNER
#endif

struct LfMutex
{
    volatile int32_t nFutex;
    // We always track the owner process ID so that we can
    // clean up when processes die while holding a mutex.
    volatile int32_t nOwnerPid;
#ifdef TRACK_MUTEX_OWNER
    // We only track the owner thread ID for debugging purposes.
    volatile int32_t nOwnerTid;
#endif
};

// Futex states.  These must be the specific integer values 0, 1, 2.
enum
{
    LFMUTEX_UNLOCKED  = 0,
    LFMUTEX_LOCKED    = 1,
    LFMUTEX_CONTENDED = 2,
};

void LfMutexInit( LfMutex *pMutex )
{
    pMutex->nOwnerPid = 0;
#ifdef TRACK_MUTEX_OWNER
    pMutex->nOwnerTid = 0;
#endif
    // Do this last in case we're unlocking an abandoned futex.
    pMutex->nFutex = LFMUTEX_UNLOCKED;
}

void LfMutexDestroy( LfMutex *pMutex )
{
    // Nothing necessary here, but clear the state.
    memset( pMutex, 0, sizeof( *pMutex ) );
}

int LfMutexLock( LfMutex *pMutex, const struct timespec *pTimeout )
{
    int nState;

    // NOTE: We're fairly sloppy on the timeout handling since we
    // do quite a few operations without accounting for them in
    // time spent.  This isn't really harmful, and thus just
    // noted instead of fixed.

    // Do a short spin to try and acquire without a full wait.
    for (int i = 0; i < 100; i++)
    {
        nState = __sync_val_compare_and_swap( &pMutex->nFutex, LFMUTEX_UNLOCKED, LFMUTEX_LOCKED );
        if ( nState == LFMUTEX_UNLOCKED )
        {
            // No contention, easy acquire.
            pMutex->nOwnerPid = getpid();
#ifdef TRACK_MUTEX_OWNER
            pMutex->nOwnerTid = SelfTid;
#endif
            return 0;
        }

        ThreadPause();
    }

    // We couldn't acquire so we have contention, upgrade the state if necessary.
    if ( nState == LFMUTEX_LOCKED )
    {
        nState = __sync_lock_test_and_set( &pMutex->nFutex, LFMUTEX_CONTENDED );
    }

    while ( nState != LFMUTEX_UNLOCKED )
    {
        // Wait for the futex to change so that we can try acquiring again.
        // We don't care about the return value here as we'll just blindly
        // try the reacquire.
        if ( linux_futex( &pMutex->nFutex, FUTEX_WAIT, LFMUTEX_CONTENDED, pTimeout, NULL, 0 ) == -1 )
        {
            if ( errno == EWOULDBLOCK )
            {
                // Futex has already changed state, see if we can acquire.
            }
            else if ( errno == EINTR )
            {
                // Ignore signals, loop and wait again.
            }
            else
            {
                return errno;
            }
        }

        // Try to acquire again.
        nState = __sync_lock_test_and_set( &pMutex->nFutex, LFMUTEX_CONTENDED );
    }

    // We've finally acquired.
    pMutex->nOwnerPid = getpid();
#ifdef TRACK_MUTEX_OWNER
    pMutex->nOwnerTid = SelfTid;
#endif
    return 0;
}

int LfMutexUnlock( LfMutex *pMutex )
{
    pMutex->nOwnerPid = 0;
#ifdef TRACK_MUTEX_OWNER
    pMutex->nOwnerTid = 0;
#endif

    if ( __sync_fetch_and_sub( &pMutex->nFutex, 1 ) == LFMUTEX_LOCKED )
    {
        // Uncontended unlock, we're done.
        return 0;
    }

    // Release the mutex.
    pMutex->nFutex = LFMUTEX_UNLOCKED;
    // Wake up a waiter.
    if ( linux_futex( &pMutex->nFutex, FUTEX_WAKE, 1, NULL, NULL, 0 ) == -1 )
    {
        return errno;
    }

    return 0;
}

void LfMutexConsistent( LfMutex *pMutex )
{
    // We don't currently support robustness, so this is just a placeholder.
}

#ifdef TRACK_MUTEX_OWNER
#define SysMutexOwner( _SysMutex ) ( ( _SysMutex )->nOwnerTid )
#endif

struct LfCondVar
{
    volatile int32_t nSequence;
};

void LfCondVarInit( LfCondVar *pCondVar )
{
    pCondVar->nSequence = 0;
}

void LfCondVarDestroy( LfCondVar *pCondVar )
{
    // Nothing necessary here, but clear the state.
    memset( pCondVar, 0, sizeof( *pCondVar ) );
}

int LfCondVarWait( LfCondVar *pCondVar, LfMutex *pMutex, const struct timespec *pTimeout )
{
    int nSequence = pCondVar->nSequence;

    LfMutexUnlock( pMutex );

    int nErr = 0;

    while ( linux_futex( &pCondVar->nSequence, FUTEX_WAIT, nSequence, pTimeout, NULL, 0 ) == -1 )
    {
        if ( errno == EWOULDBLOCK )
        {
            // Futex has already changed state, no need to wait.
            break;
        }
        else if ( errno == EINTR )
        {
            // Ignore signals, loop and wait again.
        }
        else
        {
            nErr = errno;
            break;
        }
    }

    // We have to reacquire the mutex even if we timed out, so
    // we do an infinite wait here.
    while ( __sync_lock_test_and_set( &pMutex->nFutex, LFMUTEX_CONTENDED ) != LFMUTEX_UNLOCKED )
    {
        // We can't do much with errors here (nor are we expecting any) as
        // we have to acquire.
        linux_futex( &pMutex->nFutex, FUTEX_WAIT, LFMUTEX_CONTENDED, NULL, NULL, 0 );
    }

    pMutex->nOwnerPid = getpid();
#ifdef TRACK_MUTEX_OWNER
    pMutex->nOwnerTid = SelfTid;
#endif

    return nErr;
}

int LfCondVarBroadcast( LfCondVar *pCondVar, LfMutex *pMutex )
{
    // Modify the futex so the kernel will wake waiters.
    int nSequence = __sync_add_and_fetch( &pCondVar->nSequence, 1 );

    // Wake one waiter and requeue the rest on the mutex's wait list.
    if ( !pMutex ||
         ( linux_futex( &pCondVar->nSequence, FUTEX_CMP_REQUEUE, 1, NULL, &pMutex->nFutex, nSequence ) == -1 &&
           errno == EAGAIN ) )
    {
        // As per the futex(2) man page this may indicate a race, so
        // we just use a regular wake.  This is less performant because
        // everybody will wake and then sleep on the mutex, but
        // we already failed at trying to do it quickly.
        if ( linux_futex( &pCondVar->nSequence, FUTEX_WAKE, INT32_MAX, NULL, NULL, 0 ) == -1 )
        {
            return errno;
        }
    }

    return 0;
}

int LfCondVarSignal( LfCondVar *pCondVar )
{
    // Modify the futex so the kernel will wake waiters.
    __sync_fetch_and_add( &pCondVar->nSequence, 1 );

    // Wake just one waiter.
    if ( linux_futex( &pCondVar->nSequence, FUTEX_WAKE, 1, NULL, NULL, 0 ) == -1 )
    {
        return errno;
    }

    return 0;
}

#endif

#ifdef SysMutexOwner
#define CheckIsSysMutexOwner( _SysMutex )                                   \
    if ( SysMutexOwner( _SysMutex ) != SelfTid )                            \
    {                                                                       \
        Log( "%s(%u): Invalid mutex ownership, is %d vs. expected %d\n",    \
             __FILE__, __LINE__, SysMutexOwner( _SysMutex ), SelfTid );     \
    }
#define CheckNotSysMutexOwner( _SysMutex )                                  \
    if ( SysMutexOwner( _SysMutex ) == SelfTid )                            \
    {                                                                       \
        Log( "%s(%u): Invalid mutex ownership, is %d when not expected\n",  \
             __FILE__, __LINE__, SysMutexOwner( _SysMutex ) );              \
    }
#else
#define CheckIsSysMutexOwner( _SysMutex )
#define CheckNotSysMutexOwner( _SysMutex )
#endif

#ifdef GAMEOVERLAYRENDERER_EXPORTS
#ifndef ENABLE_PROFILING
extern void Log( char const *pMsgFormat, ... );
#endif
#endif

#define ACCESS_ALL (S_IRWXU|S_IRWXG|S_IRWXO)

#if defined(OSX)

#define NAME_SHORT_LEN 32
#define SHARED_MEMORY_NAME_LEN NAME_SHORT_LEN

#define SetSharedMemoryName( _pszName, _nChars, _nSemHash, _nBytes ) \
    snprintf( _pszName, _nChars, "/Shm/VR_%s_%x_%d", getenv( "USER" ), _nSemHash, _nBytes )

#elif defined(LINUX)

// Use a longer name limit as we're adding the user name.
#define SHARED_MEMORY_NAME_LEN 128

// Prepend the user name to separate Steam instances per-user.
// Linux doesn't like slashes other than at the start of the object name.
#define SetSharedMemoryName( _pszName, _nChars, _nSemHash, _nBytes ) \
    snprintf( _pszName, _nChars, "/%s-VRShm_%x", getenv( "USER" ), _nSemHash )

#endif

#define CREATE_OBJECT( TYPE, ... )      \
	TYPE *pObj = new TYPE();            \
	if ( !pObj->Init( __VA_ARGS__ ) )   \
	{                                   \
		delete pObj;                    \
		return NULL;                    \
	}                                   \
	return pObj;
	
#if defined(OSX)

static bool BlockingWait( sem_t *pSem, unsigned int msWaitTime, int *pError )
{
	bool bSuccess = false;
	long nAttempts = 0;
	
	if ( pError )
		*pError = 0;
	
	unsigned int nTimeStart = SDL_GetTicks();
	while ( !bSuccess && ( (SDL_GetTicks() - nTimeStart) < msWaitTime ) )
	{
		usleep( 1000 * 10 ); // 10ms granularity
		bSuccess = ( sem_trywait( pSem ) == 0 );
		if ( !bSuccess && (errno != EAGAIN) && (errno != EINTR) )
		{
			if ( pError ) 
				*pError = errno;
			break;
		}
	}
	
	return bSuccess;
}

#endif // OSX

namespace IPC
{
	uint32_t crc32(uint32_t crc, void *buf, uint32_t len);
	
#if defined(OSX)

	class BinarySemaphore
	{
	public:
		BinarySemaphore()
		{
			m_pLockFloor = SEM_FAILED;
			m_pLockCeiling = SEM_FAILED;
			*m_szLockCeilName = '\0';
			*m_szLockFloorName = '\0';
		}
		
		virtual bool Lock(uint32_t msWaitTime /*= UINT32_MAX */) // NOT re-entrant. Fail if attempted
		{
			bool bSuccess = false;
			if ( msWaitTime == 0 )
			{
				bSuccess = ( sem_trywait( m_pLockFloor ) == 0 );
			}
			else
			{	// Ugh - timed wait. If this becomes a real problem, use mach semaphores
				// which have a timeout. Will also require writing a mach port server to allow
				// names of semaphores to be shared across processes.
				//
				// Note: We never do the fully blocking wait or there wouldn't be any way
				// to do WaitForMultiple.
				bSuccess = ( sem_trywait( m_pLockFloor ) == 0 );
				if ( !bSuccess && msWaitTime )
				{
					bSuccess = BlockingWait( m_pLockFloor, msWaitTime, NULL );
				}
			}
			
			// OK. If any of that succeeded, we need to release the ceiling.
			if ( bSuccess )
			{
				if ( sem_post( m_pLockCeiling ) != 0 )
				{
					bSuccess = false;
					sem_post( m_pLockFloor ); // at this point if this fails, there's little to say/do. 
					Log("Fatal on Lock(): Locked the floor, couldn't release the ceiling: %s\n", m_szLockCeilName );
				}

			}

			return bSuccess;
		}

		virtual bool Release() // must tolerate releasing when not locked for event like semantics.
		{
			// Only works of the ceiling succeeds to lock - making the semaphore a binary semaphore
			if ( sem_trywait( m_pLockCeiling ) == 0 )
			{
				if ( sem_post( m_pLockFloor ) != 0 )
				{
					sem_post( m_pLockCeiling ); // at this point if this fails, there's little to say/do. 
					Log("Fatal on Release(): Locked the ceiling, couldn't release the floor: %s\n", m_szLockFloorName );
					return false;
				}

			}
			
			return true;
		}

		virtual bool Destroy()
		{
			bool bSuccess = true;
			
			
			if ( m_pLockFloor != SEM_FAILED )
				if ( sem_close( m_pLockFloor ) != 0 )
					bSuccess = false;
			
			if ( m_pLockCeiling != SEM_FAILED )
				if ( sem_close( m_pLockCeiling ) != 0 )
					bSuccess = false;
			
			m_pLockFloor = SEM_FAILED;
			m_pLockCeiling = SEM_FAILED;
			*m_szLockCeilName = '\0';
			*m_szLockFloorName = '\0';
			
			return bSuccess;
		}

		virtual ~BinarySemaphore()
		{
			Destroy();
		}
		
		bool Init( const char *pszName, bool bCreateAndTakeLock, bool *pLockTaken )
		{
			if ( pLockTaken )
				*pLockTaken = false;
			
			unsigned int nSemHash = crc32(0, (unsigned char *)pszName, strlen(pszName) );
			snprintf( m_szLockFloorName, Q_ARRAYSIZE(m_szLockFloorName), "/BSem/%x", nSemHash );
			snprintf( m_szLockCeilName, Q_ARRAYSIZE(m_szLockCeilName), "%s.2", m_szLockFloorName );

			int nValue = bCreateAndTakeLock ? 0 : 1;
			m_pLockFloor = sem_open( m_szLockFloorName, O_CREAT|O_EXCL, ACCESS_ALL, nValue );
			if ( m_pLockFloor != SEM_FAILED )
			{
				// Open with initial value flip of floor.
				m_pLockCeiling = sem_open( m_szLockCeilName, O_CREAT|O_EXCL, ACCESS_ALL, nValue ? 0 : 1);
				if ( ( m_pLockCeiling != SEM_FAILED ) && bCreateAndTakeLock && pLockTaken )
				{
					*pLockTaken = true;
				}
			}
			else if (errno == EEXIST )
			{
				m_pLockFloor = sem_open( m_szLockFloorName, 0 );
				if ( m_pLockFloor != SEM_FAILED )
				{
					// Need to loop - someone else opened with lock, need to wait for
					// them to create the 2nd semaphore.
					for ( int i = 0; i < 200; i++ )
					{
						m_pLockCeiling = sem_open(m_szLockCeilName, 0 );
						if ( m_pLockCeiling != SEM_FAILED )
							break;
						usleep( 1000 * 10 );
					}
				}
			}
			
			// clean up if something failed
			if ( ( m_pLockFloor == SEM_FAILED ) || ( m_pLockCeiling == SEM_FAILED ) )
			{
				Log( "Failed to create BinarySemaphore: %s - %s\n", pszName, m_szLockFloorName );
				Destroy();
				return false;
			}
			
			return true;
		}
		
	private:
		sem_t *m_pLockFloor;
		sem_t *m_pLockCeiling;
		
		char m_szLockFloorName[NAME_SHORT_LEN];
		char m_szLockCeilName[NAME_SHORT_LEN];
	};
    BinarySemaphore *CreateBinarySemaphore( const char *pszName, bool bCreateAndTakeLock, bool *pbLockTaken )
    {
        CREATE_OBJECT( BinarySemaphore, pszName, bCreateAndTakeLock, pbLockTaken );
    }

	class PosixMutex : public IMutex
	{
	public:
		PosixMutex()
		{
			m_pLockFloor = SEM_FAILED;
			m_pLockCeiling = SEM_FAILED;
			m_lockHolder = 0;
			*m_szLockCeilName = '\0';
			*m_szLockFloorName = '\0';
		}
		
		virtual bool Wait(uint32_t msWaitTime /*= UINT32_MAX */) // NOT re-entrant. Fail if attempted
		{
			this->SetLastError( kSyncSuccess );
			
			// Handle re-entrant case
			if ( m_lockHolder == pthread_self() )
			{
				// Note: If the lock is held by us, it's safe to reference - no one
				//	else can muck with it.  If it's held by someone else, it could
				//	change out from under us, but it should move between another pthread
				//	id and 0 - which should never trigger a conflict/race/deadlock
				if ( sem_post( m_pLockCeiling ) != 0 )
				{
					Log("Fatal on Lock(): Locked the floor, couldn't post the ceiling: %s\n", m_szLockCeilName );
					this->SetLastError( kSyncFail );
				}
				return true;
			}

			// OK. Just need to wait
			bool bSuccess = false;

			bSuccess = ( sem_trywait( m_pLockFloor ) == 0 );
			if ( !bSuccess && msWaitTime )
			{
				int err;
				bSuccess = BlockingWait( m_pLockFloor, msWaitTime, &err );
				
				// err won't be set for just a timeout fail.
				if ( !bSuccess && err )
					this->SetLastError( kSyncInvalidObject );
			}
			
			if ( bSuccess )
			{
				// took the lock - claim the thread id - don't pop the ceiling
				// on the first lock.
				if ( !__sync_bool_compare_and_swap (&m_lockHolder, 0, pthread_self() ) )
				{
					Log("Took lock, but old thread id != 0!!!");
					m_lockHolder = pthread_self(); // <shrug>
					this->SetLastError( kSyncFail );
				}
			}
			
			return bSuccess;
		}
		
		virtual void Release() 
		{
			// Verify we hold it.
			if ( m_lockHolder != pthread_self() )
			{
				this->SetLastError(kSyncFail);
				Log("Error on Release(): Not the lock owner: %s\n", m_szLockFloorName );
				return;
			}
			
			this->SetLastError(kSyncSuccess);
			
			// If we fail to wait on the ceiling, and we know we owned it,
			// then it's time to finally give it up for real.
			if ( sem_trywait( m_pLockCeiling ) != 0 )
			{
				if ( errno != EAGAIN )
				{
					Log("Fatal on Release(): errno %d: %s\n", errno, m_szLockFloorName );
					this->SetLastError(kSyncFail);
					return;
				}

				if ( !__sync_bool_compare_and_swap (&m_lockHolder, pthread_self(), 0 ) )
				{
					Log("Fatal on Release(): Thread not marked as mine: %s\n", m_szLockFloorName );
					this->SetLastError(kSyncFail);
					return;
				}

				if ( sem_post( m_pLockFloor ) != 0 )
				{
					Log("Fatal on Release(): Locked the ceiling, couldn't release the floor: %s\n", m_szLockFloorName );
					this->SetLastError(kSyncFail);
					return;
				}
			}
		}
		
		virtual void Destroy()
		{
			if ( m_pLockFloor != SEM_FAILED )
				sem_close( m_pLockFloor );
			
			if ( m_pLockCeiling != SEM_FAILED )
				sem_close( m_pLockCeiling );
			
			m_pLockFloor = SEM_FAILED;
			m_pLockCeiling = SEM_FAILED;
			*m_szLockCeilName = '\0';
			*m_szLockFloorName = '\0';
			m_lockHolder = 0;
		}
		
		virtual ~PosixMutex()
		{
			Destroy();
		}
		
		bool Init( const char *pszName, bool bInitialOwner, bool *pbCreator )
		{
			if ( pbCreator )
				*pbCreator = false;
			
			unsigned int nSemHash = crc32(0, (unsigned char *)pszName, strlen(pszName) );
			snprintf( m_szLockFloorName, Q_ARRAYSIZE(m_szLockFloorName), "/MTX/VR_%s_%x", getenv( "USER" ), nSemHash );
			snprintf( m_szLockCeilName, Q_ARRAYSIZE(m_szLockCeilName), "%s.2", m_szLockFloorName );

			int nValue = bInitialOwner ? 0 : 1;
			m_pLockFloor = sem_open( m_szLockFloorName, O_CREAT|O_EXCL, ACCESS_ALL, nValue );
			if ( m_pLockFloor != SEM_FAILED )
			{
				m_lockHolder = pthread_self();
				// Open with initial value flip of floor.
				m_pLockCeiling = sem_open( m_szLockCeilName, O_CREAT|O_EXCL, ACCESS_ALL, 0 );
				if ( ( m_pLockCeiling != SEM_FAILED ) && pbCreator )
				{
					*pbCreator = true;
				}
			}
			else if (errno == EEXIST )
			{
				m_pLockFloor = sem_open( m_szLockFloorName, 0 );
				if ( m_pLockFloor != SEM_FAILED )
				{
					// Need to loop - someone else opened with lock, need to wait for
					// them to create the 2nd semaphore.
					for ( int i = 0; i < 200; i++ )
					{
						m_pLockCeiling = sem_open(m_szLockCeilName, 0 );
						if ( m_pLockCeiling != SEM_FAILED )
							break;
						usleep( 1000 * 10 );
					}
				}
			}
			
			// clean up if something failed
			if ( ( m_pLockFloor == SEM_FAILED ) || ( m_pLockCeiling == SEM_FAILED ) )
			{
				Log( "Failed to create PosixMutex: %s - %s\n", pszName, m_szLockFloorName );
				Destroy();
				return false;
			}
			

			
			return true;
		}
		
	private:
		sem_t *m_pLockFloor;
		sem_t *m_pLockCeiling;
		
		pthread_t m_lockHolder;
		
		char m_szLockFloorName[NAME_SHORT_LEN];
		char m_szLockCeilName[NAME_SHORT_LEN];
	};

	class PosixEvent : public IEvent
	{
	public:
		PosixEvent()
		{
			m_pLock = NULL;
			m_pPending = SEM_FAILED;
			*m_szLockName = '\0';
			*m_szPendingName = '\0';
		}
		
		virtual bool Wait(uint32_t msWaitTime /*= UINT32_MAX */) // NOT re-entrant. Fail if attempted
		{
			bool bSuccess = false;
			
			this->SetLastError( kSyncFail );
			
			if ( sem_post( m_pPending ) != 0 )
			{
				Log( "Failed to post that I was waiting for an event" );
				return false;
			}
			
			bSuccess = m_pLock->Lock( msWaitTime );
			sem_trywait( m_pPending ); // we're off the queue no matter what
			
			// OK. If that succeeded we need to see if others are waiting.
			if ( bSuccess )
			{
				if ( sem_trywait( m_pPending ) == 0 ) // more people waiting!
				{
					sem_post( m_pPending ); // fix the count back up
					m_pLock->Release();
				}
				else if ( m_bManualReset )
				{
					m_pLock->Release();
				}
			}
			
			this->SetLastError(kSyncSuccess);
			return bSuccess;
		}
		
		virtual void SetEvent()
		{
			this->SetLastError( m_pLock->Release() ? kSyncSuccess : kSyncFail );
		}
		
		virtual void ResetEvent()
		{
			this->SetLastError( m_pLock->Lock(0) ? kSyncSuccess : kSyncFail );
		}
		
		virtual void Destroy()
		{
			if ( m_pLock )
				delete m_pLock;
			
			if ( m_pPending != SEM_FAILED )
				sem_close( m_pPending );
			
			m_pLock = NULL;
			m_pPending = SEM_FAILED;
			*m_szLockName = '\0';
			*m_szPendingName = '\0';
		}
		
		virtual ~PosixEvent()
		{
			Destroy();
		}
		
		bool Init( const char *pszName, bool bManualReset, bool bInitiallySet, bool *pbCreator )
		{
			if ( pbCreator )
				*pbCreator = false;
			
			m_bManualReset = bManualReset;
			
			unsigned int nSemHash = crc32(0, (unsigned char *)pszName, strlen(pszName) );
			snprintf( m_szPendingName, Q_ARRAYSIZE(m_szPendingName), "/Evt/%x", nSemHash );
			snprintf( m_szLockName, Q_ARRAYSIZE(m_szLockName), "%s.BinSemLock", pszName );
			
			bool bCreator;
			m_pLock = CreateBinarySemaphore( m_szLockName, !bInitiallySet, &bCreator );
			if ( pbCreator )
				*pbCreator = bCreator;
			
			if ( bCreator )
			{
				m_pPending = sem_open( m_szPendingName, O_CREAT|O_EXCL, ACCESS_ALL, 0 );
			}
			else if ( m_pLock )
			{
				// Need to loop - someone else opened with lock, need to wait for
				// them to create the 2nd semaphore.
				for ( int i = 0; i < 200; i++ )
				{
					m_pPending = sem_open(m_szPendingName, 0 );
					if ( m_pPending != SEM_FAILED )
						break;
					usleep( 1000 * 10 );
				}
			}
			
			// clean up if something failed
			if ( ( m_pLock == NULL ) || ( m_pPending == SEM_FAILED ) )
			{
				Log( "Failed to create PosixAutoResetEvent: %s - %s, m_pLock:%p, m_pPending:%p\n", pszName, m_szPendingName, m_pLock, m_pPending );
				Log( "\terrno: %d, bCreator: %s\n", errno, bCreator ? "true" : "false" );
				Destroy();
				return false;
			}

			return true;
		}
		
	private:
		BinarySemaphore *m_pLock;
		sem_t *m_pPending;
		
		bool m_bManualReset;
		
		char m_szLockName[NAME_MAX];
		char m_szPendingName[NAME_SHORT_LEN];
	};

    // Look at Win32 CreateMutex for reference.  If return is non-null and *pbCreator is
    // false then an existing mutex was connected to - in which case ownership is not granted
    // even if it not currently locked.
    IMutex *CreateMutex( const char *pszName, bool bInitialOwner, bool *pbCreator )
    {
        CREATE_OBJECT( PosixMutex, pszName, bInitialOwner, pbCreator );
    }
	
    // See Win32 CreateEvent.  If return is non-null and *pbCreator is false, then the bInitiallySet
    // parameter is ignored.  If bInitially set is true, the event is signalled for a Wait().
    IEvent *CreateEvent( const char *pszName, bool bManualReset, bool bInitiallySet, bool *pbCreator )
    {
        CREATE_OBJECT( PosixEvent, pszName, bManualReset, bInitiallySet, pbCreator );
    }

#endif // OSX

#if defined(LINUX)

    // NOTE:
    // We can have a mix of 32-bit and 64-bit processes using our IPC
    // primitives, such as a 64-bit gameoverlayrenderer communicating with
    // a 32-bit gameoverlayui.  Make sure that all data in the shared
    // memory is bitness-neutral.
    class SharedObjectManager
    {
        struct Header
        {
            volatile uint32_t nVersion;
            uint32_t nEntrySize;
            uint32_t nTotalSize;
            volatile int32_t MgrMutexOwner;
            uint32_t Unused[2];
            LfMutex SysMgrMutex;
        };
        enum EntryType
        {
            ENT_UNUSED, // Must be zero.
            ENT_MUTEX,
            ENT_EVENT,
            ENT_SHARED_MEMORY,
        };
        
        static const uint32_t s_nMemSize = 65536;
        static const uint32_t s_nMaxEntrySize = 256;
        static const int32_t s_nVersion = 3;

    public:
        struct Entry
        {
            uint32_t nType;
            uint32_t nNameCrc;
            uint32_t nRefs;

			// An array of all the processes that reference this entry.
			// Zero items indicate no reference. The number of non-zero items
			// in this array should always be equal to nRefs.
            int32_t nReferencingPids[16];

			bool AddReference()
			{
				for ( int i = 0; i != Q_ARRAYSIZE( nReferencingPids ); ++i )
				{
					if ( nReferencingPids[i] == 0 )
					{
						nReferencingPids[i] = getpid();
						nRefs++;
						return true;
					}
				}

				return false;
			}

			bool RemoveReference( pid_t pid )
			{
				for ( int i = 0; i != Q_ARRAYSIZE( nReferencingPids ); ++i )
				{
					if ( nReferencingPids[i] == pid )
					{
						nReferencingPids[i] = 0;
						--nRefs;
						return nRefs <= 0;
					}
				}

				return false;
			}
        };
        struct MutexEntry : public Entry
        {
            LfMutex SysMutex;
        };
        struct EventEntry : public MutexEntry
        {
            LfCondVar SysCondVar;
            bool bSet;
            bool bManualReset;
        };
        struct SharedMemoryEntry : public Entry
        {
        };

        SharedObjectManager()
        {
            // We cannot rely on constructor order amongst globals
            // so this constructor should never do anything.
            // Instead we have the m_bGlobalInitialized flag that we
            // assume is set properly due to zero-init of global
            // data.  Therefore SharedObjectManager can only be
            // a global object.
        }
        ~SharedObjectManager()
        {
            Destroy();
        }

		const char *GetManagerSharedMemoryName()
        {
			static char *s_pszMemName;

			if ( !s_pszMemName )
            {
				char szMemName[MAX_PATH];
				size_t nStrSize;
				snprintf( szMemName, sizeof(szMemName), "/%s-ValveIPCSharedObjects5", getenv( "USER" ) );
				nStrSize = strlen( szMemName ) + 1;
				s_pszMemName = new char[nStrSize];
				memcpy( s_pszMemName, szMemName, nStrSize );
			}
			return s_pszMemName;
		}

        bool Init()
        {
            if ( !m_bGlobalInitialized )
            {
                m_fd = -1;
                m_pData = MAP_FAILED;
                m_bGlobalInitialized = true;
            }
            
            if ( m_fd >= 0 )
            {
                // Already initialized.
                return true;
            }
            
            if ( sizeof(MutexEntry) > s_nMaxEntrySize ||
                 sizeof(EventEntry) > s_nMaxEntrySize ||
                 sizeof(SharedMemoryEntry) > s_nMaxEntrySize )
            {
                Log( "Illegal shared object size\n" );
                return false;
            }

			m_fd = shm_open( GetManagerSharedMemoryName(), O_RDWR | O_CREAT | O_EXCL, ACCESS_ALL );

            bool bCreator;

            if ( m_fd >= 0 )
            {
                bCreator = true;

                Log( "Process %d created %s\n", getpid(), GetManagerSharedMemoryName() );
                
                // Take an exclusive lock on the file to mark that we are still initializing.
                // Locks are automatically cleaned up on process-exit so this serves as
                // a reliable marker that we are alive and in the process of initializing.
                // Set the memory section size.
                if ( flock( m_fd, LOCK_EX | LOCK_NB ) != 0 ||
                     ftruncate( m_fd, s_nMemSize ) != 0 )
                {
                    close( m_fd );
                    m_fd = -1;
                    return false;
                }
            }
            else if ( errno == EEXIST )
			{
                // Our creation attempt failed, so try to open an existing memory section.
                bCreator = false;
				m_fd = shm_open( GetManagerSharedMemoryName(), O_RDWR, 0 );
            }
            if ( m_fd < 0 )
            {
                return false;
            }

            m_pData = mmap( NULL, s_nMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0 );
            if ( m_pData == MAP_FAILED )
            {
                Destroy();
                return false;
            }

            bool bSuccess;

            if ( bCreator )
            {
                bSuccess = CreateNew();
            }
            else
            {
                bSuccess = OpenExisting();
            }
            if ( !bSuccess )
            {
                Destroy();
            }

            return bSuccess;
        }
        
        bool Destroy()
        {
			bool bSuccess = true;

			if ( m_bGlobalInitialized && m_pData != MAP_FAILED )
			{
				if ( munmap( m_pData, s_nMemSize ) != 0 )
                {
					bSuccess = false;
                }
			}
			
			if ( m_bGlobalInitialized && m_fd >= 0 )
			{
				if ( close( m_fd ) != 0 )
                {
					bSuccess = false;
                }
			}
			
			m_fd = -1;
			m_pData = MAP_FAILED;
            m_bGlobalInitialized = true;

            return bSuccess;
        }

        int Purge( pid_t MatchPid )
        {
            if ( !LockMgrMutex() )
            {
                return -1;
            }

            int nPurged = 0;
            
            Entry* pEnt = GetFirstEntry();
            Entry* pEndEnt = GetEntryEnd();

            while ( pEnt < pEndEnt )
            {
                if ( pEnt->nType != ENT_UNUSED )
                {
					// Search through all processes that reference this
					// entry. If any of them are dead, then remove their
					// reference to the entry.
					for ( int i = 0; i != Q_ARRAYSIZE(pEnt->nReferencingPids); ++i )
					{
						const int32_t pid = pEnt->nReferencingPids[i];
						if ( ( MatchPid == 0 || MatchPid == pid ) &&
						     pid != 0 && kill( pid, 0 ) != 0 )
						{
							if ( DerefEntry( pEnt, pid ) )
							{
								// The dereference caused the entry to be
								// destroyed. Record a purge and move on to
								// the next entry.
								nPurged++;
								break;
							}
						}
					}

                }

                pEnt = GetNextEntry( pEnt );
            }

            UnlockMgrMutex();
            return nPurged;
        }
        
        bool LockMgrMutex()
        {
            bool bAbandoned;
            
            // Wait with a timeout just in case something goes haywire.
            // It doesn't need to be long as manager operations should always be quick.
            if ( LockSysMutex( &GetHeader()->SysMgrMutex, 1000, &bAbandoned ) )
            {
                // If we're not using robust mutexes we can try and see
                // if the manager mutex owner is still alive and, if not,
                // reset the mutex.
                // If we are using robust mutexes a failure here means
                // something unknown and unrecoverable.
#if TRUST_ROBUST_MUTEX
                return false;
#else
                if ( !RecoverMgrMutex() )
                {
                    return false;
                }
#endif
            }

            GetHeader()->MgrMutexOwner = getpid();
            
            // If the manager mutex was abandoned we could have shared objects
            // in random states.  We're careful to only mark things as in-use
            // when they're ready to go, so most of the time a failure during
            // allocation is harmless (other than potentially leaking something).
            // Deallocation is harder as we could deinitialize and not zero,
            // but that's a small window.  If we want to be really conservative
            // we could mark the memory section as corrupt and fail all future
            // LockMgrMutex calls, but we're not that paranoid right now.
            return true;
        }
        void UnlockMgrMutex()
        {
            GetHeader()->MgrMutexOwner = 0;
            LfMutexUnlock( &GetHeader()->SysMgrMutex );
        }
        bool RecoverMgrMutex()
        {
            //
            // If there's a manager mutex owner and the owner process is dead
            // we try and reinitialize the mutex and acquire it.
            // This is only used when we don't have robust mutexes and
            // gives us some protection against abandonment, but
            // it isn't perfect.
            //
            
            // Hold a lock to indicate that we are trying to recover.
            if ( flock( m_fd, LOCK_EX | LOCK_NB ) != 0 )
            {
                int nRetries = 50;
                
                // Somebody else must have gotten in and now owns recovery.
                // Wait for them.
                for (;;)
                {
                    if ( nRetries-- <= 0 )
                    {
                        // Something is badly wrong and we can't recover.
                        return false;
                    }
                    
                    usleep( 10000 );
                    
                    if ( flock( m_fd, LOCK_EX | LOCK_NB ) == 0 )
                    {
                        break;
                    }
                }
            }

            // We've acquired the file lock so we can try to recover
            // (if somebody else hasn't already).
            int32_t Owner = GetHeader()->MgrMutexOwner;
            if ( Owner != 0 &&
                 kill( Owner, 0 ) != 0 )
            {
                Log("Recovering manager mutex\n");
                GetHeader()->MgrMutexOwner = 0;
                LfMutexInit( &GetHeader()->SysMgrMutex );
            }

            flock( m_fd, LOCK_UN );

            bool bAbandoned;
                
            // There was either a successful reset or
            // the lock is failing for another reason.  Try one
            // last acquire to see which one it is.
            return LockSysMutex( &GetHeader()->SysMgrMutex, 1000, &bAbandoned ) == 0;
        }      
        
        MutexEntry* AllocMutex( uint32_t nNameCrc, bool bInitialOwner,
                                bool* pbCreated )
        {
			Purge( 0 );
            if ( !LockMgrMutex() )
            {
                return NULL;
            }

            MutexEntry* pEnt = (MutexEntry*)FindEntry( ENT_MUTEX, nNameCrc );
            if ( pEnt )
            {
				pEnt->AddReference();
                *pbCreated = false;
            }
            else
            {
                pEnt = (MutexEntry*)AllocEntry( nNameCrc );
                if ( pEnt )
                {
                    LfMutexInit( &pEnt->SysMutex );

                    if ( bInitialOwner &&
                         LfMutexLock( &pEnt->SysMutex, NULL ) != 0 )
                    {
                        LfMutexDestroy( &pEnt->SysMutex );
                        FreeEntry( pEnt );
                        pEnt = NULL;
                    }
                    else
                    {
                        // Always set the type last so that the entry
                        // isn't in use until it's fully valid.
                        pEnt->nType = ENT_MUTEX;
                        *pbCreated = true;
                    }
                }
            }

            UnlockMgrMutex();

            return pEnt;
        }
        bool FreeMutex( MutexEntry* pEnt )
        {
            return LockAndDerefEntry( pEnt );
        }

        EventEntry* AllocEvent( uint32_t nNameCrc, bool bManualReset, bool bInitiallySet,
                                bool* pbCreated )
        {
			Purge( 0 );
            if ( !LockMgrMutex() )
            {
                return NULL;
            }

            EventEntry* pEnt = (EventEntry*)FindEntry( ENT_EVENT, nNameCrc );
            if ( pEnt )
            {
				pEnt->AddReference();
                *pbCreated = false;
            }
            else
            {
                pEnt = (EventEntry*)AllocEntry( nNameCrc );
                if ( pEnt )
                {
                    LfMutexInit( &pEnt->SysMutex );
                    LfCondVarInit( &pEnt->SysCondVar );
                    
                    pEnt->bSet = bInitiallySet;
                    pEnt->bManualReset = bManualReset;
                    // Always set the type last so that the entry
                    // isn't in use until it's fully valid.
                    pEnt->nType = ENT_EVENT;
                    *pbCreated = true;
                }
            }

            UnlockMgrMutex();

            return pEnt;
        }
        bool FreeEvent( EventEntry* pEnt )
        {
            return LockAndDerefEntry( pEnt );
        }

        SharedMemoryEntry* AllocSharedMemory( uint32_t nNameCrc,
                                              bool* pbCreated )
        {
			Purge( 0 );
            if ( !LockMgrMutex() )
            {
                return NULL;
            }

            SharedMemoryEntry* pEnt = (SharedMemoryEntry*)FindEntry( ENT_SHARED_MEMORY, nNameCrc );
            if ( pEnt )
            {
				pEnt->AddReference();
                *pbCreated = false;
            }
            else
            {
                pEnt = (SharedMemoryEntry*)AllocEntry( nNameCrc );
                if ( pEnt )
                {
                    // Always set the type last so that the entry
                    // isn't in use until it's fully valid.
                    pEnt->nType = ENT_SHARED_MEMORY;
                    *pbCreated = true;
                }
            }

            UnlockMgrMutex();

            return pEnt;
        }
        bool FreeSharedMemory( SharedMemoryEntry* pEnt )
        {
            return LockAndDerefEntry( pEnt );
        }

        static void GetAbsoluteTimeout( uint32_t msWaitTime, struct timespec* Timeout )
        {
            clock_gettime( CLOCK_REALTIME, Timeout );
            Timeout->tv_sec += msWaitTime / 1000;
            Timeout->tv_nsec += (msWaitTime % 1000) * 1000000;
            if ( Timeout->tv_nsec >= 1000000000 )
            {
                Timeout->tv_sec++;
                Timeout->tv_nsec -= 1000000000;
            }
        }

        static void GetRelativeTimeout( uint32_t msWaitTime, struct timespec* Timeout )
        {
            Timeout->tv_sec = msWaitTime / 1000;
            Timeout->tv_nsec = ( msWaitTime % 1000 ) * 1000000;
        }

        static int LockSysMutex( LfMutex* pSysMutex, uint32_t msWaitTime, bool* pbAbandoned )
        {
            int err;

            *pbAbandoned = false;
            if ( msWaitTime == UINT32_MAX )
            {
                err = LfMutexLock( pSysMutex, NULL );
            }
            else
            {
                struct timespec Timeout;

                GetRelativeTimeout( msWaitTime, &Timeout );
                err = LfMutexLock( pSysMutex, &Timeout );
            }
            if ( err == EOWNERDEAD )
            {
                Log( "Recovering abandoned mutex %p\n", pSysMutex );
                // The owner of the mutex died.  We don't do any special
                // revalidation, we just continue on.
                LfMutexConsistent( pSysMutex );
                *pbAbandoned = true;
                err = 0;
            }

            return err;
        }

    protected:
        Header* GetHeader()
        {
            return (Header*)m_pData;
        }
        Entry* GetFirstEntry()
        {
            return (Entry*)((Header*)m_pData + 1);
        }
        Entry* GetEntryEnd()
        {
            return (Entry*)((uint8_t*)m_pData + GetHeader()->nTotalSize);
        }
        Entry* GetNextEntry( Entry* pEnt )
        {
            return (Entry*)((uint8_t*)pEnt + GetHeader()->nEntrySize);
        }
        
        bool CreateNew()
        {
            Header* pHeader = GetHeader();

            // The memory section is implicitly zero-filled so
            // we only need to set new portions of the header.
            LfMutexInit( &pHeader->SysMgrMutex );

            pHeader->nEntrySize = s_nMaxEntrySize;
            pHeader->nTotalSize = s_nMemSize;
            
            // Set the version last as once it is set the
            // section is considered fully initialized.
            pHeader->nVersion = s_nVersion;

            // Make our change visible to all.
            _ReadWriteBarrier();

            // Now that everything is initialized we can release our lock.
            flock( m_fd, LOCK_UN );
            
            // The memory section is implicitly zero-filled so
            // we can assume all entries are in the free state.

            return true;
        }

        bool OpenExisting()
        {
            // We've opened the memory section but the creator
            // may not have finished initializing it.  That shouldn't
            // take very long so we just do a spin-wait.
            // It's possible, but unlikely, that the creator could
            // die before finishing initialization, so if while
            // we're waiting it looks like the creator died we
            // take over initialization.
            
            Header* pHeader = GetHeader();
            uint nRetries = 50;

            while ( nRetries-- > 0 )
            {
                if ( pHeader->nVersion != 0 )
                {
                    break;
                }

                // Can we take over initialization?
                if ( flock( m_fd, LOCK_EX | LOCK_NB ) == 0 )
                {
                    // Did somebody initialize before we got the lock?
                    if ( pHeader->nVersion != 0 )
                    {
                        flock( m_fd, LOCK_UN );
                        break;
                    }

                    Log( "Process %d taking over initialization of %s\n",
                         getpid(), GetManagerSharedMemoryName() );
                    // Make sure we start with fresh header content.
                    memset( pHeader, 0, sizeof(*pHeader) );
                    return CreateNew();
                }

                usleep( 10000 );
            }
            if ( pHeader->nVersion == 0 )
            {
                // The memory never was initialized and we couldn't take over
                // initialization, so we're giving up.
                Log( "Process %d initialization timeout\n", getpid() );
                return false;
            }

            // Make sure we start without stale content.
            Purge( 0 );
            
            return true;
        }

        Entry* AllocEntry( uint32_t nNameCrc )
        {
            // Must be holding the manager mutex.
            Entry* pEnt = GetFirstEntry();
            Entry* pEndEnt = GetEntryEnd();

            while ( pEnt < pEndEnt )
            {
                if ( pEnt->nType == ENT_UNUSED )
                {
                    pEnt->nNameCrc = nNameCrc;
                    pEnt->nRefs = 0;
					pEnt->AddReference();
                    // We deliberately do not set the type as
                    // we only want to set that when the entry
                    // is fully initialized.
                    return pEnt;
                }
                
                pEnt = GetNextEntry( pEnt );
            }

            return NULL;
        }
        void FreeEntry( Entry* pEnt )
        {
            // Must be holding the manager mutex.
            memset( pEnt, 0, sizeof(*pEnt) );
        }
        bool DerefEntry( Entry* pEnt )
		{
			return DerefEntry( pEnt, getpid() );
		}

        bool DerefEntry( Entry* pEnt, pid_t pid )
		{
            // Must be holding the manager mutex.
			if ( pEnt->RemoveReference( pid ) )
			{
				DestroyEntry( pEnt );
				return true;
			}

			return false;
		}

        bool DestroyEntry( Entry* pEnt )
        {
            bool bSuccess = true;
            
            switch( pEnt->nType )
            {
            case ENT_MUTEX:
                LfMutexDestroy( &((MutexEntry*)pEnt)->SysMutex );
                break;
            case ENT_EVENT:
                LfMutexDestroy( &((EventEntry*)pEnt)->SysMutex );
                LfCondVarDestroy( &((EventEntry*)pEnt)->SysCondVar );
                break;
            case ENT_SHARED_MEMORY:
                char szMemName[SHARED_MEMORY_NAME_LEN];

                SetSharedMemoryName( szMemName, Q_ARRAYSIZE( szMemName ), pEnt->nNameCrc, 0 ); // size isn't used on Linux
                shm_unlink( szMemName );
                break;
            }

            FreeEntry( pEnt );
			return bSuccess;
        }

        bool LockAndDerefEntry( Entry* pEnt )
        {
            if ( !LockMgrMutex() )
            {
                return false;
            }

            bool bSuccess = DerefEntry( pEnt );

            UnlockMgrMutex();
            return bSuccess;
        }
        bool DerefEntryByPointer( void* pFirstSpecific )
        {
            if ( !LockMgrMutex() )
            {
                return false;
            }

            Entry* pEntry = FindEntry( pFirstSpecific );
            if ( pEntry )
            {
                DerefEntry( pEntry );
            }

            UnlockMgrMutex();
            return pEntry != NULL;
        }

        Entry* FindEntry( EntryType Type, uint32_t nNameCrc )
        {
            // Must be holding the manager mutex.
            Entry* pEnt = GetFirstEntry();
            Entry* pEndEnt = GetEntryEnd();

            while ( pEnt < pEndEnt )
            {
                if ( pEnt->nType == Type &&
                     pEnt->nNameCrc == nNameCrc )
                {
                    return pEnt;
                }
                
                pEnt = GetNextEntry( pEnt );
            }

            return NULL;
        }
        Entry* FindEntry( void* pFirstSpecific )
        {
            // Must be holding the manager mutex.
            Entry* pEnt = GetFirstEntry();
            Entry* pEndEnt = GetEntryEnd();

            while ( pEnt < pEndEnt )
            {
                if ( pFirstSpecific == pEnt + 1 )
                {
                    return pEnt;
                }
                
                pEnt = GetNextEntry( pEnt );
            }

            return NULL;
        }

        // We cannot rely on constructor order so we
        // must be able to detect initialization just
        // from the global variable zero-init, thus
        // we keep an explicit init bool.
        bool m_bGlobalInitialized;
		int m_fd;
		void *m_pData;
    };

    // We must declare a global instance and mark it for
    // early construction so that we get late destruction
    // as we want to clean up after any global shared objects
    // are destructed.
    // We still hide the access to the global instance
    // with the SharedObjMgr() method.
    SharedObjectManager g_SharedObjectManager CONSTRUCT_EARLY;
    SharedObjectManager &SharedObjMgr()
	{
		return g_SharedObjectManager;
	}
    
	class SharedObjectMutex : public IMutex
	{
	public:
		SharedObjectMutex()
		{
            m_pSharedMutex = NULL;
            m_lockHolder = 0;
            m_nRecurse = 0;
		}
		
		virtual bool Wait(uint32_t msWaitTime /*= UINT32_MAX */)
		{
            this->SetLastError( kSyncSuccess );

			// Handle re-entrant case
			if ( m_lockHolder == pthread_self() )
			{
                CheckIsSysMutexOwner( &m_pSharedMutex->SysMutex );
                m_nRecurse++;
				return true;
			}

            CheckNotSysMutexOwner( &m_pSharedMutex->SysMutex );

            bool bAbandoned;
            int err = SharedObjectManager::LockSysMutex( &m_pSharedMutex->SysMutex, msWaitTime, &bAbandoned );
            if ( err == ETIMEDOUT )
            {
                // We don't set last error in this case, we just return false.
            }
            else if ( err )
            {
                this->SetLastError( kSyncFail );
            }
            else
            {
                CheckIsSysMutexOwner( &m_pSharedMutex->SysMutex );
                m_lockHolder = pthread_self();

                if ( bAbandoned )
                {
                    // We treat abandonment as failure and we hold the mutex.
                    // This is a little odd but that's what the Win32 implementation has
                    // historically done.
                    this->SetLastError( kSyncFail );
                    err = EOWNERDEAD;
                }
            }
			
			return err == 0;
		}
		
		virtual void Release() 
		{
			// Verify we hold it.
			if ( m_lockHolder != pthread_self() )
			{
				this->SetLastError(kSyncFail);
				Log( "Error on Release(): Not the lock owner\n" );
				return;
			}
			
            CheckIsSysMutexOwner( &m_pSharedMutex->SysMutex );
            
			this->SetLastError(kSyncSuccess);

            if ( m_nRecurse > 0 )
            {
                m_nRecurse--;
                return;
            }

            m_lockHolder = 0;
            LfMutexUnlock( &m_pSharedMutex->SysMutex );
		}
		
		virtual void Destroy()
		{
            if ( m_pSharedMutex )
            {
                CheckNotSysMutexOwner( &m_pSharedMutex->SysMutex );
                SharedObjMgr().FreeMutex( m_pSharedMutex );
                m_pSharedMutex = NULL;
            }

            if ( m_lockHolder != 0 )
            {
                Log( "Destroying a held mutex\n" );
            }
		}
		
		virtual ~SharedObjectMutex()
		{
			Destroy();
		}
		
		bool Init( const char *pszName, bool bInitialOwner, bool *pbCreator )
		{
            if ( !SharedObjMgr().Init() )
            {
                return false;
            }

			uint32_t nSemHash = crc32(0, (unsigned char *)pszName, strlen(pszName) );
            bool bCreated = false;

            m_pSharedMutex = SharedObjMgr().AllocMutex( nSemHash, bInitialOwner,
                                                        &bCreated );

			if ( pbCreator )
            {
                if ( m_pSharedMutex &&
                     bInitialOwner &&
                     bCreated )
                {
                    CheckIsSysMutexOwner( &m_pSharedMutex->SysMutex );
                    m_lockHolder = pthread_self();
                }
				*pbCreator = bCreated;
            }
			
			return m_pSharedMutex != NULL;
		}
		
	private:
        SharedObjectManager::MutexEntry* m_pSharedMutex;
		pthread_t m_lockHolder;
        uint32_t m_nRecurse;
	};

	class SharedObjectEvent : public IEvent
	{
	public:
		SharedObjectEvent()
		{
            m_pSharedEvent = NULL;
		}
		
		virtual bool Wait(uint32_t msWaitTime /*= UINT32_MAX */)
		{
            bool bAbandoned;

			this->SetLastError( kSyncSuccess );

            int err = SharedObjectManager::LockSysMutex( &m_pSharedEvent->SysMutex, msWaitTime, &bAbandoned );
            // Fall into loop to check err.
            
            for (;;)
            {
                if ( err )
                {
                    if ( err != ETIMEDOUT )
                    {
                        this->SetLastError( kSyncFail );
                    }

                    // Make sure we don't leave holding the mutex.
                    CheckNotSysMutexOwner( &m_pSharedEvent->SysMutex );
                    return false;
                }

                CheckIsSysMutexOwner( &m_pSharedEvent->SysMutex );

                if ( m_pSharedEvent->bSet )
                {
                    if ( !m_pSharedEvent->bManualReset )
                    {
                        m_pSharedEvent->bSet = false;
                    }

                    LfMutexUnlock( &m_pSharedEvent->SysMutex );
                    return true;
                }

                if ( msWaitTime == 0 )
                {
                    LfMutexUnlock( &m_pSharedEvent->SysMutex );
                    return false;
                }
                else if ( msWaitTime == UINT32_MAX )
                {
                    err = LfCondVarWait( &m_pSharedEvent->SysCondVar, &m_pSharedEvent->SysMutex, NULL );
                }
                else
                {
                    struct timespec Timeout;

                    SharedObjectManager::GetRelativeTimeout( msWaitTime, &Timeout );
                    err = LfCondVarWait( &m_pSharedEvent->SysCondVar, &m_pSharedEvent->SysMutex, &Timeout );
                }
                if ( err == EOWNERDEAD )
                {
                    LfMutexConsistent( &m_pSharedEvent->SysMutex );
                    err = 0;
                }
                else if ( err )
                {
                    // We still have ownership of the mutex in this case
                    // so give it up before we exit on the next loop iteration.
                    LfMutexUnlock( &m_pSharedEvent->SysMutex );
                }
            }
		}
		
		virtual void SetEvent()
		{
            int err;
            bool bAbandoned;

            err = SharedObjectManager::LockSysMutex( &m_pSharedEvent->SysMutex, UINT32_MAX, &bAbandoned );
            if ( !err )
            {
                m_pSharedEvent->bSet = true;
                if ( m_pSharedEvent->bManualReset )
                {
                    err = LfCondVarBroadcast( &m_pSharedEvent->SysCondVar, &m_pSharedEvent->SysMutex );
                }
                else
                {
                    err = LfCondVarSignal( &m_pSharedEvent->SysCondVar );
                }
                LfMutexUnlock( &m_pSharedEvent->SysMutex );
            }
			this->SetLastError( err == 0 ? kSyncSuccess : kSyncFail );
		}
		
		virtual void ResetEvent()
		{
            int err;
            bool bAbandoned;

            err = SharedObjectManager::LockSysMutex( &m_pSharedEvent->SysMutex, UINT32_MAX, &bAbandoned );
            if ( !err )
            {
                m_pSharedEvent->bSet = false;
                LfMutexUnlock( &m_pSharedEvent->SysMutex );
            }
			this->SetLastError( err == 0 ? kSyncSuccess : kSyncFail );
		}
		
		virtual void Destroy()
		{
            if ( m_pSharedEvent )
            {
                CheckNotSysMutexOwner( &m_pSharedEvent->SysMutex );
                SharedObjMgr().FreeEvent( m_pSharedEvent );
                m_pSharedEvent = NULL;
            }
		}
		
		virtual ~SharedObjectEvent()
		{
			Destroy();
		}
		
		bool Init( const char *pszName, bool bManualReset, bool bInitiallySet, bool *pbCreator )
		{
            if ( !SharedObjMgr().Init() )
            {
                return false;
            }

			uint32_t nSemHash = crc32(0, (unsigned char *)pszName, strlen(pszName) );
            bool bCreated = false;

            m_pSharedEvent = SharedObjMgr().AllocEvent( nSemHash, bManualReset, bInitiallySet,
                                                        &bCreated );

			if ( pbCreator )
            {
				*pbCreator = bCreated;
            }
			
			return m_pSharedEvent != NULL;
		}
		
	private:
        SharedObjectManager::EventEntry* m_pSharedEvent;
	};

    // Look at Win32 CreateMutex for reference.  If return is non-null and *pbCreator is
    // false then an existing mutex was connected to - in which case ownership is not granted
    // even if it not currently locked.
    IMutex *CreateMutex( const char *pszName, bool bInitialOwner, bool *pbCreator )
    {
        CREATE_OBJECT( SharedObjectMutex, pszName, bInitialOwner, pbCreator );
    }
	
    // See Win32 CreateEvent.  If return is non-null and *pbCreator is false, then the bInitiallySet
    // parameter is ignored.  If bInitially set is true, the event is signalled for a Wait().
    IEvent *CreateEvent( const char *pszName, bool bManualReset, bool bInitiallySet, bool *pbCreator )
    {
        CREATE_OBJECT( SharedObjectEvent, pszName, bManualReset, bInitiallySet, pbCreator );
    }

#endif // LINUX
	
	class PosixSharedMemory : public ISharedMem
	{
	public:
		PosixSharedMemory()
		{
			m_fd = -1;
			m_pData = MAP_FAILED;
			m_nDataSize = 0;
			*m_szMemName = '\0';
#if defined(LINUX)
            m_pSharedEntry = NULL;
#endif
		}

        virtual bool IsValid() const
        {
            return m_pData != MAP_FAILED;
        }
		virtual void *Pointer() const
		{
			return m_pData;
		}

		virtual bool Destroy()
		{
			bool bSuccess = true;

#if defined(LINUX)
            if ( m_pSharedEntry )
            {
                SharedObjMgr().FreeSharedMemory( m_pSharedEntry );
                m_pSharedEntry = NULL;
            }
#endif
			
			if ( m_pData != MAP_FAILED )
			{
				if ( munmap( m_pData, m_nDataSize ) != 0 )
					bSuccess = false;
			}
			
			if ( m_fd >= 0 )
			{
				if ( close( m_fd ) != 0 )
					bSuccess = false;
			}

			
			m_fd = -1;
			m_pData = MAP_FAILED;
			m_nDataSize = 0;
			*m_szMemName = '\0';
			
			return bSuccess;
		}

		virtual ~PosixSharedMemory()
		{
			Destroy();
		}

		bool Init( const char *pszName, uint32_t nSize, Access access )
		{
			int oflag = 0;
			int prot = 0;
			
			switch ( access ) {
				case Read:
					oflag = O_RDONLY;
					prot = PROT_READ;
					break;
				case ReadWrite:
					oflag = O_RDWR;
					prot = PROT_READ | PROT_WRITE;
					break;
				default:
					Log( "Bad Access flag on shared mem create: %s: %u\n", pszName, access );
					return false;
					break;
			}
			
			unsigned int nSemHash = crc32(0, (unsigned char *)pszName, strlen(pszName) );
            SetSharedMemoryName( m_szMemName, Q_ARRAYSIZE( m_szMemName ), nSemHash, nSize );

#if defined(LINUX)
            if ( !SharedObjMgr().Init() )
            {
                return false;
            }
            bool bCreated;
            m_pSharedEntry = SharedObjMgr().AllocSharedMemory( nSemHash, &bCreated );
            if ( !m_pSharedEntry )
            {
                return false;
            }
#endif

			bool bSuccess = false;

			m_fd = shm_open( m_szMemName, oflag | (O_CREAT | O_EXCL), ACCESS_ALL );
			if ( m_fd >= 0 )
			{
				bSuccess = ( ftruncate( m_fd, nSize ) == 0 );
			}
			else if ( errno == EEXIST )
			{
				m_fd = shm_open( m_szMemName, oflag, ACCESS_ALL );
                bSuccess = m_fd >= 0;
			}
			
			if ( bSuccess )
			{
                m_nDataSize = nSize;
                m_pData = mmap( NULL, nSize, prot, MAP_SHARED, m_fd, 0 );
                bSuccess = ( m_pData != MAP_FAILED );
			}
			
			if (!bSuccess)
				Destroy();
			
			return bSuccess;
		}
		
	private:
		int		m_fd;
		uint32_t	m_nDataSize;
		void	*m_pData;
		char	m_szMemName[SHARED_MEMORY_NAME_LEN];
#if defined(LINUX)
        SharedObjectManager::SharedMemoryEntry* m_pSharedEntry;
#endif
	};

    ISharedMem *CreateSharedMem( const char *pszName, uint32_t nSize, ISharedMem::Access access )
    {
        CREATE_OBJECT( PosixSharedMemory, pszName, nSize, access );
    }
	
	// Pass a time to wait (UINT32_MAX for block) followed by a count of objects and then the
	// list of objects to wait on.  Return value is 0 for timeout, 1 for the first
	// item succeeded waiting: 2 for the second, etc...  Return is < 0 on fatal error
	// Only waits for 1 item to signal, no guarantee about starvation on repeated use.
	int WaitMultiple( uint32_t msWaitTime, uint32_t nCount, ISyncObject *pFirst, ... )
	{
		ISyncObject *handles[8];
		uint32_t nHandleCount = 0;
		
		if ( pFirst == NULL || nCount >= 8 )
			return -1;
		
		va_list ap;
		va_start(ap, pFirst);
		ISyncObject *pNext = pFirst;
		for ( int i = 0 ; i < nCount; i++, pNext = va_arg(ap, ISyncObject *) )
		{
			if ( pNext->Wait(0) )
				return i+1;
			
			handles[nHandleCount++] = pNext;
		}
		va_end(ap);
		
		if ( msWaitTime )
		{
			unsigned int nTimeStart = SDL_GetTicks();					
			while ( (SDL_GetTicks() - nTimeStart) < msWaitTime )
			{
				if ( handles[0]->Wait( Clamp( msWaitTime, 1u, 10u ) ) )
					return 1;

				for ( int i = 1 ; i < nCount; i++ )
				{
					if ( handles[i]->Wait( 0 ) )
						return i+1;
				}				
			}
		}
		
		return 0;		
	}
	
    // On platforms that don't support automatic cleanup of shared objects
    // it may be necessary to explicitly request the IPC layer go clean
    // up shared objects that it created.
    // This can also come up with Posix where a game may exec itself into
    // an existing process and previous shared objects should be cleaned up
    // prior to that so that it doesn't look like the new execution,
    // which still has the same pid, has created those objects.
#if defined(OSX)
    void PurgeAllDead()
    {
    }
    void PurgeCurrentProcess()
    {
    }
#elif defined(LINUX)
    void PurgeAllDead()
    {
        if ( SharedObjMgr().Init() )
        {
            SharedObjMgr().Purge( 0 );
        }
    }
    void PurgeCurrentProcess()
    {
        if ( SharedObjMgr().Init() )
        {
            SharedObjMgr().Purge( getpid() );
        }
    }
#endif
	
	/* zlib.h -- interface of the 'zlib' general purpose compression library
	 version 1.1.2, March 19th, 1998
	 
	 Copyright (c) 1995-2001 by Apple Computer, Inc., All Rights Reserved.
	 
	 This software is provided 'as-is', without any express or implied
	 warranty.  In no event will the authors be held liable for any damages
	 arising from the use of this software.
	 
	 Permission is granted to anyone to use this software for any purpose,
	 including commercial applications, and to alter it and redistribute it
	 freely, subject to the following restrictions:
	 
	 1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
	 2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
	 3. This notice may not be removed or altered from any source distribution.
	 
	 Jean-loup Gailly        Mark Adler
	 jloup@gzip.org          madler@alumni.caltech.edu
	 
	 
	 The data format used by the zlib library is described by RFCs (Request for
	 Comments) 1950 to 1952 in the files ftp://ds.internic.net/rfc/rfc1950.txt
	 (zlib format), rfc1951.txt (deflate format) and rfc1952.txt (gzip format).
	 */
	
	//
	//	I got mine from: <ftp://ftp.cdrom.com/pub/infozip/zlib/>.
	//
	//	Pursuant to the above: THIS IS AN ALTERED SOURCE VERSION.
	//
	// Satisfy crc needs of ipcosx without bringing in zlib.
	
	static uint32_t gTable [256] =
	{
		0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
		0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
		0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
		0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
		0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
		0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
		0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
		0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
		0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
		0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
		0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
		0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
		0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
		0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
		0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
		0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
		0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
		0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
		0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
		0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
		0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
		0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
		0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
		0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
		0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
		0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
		0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
		0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
		0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
		0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
		0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
		0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
		0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
		0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
		0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
		0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
		0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
		0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
		0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
		0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
		0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
		0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
		0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
		0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
		0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
		0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
		0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
		0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
		0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
		0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
		0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
		0x2d02ef8dL
	};
	
#define DO1(buf) crc = gTable[(crc ^ (*buf++)) & 0xff] ^ (crc >> 8)
#define DO2(buf)  DO1(buf); DO1(buf)
#define DO4(buf)  DO2(buf); DO2(buf)
#define DO8(buf)  DO4(buf); DO4(buf)
	
	uint32_t crc32(uint32_t crc, void *bufv, uint32_t len)
	{
		uint8_t *buf = (uint8_t*)bufv;
		if (buf == NULL) {
			assert(false);
			return 0;
		}
		
		crc = crc ^ 0xffffffffL;
		
		while (len >= 8)
		{
			DO8(buf);
			len -= 8;
		}

		if (len) do
		{
			DO1(buf);
		}
		while (--len);

		return crc ^ 0xffffffffL;
	}

} // Namespace
