#include "cache.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#elif defined(hpux) || defined(__hpux) || defined(_hpux)
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
#ifdef _SC_NPROCESSORS_ONLN
	long ncpus;
#endif

#ifdef _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);

	if ((int)info.dwNumberOfProcessors > 0)
		return (int)info.dwNumberOfProcessors;
#elif defined(hpux) || defined(__hpux) || defined(_hpux)
	struct pst_dynamic psd;

	if (!pstat_getdynamic(&psd, sizeof(psd), (size_t)1, 0))
		return (int)psd.psd_proc_cnt;
#endif

#ifdef _SC_NPROCESSORS_ONLN
	if ((ncpus = (long)sysconf(_SC_NPROCESSORS_ONLN)) > 0)
		return (int)ncpus;
#endif

	return 1;
}
