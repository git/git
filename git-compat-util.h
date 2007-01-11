#ifndef GIT_COMPAT_UTIL_H
#define GIT_COMPAT_UTIL_H

#ifndef FLEX_ARRAY
#if defined(__GNUC__) && (__GNUC__ < 3)
#define FLEX_ARRAY 0
#else
#define FLEX_ARRAY /* empty */
#endif
#endif

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#if !defined(__APPLE__) && !defined(__FreeBSD__)
#define _XOPEN_SOURCE 600 /* glibc2 and AIX 5.3L need 500, OpenBSD needs 600 for S_ISLNK() */
#define _XOPEN_SOURCE_EXTENDED 1 /* AIX 5.3L needs this */
#endif
#define _ALL_SOURCE 1
#define _GNU_SOURCE 1
#define _BSD_SOURCE 1

#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
//#include <sys/wait.h>
#include <fnmatch.h>
//#include <sys/poll.h>
//#include <sys/socket.h>
#include <assert.h>
#include <regex.h>
//#include <netinet/in.h>
//#include <netinet/tcp.h>
//#include <arpa/inet.h>
#ifndef NO_ETC_PASSWD
#include <netdb.h>
#include <pwd.h>
#include <stdint.h>
#undef _ALL_SOURCE /* AIX 5.3L defines a struct list with _ALL_SOURCE. */
#include <grp.h>
#define _ALL_SOURCE 1
#endif

#ifndef NO_ICONV
#include <iconv.h>
#endif

/* On most systems <limits.h> would have given us this, but
 * not on some systems (e.g. GNU/Hurd).
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef __GNUC__
#define NORETURN __attribute__((__noreturn__))
#else
#define NORETURN
#ifndef __attribute__
#define __attribute__(x)
#endif
#endif

/* General helper functions */
extern void usage(const char *err) NORETURN;
extern void die(const char *err, ...) NORETURN __attribute__((format (printf, 1, 2)));
extern int error(const char *err, ...) __attribute__((format (printf, 1, 2)));
extern void warn(const char *err, ...) __attribute__((format (printf, 1, 2)));

extern void set_usage_routine(void (*routine)(const char *err) NORETURN);
extern void set_die_routine(void (*routine)(const char *err, va_list params) NORETURN);
extern void set_error_routine(void (*routine)(const char *err, va_list params));
extern void set_warn_routine(void (*routine)(const char *warn, va_list params));

#ifdef NO_MMAP

#ifndef PROT_READ
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 1
#define MAP_FAILED ((void*)-1)
#endif

#define mmap git_mmap
#define munmap git_munmap
extern void *git_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
extern int git_munmap(void *start, size_t length);

#define DEFAULT_PACKED_GIT_WINDOW_SIZE (1 * 1024 * 1024)

#else /* NO_MMAP */

#include <sys/mman.h>
#define DEFAULT_PACKED_GIT_WINDOW_SIZE \
	(sizeof(void*) >= 8 \
		?  1 * 1024 * 1024 * 1024 \
		: 32 * 1024 * 1024)

#endif /* NO_MMAP */

#define DEFAULT_PACKED_GIT_LIMIT \
	((1024L * 1024L) * (sizeof(void*) >= 8 ? 8192 : 256))

#ifdef NO_PREAD
#define pread git_pread
extern ssize_t git_pread(int fd, void *buf, size_t count, off_t offset);
#endif

#ifdef NO_SETENV
#define setenv gitsetenv
extern int gitsetenv(const char *, const char *, int);
#endif

#ifdef NO_UNSETENV
#define unsetenv gitunsetenv
extern void gitunsetenv(const char *);
#endif

#ifdef NO_STRCASESTR
#define strcasestr gitstrcasestr
extern char *gitstrcasestr(const char *haystack, const char *needle);
#endif

#ifdef NO_STRLCPY
#define strlcpy gitstrlcpy
extern size_t gitstrlcpy(char *, const char *, size_t);
#endif

extern void release_pack_memory(size_t);

static inline char* xstrdup(const char *str)
{
	char *ret = strdup(str);
	if (!ret) {
		release_pack_memory(strlen(str) + 1);
		ret = strdup(str);
		if (!ret)
			die("Out of memory, strdup failed");
	}
	return ret;
}

static inline void *xmalloc(size_t size)
{
	void *ret = malloc(size);
	if (!ret && !size)
		ret = malloc(1);
	if (!ret) {
		release_pack_memory(size);
		ret = malloc(size);
		if (!ret && !size)
			ret = malloc(1);
		if (!ret)
			die("Out of memory, malloc failed");
	}
#ifdef XMALLOC_POISON
	memset(ret, 0xA5, size);
#endif
	return ret;
}

static inline void *xrealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (!ret && !size)
		ret = realloc(ptr, 1);
	if (!ret) {
		release_pack_memory(size);
		ret = realloc(ptr, size);
		if (!ret && !size)
			ret = realloc(ptr, 1);
		if (!ret)
			die("Out of memory, realloc failed");
	}
	return ret;
}

static inline void *xcalloc(size_t nmemb, size_t size)
{
	void *ret = calloc(nmemb, size);
	if (!ret && (!nmemb || !size))
		ret = calloc(1, 1);
	if (!ret) {
		release_pack_memory(nmemb * size);
		ret = calloc(nmemb, size);
		if (!ret && (!nmemb || !size))
			ret = calloc(1, 1);
		if (!ret)
			die("Out of memory, calloc failed");
	}
	return ret;
}

