#include "../git-compat-util.h"
#include "../strbuf.h"

unsigned int _CRT_fmode = _O_BINARY;

#undef open
int mingw_open (const char *filename, int oflags, ...)
{
	va_list args;
	unsigned mode;
	va_start(args, oflags);
	mode = va_arg(args, int);
	va_end(args);

	if (!strcmp(filename, "/dev/null"))
		filename = "nul";
	int fd = open(filename, oflags, mode);
	if (fd < 0 && (oflags & O_CREAT) && errno == EACCES) {
		DWORD attrs = GetFileAttributes(filename);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
			errno = EISDIR;
	}
	return fd;
}

static inline time_t filetime_to_time_t(const FILETIME *ft)
{
	long long winTime = ((long long)ft->dwHighDateTime << 32) + ft->dwLowDateTime;
	winTime -= 116444736000000000LL; /* Windows to Unix Epoch conversion */
	winTime /= 10000000;		 /* Nano to seconds resolution */
	return (time_t)winTime;
}

static inline size_t size_to_blocks(size_t s)
{
	return (s+511)/512;
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
		buf->st_mode = fMode;
		buf->st_size = fdata.nFileSizeLow; /* Can't use nFileSizeHigh, since it's not a stat64 */
		buf->st_blocks = size_to_blocks(buf->st_size);
		buf->st_dev = _getdrive() - 1;
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
 * complete. Note that Git stat()s are redirected to mingw_lstat()
 * too, since Windows doesn't really handle symlinks that well.
 */
int mingw_lstat(const char *file_name, struct mingw_stat *buf)
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

#undef fstat
#undef stat
int mingw_fstat(int fd, struct mingw_stat *buf)
{
	HANDLE fh = (HANDLE)_get_osfhandle(fd);
	BY_HANDLE_FILE_INFORMATION fdata;

	if (fh == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}
	/* direct non-file handles to MS's fstat() */
	if (GetFileType(fh) != FILE_TYPE_DISK) {
		struct stat st;
		if (fstat(fd, &st))
			return -1;
		buf->st_ino = st.st_ino;
		buf->st_gid = st.st_gid;
		buf->st_uid = st.st_uid;
		buf->st_mode = st.st_mode;
		buf->st_size = st.st_size;
		buf->st_blocks = size_to_blocks(buf->st_size);
		buf->st_dev = st.st_dev;
		buf->st_atime = st.st_atime;
		buf->st_mtime = st.st_mtime;
		buf->st_ctime = st.st_ctime;
		return 0;
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
		buf->st_mode = fMode;
		buf->st_size = fdata.nFileSizeLow; /* Can't use nFileSizeHigh, since it's not a stat64 */
		buf->st_blocks = size_to_blocks(buf->st_size);
		buf->st_dev = _getdrive() - 1;
		buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
		buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
		buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
		return 0;
	}
	errno = EBADF;
	return -1;
}

static inline void time_t_to_filetime(time_t t, FILETIME *ft)
{
	long long winTime = t * 10000000LL + 116444736000000000LL;
	ft->dwLowDateTime = winTime;
	ft->dwHighDateTime = winTime >> 32;
}

int mingw_utime (const char *file_name, const struct utimbuf *times)
{
	FILETIME mft, aft;
	int fh, rc;

	/* must have write permission */
	if ((fh = open(file_name, O_RDWR | O_BINARY)) < 0)
		return -1;

	time_t_to_filetime(times->modtime, &mft);
	time_t_to_filetime(times->actime, &aft);
	if (!SetFileTime((HANDLE)_get_osfhandle(fh), NULL, &aft, &mft)) {
		errno = EINVAL;
		rc = -1;
	} else
		rc = 0;
	close(fh);
	return rc;
}

unsigned int sleep (unsigned int seconds)
{
	Sleep(seconds*1000);
	return 0;
}

int mkstemp(char *template)
{
	char *filename = mktemp(template);
	if (filename == NULL)
		return -1;
	return open(filename, O_RDWR | O_CREAT, 0600);
}

int gettimeofday(struct timeval *tv, void *tz)
{
	SYSTEMTIME st;
	struct tm tm;
	GetSystemTime(&st);
	tm.tm_year = st.wYear-1900;
	tm.tm_mon = st.wMonth-1;
	tm.tm_mday = st.wDay;
	tm.tm_hour = st.wHour;
	tm.tm_min = st.wMinute;
	tm.tm_sec = st.wSecond;
	tv->tv_sec = tm_to_time_t(&tm);
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
	fd = _open_osfhandle((int)h[0], O_NOINHERIT);
	if (fd < 0) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[0]);
		CloseHandle(h[1]);
		return -1;
	}
	close(filedes[0]);
	filedes[0] = fd;
	fd = _open_osfhandle((int)h[1], O_NOINHERIT);
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
	int i, pending;

	if (timeout != -1)
		return errno = EINVAL, error("poll timeout not supported");

	/* When there is only one fd to wait for, then we pretend that
	 * input is available and let the actual wait happen when the
	 * caller invokes read().
	 */
	if (nfds == 1) {
		if (!(ufds[0].events & POLLIN))
			return errno = EINVAL, error("POLLIN not set");
		ufds[0].revents = POLLIN;
		return 0;
	}

