#ifndef __MSVC__HEAD
#define __MSVC__HEAD

#include <direct.h>
#include <process.h>
#include <malloc.h>

/* porting function */
#define inline __inline
#define __inline__ __inline
#define __attribute__(x)
#define va_copy(dst, src)     ((dst) = (src))
#define strncasecmp  _strnicmp
#define ftruncate    _chsize

static __inline int strcasecmp (const char *s1, const char *s2)
{
	int size1 = strlen(s1);
	int sisz2 = strlen(s2);
	return _strnicmp(s1, s2, sisz2 > size1 ? sisz2 : size1);
}

#undef ERROR

/* Use mingw_lstat() instead of lstat()/stat() and mingw_fstat() instead
 * of fstat(). We add the declaration of these functions here, suppressing
 * the corresponding declarations in mingw.h, so that we can use the
 * appropriate structure type (and function) names from the msvc headers.
 */
#define stat _stat64
int mingw_lstat(const char *file_name, struct stat *buf);
int mingw_fstat(int fd, struct stat *buf);
#define fstat mingw_fstat
#define lstat mingw_lstat
#define _stat64(x,y) mingw_lstat(x,y)
#define ALREADY_DECLARED_STAT_FUNCS

#include "mingw.h"

#undef ALREADY_DECLARED_STAT_FUNCS

#define NO_PREAD
#define NO_OPENSSL
#define	NO_LIBGEN_H
#define	NO_SYMLINK_HEAD
#define	NO_IPV6
#define	NO_SETENV
#define	NO_UNSETENV
#define	NO_STRCASESTR
#define	NO_STRLCPY
#define	NO_MEMMEM
#define	NO_ICONV
#define	NO_C99_FORMAT
#define	NO_STRTOUMAX
#define	NO_STRTOULL
#define	NO_MKDTEMP
#define	NO_MKSTEMPS
#define	SNPRINTF_RETURNS_BOGUS
#define	NO_SVN_TESTS
#define	NO_PERL_MAKEMAKER 
#define	RUNTIME_PREFIX
#define	NO_POSIX_ONLY_PROGRAMS
#define	NO_ST_BLOCKS_IN_STRUCT_STAT
#define	NO_NSEC 
#ifndef USE_WIN32_MMAP
#define	USE_WIN32_MMAP
#endif
#define	UNRELIABLE_FSTAT 
#define	NO_REGEX
#define	NO_CURL
#define	NO_PTHREADS 

/*Git runtime infomation*/
#define ETC_GITCONFIG "%HOME%"
#define SHA1_HEADER "block-sha1\\sha1.h"
#define GIT_EXEC_PATH "bin"
#define GIT_VERSION "1.7.2.3"
#define GIT_VERSION_VER 1,7,2,3
#define GIT_VERSION_STRVER "1, 7, 2, 3\0"
#define BINDIR "bin"
#define PREFIX "."
#define GIT_MAN_PATH "man"
#define GIT_INFO_PATH "info"
#define GIT_HTML_PATH "html"
#define DEFAULT_GIT_TEMPLATE_DIR "templates"
#define GIT_USER_AGENT "git/1.7.2.3"
#endif
