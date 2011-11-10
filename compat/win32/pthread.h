/*
 * Header used to adapt pthread-based POSIX code to Windows API threads.
 *
 * Copyright (C) 2009 Andrzej K. Haczewski <ahaczewski@gmail.com>
 */

#ifndef PTHREAD_H
#define PTHREAD_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

/*
 * Defines that adapt Windows API threads to pthreads API
 */
#define pthread_mutex_t CRITICAL_SECTION

#define pthread_mutex_init(a,b) InitializeCriticalSection((a))
#define pthread_mutex_destroy(a) DeleteCriticalSection((a))
#define pthread_mutex_lock EnterCriticalSection
#define pthread_mutex_unlock LeaveCriticalSection

/*
 * Implement simple condition variable for Windows threads, based on ACE
 * implementation.
 *
 * See original implementation: http://bit.ly/1vkDjo
 * ACE homepage: http://www.cse.wustl.edu/~schmidt/ACE.html
 * See also: http://www.cse.wustl.edu/~schmidt/win32-cv-1.html
 */
typedef struct {
	LONG waiters;
	int was_broadcast;
	CRITICAL_SECTION waiters_lock;
	HANDLE sema;
	HANDLE continue_broadcast;
} pthread_cond_t;

extern int pthread_cond_init(pthread_cond_t *cond, const void *unused);
extern int pthread_cond_destroy(pthread_cond_t *cond);
extern int pthread_cond_wait(pthread_cond_t *cond, CRITICAL_SECTION *mutex);
extern int pthread_cond_signal(pthread_cond_t *cond);
extern int pthread_cond_broadcast(pthread_cond_t *cond);

/*
 * Simple thread creation implementation using pthread API
 */
typedef struct {
	HANDLE handle;
	void *(*start_routine)(void*);
	void *arg;
} pthread_t;

extern int pthread_create(pthread_t *thread, const void *unused,
			  void *(*start_routine)(void*), void *arg);

/*
 * To avoid the need of copying a struct, we use small macro wrapper to pass
 * pointer to win32_pthread_join instead.
 */
#define pthread_join(a, b) win32_pthread_join(&(a), (b))

extern int win32_pthread_join(pthread_t *thread, void **value_ptr);

#endif /* PTHREAD_H */
