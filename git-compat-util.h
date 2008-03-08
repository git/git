#ifndef GIT_COMPAT_UTIL_H
#define GIT_COMPAT_UTIL_H

#define _FILE_OFFSET_BITS 64

#ifndef FLEX_ARRAY
/*
 * See if our compiler is known to support flexible array members.
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
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

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#ifdef __GNUC__
#define TYPEOF(x) (__typeof__(x))
#else
#define TYPEOF(x)
#endif

#define MSB(x, bits) ((x) & TYPEOF(x)(~0ULL << (sizeof(x) * 8 - (bits))))
#define HAS_MULTI_BITS(i)  ((i) & ((i) - 1))  /* checks if an integer has more than 1 bit set */

/* Approximation of the length of the decimal representation of this type. */
#define decimal_length(x)	((int)(sizeof(x) * 2.56 + 0.5) + 1)

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
#include <fnmatch.h>
#include <assert.h>
#include <regex.h>
#ifndef __MINGW32__
#include <sys/wait.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#ifndef NO_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h>
#include <inttypes.h>
#if defined(__CYGWIN__)
#undef _XOPEN_SOURCE
#include <grp.h>
#define _XOPEN_SOURCE 600
#else
#undef _ALL_SOURCE /* AIX 5.3L defines a struct list with _ALL_SOURCE. */
#include <grp.h>
#define _ALL_SOURCE 1
#endif
#endif	/* !__MINGW32__ */

#ifndef NO_ICONV
#include <iconv.h>
#endif

/* On most systems <limits.h> would have given us this, but
 * not on some systems (e.g. GNU/Hurd).
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef PRIuMAX
#ifndef __MINGW32__
#define PRIuMAX "llu"
#else
#define PRIuMAX "I64u"
#endif
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
extern void warning(const char *err, ...) __attribute__((format (printf, 1, 2)));

extern void set_usage_routine(void (*routine)(const char *err) NORETURN);
extern void set_die_routine(void (*routine)(const char *err, va_list params) NORETURN);
extern void set_error_routine(void (*routine)(const char *err, va_list params));
extern void set_warn_routine(void (*routine)(const char *warn, va_list params));

extern int prefixcmp(const char *str, const char *prefix);

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

/* This value must be multiple of (pagesize * 2) */
#define DEFAULT_PACKED_GIT_WINDOW_SIZE (1 * 1024 * 1024)

#else /* NO_MMAP */

#include <sys/mman.h>

/* This value must be multiple of (pagesize * 2) */
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
/* Forward decl that will remind us if its twin in cache.h changes.
   This function in used in compat/pread.c.  But we can't include
   cache.h there. */
extern int read_in_full(int fd, void *buf, size_t count);

#ifdef NO_SETENV
#define setenv gitsetenv
extern int gitsetenv(const char *, const char *, int);
#endif

#ifdef NO_MKDTEMP
#define mkdtemp gitmkdtemp
extern char *gitmkdtemp(char *);
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

#ifdef NO_STRTOUMAX
#define strtoumax gitstrtoumax
extern uintmax_t gitstrtoumax(const char *, char **, int);
#endif

#ifdef NO_HSTRERROR
#define hstrerror githstrerror
extern const char *githstrerror(int herror);
#endif

#ifdef NO_MEMMEM
#define memmem gitmemmem
void *gitmemmem(const void *haystack, size_t haystacklen,
                const void *needle, size_t needlelen);
#endif

#ifdef FREAD_READS_DIRECTORIES
#define fopen(a,b) git_fopen(a,b)
extern FILE *git_fopen(const char*, const char*);
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

extern void release_pack_memory(size_t, int);

