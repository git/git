#ifndef GIT_COMPAT_UTIL_H
#define GIT_COMPAT_UTIL_H

#ifdef USE_MSVC_CRTDBG
/*
 * For these to work they must appear very early in each
 * file -- before most of the standard header files.
 */
#include <stdlib.h>
#include <crtdbg.h>
#endif

#define _FILE_OFFSET_BITS 64


/* Derived from Linux "Features Test Macro" header
 * Convenience macros to test the versions of gcc (or
 * a compatible compiler).
 * Use them like this:
 *  #if GIT_GNUC_PREREQ (2,8)
 *   ... code requiring gcc 2.8 or later ...
 *  #endif
*/
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
# define GIT_GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
 #define GIT_GNUC_PREREQ(maj, min) 0
#endif


#ifndef FLEX_ARRAY
/*
 * See if our compiler is known to support flexible array members.
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) && (!defined(__SUNPRO_C) || (__SUNPRO_C > 0x580))
# define FLEX_ARRAY /* empty */
#elif defined(__GNUC__)
# if (__GNUC__ >= 3)
#  define FLEX_ARRAY /* empty */
# else
#  define FLEX_ARRAY 0 /* older GNU extension */
# endif
#endif

/*
 * Otherwise, default to safer but a bit wasteful traditional style
 */
#ifndef FLEX_ARRAY
# define FLEX_ARRAY 1
#endif
#endif


/*
 * BUILD_ASSERT_OR_ZERO - assert a build-time dependency, as an expression.
 * @cond: the compile-time condition which must be true.
 *
 * Your compile will fail if the condition isn't true, or can't be evaluated
 * by the compiler.  This can be used in an expression: its value is "0".
 *
 * Example:
 *	#define foo_to_char(foo)					\
 *		 ((char *)(foo)						\
 *		  + BUILD_ASSERT_OR_ZERO(offsetof(struct foo, string) == 0))
 */
#define BUILD_ASSERT_OR_ZERO(cond) \
	(sizeof(char [1 - 2*!(cond)]) - 1)

#if GIT_GNUC_PREREQ(3, 1)
 /* &arr[0] degrades to a pointer: a different type from an array */
# define BARF_UNLESS_AN_ARRAY(arr)						\
	BUILD_ASSERT_OR_ZERO(!__builtin_types_compatible_p(__typeof__(arr), \
							   __typeof__(&(arr)[0])))
#else
# define BARF_UNLESS_AN_ARRAY(arr) 0
#endif
/*
 * ARRAY_SIZE - get the number of elements in a visible array
 * @x: the array whose size you want.
 *
 * This does not work on pointers, or arrays declared as [], or
 * function parameters.  With correct compiler support, such usage
 * will cause a build error (see the build_assert_or_zero macro).
 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]) + BARF_UNLESS_AN_ARRAY(x))

#define bitsizeof(x)  (CHAR_BIT * sizeof(x))

#define maximum_signed_value_of_type(a) \
    (INTMAX_MAX >> (bitsizeof(intmax_t) - bitsizeof(a)))

#define maximum_unsigned_value_of_type(a) \
    (UINTMAX_MAX >> (bitsizeof(uintmax_t) - bitsizeof(a)))

/*
 * Signed integer overflow is undefined in C, so here's a helper macro
 * to detect if the sum of two integers will overflow.
 *
 * Requires: a >= 0, typeof(a) equals typeof(b)
 */
#define signed_add_overflows(a, b) \
    ((b) > maximum_signed_value_of_type(a) - (a))

#define unsigned_add_overflows(a, b) \
    ((b) > maximum_unsigned_value_of_type(a) - (a))

/*
 * Returns true if the multiplication of "a" and "b" will
 * overflow. The types of "a" and "b" must match and must be unsigned.
 * Note that this macro evaluates "a" twice!
 */
#define unsigned_mult_overflows(a, b) \
    ((a) && (b) > maximum_unsigned_value_of_type(a) / (a))

#ifdef __GNUC__
#define TYPEOF(x) (__typeof__(x))
#else
#define TYPEOF(x)
#endif

#define MSB(x, bits) ((x) & TYPEOF(x)(~0ULL << (bitsizeof(x) - (bits))))
#define HAS_MULTI_BITS(i)  ((i) & ((i) - 1))  /* checks if an integer has more than 1 bit set */

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

/* Approximation of the length of the decimal representation of this type. */
#define decimal_length(x)	((int)(sizeof(x) * 2.56 + 0.5) + 1)

