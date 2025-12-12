#ifndef COMPAT_POSIX_H
#define COMPAT_POSIX_H

#define _FILE_OFFSET_BITS 64

/*
 * Derived from Linux "Features Test Macro" header
 * Convenience macros to test the versions of gcc (or
 * a compatible compiler).
 * Use them like this:
 *  #if GIT_GNUC_PREREQ (2,8)
 *   ... code requiring gcc 2.8 or later ...
 *  #endif
 *
 * This macro of course is not part of POSIX, but we need it for the UNUSED
 * macro which is used by some of our POSIX compatibility wrappers.
*/
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
# define GIT_GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
 #define GIT_GNUC_PREREQ(maj, min) 0
#endif

/*
 * UNUSED marks a function parameter that is always unused.  It also
 * can be used to annotate a function, a variable, or a type that is
 * always unused.
 *
 * A callback interface may dictate that a function accepts a
 * parameter at that position, but the implementation of the function
 * may not need to use the parameter.  In such a case, mark the parameter
 * with UNUSED.
 *
 * When a parameter may be used or unused, depending on conditional
 * compilation, consider using MAYBE_UNUSED instead.
 */
#if GIT_GNUC_PREREQ(4, 5)
#define UNUSED __attribute__((unused)) \
	__attribute__((deprecated ("parameter declared as UNUSED")))
#elif defined(__GNUC__)
#define UNUSED __attribute__((unused)) \
	__attribute__((deprecated))
#else
#define UNUSED
#endif

#ifdef __MINGW64__
#define _POSIX_C_SOURCE 1
#elif defined(__sun__)
 /*
  * On Solaris, when _XOPEN_EXTENDED is set, its header file
  * forces the programs to be XPG4v2, defeating any _XOPEN_SOURCE
  * setting to say we are XPG5 or XPG6.  Also on Solaris,
  * XPG6 programs must be compiled with a c99 compiler, while
  * non XPG6 programs must be compiled with a pre-c99 compiler.
  */
# if __STDC_VERSION__ - 0 >= 199901L
# define _XOPEN_SOURCE 600
# else
# define _XOPEN_SOURCE 500
# endif
#elif !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__NetBSD__) && \
      !defined(__OpenBSD__) && !defined(__DragonFly__) && !defined(__MirBSD__) && \
      !defined(__USLC__) && !defined(_M_UNIX) && !defined(__sgi) && \
      !defined(__TANDEM) && !defined(__QNX__) && !defined(__CYGWIN__)
#define _XOPEN_SOURCE 600 /* glibc2 and AIX 5.3L need 500 */
#define _XOPEN_SOURCE_EXTENDED 1 /* AIX 5.3L needs this */
#endif
#define _ALL_SOURCE 1
#define _GNU_SOURCE 1
#define _BSD_SOURCE 1
#define _DEFAULT_SOURCE 1
#define _NETBSD_SOURCE 1
#define _SGI_SOURCE 1

#if defined(WIN32) && !defined(__CYGWIN__) /* Both MinGW and MSVC */
# if !defined(_WIN32_WINNT)
#  define _WIN32_WINNT 0x0600
# endif
#define WIN32_LEAN_AND_MEAN  /* stops windows.h including winsock.h */
#include <winsock2.h>
#ifndef NO_UNIX_SOCKETS
#include <afunix.h>
#endif
#include <windows.h>
#define GIT_WINDOWS_NATIVE
#endif

#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h> /* for strcasecmp() */
#endif
#include <errno.h>
#include <limits.h>
#include <locale.h>
#ifdef NEEDS_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <assert.h>
#include <regex.h>
#include <utime.h>
#include <syslog.h>
#if !defined(NO_POLL_H)
#include <poll.h>
#elif !defined(NO_SYS_POLL_H)
#include <sys/poll.h>
#else
/* Pull the compat stuff */
#include <poll.h>
#endif
#ifdef HAVE_BSD_SYSCTL
#include <sys/sysctl.h>
#endif

#if defined(__MINGW32__)
#include "mingw-posix.h"
#elif defined(_MSC_VER)
#include "msvc-posix.h"
#else
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <termios.h>
#ifndef NO_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h>
#include <sys/un.h>
#ifndef NO_INTTYPES_H
#include <inttypes.h>
#else
#include <stdint.h>
#endif
#ifdef HAVE_ARC4RANDOM_LIBBSD
#include <bsd/stdlib.h>
#endif
#ifdef HAVE_GETRANDOM
#include <sys/random.h>
#endif
#ifdef NO_INTPTR_T
/*
 * On I16LP32, ILP32 and LP64 "long" is the safe bet, however
 * on LLP86, IL33LLP64 and P64 it needs to be "long long",
 * while on IP16 and IP16L32 it is "int" (resp. "short")
 * Size needs to match (or exceed) 'sizeof(void *)'.
 * We can't take "long long" here as not everybody has it.
 */