repeat:
	pending = 0;
	for (i = 0; i < nfds; i++) {
		DWORD avail = 0;
		HANDLE h = (HANDLE) _get_osfhandle(ufds[i].fd);
		if (h == INVALID_HANDLE_VALUE)
			return -1;	/* errno was set */

		if (!(ufds[i].events & POLLIN))
			return errno = EINVAL, error("POLLIN not set");

		/* this emulation works only for pipes */
		if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
			int err = GetLastError();
			if (err == ERROR_BROKEN_PIPE) {
				ufds[i].revents = POLLHUP;
				pending++;
			} else {
				errno = EINVAL;
				return error("PeekNamedPipe failed,"
					" GetLastError: %u", err);
			}
		} else if (avail) {
			ufds[i].revents = POLLIN;
			pending++;
		} else
			ufds[i].revents = 0;
	}
	if (!pending) {
		/* The only times that we spin here is when the process
		 * that is connected through the pipes is waiting for
		 * its own input data to become available. But since
		 * the process (pack-objects) is itself CPU intensive,
		 * it will happily pick up the time slice that we are
		 * relinguishing here.
		 */
		Sleep(0);
		goto repeat;
	}
	return 0;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
	/* gmtime() in MSVCRT.DLL is thread-safe, but not reentrant */
	memcpy(result, gmtime(timep), sizeof(struct tm));
	return result;
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
	/* localtime() in MSVCRT.DLL is thread-safe, but not reentrant */
	memcpy(result, localtime(timep), sizeof(struct tm));
	return result;
}

#undef getcwd
char *mingw_getcwd(char *pointer, int len)
{
	int i;
	char *ret = getcwd(pointer, len);
	if (!ret)
		return ret;
	for (i = 0; pointer[i]; i++)
		if (pointer[i] == '\\')
			pointer[i] = '/';
	return ret;
}

#undef getenv
char *mingw_getenv(const char *name)
{
	char *result = getenv(name);
	if (!result && !strcmp(name, "TMPDIR")) {
		/* on Windows it is TMP and TEMP */
		result = getenv("TMP");
		if (!result)
			result = getenv("TEMP");
	}
	return result;
}

/*
 * See http://msdn2.microsoft.com/en-us/library/17w5ykft(vs.71).aspx
 * (Parsing C++ Command-Line Arguments)
 */