#if defined(__sun__)
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
#elif !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__USLC__) && \
      !defined(_M_UNIX) && !defined(__sgi) && !defined(__DragonFly__) && \
      !defined(__TANDEM) && !defined(__QNX__) && !defined(__MirBSD__) && \
      !defined(__CYGWIN__)
#define _XOPEN_SOURCE 600 /* glibc2 and AIX 5.3L need 500, OpenBSD needs 600 for S_ISLNK() */
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
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h> /* for strcasecmp() */
#endif
#include <errno.h>
#include <limits.h>
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

#if defined(__CYGWIN__)
#include "compat/win32/path-utils.h"
#endif
#if defined(__MINGW32__)
/* pull in Windows compatibility stuff */
#include "compat/win32/path-utils.h"
#include "compat/mingw.h"
#include "compat/win32/fscache.h"
#elif defined(_MSC_VER)
#include "compat/win32/path-utils.h"
#include "compat/msvc.h"
#include "compat/win32/fscache.h"
#else
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
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

/* used on Mac OS X */
#ifdef PRECOMPOSE_UNICODE
#include "compat/precompose_utf8.h"
#else
static inline void precompose_argv(int argc, const char **argv)
{
	; /* nothing */
}
#define probe_utf8_pathname_composition()
#endif

#ifdef MKDIR_WO_TRAILING_SLASH
#define mkdir(a,b) compat_mkdir_wo_trailing_slash((a),(b))
int compat_mkdir_wo_trailing_slash(const char*, mode_t);
#endif

#ifdef NO_STRUCT_ITIMERVAL
struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};
#endif

#ifdef NO_SETITIMER
static inline int setitimer(int which, const struct itimerval *value, struct itimerval *newvalue) {
	; /* nothing */
}
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

#ifndef NO_OPENSSL
#ifdef __APPLE__
#define __AVAILABILITY_MACROS_USES_AVAILABILITY 0
#include <AvailabilityMacros.h>
#undef DEPRECATED_ATTRIBUTE
#define DEPRECATED_ATTRIBUTE
#undef __AVAILABILITY_MACROS_USES_AVAILABILITY
#endif
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#ifdef HAVE_SYSINFO
# include <sys/sysinfo.h>
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

typedef uintmax_t timestamp_t;
#define PRItime PRIuMAX
#define parse_timestamp strtoumax
#define TIME_MAX UINTMAX_MAX
#define TIME_MIN 0

#ifndef PATH_SEP
#define PATH_SEP ':'
#endif

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#ifndef _PATH_DEFPATH
#define _PATH_DEFPATH "/usr/local/bin:/usr/bin:/bin"
#endif

#ifndef platform_core_config
static inline int noop_core_config(const char *var, const char *value, void *cb)
{
	return 0;
}
#define platform_core_config noop_core_config
#endif

#ifndef has_dos_drive_prefix
static inline int git_has_dos_drive_prefix(const char *path)
{
	return 0;
}
#define has_dos_drive_prefix git_has_dos_drive_prefix
#endif

#ifndef skip_dos_drive_prefix
static inline int git_skip_dos_drive_prefix(char **path)
{
	return 0;
}
#define skip_dos_drive_prefix git_skip_dos_drive_prefix
#endif

#ifndef is_dir_sep
static inline int git_is_dir_sep(int c)
{
	return c == '/';
}
#define is_dir_sep git_is_dir_sep
#endif

#ifndef offset_1st_component
static inline int git_offset_1st_component(const char *path)
{
	return is_dir_sep(path[0]);
}
#define offset_1st_component git_offset_1st_component
#endif

#ifndef is_valid_path
#define is_valid_path(path) 1
#endif

#ifndef find_last_dir_sep
static inline char *git_find_last_dir_sep(const char *path)
{
	return strrchr(path, '/');
}
#define find_last_dir_sep git_find_last_dir_sep
#endif

#ifndef has_dir_sep
static inline int git_has_dir_sep(const char *path)
{
	return !!strchr(path, '/');
}
#define has_dir_sep(path) git_has_dir_sep(path)
#endif

#ifndef is_mount_point
#define is_mount_point is_mount_point_via_stat
#endif

#ifndef query_user_email
#define query_user_email() NULL
#endif

#ifndef platform_strbuf_realpath
#define platform_strbuf_realpath(resolved, path) NULL
#endif

