#include <winsock2.h>
#include <ws2tcpip.h>

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

#define WIFEXITED(x) 1
#define WIFSIGNALED(x) 0
#define WEXITSTATUS(x) ((x) & 0xff)
#define WTERMSIG(x) SIGTERM

#define SIGHUP 1
#define SIGQUIT 3
#define SIGKILL 9
#define SIGPIPE 13
#define SIGALRM 14
#define SIGCHLD 17

#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 0x1

struct passwd {
	char *pw_name;
	char *pw_gecos;
	char *pw_dir;
};

extern char *getpass(const char *prompt);

#ifndef POLLIN
struct pollfd {
	int fd;           /* file descriptor */
	short events;     /* requested events */
	short revents;    /* returned events */
};
#define POLLIN 1
#define POLLHUP 2
#endif

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
static inline int fchmod(int fildes, mode_t mode)
{ errno = ENOSYS; return -1; }
static inline int fork(void)
{ errno = ENOSYS; return -1; }
static inline unsigned int alarm(unsigned int seconds)
{ return 0; }
static inline int fsync(int fd)
{ return _commit(fd); }
static inline int getppid(void)
{ return 1; }
static inline void sync(void)
{}
static inline int getuid()
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
/* bash cannot reliably detect negative return codes as failure */
#define exit(code) exit((code) & 0xff)

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

static inline int waitpid(pid_t pid, int *status, unsigned options)
{
	if (options == 0)
		return _cwait(status, pid, 0);
	errno = EINVAL;
	return -1;
}

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
int poll(struct pollfd *ufds, unsigned int nfds, int timeout);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
int getpagesize(void);	/* defined in MinGW's libgcc.a */
struct passwd *getpwuid(int uid);
int setitimer(int type, struct itimerval *in, struct itimerval *out);
int sigaction(int sig, struct sigaction *in, struct sigaction *out);
int link(const char *oldpath, const char *newpath);

/*
 * replacements of existing functions
 */

int mingw_open (const char *filename, int oflags, ...);
#define open mingw_open

ssize_t mingw_write(int fd, const void *buf, size_t count);
#define write mingw_write

FILE *mingw_fopen (const char *filename, const char *otype);
#define fopen mingw_fopen

FILE *mingw_freopen (const char *filename, const char *otype, FILE *stream);
#define freopen mingw_freopen

char *mingw_getcwd(char *pointer, int len);
#define getcwd mingw_getcwd

char *mingw_getenv(const char *name);
#define getenv mingw_getenv

struct hostent *mingw_gethostbyname(const char *host);
#define gethostbyname mingw_gethostbyname

void mingw_freeaddrinfo(struct addrinfo *res);
#define freeaddrinfo mingw_freeaddrinfo

int mingw_getaddrinfo(const char *node, const char *service,
		      const struct addrinfo *hints, struct addrinfo **res);
#define getaddrinfo mingw_getaddrinfo

int mingw_getnameinfo(const struct sockaddr *sa, socklen_t salen,
		      char *host, DWORD hostlen, char *serv, DWORD servlen,
		      int flags);
#define getnameinfo mingw_getnameinfo

int mingw_socket(int domain, int type, int protocol);
#define socket mingw_socket

int mingw_connect(int sockfd, struct sockaddr *sa, size_t sz);
#define connect mingw_connect

int mingw_rename(const char*, const char*);
#define rename mingw_rename

#if defined(USE_WIN32_MMAP) || defined(_MSC_VER)
int mingw_getpagesize(void);
#define getpagesize mingw_getpagesize
#endif

/* Use mingw_lstat() instead of lstat()/stat() and
 * mingw_fstat() instead of fstat() on Windows.
 */
#define off_t off64_t
#define lseek _lseeki64
#ifndef ALREADY_DECLARED_STAT_FUNCS
#define stat _stati64
int mingw_lstat(const char *file_name, struct stat *buf);
int mingw_fstat(int fd, struct stat *buf);
#define fstat mingw_fstat
#define lstat mingw_lstat
#define _stati64(x,y) mingw_lstat(x,y)
#endif

int mingw_utime(const char *file_name, const struct utimbuf *times);
#define utime mingw_utime

pid_t mingw_spawnvpe(const char *cmd, const char **argv, char **env,
		     const char *dir,
		     int fhin, int fhout, int fherr);
void mingw_execvp(const char *cmd, char *const *argv);
#define execvp mingw_execvp

static inline unsigned int git_ntohl(unsigned int x)
{ return (unsigned int)ntohl(x); }
#define ntohl git_ntohl

sig_handler_t mingw_signal(int sig, sig_handler_t handler);
#define signal mingw_signal

/*
 * ANSI emulation wrappers
 */

int winansi_fputs(const char *str, FILE *stream);
int winansi_printf(const char *format, ...) __attribute__((format (printf, 1, 2)));
int winansi_fprintf(FILE *stream, const char *format, ...) __attribute__((format (printf, 2, 3)));
#define fputs winansi_fputs
#define printf(...) winansi_printf(__VA_ARGS__)
#define fprintf(...) winansi_fprintf(__VA_ARGS__)

/*
 * git specific compatibility
 */

#define has_dos_drive_prefix(path) (isalpha(*(path)) && (path)[1] == ':')
#define is_dir_sep(c) ((c) == '/' || (c) == '\\')
#define PATH_SEP ';'
#define PRIuMAX "I64u"

void mingw_open_html(const char *path);
#define open_html mingw_open_html

/*
 * helpers
 */

char **make_augmented_environ(const char *const *vars);
void free_environ(char **env);

/*
 * A replacement of main() that ensures that argv[0] has a path
 * and that default fmode and std(in|out|err) are in binary mode
 */

#define main(c,v) dummy_decl_mingw_main(); \
static int mingw_main(); \
int main(int argc, const char **argv) \
{ \
	_fmode = _O_BINARY; \
	_setmode(_fileno(stdin), _O_BINARY); \
	_setmode(_fileno(stdout), _O_BINARY); \
	_setmode(_fileno(stderr), _O_BINARY); \
	argv[0] = xstrdup(_pgmptr); \
	return mingw_main(argc, argv); \
} \
static int mingw_main(c,v)

#ifndef NO_MINGW_REPLACE_READDIR
/*
 * A replacement of readdir, to ensure that it reads the file type at
 * the same time. This avoid extra unneeded lstats in git on MinGW
 */
#undef DT_UNKNOWN
#undef DT_DIR
#undef DT_REG
#undef DT_LNK
#define DT_UNKNOWN	0
#define DT_DIR		1
#define DT_REG		2
#define DT_LNK		3

struct mingw_dirent
{
	long		d_ino;			/* Always zero. */
	union {
		unsigned short	d_reclen;	/* Always zero. */
		unsigned char   d_type;		/* Reimplementation adds this */
	};
	unsigned short	d_namlen;		/* Length of name in d_name. */
	char		d_name[FILENAME_MAX];	/* File name. */
};
#define dirent mingw_dirent
#define readdir(x) mingw_readdir(x)
struct dirent *mingw_readdir(DIR *dir);
#endif // !NO_MINGW_REPLACE_READDIR

/*
 * Used by Pthread API implementation for Windows
 */
extern int err_win_to_posix(DWORD winerr);