static inline char* xstrdup(const char *str)
{
	char *ret = strdup(str);
	if (!ret) {
		release_pack_memory(strlen(str) + 1, -1);
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
		release_pack_memory(size, -1);
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

static inline void *xmemdupz(const void *data, size_t len)
{
	char *p = xmalloc(len + 1);
	memcpy(p, data, len);
	p[len] = '\0';
	return p;
}

static inline char *xstrndup(const char *str, size_t len)
{
	char *p = memchr(str, '\0', len);
	return xmemdupz(str, p ? p - str : len);
}

static inline void *xrealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (!ret && !size)
		ret = realloc(ptr, 1);
	if (!ret) {
		release_pack_memory(size, -1);
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
		release_pack_memory(nmemb * size, -1);
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
		release_pack_memory(length, fd);
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

static inline int xdup(int fd)
{
	int ret = dup(fd);
	if (ret < 0)
		die("dup failed: %s", strerror(errno));
	return ret;
}

static inline FILE *xfdopen(int fd, const char *mode)
{
	FILE *stream = fdopen(fd, mode);
	if (stream == NULL)
		die("Out of memory? fdopen failed: %s", strerror(errno));
	return stream;
}

#ifdef __MINGW32__
int mkstemp(char *template);
#endif

static inline int xmkstemp(char *template)
{
	int fd;

	fd = mkstemp(template);
	if (fd < 0)
		die("Unable to create temporary file: %s", strerror(errno));
	return fd;
}

static inline size_t xsize_t(off_t len)
{
	return (size_t)len;
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

static inline int strtoul_ui(char const *s, int base, unsigned int *result)
{
	unsigned long ul;
	char *p;

	errno = 0;
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

#ifdef INTERNAL_QSORT
void git_qsort(void *base, size_t nmemb, size_t size,
	       int(*compar)(const void *, const void *));
#define qsort git_qsort
#endif

#ifndef DIR_HAS_BSD_GROUP_SEMANTICS
# define FORCE_DIR_SET_GID S_ISGID
#else
# define FORCE_DIR_SET_GID 0
#endif

#ifdef __MINGW32__

#include <winsock2.h>

/*
 * things that are not available in header files
 */

typedef int pid_t;
#define hstrerror strerror

#define S_IFLNK    0120000 /* Symbolic link */
#define S_ISLNK(x) (((x) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(x) 0
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_ISGID 0
#define S_IROTH 0
#define S_IXOTH 0

#define WIFEXITED(x) ((unsigned)(x) < 259)	/* STILL_ACTIVE */
#define WEXITSTATUS(x) ((x) & 0xff)
#define WIFSIGNALED(x) ((unsigned)(x) > 259)

#define SIGKILL 0
#define SIGCHLD 0
#define SIGPIPE 0
#define SIGALRM 100

#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 0x1

struct passwd {
	char *pw_name;
	char *pw_gecos;
	char *pw_dir;
};

struct pollfd {
	int fd;           /* file descriptor */
	short events;     /* requested events */
	short revents;    /* returned events */
};
#define POLLIN 1
#define POLLHUP 2

typedef void (__cdecl *sig_handler_t)(int);
struct sigaction {
	sig_handler_t sa_handler;
	unsigned sa_flags;
};
#define sigemptyset(x) (void)0
#define SA_RESTART 0

struct itimerval {
	struct timeval it_value, it_interval;
};
#define ITIMER_REAL 0

/*
 * trivial stubs
 */

static inline int readlink(const char *path, char *buf, size_t bufsiz)
{ errno = ENOSYS; return -1; }
static inline int symlink(const char *oldpath, const char *newpath)
{ errno = ENOSYS; return -1; }
static inline int link(const char *oldpath, const char *newpath)
{ errno = ENOSYS; return -1; }
static inline int fchmod(int fildes, mode_t mode)
{ errno = ENOSYS; return -1; }
static inline int fork(void)
{ errno = ENOSYS; return -1; }
static inline unsigned int alarm(unsigned int seconds)
{ return 0; }
static inline int fsync(int fd)
{ return 0; }
static inline int getppid(void)
{ return 1; }
static inline void sync(void)
{}
static inline int getuid()
{ return 1; }
static inline struct passwd *getpwnam(const char *name)
{ return NULL; }
static inline int fcntl(int fd, int cmd, long arg)
{
	if (cmd == F_GETFD || cmd == F_SETFD)
		return 0;
	errno = EINVAL;
	return -1;
}

/*
 * simple adaptors
 */

static inline int mingw_mkdir(const char *path, int mode)
{
	return mkdir(path);
}
#define mkdir mingw_mkdir

static inline int mingw_unlink(const char *pathname)
{
	/* read-only files cannot be removed */
	chmod(pathname, 0666);
	return unlink(pathname);
}
#define unlink mingw_unlink

static inline int waitpid(pid_t pid, unsigned *status, unsigned options)
{
	if (options == 0)
		return _cwait(status, pid, 0);
	errno = EINVAL;
	return -1;
}

/*
 * implementations of missing functions
 */

int pipe(int filedes[2]);
unsigned int sleep (unsigned int seconds);
int gettimeofday(struct timeval *tv, void *tz);
int poll(struct pollfd *ufds, unsigned int nfds, int timeout);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
int getpagesize(void);	/* defined in MinGW's libgcc.a */
struct passwd *getpwuid(int uid);
int setitimer(int type, struct itimerval *in, struct itimerval *out);
int sigaction(int sig, struct sigaction *in, struct sigaction *out);

/*
 * replacements of existing functions
 */

int mingw_open (const char *filename, int oflags, ...);
#define open mingw_open

char *mingw_getcwd(char *pointer, int len);
#define getcwd mingw_getcwd

struct hostent *mingw_gethostbyname(const char *host);
#define gethostbyname mingw_gethostbyname

int mingw_socket(int domain, int type, int protocol);
#define socket mingw_socket

int mingw_connect(int sockfd, struct sockaddr *sa, size_t sz);
#define connect mingw_connect

int mingw_rename(const char*, const char*);
#define rename mingw_rename

/* Use mingw_lstat() instead of lstat()/stat() and
 * mingw_fstat() instead of fstat() on Windows.
 * struct stat is redefined because it lacks the st_blocks member.
 */
struct mingw_stat {
	unsigned st_mode;
	time_t st_mtime, st_atime, st_ctime;
	unsigned st_dev, st_ino, st_uid, st_gid;
	size_t st_size;
	size_t st_blocks;
};
int mingw_lstat(const char *file_name, struct mingw_stat *buf);
int mingw_fstat(int fd, struct mingw_stat *buf);
#define fstat mingw_fstat
#define lstat mingw_lstat
#define stat mingw_stat
static inline int mingw_stat(const char *file_name, struct mingw_stat *buf)
{ return mingw_lstat(file_name, buf); }

int mingw_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
#define vsnprintf mingw_vsnprintf

pid_t mingw_spawnvpe(const char *cmd, const char **argv, char **env);
void mingw_execvp(const char *cmd, char *const *argv);
#define execvp mingw_execvp

static inline unsigned int git_ntohl(unsigned int x)
{ return (unsigned int)ntohl(x); }
#define ntohl git_ntohl

sig_handler_t mingw_signal(int sig, sig_handler_t handler);
#define signal mingw_signal

/*
 * helpers
 */

char **copy_environ(void);
void free_environ(char **env);
char **env_setenv(char **env, const char *name);

static inline int has_dos_drive_prefix(const char *path)
{
	return isalpha(*path) && path[1] == ':';
}

#else /* __MINGW32__ */

static inline int has_dos_drive_prefix(const char *path)
{
	return 0;
}

#endif /* __MINGW32__ */

#endif