#ifdef __TANDEM
#include <floss.h(floss_execl,floss_execlp,floss_execv,floss_execvp)>
#include <floss.h(floss_getpwuid)>
#ifndef NSIG
/*
 * NonStop NSE and NSX do not provide NSIG. SIGGUARDIAN(99) is the highest
 * known, by detective work using kill -l as a list is all signals
 * instead of signal.h where it should be.
 */
# define NSIG 100
#endif
#endif

#if defined(__HP_cc) && (__HP_cc >= 61000)
#define NORETURN __attribute__((noreturn))
#define NORETURN_PTR
#elif defined(__GNUC__) && !defined(NO_NORETURN)
#define NORETURN __attribute__((__noreturn__))
#define NORETURN_PTR __attribute__((__noreturn__))
#elif defined(_MSC_VER)
#define NORETURN __declspec(noreturn)
#define NORETURN_PTR
#else
#define NORETURN
#define NORETURN_PTR
#ifndef __GNUC__
#ifndef __attribute__
#define __attribute__(x)
#endif
#endif
#endif

/* The sentinel attribute is valid from gcc version 4.0 */
#if defined(__GNUC__) && (__GNUC__ >= 4)
#define LAST_ARG_MUST_BE_NULL __attribute__((sentinel))
#else
#define LAST_ARG_MUST_BE_NULL
#endif

#define MAYBE_UNUSED __attribute__((__unused__))

#include "compat/bswap.h"

#include "wildmatch.h"

struct strbuf;

/* General helper functions */
void vreportf(const char *prefix, const char *err, va_list params);
NORETURN void usage(const char *err);
NORETURN void usagef(const char *err, ...) __attribute__((format (printf, 1, 2)));
NORETURN void die(const char *err, ...) __attribute__((format (printf, 1, 2)));
NORETURN void die_errno(const char *err, ...) __attribute__((format (printf, 1, 2)));
int error(const char *err, ...) __attribute__((format (printf, 1, 2)));
int error_errno(const char *err, ...) __attribute__((format (printf, 1, 2)));
void warning(const char *err, ...) __attribute__((format (printf, 1, 2)));
void warning_errno(const char *err, ...) __attribute__((format (printf, 1, 2)));

#ifndef NO_OPENSSL
#ifdef APPLE_COMMON_CRYPTO
#include "compat/apple-common-crypto.h"
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif /* APPLE_COMMON_CRYPTO */
#include <openssl/x509v3.h>
#endif /* NO_OPENSSL */

/*
 * Let callers be aware of the constant return value; this can help
 * gcc with -Wuninitialized analysis. We restrict this trick to gcc, though,
 * because some compilers may not support variadic macros. Since we're only
 * trying to help gcc, anyway, it's OK; other compilers will fall back to
 * using the function as usual.
 */
#if defined(__GNUC__)
static inline int const_error(void)
{
	return -1;
}
#define error(...) (error(__VA_ARGS__), const_error())
#define error_errno(...) (error_errno(__VA_ARGS__), const_error())
#endif

void set_die_routine(NORETURN_PTR void (*routine)(const char *err, va_list params));
void set_error_routine(void (*routine)(const char *err, va_list params));
extern void (*get_error_routine(void))(const char *err, va_list params);
void set_warn_routine(void (*routine)(const char *warn, va_list params));
extern void (*get_warn_routine(void))(const char *warn, va_list params);
void set_die_is_recursing_routine(int (*routine)(void));

int starts_with(const char *str, const char *prefix);
int istarts_with(const char *str, const char *prefix);

/*
 * If the string "str" begins with the string found in "prefix", return 1.
 * The "out" parameter is set to "str + strlen(prefix)" (i.e., to the point in
 * the string right after the prefix).
 *
 * Otherwise, return 0 and leave "out" untouched.
 *
 * Examples:
 *
 *   [extract branch name, fail if not a branch]
 *   if (!skip_prefix(ref, "refs/heads/", &branch)
 *	return -1;
 *
 *   [skip prefix if present, otherwise use whole string]
 *   skip_prefix(name, "refs/heads/", &name);
 */
static inline int skip_prefix(const char *str, const char *prefix,
			      const char **out)
{
	do {
		if (!*prefix) {
			*out = str;
			return 1;
		}
	} while (*str++ == *prefix++);
	return 0;
}

