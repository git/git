#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include "../git-compat-util.h"

unsigned int _CRT_fmode = _O_BINARY;

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
	Sleep(__seconds*1000);
	return 0;
}
const char *inet_ntop(int af, const void *src,
                             char *dst, size_t cnt)
{
	return NULL;
}
int mkstemp (char *__template)
{
	char *filename = mktemp(__template);
	if (filename == NULL)
		return -1;
	return open(filename, O_RDWR | O_CREAT);
}
int gettimeofday(struct timeval *tv, void *tz)
{
	return -1;
}
int pipe(int filedes[2])
{
	int fd;
	HANDLE h[2], parent;

	if (_pipe(filedes, 4096, 0) < 0)
		return -1;

	parent = GetCurrentProcess();

	if (!DuplicateHandle (parent, (HANDLE)_get_osfhandle(filedes[0]),
			parent, &h[0], 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		close(filedes[0]);
		close(filedes[1]);
		return -1;
	}
	if (!DuplicateHandle (parent, (HANDLE)_get_osfhandle(filedes[1]),
			parent, &h[1], 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[0]);
		return -1;
	}
	fd = _open_osfhandle(h[0], O_NOINHERIT);
	if (fd < 0) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[0]);
		CloseHandle(h[1]);
		return -1;
	}
	close(filedes[0]);
	filedes[0] = fd;
	fd = _open_osfhandle(h[1], O_NOINHERIT);
	if (fd < 0) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[1]);
		return -1;
	}
	close(filedes[1]);
	filedes[1] = fd;
	return 0;
}

int poll(struct pollfd *ufds, unsigned int nfds, int timeout)
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

static const char *quote_arg(const char *arg)
{
	/* count chars to quote */
	int len = 0, n = 0;
	int force_quotes = 0;
	char *q, *d;
	const char *p = arg;
	while (*p) {
		if (isspace(*p))
			force_quotes = 1;
		else if (*p == '"' || *p == '\\')
			n++;
		len++;
		p++;
	}
	if (!force_quotes && n == 0)
		return arg;

	/* insert \ where necessary */
	d = q = xmalloc(len+n+3);
	*d++ = '"';
	while (*arg) {
		if (*arg == '"' || *arg == '\\')
			*d++ = '\\';
		*d++ = *arg++;
	}
	*d++ = '"';
	*d++ = 0;
	return q;
}

void quote_argv(const char **dst, const char **src)
{
	while (*src)
		*dst++ = quote_arg(*src++);
	*dst = NULL;
}

const char *parse_interpreter(const char *cmd)
{
	static char buf[100];
	char *p;
	int n, fd;

	/* don't even try a .exe */
	n = strlen(cmd);
	if (n >= 4 && !strcasecmp(cmd+n-4, ".exe"))
		return NULL;

	fd = open(cmd, O_RDONLY);
	if (fd < 0)
		return NULL;
	n = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if (n < 4)	/* at least '#!/x' and not error */
		return NULL;

	if (buf[0] != '#' || buf[1] != '!')
		return NULL;
	buf[n] = '\0';
	p = strchr(buf, '\n');
	if (!p)
		return NULL;

	*p = '\0';
	if (!(p = strrchr(buf+2, '/')) && !(p = strrchr(buf+2, '\\')))
		return NULL;
	return p+1;
}

static int try_shell_exec(const char *cmd, const char **argv, const char **env)
{
	const char **sh_argv;
	int n;
	const char *interpr = parse_interpreter(cmd);
	if (!interpr)
		return 0;

	/*
	 * expand
	 *    git-foo args...
	 * into
	 *    sh git-foo args...
	 */
	for (n = 0; argv[n];) n++;
	sh_argv = xmalloc((n+2)*sizeof(char*));
	sh_argv[0] = interpr;
	sh_argv[1] = cmd;
	quote_argv(&sh_argv[2], &argv[1]);
	n = spawnvpe(_P_WAIT, "sh", sh_argv, env);
	if (n == -1)
		return 1;	/* indicate that we tried but failed */
	exit(n);
}

void mingw_execve(const char *cmd, const char **argv, const char **env)
{
	/* check if git_command is a shell script */
	if (!try_shell_exec(cmd, argv, env)) {
		int ret = spawnve(_P_WAIT, cmd, argv, env);
		if (ret != -1)
			exit(ret);
	}
}
