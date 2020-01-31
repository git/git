#ifdef __MINGW64_VERSION_MAJOR
#include <stdint.h>
#include <wchar.h>
typedef _sigset_t sigset_t;
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

/* MinGW-w64 reports to have flockfile, but it does not actually have it. */
#ifdef __MINGW64_VERSION_MAJOR
#undef _POSIX_THREAD_SAFE_FUNCTIONS
#endif

int mingw_core_config(const char *var, const char *value, void *cb);
#define platform_core_config mingw_core_config

/*
 * things that are not available in header files
 */

typedef int uid_t;
typedef int socklen_t;
#ifndef __MINGW64_VERSION_MAJOR
typedef int pid_t;
#define hstrerror strerror
#endif

#define S_IFLNK    0120000 /* Symbolic link */
#define S_ISLNK(x) (((x) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(x) 0

#ifndef S_IRWXG
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#endif
#ifndef S_IRWXO
#define S_IROTH 0
#define S_IWOTH 0
#define S_IXOTH 0
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#endif

#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000

#define WIFEXITED(x) 1
#define WIFSIGNALED(x) 0
#define WEXITSTATUS(x) ((x) & 0xff)
#define WTERMSIG(x) SIGTERM

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef ELOOP
#define ELOOP EMLINK
#endif
#define SHUT_WR SD_SEND

#define SIGHUP 1
#define SIGQUIT 3
#define SIGKILL 9
#define SIGPIPE 13
#define SIGALRM 14
#define SIGCHLD 17

#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 0x1

#if !defined O_CLOEXEC && defined O_NOINHERIT
#define O_CLOEXEC	O_NOINHERIT
#endif

#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT WSAEAFNOSUPPORT
#endif
#ifndef ECONNABORTED
#define ECONNABORTED WSAECONNABORTED
#endif
#ifndef ENOTSOCK
#define ENOTSOCK WSAENOTSOCK
#endif

struct passwd {
	char *pw_name;
	char *pw_gecos;
	char *pw_dir;
};

typedef void (__cdecl *sig_handler_t)(int);
struct sigaction {
	sig_handler_t sa_handler;
	unsigned sa_flags;
};
#define SA_RESTART 0

struct itimerval {
	struct timeval it_value, it_interval;
};
#define ITIMER_REAL 0

struct utsname {
	char sysname[16];
	char nodename[1];
	char release[16];
	char version[16];
	char machine[1];
};

/*
 * sanitize preprocessor namespace polluted by Windows headers defining
 * macros which collide with git local versions
 */
#undef HELP_COMMAND /* from winuser.h */

/*
 * trivial stubs
 */

static inline int readlink(const char *path, char *buf, size_t bufsiz)
{ errno = ENOSYS; return -1; }
static inline int symlink(const char *oldpath, const char *newpath)
{ errno = ENOSYS; return -1; }
static inline int fchmod(int fildes, mode_t mode)
{ errno = ENOSYS; return -1; }
#ifndef __MINGW64_VERSION_MAJOR
static inline pid_t fork(void)
{ errno = ENOSYS; return -1; }
#endif
static inline unsigned int alarm(unsigned int seconds)
{ return 0; }
static inline int fsync(int fd)
{ return _commit(fd); }
static inline void sync(void)
{}
static inline uid_t getuid(void)
{ return 1; }
static inline struct passwd *getpwnam(const char *name)
{ return NULL; }
static inline int fcntl(int fd, int cmd, ...)
{
	if (cmd == F_GETFD || cmd == F_SETFD)
		return 0;
	errno = EINVAL;
	return -1;
}

#define sigemptyset(x) (void)0
static inline int sigaddset(sigset_t *set, int signum)
{ return 0; }
#define SIG_BLOCK 0
#define SIG_UNBLOCK 0
static inline int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{ return 0; }
static inline pid_t getppid(void)
{ return 1; }
static inline pid_t getpgid(pid_t pid)
{ return pid == 0 ? getpid() : pid; }
static inline pid_t tcgetpgrp(int fd)
{ return getpid(); }

