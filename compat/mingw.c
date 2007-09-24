#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include "../git-compat-util.h"

unsigned int _CRT_fmode = _O_BINARY;

int readlink(const char *path, char *buf, size_t bufsiz)
{
	errno = ENOSYS;
	return -1;
}

int symlink(const char *oldpath, const char *newpath)
{
	errno = ENOSYS;
	return -1;
}

int fchmod(int fildes, mode_t mode)
{
	errno = EBADF;
	return -1;
}

static inline time_t filetime_to_time_t(const FILETIME *ft)
{
	long long winTime = ((long long)ft->dwHighDateTime << 32) + ft->dwLowDateTime;
	winTime -= 116444736000000000LL; /* Windows to Unix Epoch conversion */
	winTime /= 10000000;		 /* Nano to seconds resolution */
	return (time_t)winTime;
}

extern int _getdrive( void );
/* We keep the do_lstat code in a separate function to avoid recursion.
 * When a path ends with a slash, the stat will fail with ENOENT. In
 * this case, we strip the trailing slashes and stat again.
 */
static int do_lstat(const char *file_name, struct stat *buf)
{
	WIN32_FILE_ATTRIBUTE_DATA fdata;

	if (GetFileAttributesExA(file_name, GetFileExInfoStandard, &fdata)) {
		int fMode = S_IREAD;
		if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			fMode |= S_IFDIR;
		else
			fMode |= S_IFREG;
		if (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
			fMode |= S_IWRITE;

		buf->st_ino = 0;
		buf->st_gid = 0;
		buf->st_uid = 0;
		buf->st_nlink = 1;
		buf->st_mode = fMode;
		buf->st_size = fdata.nFileSizeLow; /* Can't use nFileSizeHigh, since it's not a stat64 */
		buf->st_dev = buf->st_rdev = (_getdrive() - 1);
		buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
		buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
		buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
		errno = 0;
		return 0;
	}

	switch (GetLastError()) {
	case ERROR_ACCESS_DENIED:
	case ERROR_SHARING_VIOLATION:
	case ERROR_LOCK_VIOLATION:
	case ERROR_SHARING_BUFFER_EXCEEDED:
		errno = EACCES;
		break;
	case ERROR_BUFFER_OVERFLOW:
		errno = ENAMETOOLONG;
		break;
	case ERROR_NOT_ENOUGH_MEMORY:
		errno = ENOMEM;
		break;
	default:
		errno = ENOENT;
		break;
	}
	return -1;
}

/* We provide our own lstat/fstat functions, since the provided
 * lstat/fstat functions are so slow. These stat functions are
 * tailored for Git's usage (read: fast), and are not meant to be
 * complete. Note that Git stat()s are redirected to git_lstat()
 * too, since Windows doesn't really handle symlinks that well.
 */
int git_lstat(const char *file_name, struct stat *buf)
{
	int namelen;
	static char alt_name[PATH_MAX];

	if (!do_lstat(file_name, buf))
		return 0;

	/* if file_name ended in a '/', Windows returned ENOENT;
	 * try again without trailing slashes
	 */
	if (errno != ENOENT)
		return -1;

	namelen = strlen(file_name);
	if (namelen && file_name[namelen-1] != '/')
		return -1;
	while (namelen && file_name[namelen-1] == '/')
		--namelen;
	if (!namelen || namelen >= PATH_MAX)
		return -1;

	memcpy(alt_name, file_name, namelen);
	alt_name[namelen] = 0;
	return do_lstat(alt_name, buf);
}

int git_fstat(int fd, struct stat *buf)
{
	HANDLE fh = (HANDLE)_get_osfhandle(fd);
	BY_HANDLE_FILE_INFORMATION fdata;
	char dummy[sizeof(void*)];
	int s = sizeof(void*);

	if (fh == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}
	if (GetFileInformationByHandle(fh, &fdata)) {
		int fMode = S_IREAD;
		if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			fMode |= S_IFDIR;
		else
			fMode |= S_IFREG;
		if (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
			fMode |= S_IWRITE;

		buf->st_ino = 0;
		buf->st_gid = 0;
		buf->st_uid = 0;
		buf->st_nlink = 1;
		buf->st_mode = fMode;
		buf->st_size = fdata.nFileSizeLow; /* Can't use nFileSizeHigh, since it's not a stat64 */
		buf->st_dev = buf->st_rdev = (_getdrive() - 1);
		buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
		buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
		buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
		return 0;
	}
	switch (GetLastError()) {
	case ERROR_INVALID_FUNCTION:
		/* check for socket */
		if (getsockopt((int)fh, SOL_SOCKET, SO_KEEPALIVE, dummy, &s) &&
		    WSAGetLastError() == WSAENOTSOCK)
			goto badf;
		memset(buf, sizeof(*buf), 0);
		buf->st_mode = S_IREAD|S_IWRITE;
		buf->st_mode |= 0x140000; /* S_IFSOCK */
		return 0;
	default:
	case ERROR_INVALID_HANDLE:
	badf:
		errno = EBADF;
		break;
	}
	return -1;
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
	return open(filename, O_RDWR | O_CREAT, 0600);
}

int gettimeofday(struct timeval *tv, void *tz)
{
	extern time_t my_mktime(struct tm *tm);
	SYSTEMTIME st;
	struct tm tm;
	GetSystemTime(&st);
	tm.tm_year = st.wYear-1900;
	tm.tm_mon = st.wMonth-1;
	tm.tm_mday = st.wDay;
	tm.tm_hour = st.wHour;
	tm.tm_min = st.wMinute;
	tm.tm_sec = st.wSecond;
	tv->tv_sec = my_mktime(&tm);
	if (tv->tv_sec < 0)
		return -1;
	tv->tv_usec = st.wMilliseconds*1000;
	return 0;
}

int pipe(int filedes[2])
{
	int fd;
	HANDLE h[2], parent;

	if (_pipe(filedes, 8192, 0) < 0)
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
	char *p, *opt;
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
	/* strip options */
	if ((opt = strchr(p+1, ' ')))
		*opt = '\0';
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
	sh_argv[1] = quote_arg(cmd);
	quote_argv(&sh_argv[2], &argv[1]);
	n = spawnvpe(_P_WAIT, interpr, sh_argv, env);
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

int mingw_socket(int domain, int type, int protocol)
{
	SOCKET s = WSASocket(domain, type, protocol, NULL, 0, 0);
	if (s == INVALID_SOCKET) {
		/*
		 * WSAGetLastError() values are regular BSD error codes
		 * biased by WSABASEERR.
		 * However, strerror() does not know about networking
		 * specific errors, which are values beginning at 38 or so.
		 * Therefore, we choose to leave the biased error code
		 * in errno so that _if_ someone looks up the code somewhere,
		 * then it is at least the number that are usually listed.
		 */
		errno = WSAGetLastError();
		return -1;
	}
	return s;
}

#undef rename
int mingw_rename(const char *pold, const char *pnew)
{
	/*
	 * Try native rename() first to get errno right.
	 * It is based on MoveFile(), which cannot overwrite existing files.
	 */
	if (!rename(pold, pnew))
		return 0;
	if (errno != EEXIST)
		return -1;
	if (MoveFileEx(pold, pnew, MOVEFILE_REPLACE_EXISTING))
		return 0;
	/* TODO: translate more errors */
	if (GetLastError() == ERROR_ACCESS_DENIED) {
		struct stat st;
		if (!stat(pnew, &st) && S_ISDIR(st.st_mode)) {
			errno = EISDIR;
			return -1;
		}
	}
	errno = EACCES;
	return -1;
}
