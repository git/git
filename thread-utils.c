#include "git-compat-util.h"
#include "thread-utils.h"

#if defined(hpux) || defined(__hpux) || defined(_hpux)
#  include <sys/pstat.h>
#endif

/*
 * By doing this in two steps we can at least get
 * the function to be somewhat coherent, even
 * with this disgusting nest of #ifdefs.
 */
#ifndef _SC_NPROCESSORS_ONLN
#  ifdef _SC_NPROC_ONLN
#    define _SC_NPROCESSORS_ONLN _SC_NPROC_ONLN
#  elif defined _SC_CRAY_NCPU
#    define _SC_NPROCESSORS_ONLN _SC_CRAY_NCPU
#  endif
#endif

int online_cpus(void)
{
#ifdef NO_PTHREADS
	return 1;
#else
#ifdef _SC_NPROCESSORS_ONLN
	long ncpus;
#endif

#ifdef GIT_WINDOWS_NATIVE
	SYSTEM_INFO info;
	GetSystemInfo(&info);

	if ((int)info.dwNumberOfProcessors > 0)
		return (int)info.dwNumberOfProcessors;
#elif defined(hpux) || defined(__hpux) || defined(_hpux)
	struct pst_dynamic psd;

	if (!pstat_getdynamic(&psd, sizeof(psd), (size_t)1, 0))
		return (int)psd.psd_proc_cnt;
#elif defined(HAVE_BSD_SYSCTL) && defined(HW_NCPU)
	int mib[2];
	size_t len;
	int cpucount;

	mib[0] = CTL_HW;
#  ifdef HW_AVAILCPU
	mib[1] = HW_AVAILCPU;
	len = sizeof(cpucount);
	if (!sysctl(mib, 2, &cpucount, &len, NULL, 0))
		return cpucount;
#  endif /* HW_AVAILCPU */
	mib[1] = HW_NCPU;
	len = sizeof(cpucount);
	if (!sysctl(mib, 2, &cpucount, &len, NULL, 0))
		return cpucount;
#endif /* defined(HAVE_BSD_SYSCTL) && defined(HW_NCPU) */

#ifdef _SC_NPROCESSORS_ONLN
	if ((ncpus = (long)sysconf(_SC_NPROCESSORS_ONLN)) > 0)
		return (int)ncpus;
#endif

	return 1;
#endif
}

int init_recursive_mutex(pthread_mutex_t *m)
{
#ifndef NO_PTHREADS
	pthread_mutexattr_t a;
	int ret;

	ret = pthread_mutexattr_init(&a);
	if (!ret) {
		ret = pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
		if (!ret)
			ret = pthread_mutex_init(m, &a);
		pthread_mutexattr_destroy(&a);
	}
	return ret;
#else
	return 0;
#endif
}

#ifdef NO_PTHREADS
int dummy_pthread_create(pthread_t *pthread, const void *attr,
			 void *(*fn)(void *), void *data)
{
	/*
	 * Do nothing.
	 *
	 * The main purpose of this function is to break compiler's
	 * flow analysis and avoid -Wunused-variable false warnings.
	 */
	return ENOSYS;
}

int dummy_pthread_init(void *data)
{
	/*
	 * Do nothing.
	 *
	 * The main purpose of this function is to break compiler's
	 * flow analysis or it may realize that functions like
	 * pthread_mutex_init() is no-op, which means the (static)
	 * variable is not used/initialized at all and trigger
	 * -Wunused-variable
	 */
	return ENOSYS;
}

int dummy_pthread_join(pthread_t pthread, void **retval)
{
	/*
	 * Do nothing.
	 *
	 * The main purpose of this function is to break compiler's
	 * flow analysis and avoid -Wunused-variable false warnings.
	 */
	return ENOSYS;
}

#endif
