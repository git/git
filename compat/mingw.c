#include "../git-compat-util.h"
#include "win32.h"
#include <conio.h>
#include "../strbuf.h"

int err_win_to_posix(DWORD winerr)
{
	int error = ENOSYS;
	switch(winerr) {
	case ERROR_ACCESS_DENIED: error = EACCES; break;
	case ERROR_ACCOUNT_DISABLED: error = EACCES; break;
	case ERROR_ACCOUNT_RESTRICTION: error = EACCES; break;
	case ERROR_ALREADY_ASSIGNED: error = EBUSY; break;
	case ERROR_ALREADY_EXISTS: error = EEXIST; break;
	case ERROR_ARITHMETIC_OVERFLOW: error = ERANGE; break;
	case ERROR_BAD_COMMAND: error = EIO; break;
	case ERROR_BAD_DEVICE: error = ENODEV; break;
	case ERROR_BAD_DRIVER_LEVEL: error = ENXIO; break;
	case ERROR_BAD_EXE_FORMAT: error = ENOEXEC; break;
	case ERROR_BAD_FORMAT: error = ENOEXEC; break;
	case ERROR_BAD_LENGTH: error = EINVAL; break;
	case ERROR_BAD_PATHNAME: error = ENOENT; break;
	case ERROR_BAD_PIPE: error = EPIPE; break;
	case ERROR_BAD_UNIT: error = ENODEV; break;
	case ERROR_BAD_USERNAME: error = EINVAL; break;
	case ERROR_BROKEN_PIPE: error = EPIPE; break;
	case ERROR_BUFFER_OVERFLOW: error = ENAMETOOLONG; break;
	case ERROR_BUSY: error = EBUSY; break;
	case ERROR_BUSY_DRIVE: error = EBUSY; break;
	case ERROR_CALL_NOT_IMPLEMENTED: error = ENOSYS; break;
	case ERROR_CANNOT_MAKE: error = EACCES; break;
	case ERROR_CANTOPEN: error = EIO; break;
	case ERROR_CANTREAD: error = EIO; break;
	case ERROR_CANTWRITE: error = EIO; break;
	case ERROR_CRC: error = EIO; break;
	case ERROR_CURRENT_DIRECTORY: error = EACCES; break;
	case ERROR_DEVICE_IN_USE: error = EBUSY; break;
	case ERROR_DEV_NOT_EXIST: error = ENODEV; break;
	case ERROR_DIRECTORY: error = EINVAL; break;
	case ERROR_DIR_NOT_EMPTY: error = ENOTEMPTY; break;
	case ERROR_DISK_CHANGE: error = EIO; break;
	case ERROR_DISK_FULL: error = ENOSPC; break;
	case ERROR_DRIVE_LOCKED: error = EBUSY; break;
	case ERROR_ENVVAR_NOT_FOUND: error = EINVAL; break;
	case ERROR_EXE_MARKED_INVALID: error = ENOEXEC; break;
	case ERROR_FILENAME_EXCED_RANGE: error = ENAMETOOLONG; break;
	case ERROR_FILE_EXISTS: error = EEXIST; break;
	case ERROR_FILE_INVALID: error = ENODEV; break;
	case ERROR_FILE_NOT_FOUND: error = ENOENT; break;
	case ERROR_GEN_FAILURE: error = EIO; break;
	case ERROR_HANDLE_DISK_FULL: error = ENOSPC; break;
	case ERROR_INSUFFICIENT_BUFFER: error = ENOMEM; break;
	case ERROR_INVALID_ACCESS: error = EACCES; break;
	case ERROR_INVALID_ADDRESS: error = EFAULT; break;
	case ERROR_INVALID_BLOCK: error = EFAULT; break;
	case ERROR_INVALID_DATA: error = EINVAL; break;
	case ERROR_INVALID_DRIVE: error = ENODEV; break;
	case ERROR_INVALID_EXE_SIGNATURE: error = ENOEXEC; break;
	case ERROR_INVALID_FLAGS: error = EINVAL; break;
	case ERROR_INVALID_FUNCTION: error = ENOSYS; break;
	case ERROR_INVALID_HANDLE: error = EBADF; break;
	case ERROR_INVALID_LOGON_HOURS: error = EACCES; break;
	case ERROR_INVALID_NAME: error = EINVAL; break;
	case ERROR_INVALID_OWNER: error = EINVAL; break;
	case ERROR_INVALID_PARAMETER: error = EINVAL; break;
	case ERROR_INVALID_PASSWORD: error = EPERM; break;
	case ERROR_INVALID_PRIMARY_GROUP: error = EINVAL; break;
	case ERROR_INVALID_SIGNAL_NUMBER: error = EINVAL; break;
	case ERROR_INVALID_TARGET_HANDLE: error = EIO; break;
	case ERROR_INVALID_WORKSTATION: error = EACCES; break;
	case ERROR_IO_DEVICE: error = EIO; break;
	case ERROR_IO_INCOMPLETE: error = EINTR; break;
	case ERROR_LOCKED: error = EBUSY; break;
	case ERROR_LOCK_VIOLATION: error = EACCES; break;
	case ERROR_LOGON_FAILURE: error = EACCES; break;
	case ERROR_MAPPED_ALIGNMENT: error = EINVAL; break;
	case ERROR_META_EXPANSION_TOO_LONG: error = E2BIG; break;
	case ERROR_MORE_DATA: error = EPIPE; break;
	case ERROR_NEGATIVE_SEEK: error = ESPIPE; break;
	case ERROR_NOACCESS: error = EFAULT; break;
	case ERROR_NONE_MAPPED: error = EINVAL; break;
	case ERROR_NOT_ENOUGH_MEMORY: error = ENOMEM; break;
	case ERROR_NOT_READY: error = EAGAIN; break;
	case ERROR_NOT_SAME_DEVICE: error = EXDEV; break;
	case ERROR_NO_DATA: error = EPIPE; break;
	case ERROR_NO_MORE_SEARCH_HANDLES: error = EIO; break;
	case ERROR_NO_PROC_SLOTS: error = EAGAIN; break;
	case ERROR_NO_SUCH_PRIVILEGE: error = EACCES; break;
	case ERROR_OPEN_FAILED: error = EIO; break;
	case ERROR_OPEN_FILES: error = EBUSY; break;
	case ERROR_OPERATION_ABORTED: error = EINTR; break;
	case ERROR_OUTOFMEMORY: error = ENOMEM; break;
	case ERROR_PASSWORD_EXPIRED: error = EACCES; break;
	case ERROR_PATH_BUSY: error = EBUSY; break;
	case ERROR_PATH_NOT_FOUND: error = ENOENT; break;
	case ERROR_PIPE_BUSY: error = EBUSY; break;
	case ERROR_PIPE_CONNECTED: error = EPIPE; break;
	case ERROR_PIPE_LISTENING: error = EPIPE; break;
	case ERROR_PIPE_NOT_CONNECTED: error = EPIPE; break;
	case ERROR_PRIVILEGE_NOT_HELD: error = EACCES; break;
	case ERROR_READ_FAULT: error = EIO; break;
	case ERROR_SEEK: error = EIO; break;
	case ERROR_SEEK_ON_DEVICE: error = ESPIPE; break;
	case ERROR_SHARING_BUFFER_EXCEEDED: error = ENFILE; break;
	case ERROR_SHARING_VIOLATION: error = EACCES; break;
	case ERROR_STACK_OVERFLOW: error = ENOMEM; break;
	case ERROR_SWAPERROR: error = ENOENT; break;
	case ERROR_TOO_MANY_MODULES: error = EMFILE; break;
	case ERROR_TOO_MANY_OPEN_FILES: error = EMFILE; break;
	case ERROR_UNRECOGNIZED_MEDIA: error = ENXIO; break;
	case ERROR_UNRECOGNIZED_VOLUME: error = ENODEV; break;
	case ERROR_WAIT_NO_CHILDREN: error = ECHILD; break;
	case ERROR_WRITE_FAULT: error = EIO; break;
	case ERROR_WRITE_PROTECT: error = EROFS; break;
	}
	return error;
}

