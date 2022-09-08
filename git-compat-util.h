#ifndef GIT_COMPAT_UTIL_H
#define GIT_COMPAT_UTIL_H

#if __STDC_VERSION__ - 0 < 199901L
/*
 * Git is in a testing period for mandatory C99 support in the compiler.  If
 * your compiler is reasonably recent, you can try to enable C99 support (or,
 * for MSVC, C11 support).  If you encounter a problem and can't enable C99
 * support with your compiler (such as with "-std=gnu99") and don't have access
 * to one with this support, such as GCC or Clang, you can remove this #if
 * directive, but please report the details of your system to
 * git@vger.kernel.org.
 */
#error "Required C99 support is in a test phase.  Please see git-compat-util.h for more details."
#endif

#ifdef USE_MSVC_CRTDBG
/*
 * For these to work they must appear very early in each
 * file -- before most of the standard header files.
 */
#include <stdlib.h>
#include <crtdbg.h>
#endif

struct strbuf;


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

/*
 * Check vendor specific quirks first, before checking the
 * __STDC_VERSION__, as vendor compilers can lie and we need to be
 * able to work them around.  Note that by not defining FLEX_ARRAY
 * here, we can fall back to use the "safer but a bit wasteful" one
 * later.
 */
#if defined(__SUNPRO_C) && (__SUNPRO_C <= 0x580)
#elif defined(__GNUC__)
# if (__GNUC__ >= 3)
#  define FLEX_ARRAY /* empty */
# else
#  define FLEX_ARRAY 0 /* older GNU extension */
# endif
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
# define FLEX_ARRAY /* empty */
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
# define BARF_UNLESS_COPYABLE(dst, src) \
	BUILD_ASSERT_OR_ZERO(__builtin_types_compatible_p(__typeof__(*(dst)), \
							  __typeof__(*(src))))
#else
# define BARF_UNLESS_AN_ARRAY(arr) 0
# define BARF_UNLESS_COPYABLE(dst, src) \
	BUILD_ASSERT_OR_ZERO(0 ? ((*(dst) = *(src)), 0) : \
				 sizeof(*(dst)) == sizeof(*(src)))
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

/*
 * Returns true if the left shift of "a" by "shift" bits will
 * overflow. The type of "a" must be unsigned.
 */
#define unsigned_left_shift_overflows(a, shift) \
    ((shift) < bitsizeof(a) && \
     (a) > maximum_unsigned_value_of_type(a) >> (shift))

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
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#define _NETBSD_SOURCE 1
#define _SGI_SOURCE 1

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

#if defined(NO_UNIX_SOCKETS) || !defined(GIT_WINDOWS_NATIVE)
static inline int _have_unix_sockets(void)
{
#if defined(NO_UNIX_SOCKETS)
	return 0;
#else
	return 1;
#endif
}
#define have_unix_sockets _have_unix_sockets
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

/* Used by compat/win32/path-utils.h, and more */
static inline int is_xplatform_dir_sep(int c)
{
	return c == '/' || c == '\\';
}

#if defined(__CYGWIN__)
#include "compat/win32/path-utils.h"
#endif
#if defined(__MINGW32__)
/* pull in Windows compatibility stuff */
#include "compat/win32/path-utils.h"
#include "compat/mingw.h"
#elif defined(_MSC_VER)
#include "compat/win32/path-utils.h"
#include "compat/msvc.h"
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

/* used on Mac OS X */
#ifdef PRECOMPOSE_UNICODE
#include "compat/precompose_utf8.h"
#else
static inline const char *precompose_argv_prefix(int argc UNUSED,
						 const char **argv UNUSED,
						 const char *prefix)
{
	return prefix;
}
static inline const char *precompose_string_if_needed(const char *in)
{
	return in;
}

#define probe_utf8_pathname_composition()
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

#ifndef NO_OPENSSL
#ifdef __APPLE__
#undef __AVAILABILITY_MACROS_USES_AVAILABILITY
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

#ifdef USE_MIMALLOC
#include "mimalloc.h"
#define malloc mi_malloc
#define calloc mi_calloc
#define realloc mi_realloc
#define free mi_free
#define strdup mi_strdup
#define strndup mi_strndup
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
struct config_context;
static inline int noop_core_config(const char *var UNUSED,
				   const char *value UNUSED,
				   const struct config_context *ctx UNUSED,
				   void *cb UNUSED)
{
	return 0;
}
#define platform_core_config noop_core_config
#endif