/*
 * simple adaptors
 */

int mingw_mkdir(const char *path, int mode);
#define mkdir mingw_mkdir

#define WNOHANG 1
pid_t waitpid(pid_t pid, int *status, int options);

#define kill mingw_kill
int mingw_kill(pid_t pid, int sig);

#ifndef NO_OPENSSL
#include <openssl/ssl.h>
static inline int mingw_SSL_set_fd(SSL *ssl, int fd)
{
	return SSL_set_fd(ssl, _get_osfhandle(fd));
}
#define SSL_set_fd mingw_SSL_set_fd

static inline int mingw_SSL_set_rfd(SSL *ssl, int fd)
{
	return SSL_set_rfd(ssl, _get_osfhandle(fd));
}
#define SSL_set_rfd mingw_SSL_set_rfd

static inline int mingw_SSL_set_wfd(SSL *ssl, int fd)
{
	return SSL_set_wfd(ssl, _get_osfhandle(fd));
}
#define SSL_set_wfd mingw_SSL_set_wfd
#endif

/*
 * implementations of missing functions
 */

int pipe(int filedes[2]);
unsigned int sleep (unsigned int seconds);
int mkstemp(char *template);
int gettimeofday(struct timeval *tv, void *tz);
#ifndef __MINGW64_VERSION_MAJOR
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
#endif
int getpagesize(void);	/* defined in MinGW's libgcc.a */
struct passwd *getpwuid(uid_t uid);
int setitimer(int type, struct itimerval *in, struct itimerval *out);
int sigaction(int sig, struct sigaction *in, struct sigaction *out);
int link(const char *oldpath, const char *newpath);
int uname(struct utsname *buf);

/*
 * replacements of existing functions
 */

int mingw_unlink(const char *pathname);
#define unlink mingw_unlink

int mingw_rmdir(const char *path);
#define rmdir mingw_rmdir

int mingw_open (const char *filename, int oflags, ...);
#define open mingw_open
#undef OPEN_RETURNS_EINTR

int mingw_fgetc(FILE *stream);
#define fgetc mingw_fgetc

FILE *mingw_fopen (const char *filename, const char *otype);
#define fopen mingw_fopen

FILE *mingw_freopen (const char *filename, const char *otype, FILE *stream);
#define freopen mingw_freopen

int mingw_fflush(FILE *stream);
#define fflush mingw_fflush

ssize_t mingw_write(int fd, const void *buf, size_t len);
#define write mingw_write

int mingw_access(const char *filename, int mode);
#undef access
#define access mingw_access

int mingw_chdir(const char *dirname);
#define chdir mingw_chdir

int mingw_chmod(const char *filename, int mode);
#define chmod mingw_chmod

char *mingw_mktemp(char *template);
#define mktemp mingw_mktemp

char *mingw_getcwd(char *pointer, int len);
#define getcwd mingw_getcwd

#ifdef NO_UNSETENV
#error "NO_UNSETENV is incompatible with the Windows-specific startup code!"
#endif

/*
 * We bind *env() routines (even the mingw_ ones) to private mingw_ versions.
 * These talk to the CRT using UNICODE/wchar_t, but maintain the original
 * narrow-char API.
 *
 * Note that the MSCRT maintains both ANSI (getenv()) and UNICODE (_wgetenv())
 * routines and stores both versions of each environment variable in parallel
 * (and secretly updates both when you set one or the other), but it uses CP_ACP
 * to do the conversion rather than CP_UTF8.
 *
 * Since everything in the git code base is UTF8, we define the mingw_ routines
 * to access the CRT using the UNICODE routines and manually convert them to
 * UTF8.  This also avoids round-trip problems.
 *
 * This also helps with our linkage, since "_wenviron" is publicly exported
 * from the CRT.  But to access "_environ" we would have to statically link
 * to the CRT (/MT).
 *
 * We require NO_SETENV (and let gitsetenv() call our mingw_putenv).
 */