#undef open
int mingw_open (const char *filename, int oflags, ...)
{
	va_list args;
	unsigned mode;
	int fd;

	va_start(args, oflags);
	mode = va_arg(args, int);
	va_end(args);

	if (!strcmp(filename, "/dev/null"))
		filename = "nul";

	fd = open(filename, oflags, mode);

	if (fd < 0 && (oflags & O_CREAT) && errno == EACCES) {
		DWORD attrs = GetFileAttributes(filename);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
			errno = EISDIR;
	}
	return fd;
}

#undef write
ssize_t mingw_write(int fd, const void *buf, size_t count)
{
	/*
	 * While write() calls to a file on a local disk are translated
	 * into WriteFile() calls with a maximum size of 64KB on Windows
	 * XP and 256KB on Vista, no such cap is placed on writes to
	 * files over the network on Windows XP.  Unfortunately, there
	 * seems to be a limit of 32MB-28KB on X64 and 64MB-32KB on x86;
	 * bigger writes fail on Windows XP.
	 * So we cap to a nice 31MB here to avoid write failures over
	 * the net without changing the number of WriteFile() calls in
	 * the local case.
	 */
	return write(fd, buf, min(count, 31 * 1024 * 1024));
}

#undef fopen
FILE *mingw_fopen (const char *filename, const char *otype)
{
	if (!strcmp(filename, "/dev/null"))
		filename = "nul";
	return fopen(filename, otype);
}

