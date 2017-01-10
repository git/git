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

#endif
