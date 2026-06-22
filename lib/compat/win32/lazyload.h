#ifndef LAZYLOAD_H
#define LAZYLOAD_H

/*
 * A pair of macros to simplify loading of DLL functions. Example:
 *
 *   DECLARE_PROC_ADDR(kernel32.dll, BOOL, WINAPI, CreateHardLinkW,
 *                     LPCWSTR, LPCWSTR, LPSECURITY_ATTRIBUTES);
 *
 *   if (!INIT_PROC_ADDR(CreateHardLinkW))
 *           return error("Could not find CreateHardLinkW() function";
 *
 *   if (!CreateHardLinkW(source, target, NULL))
 *           return error("could not create hardlink from %S to %S",
 *                        source, target);
 */

typedef void (*FARVOIDPROC)(void);

struct proc_addr {
	const char *const dll;
	const char *const function;
	FARVOIDPROC pfunction;
	unsigned initialized : 1;
};

/* Declares a function to be loaded dynamically from a DLL. */
#define DECLARE_PROC_ADDR(dll, rettype, convention, function, ...) \
	static struct proc_addr proc_addr_##function = \
	{ #dll, #function, NULL, 0 }; \
	typedef rettype (convention *proc_type_##function)(__VA_ARGS__); \
	static proc_type_##function function

/*
 * Loads a function from a DLL (once-only).
 * Returns non-NULL function pointer on success.
 * Returns NULL + errno == ENOSYS on failure.
 * This function is not thread-safe.
 */
#define INIT_PROC_ADDR(function) \
	(function = (proc_type_##function)get_proc_addr(&proc_addr_##function))

static inline FARVOIDPROC get_proc_addr(struct proc_addr *proc)
{
	/* only do this once */
	if (!proc->initialized) {
		HANDLE hnd;
		proc->initialized = 1;
		hnd = LoadLibraryExA(proc->dll, NULL,
				     LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (hnd)
			proc->pfunction = (FARVOIDPROC)GetProcAddress(hnd,
							proc->function);
	}
	/* set ENOSYS if DLL or function was not found */
	if (!proc->pfunction)
		errno = ENOSYS;
	return proc->pfunction;
}

#endif