#undef freopen
FILE *mingw_freopen (const char *filename, const char *otype, FILE *stream)
{
	if (filename && !strcmp(filename, "/dev/null"))
		filename = "nul";
	return freopen(filename, otype, stream);
}

/*
 * The unit of FILETIME is 100-nanoseconds since January 1, 1601, UTC.
 * Returns the 100-nanoseconds ("hekto nanoseconds") since the epoch.
 */
static inline long long filetime_to_hnsec(const FILETIME *ft)
{
	long long winTime = ((long long)ft->dwHighDateTime << 32) + ft->dwLowDateTime;
	/* Windows to Unix Epoch conversion */
	return winTime - 116444736000000000LL;
}

static inline time_t filetime_to_time_t(const FILETIME *ft)
{
	return (time_t)(filetime_to_hnsec(ft) / 10000000);
}

/* We keep the do_lstat code in a separate function to avoid recursion.
 * When a path ends with a slash, the stat will fail with ENOENT. In
 * this case, we strip the trailing slashes and stat again.
 */
static int do_lstat(const char *file_name, struct stat *buf)
{
	WIN32_FILE_ATTRIBUTE_DATA fdata;

	if (!(errno = get_file_attr(file_name, &fdata))) {
		buf->st_ino = 0;
		buf->st_gid = 0;
		buf->st_uid = 0;
		buf->st_nlink = 1;
		buf->st_mode = file_attr_to_st_mode(fdata.dwFileAttributes);
		buf->st_size = fdata.nFileSizeLow |
			(((off_t)fdata.nFileSizeHigh)<<32);
		buf->st_dev = buf->st_rdev = 0; /* not used by Git */
		buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
		buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
		buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
		return 0;
	}
	return -1;
}

/* We provide our own lstat/fstat functions, since the provided
 * lstat/fstat functions are so slow. These stat functions are
 * tailored for Git's usage (read: fast), and are not meant to be
 * complete. Note that Git stat()s are redirected to mingw_lstat()
 * too, since Windows doesn't really handle symlinks that well.
 */