#define getenv       mingw_getenv
#define putenv       mingw_putenv
#define unsetenv     mingw_putenv
char *mingw_getenv(const char *name);
int   mingw_putenv(const char *name);

int mingw_gethostname(char *host, int namelen);
#define gethostname mingw_gethostname

struct hostent *mingw_gethostbyname(const char *host);
#define gethostbyname mingw_gethostbyname

int mingw_getaddrinfo(const char *node, const char *service,
		      const struct addrinfo *hints, struct addrinfo **res);
#define getaddrinfo mingw_getaddrinfo

int mingw_socket(int domain, int type, int protocol);
#define socket mingw_socket

int mingw_connect(int sockfd, struct sockaddr *sa, size_t sz);
#define connect mingw_connect

int mingw_bind(int sockfd, struct sockaddr *sa, size_t sz);
#define bind mingw_bind

int mingw_setsockopt(int sockfd, int lvl, int optname, void *optval, int optlen);
#define setsockopt mingw_setsockopt

int mingw_shutdown(int sockfd, int how);
#define shutdown mingw_shutdown

int mingw_listen(int sockfd, int backlog);
#define listen mingw_listen

int mingw_accept(int sockfd, struct sockaddr *sa, socklen_t *sz);
#define accept mingw_accept

int mingw_rename(const char*, const char*);
#define rename mingw_rename

#if defined(USE_WIN32_MMAP) || defined(_MSC_VER)
int mingw_getpagesize(void);
#define getpagesize mingw_getpagesize
#endif

int win32_fsync_no_flush(int fd);
#define fsync_no_flush win32_fsync_no_flush

struct rlimit {
	unsigned int rlim_cur;
};
#define RLIMIT_NOFILE 0

static inline int getrlimit(int resource, struct rlimit *rlp)
{
	if (resource != RLIMIT_NOFILE) {
		errno = EINVAL;
		return -1;
	}

	rlp->rlim_cur = 2048;
	return 0;
}

/*
 * Use mingw specific stat()/lstat()/fstat() implementations on Windows,
 * including our own struct stat with 64 bit st_size and nanosecond-precision
 * file times.
 */
#ifndef __MINGW64_VERSION_MAJOR
#define off_t off64_t
#define lseek _lseeki64
#ifndef _MSC_VER
struct timespec {
	time_t tv_sec;
	long tv_nsec;
};
#endif
#endif

struct mingw_stat {
    _dev_t st_dev;
    _ino_t st_ino;
    _mode_t st_mode;
    short st_nlink;
    short st_uid;
    short st_gid;
    _dev_t st_rdev;
    off64_t st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
};

#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

#ifdef stat
#undef stat
#endif
#define stat mingw_stat
int mingw_lstat(const char *file_name, struct stat *buf);
int mingw_stat(const char *file_name, struct stat *buf);
int mingw_fstat(int fd, struct stat *buf);
#ifdef fstat
#undef fstat
#endif
#define fstat mingw_fstat
#ifdef lstat
#undef lstat
#endif
#define lstat mingw_lstat


int mingw_utime(const char *file_name, const struct utimbuf *times);
#define utime mingw_utime
size_t mingw_strftime(char *s, size_t max,
		   const char *format, const struct tm *tm);
#define strftime mingw_strftime

pid_t mingw_spawnvpe(const char *cmd, const char **argv, char **env,
		     const char *dir,
		     int fhin, int fhout, int fherr);
int mingw_execvp(const char *cmd, char *const *argv);
#define execvp mingw_execvp
int mingw_execv(const char *cmd, char *const *argv);
#define execv mingw_execv

static inline unsigned int git_ntohl(unsigned int x)
{ return (unsigned int)ntohl(x); }
#define ntohl git_ntohl

sig_handler_t mingw_signal(int sig, sig_handler_t handler);
#define signal mingw_signal