/*
 * If the string "str" is the same as the string in "prefix", then the "arg"
 * parameter is set to the "def" parameter and 1 is returned.
 * If the string "str" begins with the string found in "prefix" and then a
 * "=" sign, then the "arg" parameter is set to "str + strlen(prefix) + 1"
 * (i.e., to the point in the string right after the prefix and the "=" sign),
 * and 1 is returned.
 *
 * Otherwise, return 0 and leave "arg" untouched.
 *
 * When we accept both a "--key" and a "--key=<val>" option, this function
 * can be used instead of !strcmp(arg, "--key") and then
 * skip_prefix(arg, "--key=", &arg) to parse such an option.
 */
int skip_to_optional_arg_default(const char *str, const char *prefix,
				 const char **arg, const char *def);

static inline int skip_to_optional_arg(const char *str, const char *prefix,
				       const char **arg)
{
	return skip_to_optional_arg_default(str, prefix, arg, "");
}

/*
 * Like skip_prefix, but promises never to read past "len" bytes of the input
 * buffer, and returns the remaining number of bytes in "out" via "outlen".
 */
static inline int skip_prefix_mem(const char *buf, size_t len,
				  const char *prefix,
				  const char **out, size_t *outlen)
{
	size_t prefix_len = strlen(prefix);
	if (prefix_len <= len && !memcmp(buf, prefix, prefix_len)) {
		*out = buf + prefix_len;
		*outlen = len - prefix_len;
		return 1;
	}
	return 0;
}

/*
 * If buf ends with suffix, return 1 and subtract the length of the suffix
 * from *len. Otherwise, return 0 and leave *len untouched.
 */
static inline int strip_suffix_mem(const char *buf, size_t *len,
				   const char *suffix)
{
	size_t suflen = strlen(suffix);
	if (*len < suflen || memcmp(buf + (*len - suflen), suffix, suflen))
		return 0;
	*len -= suflen;
	return 1;
}

/*
 * If str ends with suffix, return 1 and set *len to the size of the string
 * without the suffix. Otherwise, return 0 and set *len to the size of the
 * string.
 *
 * Note that we do _not_ NUL-terminate str to the new length.
 */
static inline int strip_suffix(const char *str, const char *suffix, size_t *len)
{
	*len = strlen(str);
	return strip_suffix_mem(str, len, suffix);
}

static inline int ends_with(const char *str, const char *suffix)
{
	size_t len;
	return strip_suffix(str, suffix, &len);
}

#define SWAP(a, b) do {						\
	void *_swap_a_ptr = &(a);				\
	void *_swap_b_ptr = &(b);				\
	unsigned char _swap_buffer[sizeof(a)];			\
	memcpy(_swap_buffer, _swap_a_ptr, sizeof(a));		\
	memcpy(_swap_a_ptr, _swap_b_ptr, sizeof(a) +		\
	       BUILD_ASSERT_OR_ZERO(sizeof(a) == sizeof(b)));	\
	memcpy(_swap_b_ptr, _swap_buffer, sizeof(a));		\
} while (0)

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

#ifdef NO_MMAP

/* This value must be multiple of (pagesize * 2) */
#define DEFAULT_PACKED_GIT_WINDOW_SIZE (1 * 1024 * 1024)

#else /* NO_MMAP */

/* This value must be multiple of (pagesize * 2) */
#define DEFAULT_PACKED_GIT_WINDOW_SIZE \
	(sizeof(void*) >= 8 \
		?  1 * 1024 * 1024 * 1024 \
		: 32 * 1024 * 1024)

#endif /* NO_MMAP */

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#ifdef NO_ST_BLOCKS_IN_STRUCT_STAT
#define on_disk_bytes(st) ((st).st_size)
#else
#define on_disk_bytes(st) ((st).st_blocks * 512)
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

#define DEFAULT_PACKED_GIT_LIMIT \
	((1024L * 1024L) * (size_t)(sizeof(void*) >= 8 ? (32 * 1024L * 1024L) : 256))

#ifdef NO_PREAD
#define pread git_pread
ssize_t git_pread(int fd, void *buf, size_t count, off_t offset);
#endif
/*
 * Forward decl that will remind us if its twin in cache.h changes.
 * This function is used in compat/pread.c.  But we can't include
 * cache.h there.
 */
ssize_t read_in_full(int fd, void *buf, size_t count);

#ifdef NO_SETENV
#define setenv gitsetenv
int gitsetenv(const char *, const char *, int);
#endif

#ifdef NO_MKDTEMP
#define mkdtemp gitmkdtemp
char *gitmkdtemp(char *);
#endif