int mingw_lstat(const char *file_name, struct stat *buf)
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
int mingw_fstat(int fd, struct stat *buf)
{
	HANDLE fh = (HANDLE)_get_osfhandle(fd);
	BY_HANDLE_FILE_INFORMATION fdata;

	if (fh == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}
	/* direct non-file handles to MS's fstat() */
	if (GetFileType(fh) != FILE_TYPE_DISK)
		return _fstati64(fd, buf);

	if (GetFileInformationByHandle(fh, &fdata)) {
		buf->st_ino = 0;
		buf->st_gid = 0;
		buf->st_uid = 0;
		buf->st_nlink = 1;
		buf->st_mode = file_attr_to_st_mode(fdata.dwFileAttributes);
		buf->st_size = fdata.nFileSizeLow |
			(((off_t)fdata.nFileSizeHigh)<<32);
		buf->st_dev = buf->st_rdev = 0; /* not used by Git */
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
	DWORD attrs = GetFileAttributes(file_name);
	if (attrs != INVALID_FILE_ATTRIBUTES &&
	    (attrs & FILE_ATTRIBUTE_READONLY)) {
		/* ignore errors here; open() will report them */
		SetFileAttributes(file_name, attrs & ~FILE_ATTRIBUTE_READONLY);
	}

	if ((fh = open(file_name, O_RDWR | O_BINARY)) < 0) {
		rc = -1;
		goto revert_attrs;
	}

	if (times) {
		time_t_to_filetime(times->modtime, &mft);
		time_t_to_filetime(times->actime, &aft);
	} else {
		GetSystemTimeAsFileTime(&mft);
		aft = mft;
	}
	if (!SetFileTime((HANDLE)_get_osfhandle(fh), NULL, &aft, &mft)) {
		errno = EINVAL;
		rc = -1;
	} else
		rc = 0;
	close(fh);

revert_attrs:
	if (attrs != INVALID_FILE_ATTRIBUTES &&
	    (attrs & FILE_ATTRIBUTE_READONLY)) {
		/* ignore errors again */
		SetFileAttributes(file_name, attrs);
	}
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
	FILETIME ft;
	long long hnsec;

	GetSystemTimeAsFileTime(&ft);
	hnsec = filetime_to_hnsec(&ft);
	tv->tv_sec = hnsec / 10000000;
	tv->tv_usec = (hnsec % 10000000) / 10;
	return 0;
}

int pipe(int filedes[2])
{
	HANDLE h[2];

	/* this creates non-inheritable handles */
	if (!CreatePipe(&h[0], &h[1], NULL, 8192)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	filedes[0] = _open_osfhandle((int)h[0], O_NOINHERIT);
	if (filedes[0] < 0) {
		CloseHandle(h[0]);
		CloseHandle(h[1]);
		return -1;
	}
	filedes[1] = _open_osfhandle((int)h[1], O_NOINHERIT);
	if (filedes[0] < 0) {
		close(filedes[0]);
		CloseHandle(h[1]);
		return -1;
	}
	return 0;
}

int poll(struct pollfd *ufds, unsigned int nfds, int timeout)
{
	int i, pending;

	if (timeout >= 0) {
		if (nfds == 0) {
			Sleep(timeout);
			return 0;
		}
		return errno = EINVAL, error("poll timeout not supported");
	}

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
		 * relinquishing here.
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
		if (isspace(*p) || *p == '*' || *p == '?' || *p == '{' || *p == '\'')
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
	p = buf + strcspn(buf, "\r\n");
	if (!*p)
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

	path = xmalloc((n+1)*sizeof(char *));
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
	char **p = path;

	if (!path)
		return;

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
 * Determines the absolute path of cmd using the split path in path.
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

static pid_t mingw_spawnve_fd(const char *cmd, const char **argv, char **env,
			      const char *dir,
			      int prepend_cmd, int fhin, int fhout, int fherr)
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
		 * It is necessary to use DETACHED_PROCESS
		 * instead of CREATE_NO_WINDOW to make ssh
		 * recognize that it has no console.
		 */
		flags = DETACHED_PROCESS;
	} else {
		/* There is already a console. If we specified
		 * DETACHED_PROCESS here, too, Windows would
		 * disassociate the child from the console.
		 * The same is true for CREATE_NO_WINDOW.
		 * Go figure!
		 */
		flags = 0;
		CloseHandle(cons);
	}
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = (HANDLE) _get_osfhandle(fhin);
	si.hStdOutput = (HANDLE) _get_osfhandle(fhout);
	si.hStdError = (HANDLE) _get_osfhandle(fherr);

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
		env ? envblk.buf : NULL, dir, &si, &pi);

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

static pid_t mingw_spawnve(const char *cmd, const char **argv, char **env,
			   int prepend_cmd)
{
	return mingw_spawnve_fd(cmd, argv, env, NULL, prepend_cmd, 0, 1, 2);
}

pid_t mingw_spawnvpe(const char *cmd, const char **argv, char **env,
		     const char *dir,
		     int fhin, int fhout, int fherr)
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
				pid = mingw_spawnve_fd(iprog, argv, env, dir, 1,
						       fhin, fhout, fherr);
				free(iprog);
			}
			argv[0] = argv0;
		}
		else
			pid = mingw_spawnve_fd(prog, argv, env, dir, 0,
					       fhin, fhout, fherr);
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

static char **copy_environ(void)
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
static char **env_setenv(char **env, const char *name)
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

/*
 * Copies global environ and adjusts variables as specified by vars.
 */