int lstat_cache_aware_rmdir(const char *path);
#if !defined(__MINGW32__) && !defined(_MSC_VER)
#define rmdir lstat_cache_aware_rmdir
#endif

#ifndef has_dos_drive_prefix
static inline int git_has_dos_drive_prefix(const char *path UNUSED)
{
	return 0;
}
#define has_dos_drive_prefix git_has_dos_drive_prefix
#endif

#ifndef skip_dos_drive_prefix
static inline int git_skip_dos_drive_prefix(char **path UNUSED)
{
	return 0;
}
#define skip_dos_drive_prefix git_skip_dos_drive_prefix
#endif

static inline int git_is_dir_sep(int c)
{
	return c == '/';
}
#ifndef is_dir_sep
#define is_dir_sep git_is_dir_sep
#endif

#ifndef offset_1st_component
static inline int git_offset_1st_component(const char *path)
{
	return is_dir_sep(path[0]);
}
#define offset_1st_component git_offset_1st_component
#endif

#ifndef fspathcmp
#define fspathcmp git_fspathcmp
#endif

#ifndef fspathncmp
#define fspathncmp git_fspathncmp
#endif

#ifndef is_valid_path
#define is_valid_path(path) 1
#endif

#ifndef is_path_owned_by_current_user

#ifdef __TANDEM
#define ROOT_UID 65535
#else
#define ROOT_UID 0
#endif

/*
 * Do not use this function when
 * (1) geteuid() did not say we are running as 'root', or
 * (2) using this function will compromise the system.
 *
 * PORTABILITY WARNING:
 * This code assumes uid_t is unsigned because that is what sudo does.
 * If your uid_t type is signed and all your ids are positive then it
 * should all work fine.
 * If your version of sudo uses negative values for uid_t or it is
 * buggy and return an overflowed value in SUDO_UID, then git might
 * fail to grant access to your repository properly or even mistakenly
 * grant access to someone else.
 * In the unlikely scenario this happened to you, and that is how you
 * got to this message, we would like to know about it; so sent us an
 * email to git@vger.kernel.org indicating which platform you are
 * using and which version of sudo, so we can improve this logic and
 * maybe provide you with a patch that would prevent this issue again
 * in the future.
 */
static inline void extract_id_from_env(const char *env, uid_t *id)
{
	const char *real_uid = getenv(env);

	/* discard anything empty to avoid a more complex check below */
	if (real_uid && *real_uid) {
		char *endptr = NULL;
		unsigned long env_id;

		errno = 0;
		/* silent overflow errors could trigger a bug here */
		env_id = strtoul(real_uid, &endptr, 10);
		if (!*endptr && !errno)
			*id = env_id;
	}
}

static inline int is_path_owned_by_current_uid(const char *path,
					       struct strbuf *report UNUSED)
{
	struct stat st;
	uid_t euid;

	if (lstat(path, &st))
		return 0;

	euid = geteuid();
	if (euid == ROOT_UID)
	{
		if (st.st_uid == ROOT_UID)
			return 1;
		else
			extract_id_from_env("SUDO_UID", &euid);
	}

	return st.st_uid == euid;
}

#define is_path_owned_by_current_user is_path_owned_by_current_uid
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
/* warn_unused_result exists as of gcc 3.4.0, but be lazy and check 4.0 */
#define RESULT_MUST_BE_USED __attribute__ ((warn_unused_result))
#else
#define LAST_ARG_MUST_BE_NULL
#define RESULT_MUST_BE_USED
#endif

/*
 * MAYBE_UNUSED marks a function parameter that may be unused, but
 * whose use is not an error.  It also can be used to annotate a
 * function, a variable, or a type that may be unused.
 *
 * Depending on a configuration, all uses of such a thing may become
 * #ifdef'ed away.  Marking it with UNUSED would give a warning in a
 * compilation where it is indeed used, and not marking it at all
 * would give a warning in a compilation where it is unused.  In such
 * a case, MAYBE_UNUSED is the appropriate annotation to use.
 */
