#ifndef WIN32_H
#define WIN32_H

/* common Win32 functions for MinGW and Cygwin */
#ifndef GIT_WINDOWS_NATIVE	/* Not defined for Cygwin */
#include <windows.h>
#endif

static inline int file_attr_to_st_mode (DWORD attr, DWORD tag)
{
	int fMode = S_IREAD;
	if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) && tag == IO_REPARSE_TAG_SYMLINK)
		fMode |= S_IFLNK;
	else if (attr & FILE_ATTRIBUTE_DIRECTORY)
		fMode |= S_IFDIR;
	else
		fMode |= S_IFREG;
	if (!(attr & FILE_ATTRIBUTE_READONLY))
		fMode |= S_IWRITE;
	return fMode;
}

static inline int get_file_attr(const char *fname, WIN32_FILE_ATTRIBUTE_DATA *fdata)
{
	if (GetFileAttributesExA(fname, GetFileExInfoStandard, fdata))
		return 0;

	switch (GetLastError()) {
	case ERROR_ACCESS_DENIED:
	case ERROR_SHARING_VIOLATION:
	case ERROR_LOCK_VIOLATION:
	case ERROR_SHARING_BUFFER_EXCEEDED:
		return EACCES;
	case ERROR_BUFFER_OVERFLOW:
		return ENAMETOOLONG;
	case ERROR_NOT_ENOUGH_MEMORY:
		return ENOMEM;
	default:
		return ENOENT;
	}
}

/* simplify loading of DLL functions */

struct proc_addr {
	const char *const dll;
	const char *const function;
	FARPROC pfunction;
	unsigned initialized : 1;
};

/* Declares a function to be loaded dynamically from a DLL. */
#define DECLARE_PROC_ADDR(dll, rettype, function, ...) \
	static struct proc_addr proc_addr_##function = \
	{ #dll, #function, NULL, 0 }; \
	static rettype (WINAPI *function)(__VA_ARGS__)

/*
 * Loads a function from a DLL (once-only).
 * Returns non-NULL function pointer on success.
 * Returns NULL + errno == ENOSYS on failure.
 */
#define INIT_PROC_ADDR(function) (function = get_proc_addr(&proc_addr_##function))

static inline void *get_proc_addr(struct proc_addr *proc)
{
	/* only do this once */
	if (!proc->initialized) {
		HANDLE hnd;
		proc->initialized = 1;
		hnd = LoadLibraryA(proc->dll);
		if (hnd)
			proc->pfunction = GetProcAddress(hnd, proc->function);
	}
	/* set ENOSYS if DLL or function was not found */
	if (!proc->pfunction)
		errno = ENOSYS;
	return proc->pfunction;
}

#endif