static const char *quote_arg(const char *arg)
{
	/* count chars to quote */
	int len = 0, n = 0;
	int force_quotes = 0;
	char *q, *d;
	const char *p = arg;
	if (!*p) force_quotes = 1;
	while (*p) {
		if (isspace(*p) || *p == '*' || *p == '?' || *p == '{')
			force_quotes = 1;
		else if (*p == '"')
			n++;
		else if (*p == '\\') {
			int count = 0;
			while (*p == '\\') {
				count++;
				p++;
				len++;
			}
			if (*p == '"')
				n += count*2 + 1;
			continue;
		}
		len++;
		p++;
	}
	if (!force_quotes && n == 0)
		return arg;

	/* insert \ where necessary */
	d = q = xmalloc(len+n+3);
	*d++ = '"';
	while (*arg) {
		if (*arg == '"')
			*d++ = '\\';
		else if (*arg == '\\') {
			int count = 0;
			while (*arg == '\\') {
				count++;
				*d++ = *arg++;
			}
			if (*arg == '"') {
				while (count-- > 0)
					*d++ = '\\';
				*d++ = '\\';
			}
		}
		*d++ = *arg++;
	}
	*d++ = '"';
	*d++ = 0;
	return q;
}

static const char *parse_interpreter(const char *cmd)
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

/*
 * Splits the PATH into parts.
 */
static char **get_path_split(void)
{
	char *p, **path, *envpath = getenv("PATH");
	int i, n = 0;

	if (!envpath || !*envpath)
		return NULL;

	envpath = xstrdup(envpath);
	p = envpath;
	while (p) {
		char *dir = p;
		p = strchr(p, ';');
		if (p) *p++ = '\0';
		if (*dir) {	/* not earlier, catches series of ; */
			++n;
		}
	}
	if (!n)
		return NULL;

	path = xmalloc((n+1)*sizeof(char*));
	p = envpath;
	i = 0;
	do {
		if (*p)
			path[i++] = xstrdup(p);
		p = p+strlen(p)+1;
	} while (i < n);
	path[i] = NULL;

	free(envpath);

	return path;
}

static void free_path_split(char **path)
{
	if (!path)
		return;

	char **p = path;
	while (*p)
		free(*p++);
	free(path);
}

/*
 * exe_only means that we only want to detect .exe files, but not scripts
 * (which do not have an extension)
 */
static char *lookup_prog(const char *dir, const char *cmd, int isexe, int exe_only)
{
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s.exe", dir, cmd);

	if (!isexe && access(path, F_OK) == 0)
		return xstrdup(path);
	path[strlen(path)-4] = '\0';
	if ((!exe_only || isexe) && access(path, F_OK) == 0)
		if (!(GetFileAttributes(path) & FILE_ATTRIBUTE_DIRECTORY))
			return xstrdup(path);
	return NULL;
}

/*
 * Determines the absolute path of cmd using the the split path in path.
 * If cmd contains a slash or backslash, no lookup is performed.
 */
static char *path_lookup(const char *cmd, char **path, int exe_only)
{
	char *prog = NULL;
	int len = strlen(cmd);
	int isexe = len >= 4 && !strcasecmp(cmd+len-4, ".exe");

	if (strchr(cmd, '/') || strchr(cmd, '\\'))
		prog = xstrdup(cmd);

	while (!prog && *path)
		prog = lookup_prog(*path++, cmd, isexe, exe_only);

	return prog;
}

static int env_compare(const void *a, const void *b)
{
	char *const *ea = a;
	char *const *eb = b;
	return strcasecmp(*ea, *eb);
}

