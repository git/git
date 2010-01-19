/*
 * Copyright (C) 2009 Andrzej K. Haczewski <ahaczewski@gmail.com>
 *
 * DISCLAMER: The implementation is Git-specific, it is subset of original
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

int pthread_cond_init(pthread_cond_t *cond, const void *unused)
{
	cond->waiters = 0;

	cond->sema = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
	if (!cond->sema)
		die("CreateSemaphore() failed");
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
	CloseHandle(cond->sema);
	cond->sema = NULL;

	return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, CRITICAL_SECTION *mutex)
{
	InterlockedIncrement(&cond->waiters);

	/*
	 * Unlock external mutex and wait for signal.
	 * NOTE: we've held mutex locked long enough to increment
	 * waiters count above, so there's no problem with
	 * leaving mutex unlocked before we wait on semaphore.
	 */
	LeaveCriticalSection(mutex);

	/* let's wait - ignore return value */
	WaitForSingleObject(cond->sema, INFINITE);

	/* we're done waiting, so make sure we decrease waiters count */
	InterlockedDecrement(&cond->waiters);

	/* lock external mutex again */
	EnterCriticalSection(mutex);

	return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
	/*
	 * Access to waiters count is atomic; see "Interlocked Variable Access"
	 * http://msdn.microsoft.com/en-us/library/ms684122(VS.85).aspx
	 */
	int have_waiters = cond->waiters > 0;

	/*
	 * Signal only when there are waiters
	 */
	if (have_waiters)
		return ReleaseSemaphore(cond->sema, 1, NULL) ?
			0 : err_win_to_posix(GetLastError());
	else
		return 0;
}
