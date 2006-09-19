#include <stdint.h>
#include "../git-compat-util.h"

int readlink(const char *path, char *buf, size_t bufsiz)
{
	errno = EINVAL;
	return -1;
}

int symlink(const char *oldpath, const char *newpath)
{
	errno = EFAULT;
	return -1;
}

int fchmod(int fildes, mode_t mode)
{
	errno = EBADF;
	return -1;
}

int lstat(const char *file_name, struct stat *buf)
{
	return stat(file_name, buf);
}

/* missing: link, mkstemp, fchmod, getuid (?), gettimeofday */
int socketpair(int d, int type, int protocol, int sv[2])
{
	return -1;
}
int syslog(int type, char *bufp, ...)
{
	return -1;
}
unsigned int alarm(unsigned int seconds)
{
	return 0;
}
#include <winsock2.h>
int fork()
{
	return -1;
}
typedef int pid_t;
pid_t waitpid(pid_t pid, int *status, int options)
{
	errno = ECHILD;
	return -1;
}

int kill(pid_t pid, int sig)
{
	return -1;
}
int sigaction(int p1, const struct sigaction *p2, struct sigaction *p3)
{
	return -1;
}
int sigemptyset(sigset_t *p1)
{
	return -1;
}
int setitimer(int __which, const struct itimerval *__value,
                                        struct itimerval *__ovalue)
{
	return -1;
}
unsigned int sleep (unsigned int __seconds)
{
	return 0;
}
const char *inet_ntop(int af, const void *src,
                             char *dst, size_t cnt)
{
	return NULL;
}
int mkstemp (char *__template)
{
	return -1;
}
int gettimeofday(struct timeval *tv, void *tz)
{
	return -1;
}
int pipe(int filedes[2])
{
	return -1;
}

int poll(struct pollfd *ufds, unsigned int nfds, int timeout)
{
	return -1;
}

int fnmatch(const char *pattern, const char *string, int flags)
{
	return -1;
}

int regcomp(regex_t *preg, const char *regex, int cflags)
{
	return -1;
}
size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
	return 0;
}
void regfree(regex_t *preg)
{
}

int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags)
{
	return -1;
}

#include <time.h>

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
	memcpy(result, gmtime(timep), sizeof(struct tm));
	return result;
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
	memcpy(result, localtime(timep), sizeof(struct tm));
	return result;
}

#undef getcwd
char *mingw_getcwd(char *pointer, int len)
{
	char *ret = getcwd(pointer, len);
	if (!ret)
		return ret;
	if (pointer[0] != 0 && pointer[1] == ':') {
		int i;
		pointer[1] = pointer[0];
		pointer[0] = '/';
		for (i = 2; pointer[i]; i++)
			/* Thanks, Bill. You'll burn in hell for that. */
			if (pointer[i] == '\\')
				pointer[i] = '/';
	}
	return ret;
}
const char *strptime(char *buf, const char *format, struct tm *tm)
{
	die("MinGW does not yet support strptime!");
}
void sync(void)
{
}
void openlog(const char *ident, int option, int facility)
{
}