#ifdef NO_UNSETENV
#define unsetenv gitunsetenv
void gitunsetenv(const char *);
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

static inline size_t st_add(size_t a, size_t b)
{
	if (unsigned_add_overflows(a, b))
		die("size_t overflow: %"PRIuMAX" + %"PRIuMAX,
		    (uintmax_t)a, (uintmax_t)b);
	return a + b;
}
#define st_add3(a,b,c)   st_add(st_add((a),(b)),(c))
#define st_add4(a,b,c,d) st_add(st_add3((a),(b),(c)),(d))

static inline size_t st_mult(size_t a, size_t b)
{
	if (unsigned_mult_overflows(a, b))
		die("size_t overflow: %"PRIuMAX" * %"PRIuMAX,
		    (uintmax_t)a, (uintmax_t)b);
	return a * b;
}

static inline size_t st_sub(size_t a, size_t b)
{
	if (a < b)
		die("size_t underflow: %"PRIuMAX" - %"PRIuMAX,
		    (uintmax_t)a, (uintmax_t)b);
	return a - b;
}

#ifdef HAVE_ALLOCA_H
# include <alloca.h>
# define xalloca(size)      (alloca(size))
# define xalloca_free(p)    do {} while (0)
#else
# define xalloca(size)      (xmalloc(size))
# define xalloca_free(p)    (free(p))
#endif
char *xstrdup(const char *str);
void *xmalloc(size_t size);
void *xmallocz(size_t size);
void *xmallocz_gently(size_t size);
void *xmemdupz(const void *data, size_t len);
char *xstrndup(const char *str, size_t len);
void *xrealloc(void *ptr, size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xmmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
void *xmmap_gently(void *start, size_t length, int prot, int flags, int fd, off_t offset);
int xopen(const char *path, int flags, ...);
ssize_t xread(int fd, void *buf, size_t len);
ssize_t xwrite(int fd, const void *buf, size_t len);
ssize_t xpread(int fd, void *buf, size_t len, off_t offset);
int xdup(int fd);
FILE *xfopen(const char *path, const char *mode);
FILE *xfdopen(int fd, const char *mode);
int xmkstemp(char *temp_filename);
int xmkstemp_mode(char *temp_filename, int mode);
char *xgetcwd(void);
FILE *fopen_for_writing(const char *path);
FILE *fopen_or_warn(const char *path, const char *mode);

/*
 * Like strncmp, but only return zero if s is NUL-terminated and exactly len
 * characters long.  If it is not, consider it greater than t.
 */
int xstrncmpz(const char *s, const char *t, size_t len);

/*
 * FREE_AND_NULL(ptr) is like free(ptr) followed by ptr = NULL. Note
 * that ptr is used twice, so don't pass e.g. ptr++.
 */
#define FREE_AND_NULL(p) do { free(p); (p) = NULL; } while (0)

#define ALLOC_ARRAY(x, alloc) (x) = xmalloc(st_mult(sizeof(*(x)), (alloc)))
#define CALLOC_ARRAY(x, alloc) (x) = xcalloc((alloc), sizeof(*(x)));
#define REALLOC_ARRAY(x, alloc) (x) = xrealloc((x), st_mult(sizeof(*(x)), (alloc)))

#define COPY_ARRAY(dst, src, n) copy_array((dst), (src), (n), sizeof(*(dst)) + \
	BUILD_ASSERT_OR_ZERO(sizeof(*(dst)) == sizeof(*(src))))
static inline void copy_array(void *dst, const void *src, size_t n, size_t size)
{
	if (n)
		memcpy(dst, src, st_mult(size, n));
}

#define MOVE_ARRAY(dst, src, n) move_array((dst), (src), (n), sizeof(*(dst)) + \
	BUILD_ASSERT_OR_ZERO(sizeof(*(dst)) == sizeof(*(src))))
static inline void move_array(void *dst, const void *src, size_t n, size_t size)
{
	if (n)
		memmove(dst, src, st_mult(size, n));
}

