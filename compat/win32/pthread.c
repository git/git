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

int win32_pthread_create(pthread_t *thread, const void *attr UNUSED,
			 void *(*start_routine)(void *), void *arg)
{
	thread->arg = arg;
	thread->start_routine = start_routine;
	thread->handle = (HANDLE)_beginthreadex(NULL, 0, win32_start_routine,
						thread, 0, NULL);

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
		CloseHandle(thread->handle);
		return 0;
	case WAIT_ABANDONED:
		CloseHandle(thread->handle);
		return EINVAL;
	default:
		/* the wait failed, so do not detach */
		return err_win_to_posix(GetLastError());
	}
}

pthread_t win32_pthread_self(void)
{
	pthread_t t = { NULL };
	t.tid = GetCurrentThreadId();
	return t;
}