static inline void *xmmap(void *start, size_t length,
	int prot, int flags, int fd, off_t offset)
{
	void *ret = mmap(start, length, prot, flags, fd, offset);
	if (ret == MAP_FAILED) {
		if (!length)
			return NULL;
		release_pack_memory(length);
		ret = mmap(start, length, prot, flags, fd, offset);
		if (ret == MAP_FAILED)
			die("Out of memory? mmap failed: %s", strerror(errno));
	}
	return ret;
}

static inline ssize_t xread(int fd, void *buf, size_t len)
{
	ssize_t nr;
	while (1) {
		nr = read(fd, buf, len);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

static inline ssize_t xwrite(int fd, const void *buf, size_t len)
{
	ssize_t nr;
	while (1) {
		nr = write(fd, buf, len);
		if ((nr < 0) && (errno == EAGAIN || errno == EINTR))
			continue;
		return nr;
	}
}

static inline int has_extension(const char *filename, const char *ext)
{
	size_t len = strlen(filename);
	size_t extlen = strlen(ext);
	return len > extlen && !memcmp(filename + len - extlen, ext, extlen);
}

/* Sane ctype - no locale, and works with signed chars */
#undef isspace
#undef isdigit
#undef isalpha
#undef isalnum
#undef tolower
#undef toupper
extern unsigned char sane_ctype[256];
#define GIT_SPACE 0x01
#define GIT_DIGIT 0x02
#define GIT_ALPHA 0x04
#define sane_istest(x,mask) ((sane_ctype[(unsigned char)(x)] & (mask)) != 0)
#define isspace(x) sane_istest(x,GIT_SPACE)
#define isdigit(x) sane_istest(x,GIT_DIGIT)
#define isalpha(x) sane_istest(x,GIT_ALPHA)
#define isalnum(x) sane_istest(x,GIT_ALPHA | GIT_DIGIT)
#define tolower(x) sane_case((unsigned char)(x), 0x20)
#define toupper(x) sane_case((unsigned char)(x), 0)

static inline int sane_case(int x, int high)
{
	if (sane_istest(x, GIT_ALPHA))
		x = (x & ~0x20) | high;
	return x;
}

// MinGW

#ifndef S_ISLNK
#define S_ISLNK(x) 0
#define S_IFLNK 0
#endif

#ifndef S_ISGRP
#define S_ISGRP(x) 0
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_ISGID 0
#define S_IROTH 0
#define S_IXOTH 0
#endif

int readlink(const char *path, char *buf, size_t bufsiz);
int symlink(const char *oldpath, const char *newpath);
#define link symlink
int fchmod(int fildes, mode_t mode);
int lstat(const char *file_name, struct stat *buf);

/* missing: link, mkstemp, fchmod, getuid (?), gettimeofday */
int socketpair(int d, int type, int protocol, int sv[2]);
#define AF_UNIX 0
#define SOCK_STREAM 0
int syslog(int type, char *bufp, ...);
#define LOG_ERR 1
#define LOG_INFO 2
#define LOG_DAEMON 4
unsigned int alarm(unsigned int seconds);
#include <winsock2.h>
void mingw_execve(const char *cmd, const char **argv, const char **env);
#define execve mingw_execve
int fork();
typedef int pid_t;
#define waitpid(pid, status, options) \
	((options == 0) ? _cwait((status), (pid), 0) \
		: (errno = EINVAL, -1))
#define WIFEXITED(x) ((unsigned)(x) < 259)	/* STILL_ACTIVE */
#define WEXITSTATUS(x) ((x) & 0xff)
#define WIFSIGNALED(x) ((unsigned)(x) > 259)
#define WTERMSIG(x) (x)
#define WNOHANG 1
#define SIGKILL 0
#define SIGCHLD 0
#define SIGALRM 0
#define ECONNABORTED 0

int kill(pid_t pid, int sig);
unsigned int sleep (unsigned int __seconds);
const char *inet_ntop(int af, const void *src,
                             char *dst, size_t cnt);
int mkstemp (char *__template);
int gettimeofday(struct timeval *tv, void *tz);
int pipe(int filedes[2]);

struct pollfd {
	int fd;           /* file descriptor */
	short events;     /* requested events */
	short revents;    /* returned events */
};
int poll(struct pollfd *ufds, unsigned int nfds, int timeout);
#define POLLIN 1
#define POLLHUP 2

typedef int siginfo_t;
struct sigaction {
	void (*sa_handler)(int);
	void (*sa_sigaction)(int, siginfo_t *, void *);
	sigset_t sa_mask;
	int sa_flags;
	void (*sa_restorer)(void);
};
#define SA_RESTART 0
#define ITIMER_REAL 0

struct itimerval { struct timeval it_interval, it_value; };

static inline int git_mkdir(const char *path, int mode)
{
	return mkdir(path);
}
#define mkdir git_mkdir

#include <time.h>
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
#define hstrerror strerror

char *mingw_getcwd(char *pointer, int len);
#define getcwd mingw_getcwd

#define setlinebuf(x)
#define fsync(x)

extern void quote_argv(const char **dst, const char **src);
extern const char *parse_interpreter(const char *cmd);

#endif