/*
 * These functions help you allocate structs with flex arrays, and copy
 * the data directly into the array. For example, if you had:
 *
 *   struct foo {
 *     int bar;
 *     char name[FLEX_ARRAY];
 *   };
 *
 * you can do:
 *
 *   struct foo *f;
 *   FLEX_ALLOC_MEM(f, name, src, len);
 *
 * to allocate a "foo" with the contents of "src" in the "name" field.
 * The resulting struct is automatically zero'd, and the flex-array field
 * is NUL-terminated (whether the incoming src buffer was or not).
 *
 * The FLEXPTR_* variants operate on structs that don't use flex-arrays,
 * but do want to store a pointer to some extra data in the same allocated
 * block. For example, if you have:
 *
 *   struct foo {
 *     char *name;
 *     int bar;
 *   };
 *
 * you can do:
 *
 *   struct foo *f;
 *   FLEXPTR_ALLOC_STR(f, name, src);
 *
 * and "name" will point to a block of memory after the struct, which will be
 * freed along with the struct (but the pointer can be repointed anywhere).
 *
 * The *_STR variants accept a string parameter rather than a ptr/len
 * combination.
 *
 * Note that these macros will evaluate the first parameter multiple
 * times, and it must be assignable as an lvalue.
 */
#define FLEX_ALLOC_MEM(x, flexname, buf, len) do { \
	size_t flex_array_len_ = (len); \
	(x) = xcalloc(1, st_add3(sizeof(*(x)), flex_array_len_, 1)); \
	memcpy((void *)(x)->flexname, (buf), flex_array_len_); \
} while (0)
#define FLEXPTR_ALLOC_MEM(x, ptrname, buf, len) do { \
	size_t flex_array_len_ = (len); \
	(x) = xcalloc(1, st_add3(sizeof(*(x)), flex_array_len_, 1)); \
	memcpy((x) + 1, (buf), flex_array_len_); \
	(x)->ptrname = (void *)((x)+1); \
} while(0)
#define FLEX_ALLOC_STR(x, flexname, str) \
	FLEX_ALLOC_MEM((x), flexname, (str), strlen(str))
#define FLEXPTR_ALLOC_STR(x, ptrname, str) \
	FLEXPTR_ALLOC_MEM((x), ptrname, (str), strlen(str))

static inline char *xstrdup_or_null(const char *str)
{
	return str ? xstrdup(str) : NULL;
}

static inline size_t xsize_t(off_t len)
{
	size_t size = (size_t) len;

	if (len != (off_t) size)
		die("Cannot handle files this big");
	return size;
}

__attribute__((format (printf, 3, 4)))
int xsnprintf(char *dst, size_t max, const char *fmt, ...);

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

int xgethostname(char *buf, size_t len);

/* in ctype.c, for kwset users */
extern const unsigned char tolower_trans_tbl[256];

/* Sane ctype - no locale, and works with signed chars */
#undef isascii
#undef isspace
#undef isdigit
#undef isalpha
#undef isalnum
#undef isprint
#undef islower
#undef isupper
#undef tolower
#undef toupper
#undef iscntrl
#undef ispunct
#undef isxdigit

extern const unsigned char sane_ctype[256];
#define GIT_SPACE 0x01
#define GIT_DIGIT 0x02
#define GIT_ALPHA 0x04
#define GIT_GLOB_SPECIAL 0x08
#define GIT_REGEX_SPECIAL 0x10
#define GIT_PATHSPEC_MAGIC 0x20
#define GIT_CNTRL 0x40
#define GIT_PUNCT 0x80
#define sane_istest(x,mask) ((sane_ctype[(unsigned char)(x)] & (mask)) != 0)
#define isascii(x) (((x) & ~0x7f) == 0)
#define isspace(x) sane_istest(x,GIT_SPACE)
#define isdigit(x) sane_istest(x,GIT_DIGIT)
#define isalpha(x) sane_istest(x,GIT_ALPHA)
#define isalnum(x) sane_istest(x,GIT_ALPHA | GIT_DIGIT)
#define isprint(x) ((x) >= 0x20 && (x) <= 0x7e)
#define islower(x) sane_iscase(x, 1)
#define isupper(x) sane_iscase(x, 0)
#define is_glob_special(x) sane_istest(x,GIT_GLOB_SPECIAL)
#define is_regex_special(x) sane_istest(x,GIT_GLOB_SPECIAL | GIT_REGEX_SPECIAL)
#define iscntrl(x) (sane_istest(x,GIT_CNTRL))
#define ispunct(x) sane_istest(x, GIT_PUNCT | GIT_REGEX_SPECIAL | \
		GIT_GLOB_SPECIAL | GIT_PATHSPEC_MAGIC)