typedef long intptr_t;
typedef unsigned long uintptr_t;
#endif
#undef _ALL_SOURCE /* AIX 5.3L defines a struct list with _ALL_SOURCE. */
#include <grp.h>
#define _ALL_SOURCE 1
#endif

#ifdef MKDIR_WO_TRAILING_SLASH
#define mkdir(a,b) compat_mkdir_wo_trailing_slash((a),(b))
int compat_mkdir_wo_trailing_slash(const char*, mode_t);
#endif

#ifdef time
#undef time
#endif
static inline time_t git_time(time_t *tloc)
{
	struct timeval tv;

	/*
	 * Avoid time(NULL), which can disagree with gettimeofday(2)
	 * and filesystem timestamps.
	 */
	gettimeofday(&tv, NULL);

	if (tloc)
		*tloc = tv.tv_sec;
	return tv.tv_sec;
}
#define time git_time

#ifdef NO_STRUCT_ITIMERVAL
struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};
#endif

#ifdef NO_SETITIMER
static inline int git_setitimer(int which UNUSED,
				const struct itimerval *value UNUSED,
				struct itimerval *newvalue UNUSED) {
	return 0; /* pretend success */
}
#undef setitimer
#define setitimer(which,value,ovalue) git_setitimer(which,value,ovalue)
#endif

#ifndef NO_LIBGEN_H
#include <libgen.h>
#else
#define basename gitbasename
char *gitbasename(char *);
#define dirname gitdirname
char *gitdirname(char *);
#endif

#ifndef NO_ICONV
#include <iconv.h>
#endif

/* On most systems <netdb.h> would have given us this, but
 * not on some systems (e.g. z/OS).
 */
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

/* On most systems <limits.h> would have given us this, but
 * not on some systems (e.g. GNU/Hurd).
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

typedef uintmax_t timestamp_t;
#define PRItime PRIuMAX
#define parse_timestamp strtoumax
#define parse_timestamp_from_buf(buf, len, ep, result) \
	parse_unsigned_from_buf((buf), (len), (ep), (result), TIME_MAX)
#define TIME_MAX UINTMAX_MAX
#define TIME_MIN 0

int lstat_cache_aware_rmdir(const char *path);
#if !defined(__MINGW32__) && !defined(_MSC_VER)
#define rmdir lstat_cache_aware_rmdir
#endif

#if defined(NO_MMAP) || defined(USE_WIN32_MMAP)

#ifndef PROT_READ
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 1
#endif

#define mmap git_mmap
#define munmap git_munmap
void *git_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
int git_munmap(void *start, size_t length);

#else /* NO_MMAP || USE_WIN32_MMAP */

#include <sys/mman.h>

#endif /* NO_MMAP || USE_WIN32_MMAP */

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#ifdef NEEDS_MODE_TRANSLATION
#undef S_IFMT
#undef S_IFREG
#undef S_IFDIR
#undef S_IFLNK
#undef S_IFBLK
#undef S_IFCHR
#undef S_IFIFO
#undef S_IFSOCK
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFLNK  0120000
#define S_IFBLK  0060000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_IFSOCK 0140000
#ifdef stat
#undef stat
#endif
#define stat(path, buf) git_stat(path, buf)
int git_stat(const char *, struct stat *);
#ifdef fstat
#undef fstat
#endif
#define fstat(fd, buf) git_fstat(fd, buf)
int git_fstat(int, struct stat *);
#ifdef lstat
#undef lstat
#endif
#define lstat(path, buf) git_lstat(path, buf)
int git_lstat(const char *, struct stat *);
#endif

#ifdef NO_PREAD
#define pread git_pread
ssize_t git_pread(int fd, void *buf, size_t count, off_t offset);
#endif

#ifdef NO_SETENV
#define setenv gitsetenv
int gitsetenv(const char *, const char *, int);
#endif

#ifdef NO_MKDTEMP
#define mkdtemp git_mkdtemp
#endif

#ifdef NO_UNSETENV
#define unsetenv gitunsetenv
int gitunsetenv(const char *);
#endif

#ifdef NO_STRCASESTR
#define strcasestr gitstrcasestr
char *gitstrcasestr(const char *haystack, const char *needle);
#endif

#ifdef NO_STRLCPY
#define strlcpy gitstrlcpy
size_t gitstrlcpy(char *, const char *, size_t);
#endif

#ifdef NO_STRTOUMAX
#define strtoumax gitstrtoumax
uintmax_t gitstrtoumax(const char *, char **, int);
#define strtoimax gitstrtoimax
intmax_t gitstrtoimax(const char *, char **, int);
#endif

#ifdef NO_HSTRERROR
#define hstrerror githstrerror
const char *githstrerror(int herror);
#endif

#ifdef NO_MEMMEM
#define memmem gitmemmem
void *gitmemmem(const void *haystack, size_t haystacklen,
		const void *needle, size_t needlelen);
#endif