char **make_augmented_environ(const char *const *vars)
{
	char **env = copy_environ();

	while (*vars)
		env = env_setenv(env, *vars++);
	return env;
}

/*
 * Note, this isn't a complete replacement for getaddrinfo. It assumes
 * that service contains a numerical port, or that it it is null. It
 * does a simple search using gethostbyname, and returns one IPv4 host
 * if one was found.
 */
static int WSAAPI getaddrinfo_stub(const char *node, const char *service,
				   const struct addrinfo *hints,
				   struct addrinfo **res)
{
	struct hostent *h = gethostbyname(node);
	struct addrinfo *ai;
	struct sockaddr_in *sin;

	if (!h)
		return WSAGetLastError();

	ai = xmalloc(sizeof(struct addrinfo));
	*res = ai;
	ai->ai_flags = 0;
	ai->ai_family = AF_INET;
	ai->ai_socktype = hints->ai_socktype;
	switch (hints->ai_socktype) {
	case SOCK_STREAM:
		ai->ai_protocol = IPPROTO_TCP;
		break;
	case SOCK_DGRAM:
		ai->ai_protocol = IPPROTO_UDP;
		break;
	default:
		ai->ai_protocol = 0;
		break;
	}
	ai->ai_addrlen = sizeof(struct sockaddr_in);
	ai->ai_canonname = strdup(h->h_name);

	sin = xmalloc(ai->ai_addrlen);
	memset(sin, 0, ai->ai_addrlen);
	sin->sin_family = AF_INET;
	if (service)
		sin->sin_port = htons(atoi(service));
	sin->sin_addr = *(struct in_addr *)h->h_addr;
	ai->ai_addr = (struct sockaddr *)sin;
	ai->ai_next = 0;
	return 0;
}

static void WSAAPI freeaddrinfo_stub(struct addrinfo *res)
{
	free(res->ai_canonname);
	free(res->ai_addr);
	free(res);
}

static int WSAAPI getnameinfo_stub(const struct sockaddr *sa, socklen_t salen,
				   char *host, DWORD hostlen,
				   char *serv, DWORD servlen, int flags)
{
	const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
	if (sa->sa_family != AF_INET)
		return EAI_FAMILY;
	if (!host && !serv)
		return EAI_NONAME;

	if (host && hostlen > 0) {
		struct hostent *ent = NULL;
		if (!(flags & NI_NUMERICHOST))
			ent = gethostbyaddr((const char *)&sin->sin_addr,
					    sizeof(sin->sin_addr), AF_INET);

		if (ent)
			snprintf(host, hostlen, "%s", ent->h_name);
		else if (flags & NI_NAMEREQD)
			return EAI_NONAME;
		else
			snprintf(host, hostlen, "%s", inet_ntoa(sin->sin_addr));
	}

	if (serv && servlen > 0) {
		struct servent *ent = NULL;
		if (!(flags & NI_NUMERICSERV))
			ent = getservbyport(sin->sin_port,
					    flags & NI_DGRAM ? "udp" : "tcp");

		if (ent)
			snprintf(serv, servlen, "%s", ent->s_name);
		else
			snprintf(serv, servlen, "%d", ntohs(sin->sin_port));
	}

	return 0;
}

static HMODULE ipv6_dll = NULL;
static void (WSAAPI *ipv6_freeaddrinfo)(struct addrinfo *res);
static int (WSAAPI *ipv6_getaddrinfo)(const char *node, const char *service,
				      const struct addrinfo *hints,
				      struct addrinfo **res);
static int (WSAAPI *ipv6_getnameinfo)(const struct sockaddr *sa, socklen_t salen,
				      char *host, DWORD hostlen,
				      char *serv, DWORD servlen, int flags);
/*
 * gai_strerror is an inline function in the ws2tcpip.h header, so we
 * don't need to try to load that one dynamically.
 */

static void socket_cleanup(void)
{
	WSACleanup();
	if (ipv6_dll)
		FreeLibrary(ipv6_dll);
	ipv6_dll = NULL;
	ipv6_freeaddrinfo = freeaddrinfo_stub;
	ipv6_getaddrinfo = getaddrinfo_stub;
	ipv6_getnameinfo = getnameinfo_stub;
}