static pid_t mingw_spawnve(const char *cmd, const char **argv, char **env,
			   int prepend_cmd)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	struct strbuf envblk, args;
	unsigned flags;
	BOOL ret;

	/* Determine whether or not we are associated to a console */
	HANDLE cons = CreateFile("CONOUT$", GENERIC_WRITE,
			FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
	if (cons == INVALID_HANDLE_VALUE) {
		/* There is no console associated with this process.
		 * Since the child is a console process, Windows
		 * would normally create a console window. But
		 * since we'll be redirecting std streams, we do
		 * not need the console.
		 */
		flags = CREATE_NO_WINDOW;
	} else {
		/* There is already a console. If we specified
		 * CREATE_NO_WINDOW here, too, Windows would
		 * disassociate the child from the console.
		 * Go figure!
		 */
		flags = 0;
		CloseHandle(cons);
	}
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = (HANDLE) _get_osfhandle(0);
	si.hStdOutput = (HANDLE) _get_osfhandle(1);
	si.hStdError = (HANDLE) _get_osfhandle(2);

	/* concatenate argv, quoting args as we go */
	strbuf_init(&args, 0);
	if (prepend_cmd) {
		char *quoted = (char *)quote_arg(cmd);
		strbuf_addstr(&args, quoted);
		if (quoted != cmd)
			free(quoted);
	}
	for (; *argv; argv++) {
		char *quoted = (char *)quote_arg(*argv);
		if (*args.buf)
			strbuf_addch(&args, ' ');
		strbuf_addstr(&args, quoted);
		if (quoted != *argv)
			free(quoted);
	}

	if (env) {
		int count = 0;
		char **e, **sorted_env;

		for (e = env; *e; e++)
			count++;

		/* environment must be sorted */
		sorted_env = xmalloc(sizeof(*sorted_env) * (count + 1));
		memcpy(sorted_env, env, sizeof(*sorted_env) * (count + 1));
		qsort(sorted_env, count, sizeof(*sorted_env), env_compare);

		strbuf_init(&envblk, 0);
		for (e = sorted_env; *e; e++) {
			strbuf_addstr(&envblk, *e);
			strbuf_addch(&envblk, '\0');
		}
		free(sorted_env);
	}

	memset(&pi, 0, sizeof(pi));
	ret = CreateProcess(cmd, args.buf, NULL, NULL, TRUE, flags,
		env ? envblk.buf : NULL, NULL, &si, &pi);

	if (env)
		strbuf_release(&envblk);
	strbuf_release(&args);

	if (!ret) {
		errno = ENOENT;
		return -1;
	}
	CloseHandle(pi.hThread);
	return (pid_t)pi.hProcess;
}

pid_t mingw_spawnvpe(const char *cmd, const char **argv, char **env)
{
	pid_t pid;
	char **path = get_path_split();
	char *prog = path_lookup(cmd, path, 0);

	if (!prog) {
		errno = ENOENT;
		pid = -1;
	}
	else {
		const char *interpr = parse_interpreter(prog);

		if (interpr) {
			const char *argv0 = argv[0];
			char *iprog = path_lookup(interpr, path, 1);
			argv[0] = prog;
			if (!iprog) {
				errno = ENOENT;
				pid = -1;
			}
			else {
				pid = mingw_spawnve(iprog, argv, env, 1);
				free(iprog);
			}
			argv[0] = argv0;
		}
		else
			pid = mingw_spawnve(prog, argv, env, 0);
		free(prog);
	}
	free_path_split(path);
	return pid;
}

static int try_shell_exec(const char *cmd, char *const *argv, char **env)
{
	const char *interpr = parse_interpreter(cmd);
	char **path;
	char *prog;
	int pid = 0;

	if (!interpr)
		return 0;
	path = get_path_split();
	prog = path_lookup(interpr, path, 1);
	if (prog) {
		int argc = 0;
		const char **argv2;
		while (argv[argc]) argc++;
		argv2 = xmalloc(sizeof(*argv) * (argc+1));
		argv2[0] = (char *)cmd;	/* full path to the script file */
		memcpy(&argv2[1], &argv[1], sizeof(*argv) * argc);
		pid = mingw_spawnve(prog, argv2, env, 1);
		if (pid >= 0) {
			int status;
			if (waitpid(pid, &status, 0) < 0)
				status = 255;
			exit(status);
		}
		pid = 1;	/* indicate that we tried but failed */
		free(prog);
		free(argv2);
	}
	free_path_split(path);
	return pid;
}