#define isxdigit(x) (hexval_table[(unsigned char)(x)] != -1)
#define tolower(x) sane_case((unsigned char)(x), 0x20)
#define toupper(x) sane_case((unsigned char)(x), 0)
#define is_pathspec_magic(x) sane_istest(x,GIT_PATHSPEC_MAGIC)

static inline int sane_case(int x, int high)
{
	if (sane_istest(x, GIT_ALPHA))
		x = (x & ~0x20) | high;
	return x;
}

static inline int sane_iscase(int x, int is_lower)
{
	if (!sane_istest(x, GIT_ALPHA))
		return 0;

	if (is_lower)
		return (x & 0x20) != 0;
	else
		return (x & 0x20) == 0;
}

/*
 * Like skip_prefix, but compare case-insensitively. Note that the comparison
 * is done via tolower(), so it is strictly ASCII (no multi-byte characters or
 * locale-specific conversions).
 */
static inline int skip_iprefix(const char *str, const char *prefix,
			       const char **out)
{
	do {
		if (!*prefix) {
			*out = str;
			return 1;
		}
	} while (tolower(*str++) == tolower(*prefix++));
	return 0;
}

static inline int strtoul_ui(char const *s, int base, unsigned int *result)
{
	unsigned long ul;
	char *p;

	errno = 0;
	/* negative values would be accepted by strtoul */
	if (strchr(s, '-'))
		return -1;
	ul = strtoul(s, &p, base);
	if (errno || *p || p == s || (unsigned int) ul != ul)
		return -1;
	*result = ul;
	return 0;
}

static inline int strtol_i(char const *s, int base, int *result)
{
	long ul;
	char *p;

	errno = 0;
	ul = strtol(s, &p, base);
	if (errno || *p || p == s || (int) ul != ul)
		return -1;
	*result = ul;
	return 0;
}

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

#ifndef REG_STARTEND
#error "Git requires REG_STARTEND support. Compile with NO_REGEX=NeedsStartEnd"
#endif

static inline int regexec_buf(const regex_t *preg, const char *buf, size_t size,
			      size_t nmatch, regmatch_t pmatch[], int eflags)
{
	assert(nmatch > 0 && pmatch);
	pmatch[0].rm_so = 0;
	pmatch[0].rm_eo = size;
	return regexec(preg, buf, nmatch, pmatch, eflags | REG_STARTEND);
}

#ifndef DIR_HAS_BSD_GROUP_SEMANTICS
# define FORCE_DIR_SET_GID S_ISGID
#else
# define FORCE_DIR_SET_GID 0
#endif

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

#ifdef UNRELIABLE_FSTAT
#define fstat_is_reliable() 0
#else
#define fstat_is_reliable() 1
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

#if defined(__GNUC__) || (_MSC_VER >= 1400) || defined(__C99_MACRO_WITH_VA_ARGS)
#define HAVE_VARIADIC_MACROS 1
#endif

/* usage.c: only to be used for testing BUG() implementation (see test-tool) */
extern int BUG_exit_code;

#ifdef HAVE_VARIADIC_MACROS
__attribute__((format (printf, 3, 4))) NORETURN
void BUG_fl(const char *file, int line, const char *fmt, ...);
#define BUG(...) BUG_fl(__FILE__, __LINE__, __VA_ARGS__)
#else
__attribute__((format (printf, 1, 2))) NORETURN
void BUG(const char *fmt, ...);
#endif

/*
 * Preserves errno, prints a message, but gives no warning for ENOENT.
 * Returns 0 on success, which includes trying to unlink an object that does
 * not exist.
 */
int unlink_or_warn(const char *path);
 /*
  * Tries to unlink file.  Returns 0 if unlink succeeded
  * or the file already didn't exist.  Returns -1 and
  * appends a message to err suitable for
  * 'error("%s", err->buf)' on error.
  */
int unlink_or_msg(const char *file, struct strbuf *err);
/*
 * Preserves errno, prints a message, but gives no warning for ENOENT.
 * Returns 0 on success, which includes trying to remove a directory that does
 * not exist.
 */
int rmdir_or_warn(const char *path);
/*
 * Calls the correct function out of {unlink,rmdir}_or_warn based on
 * the supplied file mode.
 */
int remove_or_warn(unsigned int mode, const char *path);

/*
 * Call access(2), but warn for any error except "missing file"
 * (ENOENT or ENOTDIR).
 */
#define ACCESS_EACCES_OK (1U << 0)
int access_or_warn(const char *path, int mode, unsigned flag);
int access_or_die(const char *path, int mode, unsigned flag);