#define MAYBE_UNUSED __attribute__((__unused__))

#include "compat/bswap.h"

#include "wrapper.h"

/* General helper functions */
NORETURN void usage(const char *err);
NORETURN void usagef(const char *err, ...) __attribute__((format (printf, 1, 2)));
NORETURN void die(const char *err, ...) __attribute__((format (printf, 1, 2)));
NORETURN void die_errno(const char *err, ...) __attribute__((format (printf, 1, 2)));
int die_message(const char *err, ...) __attribute__((format (printf, 1, 2)));
int die_message_errno(const char *err, ...) __attribute__((format (printf, 1, 2)));
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

#ifdef HAVE_OPENSSL_CSPRNG
#include <openssl/rand.h>
#endif

/*
 * Let callers be aware of the constant return value; this can help
 * gcc with -Wuninitialized analysis. We restrict this trick to gcc, though,
 * because other compilers may be confused by this.
 */
#if defined(__GNUC__)
static inline int const_error(void)
{
	return -1;
}
#define error(...) (error(__VA_ARGS__), const_error())
#define error_errno(...) (error_errno(__VA_ARGS__), const_error())
#endif

typedef void (*report_fn)(const char *, va_list params);

void set_die_routine(NORETURN_PTR report_fn routine);
report_fn get_die_message_routine(void);
void set_error_routine(report_fn routine);
report_fn get_error_routine(void);
void set_warn_routine(report_fn routine);
report_fn get_warn_routine(void);
void set_die_is_recursing_routine(int (*routine)(void));

/*
 * If the string "str" begins with the string found in "prefix", return true.
 * The "out" parameter is set to "str + strlen(prefix)" (i.e., to the point in
 * the string right after the prefix).
 *
 * Otherwise, return false and leave "out" untouched.
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
static inline bool skip_prefix(const char *str, const char *prefix,
			       const char **out)
{
	do {
		if (!*prefix) {
			*out = str;
			return true;
		}
	} while (*str++ == *prefix++);
	return false;
}

/*
 * Like skip_prefix, but promises never to read past "len" bytes of the input
 * buffer, and returns the remaining number of bytes in "out" via "outlen".
 */
static inline bool skip_prefix_mem(const char *buf, size_t len,
				   const char *prefix,
				   const char **out, size_t *outlen)
{
	size_t prefix_len = strlen(prefix);
	if (prefix_len <= len && !memcmp(buf, prefix, prefix_len)) {
		*out = buf + prefix_len;
		*outlen = len - prefix_len;
		return true;
	}
	return false;
}

/*
 * If buf ends with suffix, return true and subtract the length of the suffix
 * from *len. Otherwise, return false and leave *len untouched.
 */
static inline bool strip_suffix_mem(const char *buf, size_t *len,
				    const char *suffix)
{
	size_t suflen = strlen(suffix);
	if (*len < suflen || memcmp(buf + (*len - suflen), suffix, suflen))
		return false;
	*len -= suflen;
	return true;
}

/*
 * If str ends with suffix, return true and set *len to the size of the string
 * without the suffix. Otherwise, return false and set *len to the size of the
 * string.
 *
 * Note that we do _not_ NUL-terminate str to the new length.
 */