int mingw_raise(int sig);
#define raise mingw_raise

/*
 * ANSI emulation wrappers
 */

int winansi_isatty(int fd);
#define isatty winansi_isatty

int winansi_dup2(int oldfd, int newfd);
#define dup2 winansi_dup2

void winansi_init(void);
HANDLE winansi_get_osfhandle(int fd);

/*
 * git specific compatibility
 */

static inline void convert_slashes(char *path)
{
	for (; *path; path++)
		if (*path == '\\')
			*path = '/';
}
#define PATH_SEP ';'
char *mingw_query_user_email(void);
#define query_user_email mingw_query_user_email
struct strbuf;
char *mingw_strbuf_realpath(struct strbuf *resolved, const char *path);
#define platform_strbuf_realpath mingw_strbuf_realpath
#if !defined(__MINGW64_VERSION_MAJOR) && (!defined(_MSC_VER) || _MSC_VER < 1800)
#define PRIuMAX "I64u"
#define PRId64 "I64d"
#else
#include <inttypes.h>
#endif

/**
 * Verifies that the given path is a valid one on Windows.
 *
 * In particular, path segments are disallowed which
 *
 * - end in a period or a space (except the special directories `.` and `..`).
 *
 * - contain any of the reserved characters, e.g. `:`, `;`, `*`, etc
 *
 * - correspond to reserved names (such as `AUX`, `PRN`, etc)
 *
 * The `allow_literal_nul` parameter controls whether the path `NUL` should
 * be considered valid (this makes sense e.g. before opening files, as it is
 * perfectly legitimate to open `NUL` on Windows, just as it is to open
 * `/dev/null` on Unix/Linux).
 *
 * Returns 1 upon success, otherwise 0.
 */
int is_valid_win32_path(const char *path, int allow_literal_nul);
#define is_valid_path(path) is_valid_win32_path(path, 0)

/**
 * Converts UTF-8 encoded string to UTF-16LE.
 *
 * To support repositories with legacy-encoded file names, invalid UTF-8 bytes
 * 0xa0 - 0xff are converted to corresponding printable Unicode chars \u00a0 -
 * \u00ff, and invalid UTF-8 bytes 0x80 - 0x9f (which would make non-printable
 * Unicode) are converted to hex-code.
 *
 * Lead-bytes not followed by an appropriate number of trail-bytes, over-long
 * encodings and 4-byte encodings > \u10ffff are detected as invalid UTF-8.
 *
 * Maximum space requirement for the target buffer is two wide chars per UTF-8
 * char (((strlen(utf) * 2) + 1) [* sizeof(wchar_t)]).
 *
 * The maximum space is needed only if the entire input string consists of
 * invalid UTF-8 bytes in range 0x80-0x9f, as per the following table:
 *
 *               |                   | UTF-8 | UTF-16 |
 *   Code point  |  UTF-8 sequence   | bytes | words  | ratio
 * --------------+-------------------+-------+--------+-------
 * 000000-00007f | 0-7f              |   1   |   1    |  1
 * 000080-0007ff | c2-df + 80-bf     |   2   |   1    |  0.5
 * 000800-00ffff | e0-ef + 2 * 80-bf |   3   |   1    |  0.33
 * 010000-10ffff | f0-f4 + 3 * 80-bf |   4   |  2 (a) |  0.5
 * invalid       | 80-9f             |   1   |  2 (b) |  2
 * invalid       | a0-ff             |   1   |   1    |  1
 *
 * (a) encoded as UTF-16 surrogate pair
 * (b) encoded as two hex digits
 *
 * Note that, while the UTF-8 encoding scheme can be extended to 5-byte, 6-byte
 * or even indefinite-byte sequences, the largest valid code point \u10ffff
 * encodes as only 4 UTF-8 bytes.
 *
 * Parameters:
 * wcs: wide char target buffer
 * utf: string to convert
 * wcslen: size of target buffer (in wchar_t's)
 * utflen: size of string to convert, or -1 if 0-terminated
 *
 * Returns:
 * length of converted string (_wcslen(wcs)), or -1 on failure
 *
 * Errors:
 * EINVAL: one of the input parameters is invalid (e.g. NULL)
 * ERANGE: the output buffer is too small
 */