/* Warn on an inaccessible file if errno indicates this is an error */
int warn_on_fopen_errors(const char *path);

#if !defined(USE_PARENS_AROUND_GETTEXT_N) && defined(__GNUC__)
#define USE_PARENS_AROUND_GETTEXT_N 1
#endif

#ifndef SHELL_PATH
# define SHELL_PATH "/bin/sh"
#endif

#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
static inline void flockfile(FILE *fh)
{
	; /* nothing */
}
static inline void funlockfile(FILE *fh)
{
	; /* nothing */
}
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

/*
 * Our code often opens a path to an optional file, to work on its
 * contents when we can successfully open it.  We can ignore a failure
 * to open if such an optional file does not exist, but we do want to
 * report a failure in opening for other reasons (e.g. we got an I/O
 * error, or the file is there, but we lack the permission to open).
 *
 * Call this function after seeing an error from open() or fopen() to
 * see if the errno indicates a missing file that we can safely ignore.
 */
static inline int is_missing_file_error(int errno_)
{
	return (errno_ == ENOENT || errno_ == ENOTDIR);
}

/*
 * Enable/disable a read-only cache for file system data on platforms that
 * support it.
 *
 * Implementing a live-cache is complicated and requires special platform
 * support (inotify, ReadDirectoryChangesW...). enable_fscache shall be used
 * to mark sections of git code that extensively read from the file system
 * without modifying anything. Implementations can use this to cache e.g. stat
 * data or even file content without the need to synchronize with the file
 * system.
 */
#ifndef enable_fscache
#define enable_fscache(x) /* noop */
#endif

#ifndef disable_fscache
#define disable_fscache() /* noop */
#endif

#ifndef is_fscache_enabled
#define is_fscache_enabled(path) (0)
#endif

#ifndef flush_fscache
#define flush_fscache() /* noop */
#endif

int cmd_main(int, const char **);

/*
 * Intercept all calls to exit() and route them to trace2 to
 * optionally emit a message before calling the real exit().
 */
int trace2_cmd_exit_fl(const char *file, int line, int code);
#define exit(code) exit(trace2_cmd_exit_fl(__FILE__, __LINE__, (code)))

/*
 * You can mark a stack variable with UNLEAK(var) to avoid it being
 * reported as a leak by tools like LSAN or valgrind. The argument
 * should generally be the variable itself (not its address and not what
 * it points to). It's safe to use this on pointers which may already
 * have been freed, or on pointers which may still be in use.
 *
 * Use this _only_ for a variable that leaks by going out of scope at
 * program exit (so only from cmd_* functions or their direct helpers).
 * Normal functions, especially those which may be called multiple
 * times, should actually free their memory. This is only meant as
 * an annotation, and does nothing in non-leak-checking builds.
 */
#ifdef SUPPRESS_ANNOTATED_LEAKS
void unleak_memory(const void *ptr, size_t len);
#define UNLEAK(var) unleak_memory(&(var), sizeof(var))
#else
#define UNLEAK(var) do {} while (0)
#endif

/*
 * This include must come after system headers, since it introduces macros that
 * replace system names.
 */
#include "banned.h"

/*
 * container_of - Get the address of an object containing a field.
 *
 * @ptr: pointer to the field.
 * @type: type of the object.
 * @member: name of the field within the object.
 */
#define container_of(ptr, type, member) \
	((type *) ((char *)(ptr) - offsetof(type, member)))

/*
 * helper function for `container_of_or_null' to avoid multiple
 * evaluation of @ptr
 */
static inline void *container_of_or_null_offset(void *ptr, size_t offset)
{
	return ptr ? (char *)ptr - offset : NULL;
}

/*
 * like `container_of', but allows returned value to be NULL
 */
#define container_of_or_null(ptr, type, member) \
	(type *)container_of_or_null_offset(ptr, offsetof(type, member))

/*
 * like offsetof(), but takes a pointer to a a variable of type which
 * contains @member, instead of a specified type.
 * @ptr is subject to multiple evaluation since we can't rely on __typeof__
 * everywhere.
 */
#if defined(__GNUC__) /* clang sets this, too */
#define OFFSETOF_VAR(ptr, member) offsetof(__typeof__(*ptr), member)
#else /* !__GNUC__ */
#define OFFSETOF_VAR(ptr, member) \
	((uintptr_t)&(ptr)->member - (uintptr_t)(ptr))
#endif /* !__GNUC__ */

#endif
