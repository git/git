#ifndef __MSVC__HEAD
#define __MSVC__HEAD

/* Define minimize windows version */
#define WINVER 0x0500
#define _WIN32_WINNT 0x0500
#define _WIN32_WINDOWS 0x0410
#define _WIN32_IE 0x0700
#define NTDDI_VERSION NTDDI_WIN2KSP1
#include <winsock2.h>
#include <direct.h>
#include <process.h>
#include <malloc.h>

/* porting function */
#define inline __inline
#define __inline__ __inline
#define __attribute__(x)
#define va_copy(dst, src)     ((dst) = (src))

static __inline int strcasecmp (const char *s1, const char *s2)
{
	int size1 = strlen(s1);
	int sisz2 = strlen(s2);
	return _strnicmp(s1, s2, sisz2 > size1 ? sisz2 : size1);
}

#undef ERROR
#undef stat
#undef _stati64
#include "compat/mingw.h"
#undef stat
#define stat _stati64
#define _stat64(x,y) mingw_lstat(x,y)

/*
   Even though _stati64 is normally just defined at _stat64
   on Windows, we specify it here as a proper struct to avoid
   compiler warnings about macro redefinition due to magic in
   mingw.h. Struct taken from ReactOS (GNU GPL license).
*/
struct _stati64 {
	_dev_t  st_dev;
	_ino_t  st_ino;
	unsigned short st_mode;
	short   st_nlink;
	short   st_uid;
	short   st_gid;
	_dev_t  st_rdev;
	__int64 st_size;
	time_t  st_atime;
	time_t  st_mtime;
	time_t  st_ctime;
};
#endif
