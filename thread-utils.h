#ifndef THREAD_COMPAT_H
#define THREAD_COMPAT_H

#ifndef NO_PTHREADS
#include <pthread.h>

extern int online_cpus(void);
extern int init_recursive_mutex(pthread_mutex_t*);

#else

#define online_cpus() 1

#endif
#endif /* THREAD_COMPAT_H */
