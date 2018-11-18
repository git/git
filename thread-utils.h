#ifndef THREAD_COMPAT_H
#define THREAD_COMPAT_H

#ifndef NO_PTHREADS
#include <pthread.h>

#define HAVE_THREADS 1

#else

#define HAVE_THREADS 0

/*
 * macros instead of typedefs because pthread definitions may have
 * been pulled in by some system dependencies even though the user
 * wants to disable pthread.
 */
#define pthread_t int
#define pthread_mutex_t int
#define pthread_cond_t int
#define pthread_key_t int

#define pthread_mutex_init(mutex, attr) dummy_pthread_init(mutex)
#define pthread_mutex_lock(mutex)
#define pthread_mutex_unlock(mutex)
#define pthread_mutex_destroy(mutex)

#define pthread_cond_init(cond, attr) dummy_pthread_init(cond)
#define pthread_cond_wait(cond, mutex)
#define pthread_cond_signal(cond)
#define pthread_cond_broadcast(cond)
#define pthread_cond_destroy(cond)

#define pthread_key_create(key, attr) dummy_pthread_init(key)
#define pthread_key_delete(key)

#define pthread_create(thread, attr, fn, data) \
	dummy_pthread_create(thread, attr, fn, data)
#define pthread_join(thread, retval) \
	dummy_pthread_join(thread, retval)

#define pthread_setspecific(key, data)
#define pthread_getspecific(key) NULL

int dummy_pthread_create(pthread_t *pthread, const void *attr,
			 void *(*fn)(void *), void *data);
int dummy_pthread_join(pthread_t pthread, void **retval);

int dummy_pthread_init(void *);

#endif

int online_cpus(void);
int init_recursive_mutex(pthread_mutex_t*);


#endif /* THREAD_COMPAT_H */