static void ensure_socket_initialization(void)
{
	WSADATA wsa;
	static int initialized = 0;
	const char *libraries[] = { "ws2_32.dll", "wship6.dll", NULL };
	const char **name;

	if (initialized)
		return;

	if (WSAStartup(MAKEWORD(2,2), &wsa))
		die("unable to initialize winsock subsystem, error %d",
			WSAGetLastError());

	for (name = libraries; *name; name++) {
		ipv6_dll = LoadLibrary(*name);
		if (!ipv6_dll)
			continue;

		ipv6_freeaddrinfo = (void (WSAAPI *)(struct addrinfo *))
			GetProcAddress(ipv6_dll, "freeaddrinfo");
		ipv6_getaddrinfo = (int (WSAAPI *)(const char *, const char *,
						   const struct addrinfo *,
						   struct addrinfo **))
			GetProcAddress(ipv6_dll, "getaddrinfo");
		ipv6_getnameinfo = (int (WSAAPI *)(const struct sockaddr *,
						   socklen_t, char *, DWORD,
						   char *, DWORD, int))
			GetProcAddress(ipv6_dll, "getnameinfo");
		if (!ipv6_freeaddrinfo || !ipv6_getaddrinfo || !ipv6_getnameinfo) {
			FreeLibrary(ipv6_dll);
			ipv6_dll = NULL;
		} else
			break;
	}
	if (!ipv6_freeaddrinfo || !ipv6_getaddrinfo || !ipv6_getnameinfo) {
		ipv6_freeaddrinfo = freeaddrinfo_stub;
		ipv6_getaddrinfo = getaddrinfo_stub;
		ipv6_getnameinfo = getnameinfo_stub;
	}

	atexit(socket_cleanup);
	initialized = 1;
}

#undef gethostbyname
struct hostent *mingw_gethostbyname(const char *host)
{
	ensure_socket_initialization();
	return gethostbyname(host);
}

void mingw_freeaddrinfo(struct addrinfo *res)
{
	ipv6_freeaddrinfo(res);
}

int mingw_getaddrinfo(const char *node, const char *service,
		      const struct addrinfo *hints, struct addrinfo **res)
{
	ensure_socket_initialization();
	return ipv6_getaddrinfo(node, service, hints, res);
}

int mingw_getnameinfo(const struct sockaddr *sa, socklen_t salen,
		      char *host, DWORD hostlen, char *serv, DWORD servlen,
		      int flags)
{
	ensure_socket_initialization();
	return ipv6_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
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
	DWORD attrs, gle;
	int tries = 0;
	static const int delay[] = { 0, 1, 10, 20, 40 };

	/*
	 * Try native rename() first to get errno right.
	 * It is based on MoveFile(), which cannot overwrite existing files.
	 */
	if (!rename(pold, pnew))
		return 0;
	if (errno != EEXIST)
		return -1;
repeat:
	if (MoveFileEx(pold, pnew, MOVEFILE_REPLACE_EXISTING))
		return 0;
	/* TODO: translate more errors */
	gle = GetLastError();
	if (gle == ERROR_ACCESS_DENIED &&
	    (attrs = GetFileAttributes(pnew)) != INVALID_FILE_ATTRIBUTES) {
		if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
			errno = EISDIR;
			return -1;
		}
		if ((attrs & FILE_ATTRIBUTE_READONLY) &&
		    SetFileAttributes(pnew, attrs & ~FILE_ATTRIBUTE_READONLY)) {
			if (MoveFileEx(pold, pnew, MOVEFILE_REPLACE_EXISTING))
				return 0;
			gle = GetLastError();
			/* revert file attributes on failure */
			SetFileAttributes(pnew, attrs);
		}
	}
	if (tries < ARRAY_SIZE(delay) && gle == ERROR_ACCESS_DENIED) {
		/*
		 * We assume that some other process had the source or
		 * destination file open at the wrong moment and retry.
		 * In order to give the other process a higher chance to
		 * complete its operation, we give up our time slice now.
		 * If we have to retry again, we do sleep a bit.
		 */
		Sleep(delay[tries]);
		tries++;
		goto repeat;
	}
	errno = EACCES;
	return -1;
}

/*
 * Note that this doesn't return the actual pagesize, but
 * the allocation granularity. If future Windows specific git code
 * needs the real getpagesize function, we need to find another solution.
 */
int mingw_getpagesize(void)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwAllocationGranularity;
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