int xutftowcsn(wchar_t *wcs, const char *utf, size_t wcslen, int utflen);

/**
 * Simplified variant of xutftowcsn, assumes input string is \0-terminated.
 */
static inline int xutftowcs(wchar_t *wcs, const char *utf, size_t wcslen)
{
	return xutftowcsn(wcs, utf, wcslen, -1);
}

/**
 * Simplified file system specific variant of xutftowcsn, assumes output
 * buffer size is MAX_PATH wide chars and input string is \0-terminated,
 * fails with ENAMETOOLONG if input string is too long.
 */
static inline int xutftowcs_path(wchar_t *wcs, const char *utf)
{
	int result = xutftowcsn(wcs, utf, MAX_PATH, -1);
	if (result < 0 && errno == ERANGE)
		errno = ENAMETOOLONG;
	return result;
}

/**
 * Converts UTF-16LE encoded string to UTF-8.
 *
 * Maximum space requirement for the target buffer is three UTF-8 chars per
 * wide char ((_wcslen(wcs) * 3) + 1).
 *
 * The maximum space is needed only if the entire input string consists of
 * UTF-16 words in range 0x0800-0xd7ff or 0xe000-0xffff (i.e. \u0800-\uffff
 * modulo surrogate pairs), as per the following table:
 *
 *               |                       | UTF-16 | UTF-8 |
 *   Code point  |  UTF-16 sequence      | words  | bytes | ratio
 * --------------+-----------------------+--------+-------+-------
 * 000000-00007f | 0000-007f             |   1    |   1   |  1
 * 000080-0007ff | 0080-07ff             |   1    |   2   |  2
 * 000800-00ffff | 0800-d7ff / e000-ffff |   1    |   3   |  3
 * 010000-10ffff | d800-dbff + dc00-dfff |   2    |   4   |  2
 *
 * Note that invalid code points > 10ffff cannot be represented in UTF-16.
 *
 * Parameters:
 * utf: target buffer
 * wcs: wide string to convert
 * utflen: size of target buffer
 *
 * Returns:
 * length of converted string, or -1 on failure
 *
 * Errors:
 * EINVAL: one of the input parameters is invalid (e.g. NULL)
 * ERANGE: the output buffer is too small
 */
int xwcstoutf(char *utf, const wchar_t *wcs, size_t utflen);

/*
 * A critical section used in the implementation of the spawn
 * functions (mingw_spawnv[p]e()) and waitpid(). Initialised in
 * the replacement main() macro below.
 */
extern CRITICAL_SECTION pinfo_cs;

/*
 * Git, like most portable C applications, implements a main() function. On
 * Windows, this main() function would receive parameters encoded in the
 * current locale, but Git for Windows would prefer UTF-8 encoded  parameters.
 *
 * To make that happen, we still declare main() here, and then declare and
 * implement wmain() (which is the Unicode variant of main()) and compile with
 * -municode. This wmain() function reencodes the parameters from UTF-16 to
 * UTF-8 format, sets up a couple of other things as required on Windows, and
 * then hands off to the main() function.
 */
int wmain(int argc, const wchar_t **w_argv);
int main(int argc, const char **argv);

/*
 * For debugging: if a problem occurs, say, in a Git process that is spawned
 * from another Git process which in turn is spawned from yet another Git
 * process, it can be quite daunting to figure out what is going on.
 *
 * Call this function to open a new MinTTY (this assumes you are in Git for
 * Windows' SDK) with a GDB that attaches to the current process right away.
 */
void open_in_gdb(void);

/*
 * Used by Pthread API implementation for Windows
 */
int err_win_to_posix(DWORD winerr);