static void mingw_execve(const char *cmd, char *const *argv, char *const *env)
{
	/* check if git_command is a shell script */
	if (!try_shell_exec(cmd, argv, (char **)env)) {
		int pid, status;

		pid = mingw_spawnve(cmd, (const char **)argv, (char **)env, 0);
		if (pid < 0)
			return;
		if (waitpid(pid, &status, 0) < 0)
			status = 255;
		exit(status);
	}
}

void mingw_execvp(const char *cmd, char *const *argv)
{
	char **path = get_path_split();
	char *prog = path_lookup(cmd, path, 0);

	if (prog) {
		mingw_execve(prog, argv, environ);
		free(prog);
	} else
		errno = ENOENT;

	free_path_split(path);
}

char **copy_environ()
{
	char **env;
	int i = 0;
	while (environ[i])
		i++;
	env = xmalloc((i+1)*sizeof(*env));
	for (i = 0; environ[i]; i++)
		env[i] = xstrdup(environ[i]);
	env[i] = NULL;
	return env;
}

void free_environ(char **env)
{
	int i;
	for (i = 0; env[i]; i++)
		free(env[i]);
	free(env);
}

static int lookup_env(char **env, const char *name, size_t nmln)
{
	int i;

	for (i = 0; env[i]; i++) {
		if (0 == strncmp(env[i], name, nmln)
		    && '=' == env[i][nmln])
			/* matches */
			return i;
	}
	return -1;
}

/*
 * If name contains '=', then sets the variable, otherwise it unsets it
 */
char **env_setenv(char **env, const char *name)
{
	char *eq = strchrnul(name, '=');
	int i = lookup_env(env, name, eq-name);

	if (i < 0) {
		if (*eq) {
			for (i = 0; env[i]; i++)
				;
			env = xrealloc(env, (i+2)*sizeof(*env));
			env[i] = xstrdup(name);
			env[i+1] = NULL;
		}
	}
	else {
		free(env[i]);
		if (*eq)
			env[i] = xstrdup(name);
		else
			for (; env[i]; i++)
				env[i] = env[i+1];
	}
	return env;
}

/* this is the first function to call into WS_32; initialize it */
#undef gethostbyname
struct hostent *mingw_gethostbyname(const char *host)
{
	WSADATA wsa;

	if (WSAStartup(MAKEWORD(2,2), &wsa))
		die("unable to initialize winsock subsystem, error %d",
			WSAGetLastError());
	atexit((void(*)(void)) WSACleanup);
	return gethostbyname(host);
}

int mingw_socket(int domain, int type, int protocol)
{
	int sockfd;
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
	/* convert into a file descriptor */
	if ((sockfd = _open_osfhandle(s, O_RDWR|O_BINARY)) < 0) {
		closesocket(s);
		return error("unable to make a socket file descriptor: %s",
			strerror(errno));
	}
	return sockfd;
}

#undef connect
int mingw_connect(int sockfd, struct sockaddr *sa, size_t sz)
{
	SOCKET s = (SOCKET)_get_osfhandle(sockfd);
	return connect(s, sa, sz);
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
		DWORD attrs = GetFileAttributes(pnew);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
			errno = EISDIR;
			return -1;
		}
	}
	errno = EACCES;
	return -1;
}

struct passwd *getpwuid(int uid)
{
	static char user_name[100];
	static struct passwd p;

	DWORD len = sizeof(user_name);
	if (!GetUserName(user_name, &len))
		return NULL;
	p.pw_name = user_name;
	p.pw_gecos = "unknown";
	p.pw_dir = NULL;
	return &p;
}

static HANDLE timer_event;
static HANDLE timer_thread;
static int timer_interval;
static int one_shot;
static sig_handler_t timer_fn = SIG_DFL;

