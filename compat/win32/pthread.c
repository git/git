/*
 * Copyright (C) 2009 Andrzej K. Haczewski <ahaczewski@gmail.com>
 *
 * DISCLAIMER: The implementation is Git-specific, it is subset of original
 * Pthreads API, without lots of other features that Git doesn't use.
 * Git also makes sure that the passed arguments are valid, so there's
 * no need for double-checking.
 */

#include "../../git-compat-util.h"
#include "pthread.h"

#include <errno.h>
#include <limits.h>

static unsigned __stdcall win32_start_routine(void *arg)
{
	pthread_t *thread = arg;
	thread->tid = GetCurrentThreadId();
	thread->arg = thread->start_routine(thread->arg);
	return 0;
}

int pthread_create(pthread_t *thread, const void *unused,
		   void *(*start_routine)(void*), void *arg)
{
	thread->arg = arg;
	thread->start_routine = start_routine;
	thread->handle = (HANDLE)
		_beginthreadex(NULL, 0, win32_start_routine, thread, 0, NULL);

	if (!thread->handle)
		return errno;
	else
		return 0;
}

int win32_pthread_join(pthread_t *thread, void **value_ptr)
{
	DWORD result = WaitForSingleObject(thread->handle, INFINITE);
	switch (result) {
		case WAIT_OBJECT_0:
			if (value_ptr)
				*value_ptr = thread->arg;
			return 0;
		case WAIT_ABANDONED:
			return EINVAL;
		default:
			return err_win_to_posix(GetLastError());
	}
}

pthread_t pthread_self(void)
{
	pthread_t t = { NULL };
	t.tid = GetCurrentThreadId();
	return t;
}

int pthread_cond_init(pthread_cond_t *cond, const void *unused)
{
	cond->waiters = 0;
	cond->was_broadcast = 0;
	InitializeCriticalSection(&cond->waiters_lock);

	cond->sema = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
	if (!cond->sema)
		die("CreateSemaphore() failed");

	cond->continue_broadcast = CreateEvent(NULL,	/* security */
				FALSE,			/* auto-reset */
				FALSE,			/* not signaled */
				NULL);			/* name */
	if (!cond->continue_broadcast)
		die("CreateEvent() failed");

	return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
	CloseHandle(cond->sema);
	CloseHandle(cond->continue_broadcast);
	DeleteCriticalSection(&cond->waiters_lock);
	return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, CRITICAL_SECTION *mutex)
{
	int last_waiter;

	EnterCriticalSection(&cond->waiters_lock);
	cond->waiters++;
	LeaveCriticalSection(&cond->waiters_lock);

	/*
	 * Unlock external mutex and wait for signal.
	 * NOTE: we've held mutex locked long enough to increment
	 * waiters count above, so there's no problem with
	 * leaving mutex unlocked before we wait on semaphore.
	 */
	LeaveCriticalSection(mutex);

	/* let's wait - ignore return value */
	WaitForSingleObject(cond->sema, INFINITE);

	/*
	 * Decrease waiters count. If we are the last waiter, then we must
	 * notify the broadcasting thread that it can continue.
	 * But if we continued due to cond_signal, we do not have to do that
	 * because the signaling thread knows that only one waiter continued.
	 */
	EnterCriticalSection(&cond->waiters_lock);
	cond->waiters--;
	last_waiter = cond->was_broadcast && cond->waiters == 0;
	LeaveCriticalSection(&cond->waiters_lock);

	if (last_waiter) {
		/*
		 * cond_broadcast was issued while mutex was held. This means
		 * that all other waiters have continued, but are contending
		 * for the mutex at the end of this function because the
		 * broadcasting thread did not leave cond_broadcast, yet.
		 * (This is so that it can be sure that each waiter has
		 * consumed exactly one slice of the semaphor.)
		 * The last waiter must tell the broadcasting thread that it
		 * can go on.
		 */
		SetEvent(cond->continue_broadcast);
		/*
		 * Now we go on to contend with all other waiters for
		 * the mutex. Auf in den Kampf!
		 */
	}
	/* lock external mutex again */
	EnterCriticalSection(mutex);

	return 0;
}

/*
 * IMPORTANT: This implementation requires that pthread_cond_signal
 * is called while the mutex is held that is used in the corresponding
 * pthread_cond_wait calls!
 */
int pthread_cond_signal(pthread_cond_t *cond)
{
	int have_waiters;

	EnterCriticalSection(&cond->waiters_lock);
	have_waiters = cond->waiters > 0;
	LeaveCriticalSection(&cond->waiters_lock);

	/*
	 * Signal only when there are waiters
	 */
	if (have_waiters)
		return ReleaseSemaphore(cond->sema, 1, NULL) ?
			0 : err_win_to_posix(GetLastError());
	else
		return 0;
}

/*
 * DOUBLY IMPORTANT: This implementation requires that pthread_cond_broadcast
 * is called while the mutex is held that is used in the corresponding
 * pthread_cond_wait calls!
 */
int pthread_cond_broadcast(pthread_cond_t *cond)
{
	EnterCriticalSection(&cond->waiters_lock);

	if ((cond->was_broadcast = cond->waiters > 0)) {
		/* wake up all waiters */
		ReleaseSemaphore(cond->sema, cond->waiters, NULL);
		LeaveCriticalSection(&cond->waiters_lock);
		/*
		 * At this point all waiters continue. Each one takes its
		 * slice of the semaphor. Now it's our turn to wait: Since
		 * the external mutex is held, no thread can leave cond_wait,
		 * yet. For this reason, we can be sure that no thread gets
		 * a chance to eat *more* than one slice. OTOH, it means
		 * that the last waiter must send us a wake-up.
		 */
		WaitForSingleObject(cond->continue_broadcast, INFINITE);
		/*
		 * Since the external mutex is held, no thread can enter
		 * cond_wait, and, hence, it is safe to reset this flag
		 * without cond->waiters_lock held.
		 */
		cond->was_broadcast = 0;
	} else {
		LeaveCriticalSection(&cond->waiters_lock);
	}
	return 0;
}