#ifdef OVERRIDE_STRDUP
#ifdef strdup
#undef strdup
#endif
#define strdup gitstrdup
char *gitstrdup(const char *s);
#endif

#ifdef NO_GETPAGESIZE
#define getpagesize() sysconf(_SC_PAGESIZE)
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifdef FREAD_READS_DIRECTORIES
# if !defined(SUPPRESS_FOPEN_REDEFINITION)
#  ifdef fopen
#   undef fopen
#  endif
#  define fopen(a,b) git_fopen(a,b)
# endif
FILE *git_fopen(const char*, const char*);
#endif

#ifdef SNPRINTF_RETURNS_BOGUS
#ifdef snprintf
#undef snprintf
#endif
#define snprintf git_snprintf
int git_snprintf(char *str, size_t maxsize,
		 const char *format, ...);
#ifdef vsnprintf
#undef vsnprintf
#endif
#define vsnprintf git_vsnprintf
int git_vsnprintf(char *str, size_t maxsize,
		  const char *format, va_list ap);
#endif

#ifdef OPEN_RETURNS_EINTR
#undef open
#define open git_open_with_retry
int git_open_with_retry(const char *path, int flag, ...);
#endif

#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2, 1)
#define HAVE_STRCHRNUL
#endif
#endif

#ifndef HAVE_STRCHRNUL
#define strchrnul gitstrchrnul
static inline char *gitstrchrnul(const char *s, int c)
{
	while (*s && *s != c)
		s++;
	return (char *)s;
}
#endif

#ifdef NO_INET_PTON
int inet_pton(int af, const char *src, void *dst);
#endif

#ifdef NO_INET_NTOP
const char *inet_ntop(int af, const void *src, char *dst, size_t size);
#endif

#ifdef NO_PTHREADS
#define atexit git_atexit
int git_atexit(void (*handler)(void));
#endif

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

#include "../sane-ctype.h"

void git_stable_qsort(void *base, size_t nmemb, size_t size,
		      int(*compar)(const void *, const void *));
#ifdef INTERNAL_QSORT
#define qsort git_stable_qsort
#endif

#define QSORT(base, n, compar) sane_qsort((base), (n), sizeof(*(base)), compar)
static inline void sane_qsort(void *base, size_t nmemb, size_t size,
			      int(*compar)(const void *, const void *))
{
	if (nmemb > 1)
		qsort(base, nmemb, size, compar);
}

#define STABLE_QSORT(base, n, compar) \
	git_stable_qsort((base), (n), sizeof(*(base)), compar)

#ifndef HAVE_ISO_QSORT_S
int git_qsort_s(void *base, size_t nmemb, size_t size,
		int (*compar)(const void *, const void *, void *), void *ctx);
#define qsort_s git_qsort_s
#endif

#define QSORT_S(base, n, compar, ctx) do {			\
	if (qsort_s((base), (n), sizeof(*(base)), compar, ctx))	\
		BUG("qsort_s() failed");			\
} while (0)

#ifdef NO_NSEC
#undef USE_NSEC
#define ST_CTIME_NSEC(st) 0
#define ST_MTIME_NSEC(st) 0
#else
#ifdef USE_ST_TIMESPEC
#define ST_CTIME_NSEC(st) ((unsigned int)((st).st_ctimespec.tv_nsec))
#define ST_MTIME_NSEC(st) ((unsigned int)((st).st_mtimespec.tv_nsec))
#else
#define ST_CTIME_NSEC(st) ((unsigned int)((st).st_ctim.tv_nsec))
#define ST_MTIME_NSEC(st) ((unsigned int)((st).st_mtim.tv_nsec))
#endif
#endif

#ifndef va_copy
/*
 * Since an obvious implementation of va_list would be to make it a
 * pointer into the stack frame, a simple assignment will work on
 * many systems.  But let's try to be more portable.
 */
#ifdef __va_copy
#define va_copy(dst, src) __va_copy(dst, src)
#else
#define va_copy(dst, src) ((dst) = (src))
#endif
#endif

#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
static inline void git_flockfile(FILE *fh UNUSED)
{
	; /* nothing */
}
static inline void git_funlockfile(FILE *fh UNUSED)
{
	; /* nothing */
}
#undef flockfile
#undef funlockfile
#undef getc_unlocked
#define flockfile(fh) git_flockfile(fh)
#define funlockfile(fh) git_funlockfile(fh)
#define getc_unlocked(fh) getc(fh)
#endif

#ifdef FILENO_IS_A_MACRO
int git_fileno(FILE *stream);
# ifndef COMPAT_CODE_FILENO
#  undef fileno
#  define fileno(p) git_fileno(p)
# endif
#endif

#ifdef NEED_ACCESS_ROOT_HANDLER
int git_access(const char *path, int mode);
# ifndef COMPAT_CODE_ACCESS
#  ifdef access
#  undef access
#  endif
#  define access(path, mode) git_access(path, mode)
# endif
#endif

#endif /* COMPAT_POSIX_H */