/* The timer works like this:
 * The thread, ticktack(), is a trivial routine that most of the time
 * only waits to receive the signal to terminate. The main thread tells
 * the thread to terminate by setting the timer_event to the signalled
 * state.
 * But ticktack() interrupts the wait state after the timer's interval
 * length to call the signal handler.
 */

static __stdcall unsigned ticktack(void *dummy)
{
	while (WaitForSingleObject(timer_event, timer_interval) == WAIT_TIMEOUT) {
		if (timer_fn == SIG_DFL)
			die("Alarm");
		if (timer_fn != SIG_IGN)
			timer_fn(SIGALRM);
		if (one_shot)
			break;
	}
	return 0;
}

static int start_timer_thread(void)
{
	timer_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (timer_event) {
		timer_thread = (HANDLE) _beginthreadex(NULL, 0, ticktack, NULL, 0, NULL);
		if (!timer_thread )
			return errno = ENOMEM,
				error("cannot start timer thread");
	} else
		return errno = ENOMEM,
			error("cannot allocate resources for timer");
	return 0;
}

static void stop_timer_thread(void)
{
	if (timer_event)
		SetEvent(timer_event);	/* tell thread to terminate */
	if (timer_thread) {
		int rc = WaitForSingleObject(timer_thread, 1000);
		if (rc == WAIT_TIMEOUT)
			error("timer thread did not terminate timely");
		else if (rc != WAIT_OBJECT_0)
			error("waiting for timer thread failed: %lu",
			      GetLastError());
		CloseHandle(timer_thread);
	}
	if (timer_event)
		CloseHandle(timer_event);
	timer_event = NULL;
	timer_thread = NULL;
}

static inline int is_timeval_eq(const struct timeval *i1, const struct timeval *i2)
{
	return i1->tv_sec == i2->tv_sec && i1->tv_usec == i2->tv_usec;
}

int setitimer(int type, struct itimerval *in, struct itimerval *out)
{
	static const struct timeval zero;
	static int atexit_done;

	if (out != NULL)
		return errno = EINVAL,
			error("setitimer param 3 != NULL not implemented");
	if (!is_timeval_eq(&in->it_interval, &zero) &&
	    !is_timeval_eq(&in->it_interval, &in->it_value))
		return errno = EINVAL,
			error("setitimer: it_interval must be zero or eq it_value");

	if (timer_thread)
		stop_timer_thread();

	if (is_timeval_eq(&in->it_value, &zero) &&
	    is_timeval_eq(&in->it_interval, &zero))
		return 0;

	timer_interval = in->it_value.tv_sec * 1000 + in->it_value.tv_usec / 1000;
	one_shot = is_timeval_eq(&in->it_interval, &zero);
	if (!atexit_done) {
		atexit(stop_timer_thread);
		atexit_done = 1;
	}
	return start_timer_thread();
}

int sigaction(int sig, struct sigaction *in, struct sigaction *out)
{
	if (sig != SIGALRM)
		return errno = EINVAL,
			error("sigaction only implemented for SIGALRM");
	if (out != NULL)
		return errno = EINVAL,
			error("sigaction: param 3 != NULL not implemented");

	timer_fn = in->sa_handler;
	return 0;
}

#undef signal
sig_handler_t mingw_signal(int sig, sig_handler_t handler)
{
	if (sig != SIGALRM)
		return signal(sig, handler);
	sig_handler_t old = timer_fn;
	timer_fn = handler;
	return old;
}

static const char *make_backslash_path(const char *path)
{
	static char buf[PATH_MAX + 1];
	char *c;

	if (strlcpy(buf, path, PATH_MAX) >= PATH_MAX)
		die("Too long path: %.*s", 60, path);

	for (c = buf; *c; c++) {
		if (*c == '/')
			*c = '\\';
	}
	return buf;
}

void mingw_open_html(const char *unixpath)
{
	const char *htmlpath = make_backslash_path(unixpath);
	printf("Launching default browser to display HTML ...\n");
	ShellExecute(NULL, "open", htmlpath, NULL, "\\", 0);
}