static inline bool strip_suffix(const char *str, const char *suffix,
				size_t *len)
{
	*len = strlen(str);
	return strip_suffix_mem(str, len, suffix);
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

static inline size_t st_left_shift(size_t a, unsigned shift)
{
	if (unsigned_left_shift_overflows(a, shift))
		die("size_t overflow: %"PRIuMAX" << %u",
		    (uintmax_t)a, shift);
	return a << shift;
}

static inline unsigned long cast_size_t_to_ulong(size_t a)
{
	if (a != (unsigned long)a)
		die("object too large to read on this platform: %"
		    PRIuMAX" is cut off to %lu",
		    (uintmax_t)a, (unsigned long)a);
	return (unsigned long)a;
}

static inline uint32_t cast_size_t_to_uint32_t(size_t a)
{
	if (a != (uint32_t)a)
		die("object too large to read on this platform: %"
		    PRIuMAX" is cut off to %u",
		    (uintmax_t)a, (uint32_t)a);
	return (uint32_t)a;
}

static inline int cast_size_t_to_int(size_t a)
{
	if (a > INT_MAX)
		die("number too large to represent as int on this platform: %"PRIuMAX,
		    (uintmax_t)a);
	return (int)a;
}

/*
 * Limit size of IO chunks, because huge chunks only cause pain.  OS X
 * 64-bit is buggy, returning EINVAL if len >= INT_MAX; and even in
 * the absence of bugs, large chunks can result in bad latencies when
 * you decide to kill the process.
 *
 * We pick 8 MiB as our default, but if the platform defines SSIZE_MAX
 * that is smaller than that, clip it to SSIZE_MAX, as a call to
 * read(2) or write(2) larger than that is allowed to fail.  As the last
 * resort, we allow a port to pass via CFLAGS e.g. "-DMAX_IO_SIZE=value"
 * to override this, if the definition of SSIZE_MAX given by the platform
 * is broken.
 */
#ifndef MAX_IO_SIZE
# define MAX_IO_SIZE_DEFAULT (8*1024*1024)
# if defined(SSIZE_MAX) && (SSIZE_MAX < MAX_IO_SIZE_DEFAULT)
#  define MAX_IO_SIZE SSIZE_MAX
# else
#  define MAX_IO_SIZE MAX_IO_SIZE_DEFAULT
# endif
#endif

#ifdef HAVE_ALLOCA_H
# include <alloca.h>
# define xalloca(size)      (alloca(size))
# define xalloca_free(p)    do {} while (0)
#else
# define xalloca(size)      (xmalloc(size))
# define xalloca_free(p)    (free(p))
#endif

/*
 * FREE_AND_NULL(ptr) is like free(ptr) followed by ptr = NULL. Note
 * that ptr is used twice, so don't pass e.g. ptr++.
 */
#define FREE_AND_NULL(p) do { free(p); (p) = NULL; } while (0)

#define ALLOC_ARRAY(x, alloc) (x) = xmalloc(st_mult(sizeof(*(x)), (alloc)))
#define CALLOC_ARRAY(x, alloc) (x) = xcalloc((alloc), sizeof(*(x)))
#define REALLOC_ARRAY(x, alloc) (x) = xrealloc((x), st_mult(sizeof(*(x)), (alloc)))

#define COPY_ARRAY(dst, src, n) copy_array((dst), (src), (n), sizeof(*(dst)) + \
	BARF_UNLESS_COPYABLE((dst), (src)))
static inline void copy_array(void *dst, const void *src, size_t n, size_t size)
{
	if (n)
		memcpy(dst, src, st_mult(size, n));
}

#define MOVE_ARRAY(dst, src, n) move_array((dst), (src), (n), sizeof(*(dst)) + \
	BARF_UNLESS_COPYABLE((dst), (src)))
static inline void move_array(void *dst, const void *src, size_t n, size_t size)
{
	if (n)
		memmove(dst, src, st_mult(size, n));
}

#define DUP_ARRAY(dst, src, n) do { \
	size_t dup_array_n_ = (n); \
	COPY_ARRAY(ALLOC_ARRAY((dst), dup_array_n_), (src), dup_array_n_); \
} while (0)

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

#define alloc_nr(x) (((x)+16)*3/2)

/**
 * Dynamically growing an array using realloc() is error prone and boring.
 *
 * Define your array with:
 *
 * - a pointer (`item`) that points at the array, initialized to `NULL`
 *   (although please name the variable based on its contents, not on its
 *   type);
 *
 * - an integer variable (`alloc`) that keeps track of how big the current
 *   allocation is, initialized to `0`;
 *
 * - another integer variable (`nr`) to keep track of how many elements the
 *   array currently has, initialized to `0`.
 *
 * Then before adding `n`th element to the item, call `ALLOC_GROW(item, n,
 * alloc)`.  This ensures that the array can hold at least `n` elements by
 * calling `realloc(3)` and adjusting `alloc` variable.
 *
 * ------------
 * sometype *item;
 * size_t nr;
 * size_t alloc
 *
 * for (i = 0; i < nr; i++)
 * 	if (we like item[i] already)
 * 		return;
 *
 * // we did not like any existing one, so add one
 * ALLOC_GROW(item, nr + 1, alloc);
 * item[nr++] = value you like;
 * ------------
 *
 * You are responsible for updating the `nr` variable.
 *
 * If you need to specify the number of elements to allocate explicitly
 * then use the macro `REALLOC_ARRAY(item, alloc)` instead of `ALLOC_GROW`.
 *
 * Consider using ALLOC_GROW_BY instead of ALLOC_GROW as it has some
 * added niceties.
 *
 * DO NOT USE any expression with side-effect for 'x', 'nr', or 'alloc'.
 */
