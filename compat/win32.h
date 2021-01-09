#ifndef WIN32_H
#define WIN32_H

/* common Win32 functions for MinGW and Cygwin */
#ifndef GIT_WINDOWS_NATIVE	/* Not defined for Cygwin */
#include <windows.h>
#endif

extern int file_attr_to_st_mode (DWORD attr, DWORD tag, const char *path);

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

#endif