static unsigned __stdcall ticktack(void *dummy)
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
	sig_handler_t old = timer_fn;
	if (sig != SIGALRM)
		return signal(sig, handler);
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
	typedef HINSTANCE (WINAPI *T)(HWND, const char *,
			const char *, const char *, const char *, INT);
	T ShellExecute;
	HMODULE shell32;

	shell32 = LoadLibrary("shell32.dll");
	if (!shell32)
		die("cannot load shell32.dll");
	ShellExecute = (T)GetProcAddress(shell32, "ShellExecuteA");
	if (!ShellExecute)
		die("cannot run browser");

	printf("Launching default browser to display HTML ...\n");
	ShellExecute(NULL, "open", htmlpath, NULL, "\\", 0);

	FreeLibrary(shell32);
}

int link(const char *oldpath, const char *newpath)
{
	typedef BOOL (WINAPI *T)(const char*, const char*, LPSECURITY_ATTRIBUTES);
	static T create_hard_link = NULL;
	if (!create_hard_link) {
		create_hard_link = (T) GetProcAddress(
			GetModuleHandle("kernel32.dll"), "CreateHardLinkA");
		if (!create_hard_link)
			create_hard_link = (T)-1;
	}
	if (create_hard_link == (T)-1) {
		errno = ENOSYS;
		return -1;
	}
	if (!create_hard_link(newpath, oldpath, NULL)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	return 0;
}

char *getpass(const char *prompt)
{
	struct strbuf buf = STRBUF_INIT;

	fputs(prompt, stderr);
	for (;;) {
		char c = _getch();
		if (c == '\r' || c == '\n')
			break;
		strbuf_addch(&buf, c);
	}
	fputs("\n", stderr);
	return strbuf_detach(&buf, NULL);
}

#ifndef NO_MINGW_REPLACE_READDIR
/* MinGW readdir implementation to avoid extra lstats for Git */
struct mingw_DIR
{
	struct _finddata_t	dd_dta;		/* disk transfer area for this dir */
	struct mingw_dirent	dd_dir;		/* Our own implementation, including d_type */
	long			dd_handle;	/* _findnext handle */
	int			dd_stat; 	/* 0 = next entry to read is first entry, -1 = off the end, positive = 0 based index of next entry */
	char			dd_name[1]; 	/* given path for dir with search pattern (struct is extended) */
};

struct dirent *mingw_readdir(DIR *dir)
{
	WIN32_FIND_DATAA buf;
	HANDLE handle;
	struct mingw_DIR *mdir = (struct mingw_DIR*)dir;

	if (!dir->dd_handle) {
		errno = EBADF; /* No set_errno for mingw */
		return NULL;
	}

	if (dir->dd_handle == (long)INVALID_HANDLE_VALUE && dir->dd_stat == 0)
	{
		DWORD lasterr;
		handle = FindFirstFileA(dir->dd_name, &buf);
		lasterr = GetLastError();
		dir->dd_handle = (long)handle;
		if (handle == INVALID_HANDLE_VALUE && (lasterr != ERROR_NO_MORE_FILES)) {
			errno = err_win_to_posix(lasterr);
			return NULL;
		}
	} else if (dir->dd_handle == (long)INVALID_HANDLE_VALUE) {
		return NULL;
	} else if (!FindNextFileA((HANDLE)dir->dd_handle, &buf)) {
		DWORD lasterr = GetLastError();
		FindClose((HANDLE)dir->dd_handle);
		dir->dd_handle = (long)INVALID_HANDLE_VALUE;
		/* POSIX says you shouldn't set errno when readdir can't
		   find any more files; so, if another error we leave it set. */
		if (lasterr != ERROR_NO_MORE_FILES)
			errno = err_win_to_posix(lasterr);
		return NULL;
	}

	/* We get here if `buf' contains valid data.  */
	strcpy(dir->dd_dir.d_name, buf.cFileName);
	++dir->dd_stat;

	/* Set file type, based on WIN32_FIND_DATA */
	mdir->dd_dir.d_type = 0;
	if (buf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		mdir->dd_dir.d_type |= DT_DIR;
	else
		mdir->dd_dir.d_type |= DT_REG;

	return (struct dirent*)&dir->dd_dir;
}
#endif // !NO_MINGW_REPLACE_READDIR