#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			REALLOC_ARRAY(x, alloc); \
		} \
	} while (0)

/*
 * Similar to ALLOC_GROW but handles updating of the nr value and
 * zeroing the bytes of the newly-grown array elements.
 *
 * DO NOT USE any expression with side-effect for any of the
 * arguments.
 */
#define ALLOC_GROW_BY(x, nr, increase, alloc) \
	do { \
		if (increase) { \
			size_t new_nr = nr + (increase); \
			if (new_nr < nr) \
				BUG("negative growth in ALLOC_GROW_BY"); \
			ALLOC_GROW(x, new_nr, alloc); \
			memset((x) + nr, 0, sizeof(*(x)) * (increase)); \
			nr = new_nr; \
		} \
	} while (0)

static inline char *xstrdup_or_null(const char *str)
{
	return str ? xstrdup(str) : NULL;
}

static inline size_t xsize_t(off_t len)
{
	if (len < 0 || (uintmax_t) len > SIZE_MAX)
		die("Cannot handle files this big");
	return (size_t) len;
}

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

#include "sane-ctype.h"

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

/*
 * Like skip_prefix_mem, but compare case-insensitively. Note that the
 * comparison is done via tolower(), so it is strictly ASCII (no multi-byte
 * characters or locale-specific conversions).
 */
static inline int skip_iprefix_mem(const char *buf, size_t len,
				   const char *prefix,
				   const char **out, size_t *outlen)
{
	do {
		if (!*prefix) {
			*out = buf;
			*outlen = len;
			return 1;
		}
	} while (len-- > 0 && tolower(*buf++) == tolower(*prefix++));
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

#ifdef USE_ENHANCED_BASIC_REGULAR_EXPRESSIONS
int git_regcomp(regex_t *preg, const char *pattern, int cflags);
#define regcomp git_regcomp
#endif

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

/* usage.c: only to be used for testing BUG() implementation (see test-tool) */
extern int BUG_exit_code;

/* usage.c: if bug() is called we should have a BUG_if_bug() afterwards */
extern int bug_called_must_BUG;

__attribute__((format (printf, 3, 4))) NORETURN
void BUG_fl(const char *file, int line, const char *fmt, ...);
#define BUG(...) BUG_fl(__FILE__, __LINE__, __VA_ARGS__)
__attribute__((format (printf, 3, 4)))
void bug_fl(const char *file, int line, const char *fmt, ...);
#define bug(...) bug_fl(__FILE__, __LINE__, __VA_ARGS__)
#define BUG_if_bug(...) do { \
	if (bug_called_must_BUG) \
		BUG_fl(__FILE__, __LINE__, __VA_ARGS__); \
} while (0)

#ifndef FSYNC_METHOD_DEFAULT
#ifdef __APPLE__
#define FSYNC_METHOD_DEFAULT FSYNC_METHOD_WRITEOUT_ONLY
#else
#define FSYNC_METHOD_DEFAULT FSYNC_METHOD_FSYNC
#endif
#endif

#ifndef SHELL_PATH
# define SHELL_PATH "/bin/sh"
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

int cmd_main(int, const char **);

/*
 * Intercept all calls to exit() and route them to trace2 to
 * optionally emit a message before calling the real exit().
 */
int common_exit(const char *file, int line, int code);
#define exit(code) exit(common_exit(__FILE__, __LINE__, (code)))

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

#define z_const
#include <zlib.h>

#if ZLIB_VERNUM < 0x1290
/*
 * This is uncompress2, which is only available in zlib >= 1.2.9
 * (released as of early 2017). See compat/zlib-uncompress2.c.
 */
int uncompress2(Bytef *dest, uLongf *destLen, const Bytef *source,
		uLong *sourceLen);
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
 * like offsetof(), but takes a pointer to a variable of type which
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
