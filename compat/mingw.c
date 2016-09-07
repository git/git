#include "../git-compat-util.h"
#include "win32.h"
#include <conio.h>
#include <wchar.h>
#include "../strbuf.h"
#include "../run-command.h"
#include "../cache.h"

#define HCAST(type, handle) ((type)(intptr_t)handle)

static const int delay[] = { 0, 1, 10, 20, 40 };

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

static inline int is_file_in_use_error(DWORD errcode)
{
	switch (errcode) {
	case ERROR_SHARING_VIOLATION:
	case ERROR_ACCESS_DENIED:
		return 1;
	}

	return 0;
}

static int read_yes_no_answer(void)
{
	char answer[1024];

	if (fgets(answer, sizeof(answer), stdin)) {
		size_t answer_len = strlen(answer);
		int got_full_line = 0, c;

		/* remove the newline */
		if (answer_len >= 2 && answer[answer_len-2] == '\r') {
			answer[answer_len-2] = '\0';
			got_full_line = 1;
		} else if (answer_len >= 1 && answer[answer_len-1] == '\n') {
			answer[answer_len-1] = '\0';
			got_full_line = 1;
		}
		/* flush the buffer in case we did not get the full line */
		if (!got_full_line)
			while ((c = getchar()) != EOF && c != '\n')
				;
	} else
		/* we could not read, return the
		 * default answer which is no */
		return 0;

	if (tolower(answer[0]) == 'y' && !answer[1])
		return 1;
	if (!strncasecmp(answer, "yes", sizeof(answer)))
		return 1;
	if (tolower(answer[0]) == 'n' && !answer[1])
		return 0;
	if (!strncasecmp(answer, "no", sizeof(answer)))
		return 0;

	/* did not find an answer we understand */
	return -1;
}

static int ask_yes_no_if_possible(const char *format, ...)
{
	char question[4096];
	const char *retry_hook[] = { NULL, NULL, NULL };
	va_list args;

	va_start(args, format);
	vsnprintf(question, sizeof(question), format, args);
	va_end(args);

	if ((retry_hook[0] = mingw_getenv("GIT_ASK_YESNO"))) {
		retry_hook[1] = question;
		return !run_command_v_opt(retry_hook, 0);
	}

	if (!isatty(_fileno(stdin)) || !isatty(_fileno(stderr)))
		return 0;

	while (1) {
		int answer;
		fprintf(stderr, "%s (y/n) ", question);

		if ((answer = read_yes_no_answer()) >= 0)
			return answer;

		fprintf(stderr, "Sorry, I did not understand your answer. "
				"Please type 'y' or 'n'\n");
	}
}

int mingw_unlink(const char *pathname)
{
	int ret, tries = 0;
	wchar_t wpathname[MAX_PATH];
	if (xutftowcs_path(wpathname, pathname) < 0)
		return -1;

	/* read-only files cannot be removed */
	_wchmod(wpathname, 0666);
	while ((ret = _wunlink(wpathname)) == -1 && tries < ARRAY_SIZE(delay)) {
		if (!is_file_in_use_error(GetLastError()))
			break;
		/*
		 * We assume that some other process had the source or
		 * destination file open at the wrong moment and retry.
		 * In order to give the other process a higher chance to
		 * complete its operation, we give up our time slice now.
		 * If we have to retry again, we do sleep a bit.
		 */
		Sleep(delay[tries]);
		tries++;
	}
	while (ret == -1 && is_file_in_use_error(GetLastError()) &&
	       ask_yes_no_if_possible("Unlink of file '%s' failed. "
			"Should I try again?", pathname))
	       ret = _wunlink(wpathname);
	return ret;
}

static int is_dir_empty(const wchar_t *wpath)
{
	WIN32_FIND_DATAW findbuf;
	HANDLE handle;
	wchar_t wbuf[MAX_PATH + 2];
	wcscpy(wbuf, wpath);
	wcscat(wbuf, L"\\*");
	handle = FindFirstFileW(wbuf, &findbuf);
	if (handle == INVALID_HANDLE_VALUE)
		return GetLastError() == ERROR_NO_MORE_FILES;

	while (!wcscmp(findbuf.cFileName, L".") ||
			!wcscmp(findbuf.cFileName, L".."))
		if (!FindNextFileW(handle, &findbuf)) {
			DWORD err = GetLastError();
			FindClose(handle);
			return err == ERROR_NO_MORE_FILES;
		}
	FindClose(handle);
	return 0;
}

int mingw_rmdir(const char *pathname)
{
	int ret, tries = 0;
	wchar_t wpathname[MAX_PATH];
	if (xutftowcs_path(wpathname, pathname) < 0)
		return -1;

	while ((ret = _wrmdir(wpathname)) == -1 && tries < ARRAY_SIZE(delay)) {
		if (!is_file_in_use_error(GetLastError()))
			errno = err_win_to_posix(GetLastError());
		if (errno != EACCES)
			break;
		if (!is_dir_empty(wpathname)) {
			errno = ENOTEMPTY;
			break;
		}
		/*
		 * We assume that some other process had the source or
		 * destination file open at the wrong moment and retry.
		 * In order to give the other process a higher chance to
		 * complete its operation, we give up our time slice now.
		 * If we have to retry again, we do sleep a bit.
		 */
		Sleep(delay[tries]);
		tries++;
	}
	while (ret == -1 && errno == EACCES && is_file_in_use_error(GetLastError()) &&
	       ask_yes_no_if_possible("Deletion of directory '%s' failed. "
			"Should I try again?", pathname))
	       ret = _wrmdir(wpathname);
	return ret;
}

static inline int needs_hiding(const char *path)
{
	const char *basename;

	if (hide_dotfiles == HIDE_DOTFILES_FALSE)
		return 0;

	/* We cannot use basename(), as it would remove trailing slashes */
	mingw_skip_dos_drive_prefix((char **)&path);
	if (!*path)
		return 0;

	for (basename = path; *path; path++)
		if (is_dir_sep(*path)) {
			do {
				path++;
			} while (is_dir_sep(*path));
			/* ignore trailing slashes */
			if (*path)
				basename = path;
		}

	if (hide_dotfiles == HIDE_DOTFILES_TRUE)
		return *basename == '.';

	assert(hide_dotfiles == HIDE_DOTFILES_DOTGITONLY);
	return !strncasecmp(".git", basename, 4) &&
		(!basename[4] || is_dir_sep(basename[4]));
}

static int set_hidden_flag(const wchar_t *path, int set)
{
	DWORD original = GetFileAttributesW(path), modified;
	if (set)
		modified = original | FILE_ATTRIBUTE_HIDDEN;
	else
		modified = original & ~FILE_ATTRIBUTE_HIDDEN;
	if (original == modified || SetFileAttributesW(path, modified))
		return 0;
	errno = err_win_to_posix(GetLastError());
	return -1;
}

int mingw_mkdir(const char *path, int mode)
{
	int ret;
	wchar_t wpath[MAX_PATH];
	if (xutftowcs_path(wpath, path) < 0)
		return -1;
	ret = _wmkdir(wpath);
	if (!ret && needs_hiding(path))
		return set_hidden_flag(wpath, 1);
	return ret;
}

int mingw_open (const char *filename, int oflags, ...)
{
	va_list args;
	unsigned mode;
	int fd;
	wchar_t wfilename[MAX_PATH];

	va_start(args, oflags);
	mode = va_arg(args, int);
	va_end(args);

	if (filename && !strcmp(filename, "/dev/null"))
		filename = "nul";

	if (xutftowcs_path(wfilename, filename) < 0)
		return -1;
	fd = _wopen(wfilename, oflags, mode);

	if (fd < 0 && (oflags & O_ACCMODE) != O_RDONLY && errno == EACCES) {
		DWORD attrs = GetFileAttributesW(wfilename);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
			errno = EISDIR;
	}
	if ((oflags & O_CREAT) && needs_hiding(filename)) {
		/*
		 * Internally, _wopen() uses the CreateFile() API which errors
		 * out with an ERROR_ACCESS_DENIED if CREATE_ALWAYS was
		 * specified and an already existing file's attributes do not
		 * match *exactly*. As there is no mode or flag we can set that
		 * would correspond to FILE_ATTRIBUTE_HIDDEN, let's just try
		 * again *without* the O_CREAT flag (that corresponds to the
		 * CREATE_ALWAYS flag of CreateFile()).
		 */
		if (fd < 0 && errno == EACCES)
			fd = _wopen(wfilename, oflags & ~O_CREAT, mode);
		if (fd >= 0 && set_hidden_flag(wfilename, 1))
			warning("could not mark '%s' as hidden.", filename);
	}
	return fd;
}

static BOOL WINAPI ctrl_ignore(DWORD type)
{
	return TRUE;
}

#undef fgetc
int mingw_fgetc(FILE *stream)
{
	int ch;
	if (!isatty(_fileno(stream)))
		return fgetc(stream);

	SetConsoleCtrlHandler(ctrl_ignore, TRUE);
	while (1) {
		ch = fgetc(stream);
		if (ch != EOF || GetLastError() != ERROR_OPERATION_ABORTED)
			break;

		/* Ctrl+C was pressed, simulate SIGINT and retry */
		mingw_raise(SIGINT);
	}
	SetConsoleCtrlHandler(ctrl_ignore, FALSE);
	return ch;
}

#undef fopen
FILE *mingw_fopen (const char *filename, const char *otype)
{
	int hide = needs_hiding(filename);
	FILE *file;
	wchar_t wfilename[MAX_PATH], wotype[4];
	if (filename && !strcmp(filename, "/dev/null"))
		filename = "nul";
	if (xutftowcs_path(wfilename, filename) < 0 ||
		xutftowcs(wotype, otype, ARRAY_SIZE(wotype)) < 0)
		return NULL;
	if (hide && !access(filename, F_OK) && set_hidden_flag(wfilename, 0)) {
		error("could not unhide %s", filename);
		return NULL;
	}
	file = _wfopen(wfilename, wotype);
	if (!file && GetLastError() == ERROR_INVALID_NAME)
		errno = ENOENT;
	if (file && hide && set_hidden_flag(wfilename, 1))
		warning("could not mark '%s' as hidden.", filename);
	return file;
}

FILE *mingw_freopen (const char *filename, const char *otype, FILE *stream)
{
	int hide = needs_hiding(filename);
	FILE *file;
	wchar_t wfilename[MAX_PATH], wotype[4];
	if (filename && !strcmp(filename, "/dev/null"))
		filename = "nul";
	if (xutftowcs_path(wfilename, filename) < 0 ||
		xutftowcs(wotype, otype, ARRAY_SIZE(wotype)) < 0)
		return NULL;
	if (hide && !access(filename, F_OK) && set_hidden_flag(wfilename, 0)) {
		error("could not unhide %s", filename);
		return NULL;
	}
	file = _wfreopen(wfilename, wotype, stream);
	if (file && hide && set_hidden_flag(wfilename, 1))
		warning("could not mark '%s' as hidden.", filename);
	return file;
}

#undef fflush
int mingw_fflush(FILE *stream)
{
	int ret = fflush(stream);

	/*
	 * write() is used behind the scenes of stdio output functions.
	 * Since git code does not check for errors after each stdio write
	 * operation, it can happen that write() is called by a later
	 * stdio function even if an earlier write() call failed. In the
	 * case of a pipe whose readable end was closed, only the first
	 * call to write() reports EPIPE on Windows. Subsequent write()
	 * calls report EINVAL. It is impossible to notice whether this
	 * fflush invocation triggered such a case, therefore, we have to
	 * catch all EINVAL errors whole-sale.
	 */
	if (ret && errno == EINVAL)
		errno = EPIPE;

	return ret;
}

#undef write
ssize_t mingw_write(int fd, const void *buf, size_t len)
{
	ssize_t result = write(fd, buf, len);

	if (result < 0 && errno == EINVAL && buf) {
		/* check if fd is a pipe */
		HANDLE h = (HANDLE) _get_osfhandle(fd);
		if (GetFileType(h) == FILE_TYPE_PIPE)
			errno = EPIPE;
		else
			errno = EINVAL;
	}

	return result;
}

int mingw_access(const char *filename, int mode)
{
	wchar_t wfilename[MAX_PATH];
	if (xutftowcs_path(wfilename, filename) < 0)
		return -1;
	/* X_OK is not supported by the MSVCRT version */
	return _waccess(wfilename, mode & ~X_OK);
}

int mingw_chdir(const char *dirname)
{
	wchar_t wdirname[MAX_PATH];
	if (xutftowcs_path(wdirname, dirname) < 0)
		return -1;
	return _wchdir(wdirname);
}

int mingw_chmod(const char *filename, int mode)
{
	wchar_t wfilename[MAX_PATH];
	if (xutftowcs_path(wfilename, filename) < 0)
		return -1;
	return _wchmod(wfilename, mode);
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

/**
 * Verifies that safe_create_leading_directories() would succeed.
 */
static int has_valid_directory_prefix(wchar_t *wfilename)
{
	int n = wcslen(wfilename);

	while (n > 0) {
		wchar_t c = wfilename[--n];
		DWORD attributes;

		if (!is_dir_sep(c))
			continue;

		wfilename[n] = L'\0';
		attributes = GetFileAttributesW(wfilename);
		wfilename[n] = c;
		if (attributes == FILE_ATTRIBUTE_DIRECTORY ||
				attributes == FILE_ATTRIBUTE_DEVICE)
			return 1;
		if (attributes == INVALID_FILE_ATTRIBUTES)
			switch (GetLastError()) {
			case ERROR_PATH_NOT_FOUND:
				continue;
			case ERROR_FILE_NOT_FOUND:
				/* This implies parent directory exists. */
				return 1;
			}
		return 0;
	}
	return 1;
}

/* We keep the do_lstat code in a separate function to avoid recursion.
 * When a path ends with a slash, the stat will fail with ENOENT. In
 * this case, we strip the trailing slashes and stat again.
 *
 * If follow is true then act like stat() and report on the link
 * target. Otherwise report on the link itself.
 */
static int do_lstat(int follow, const char *file_name, struct stat *buf)
{
	WIN32_FILE_ATTRIBUTE_DATA fdata;
	wchar_t wfilename[MAX_PATH];
	if (xutftowcs_path(wfilename, file_name) < 0)
		return -1;

	if (GetFileAttributesExW(wfilename, GetFileExInfoStandard, &fdata)) {
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
		if (fdata.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
			WIN32_FIND_DATAW findbuf;
			HANDLE handle = FindFirstFileW(wfilename, &findbuf);
			if (handle != INVALID_HANDLE_VALUE) {
				if ((findbuf.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
						(findbuf.dwReserved0 == IO_REPARSE_TAG_SYMLINK)) {
					if (follow) {
						char buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
						buf->st_size = readlink(file_name, buffer, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
					} else {
						buf->st_mode = S_IFLNK;
					}
					buf->st_mode |= S_IREAD;
					if (!(findbuf.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
						buf->st_mode |= S_IWRITE;
				}
				FindClose(handle);
			}
		}
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
	case ERROR_PATH_NOT_FOUND:
		if (!has_valid_directory_prefix(wfilename)) {
			errno = ENOTDIR;
			break;
		}
		/* fallthru */
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
static int do_stat_internal(int follow, const char *file_name, struct stat *buf)
{
	int namelen;
	char alt_name[PATH_MAX];

	if (!do_lstat(follow, file_name, buf))
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
	return do_lstat(follow, alt_name, buf);
}

int mingw_lstat(const char *file_name, struct stat *buf)
{
	return do_stat_internal(0, file_name, buf);
}
int mingw_stat(const char *file_name, struct stat *buf)
{
	return do_stat_internal(1, file_name, buf);
}

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
	DWORD attrs;
	wchar_t wfilename[MAX_PATH];
	if (xutftowcs_path(wfilename, file_name) < 0)
		return -1;

	/* must have write permission */
	attrs = GetFileAttributesW(wfilename);
	if (attrs != INVALID_FILE_ATTRIBUTES &&
	    (attrs & FILE_ATTRIBUTE_READONLY)) {
		/* ignore errors here; open() will report them */
		SetFileAttributesW(wfilename, attrs & ~FILE_ATTRIBUTE_READONLY);
	}

	if ((fh = _wopen(wfilename, O_RDWR | O_BINARY)) < 0) {
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
		SetFileAttributesW(wfilename, attrs);
	}
	return rc;
}

#undef strftime
size_t mingw_strftime(char *s, size_t max,
		      const char *format, const struct tm *tm)
{
	size_t ret = strftime(s, max, format, tm);

	if (!ret && errno == EINVAL)
		die("invalid strftime format: '%s'", format);
	return ret;
}

unsigned int sleep (unsigned int seconds)
{
	Sleep(seconds*1000);
	return 0;
}

char *mingw_mktemp(char *template)
{
	wchar_t wtemplate[MAX_PATH];
	if (xutftowcs_path(wtemplate, template) < 0)
		return NULL;
	if (!_wmktemp(wtemplate))
		return NULL;
	if (xwcstoutf(template, wtemplate, strlen(template) + 1) < 0)
		return NULL;
	return template;
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
	filedes[0] = _open_osfhandle(HCAST(int, h[0]), O_NOINHERIT);
	if (filedes[0] < 0) {
		CloseHandle(h[0]);
		CloseHandle(h[1]);
		return -1;
	}
	filedes[1] = _open_osfhandle(HCAST(int, h[1]), O_NOINHERIT);
	if (filedes[1] < 0) {
		close(filedes[0]);
		CloseHandle(h[1]);
		return -1;
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

char *mingw_getcwd(char *pointer, int len)
{
	wchar_t wpointer[MAX_PATH];
	if (!_wgetcwd(wpointer, ARRAY_SIZE(wpointer)))
		return NULL;
	if (xwcstoutf(pointer, wpointer, len) < 0)
		return NULL;
	convert_slashes(pointer);
	return pointer;
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
	d = q = xmalloc(st_add3(len, n, 3));
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
 * exe_only means that we only want to detect .exe files, but not scripts
 * (which do not have an extension)
 */
static char *lookup_prog(const char *dir, int dirlen, const char *cmd,
			 int isexe, int exe_only)
{
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%.*s\\%s.exe", dirlen, dir, cmd);

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
static char *path_lookup(const char *cmd, int exe_only)
{
	const char *path;
	char *prog = NULL;
	int len = strlen(cmd);
	int isexe = len >= 4 && !strcasecmp(cmd+len-4, ".exe");

	if (strchr(cmd, '/') || strchr(cmd, '\\'))
		return xstrdup(cmd);

	path = mingw_getenv("PATH");
	if (!path)
		return NULL;

	while (!prog) {
		const char *sep = strchrnul(path, ';');
		int dirlen = sep - path;
		if (dirlen)
			prog = lookup_prog(path, dirlen, cmd, isexe, exe_only);
		if (!*sep)
			break;
		path = sep + 1;
	}

	return prog;
}

static int do_putenv(char **env, const char *name, int size, int free_old);

/* used number of elements of environ array, including terminating NULL */
static int environ_size = 0;
/* allocated size of environ array, in bytes */
static int environ_alloc = 0;

/*
 * Create environment block suitable for CreateProcess. Merges current
 * process environment and the supplied environment changes.
 */
static wchar_t *make_environment_block(char **deltaenv)
{
	wchar_t *wenvblk = NULL;
	char **tmpenv;
	int i = 0, size = environ_size, wenvsz = 0, wenvpos = 0;

	while (deltaenv && deltaenv[i])
		i++;

	/* copy the environment, leaving space for changes */
	ALLOC_ARRAY(tmpenv, size + i);
	memcpy(tmpenv, environ, size * sizeof(char*));

	/* merge supplied environment changes into the temporary environment */
	for (i = 0; deltaenv && deltaenv[i]; i++)
		size = do_putenv(tmpenv, deltaenv[i], size, 0);

	/* create environment block from temporary environment */
	for (i = 0; tmpenv[i]; i++) {
		size = 2 * strlen(tmpenv[i]) + 2; /* +2 for final \0 */
		ALLOC_GROW(wenvblk, (wenvpos + size) * sizeof(wchar_t), wenvsz);
		wenvpos += xutftowcs(&wenvblk[wenvpos], tmpenv[i], size) + 1;
	}
	/* add final \0 terminator */
	wenvblk[wenvpos] = 0;
	free(tmpenv);
	return wenvblk;
}

struct pinfo_t {
	struct pinfo_t *next;
	pid_t pid;
	HANDLE proc;
};
static struct pinfo_t *pinfo = NULL;
CRITICAL_SECTION pinfo_cs;

static pid_t mingw_spawnve_fd(const char *cmd, const char **argv, char **deltaenv,
			      const char *dir,
			      int prepend_cmd, int fhin, int fhout, int fherr)
{
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	struct strbuf args;
	wchar_t wcmd[MAX_PATH], wdir[MAX_PATH], *wargs, *wenvblk = NULL;
	unsigned flags = CREATE_UNICODE_ENVIRONMENT;
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
		flags |= DETACHED_PROCESS;
	} else {
		/* There is already a console. If we specified
		 * DETACHED_PROCESS here, too, Windows would
		 * disassociate the child from the console.
		 * The same is true for CREATE_NO_WINDOW.
		 * Go figure!
		 */
		CloseHandle(cons);
	}
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = winansi_get_osfhandle(fhin);
	si.hStdOutput = winansi_get_osfhandle(fhout);
	si.hStdError = winansi_get_osfhandle(fherr);

	if (xutftowcs_path(wcmd, cmd) < 0)
		return -1;
	if (dir && xutftowcs_path(wdir, dir) < 0)
		return -1;

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

	ALLOC_ARRAY(wargs, st_add(st_mult(2, args.len), 1));
	xutftowcs(wargs, args.buf, 2 * args.len + 1);
	strbuf_release(&args);

	wenvblk = make_environment_block(deltaenv);

	memset(&pi, 0, sizeof(pi));
	ret = CreateProcessW(wcmd, wargs, NULL, NULL, TRUE, flags,
		wenvblk, dir ? wdir : NULL, &si, &pi);

	free(wenvblk);
	free(wargs);

	if (!ret) {
		errno = ENOENT;
		return -1;
	}
	CloseHandle(pi.hThread);

	/*
	 * The process ID is the human-readable identifier of the process
	 * that we want to present in log and error messages. The handle
	 * is not useful for this purpose. But we cannot close it, either,
	 * because it is not possible to turn a process ID into a process
	 * handle after the process terminated.
	 * Keep the handle in a list for waitpid.
	 */
	EnterCriticalSection(&pinfo_cs);
	{
		struct pinfo_t *info = xmalloc(sizeof(struct pinfo_t));
		info->pid = pi.dwProcessId;
		info->proc = pi.hProcess;
		info->next = pinfo;
		pinfo = info;
	}
	LeaveCriticalSection(&pinfo_cs);

	return (pid_t)pi.dwProcessId;
}

static pid_t mingw_spawnv(const char *cmd, const char **argv, int prepend_cmd)
{
	return mingw_spawnve_fd(cmd, argv, NULL, NULL, prepend_cmd, 0, 1, 2);
}

pid_t mingw_spawnvpe(const char *cmd, const char **argv, char **deltaenv,
		     const char *dir,
		     int fhin, int fhout, int fherr)
{
	pid_t pid;
	char *prog = path_lookup(cmd, 0);

	if (!prog) {
		errno = ENOENT;
		pid = -1;
	}
	else {
		const char *interpr = parse_interpreter(prog);

		if (interpr) {
			const char *argv0 = argv[0];
			char *iprog = path_lookup(interpr, 1);
			argv[0] = prog;
			if (!iprog) {
				errno = ENOENT;
				pid = -1;
			}
			else {
				pid = mingw_spawnve_fd(iprog, argv, deltaenv, dir, 1,
						       fhin, fhout, fherr);
				free(iprog);
			}
			argv[0] = argv0;
		}
		else
			pid = mingw_spawnve_fd(prog, argv, deltaenv, dir, 0,
					       fhin, fhout, fherr);
		free(prog);
	}
	return pid;
}

static int try_shell_exec(const char *cmd, char *const *argv)
{
	const char *interpr = parse_interpreter(cmd);
	char *prog;
	int pid = 0;

	if (!interpr)
		return 0;
	prog = path_lookup(interpr, 1);
	if (prog) {
		int argc = 0;
		const char **argv2;
		while (argv[argc]) argc++;
		ALLOC_ARRAY(argv2, argc + 1);
		argv2[0] = (char *)cmd;	/* full path to the script file */
		memcpy(&argv2[1], &argv[1], sizeof(*argv) * argc);
		pid = mingw_spawnv(prog, argv2, 1);
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
	return pid;
}

int mingw_execv(const char *cmd, char *const *argv)
{
	/* check if git_command is a shell script */
	if (!try_shell_exec(cmd, argv)) {
		int pid, status;

		pid = mingw_spawnv(cmd, (const char **)argv, 0);
		if (pid < 0)
			return -1;
		if (waitpid(pid, &status, 0) < 0)
			status = 255;
		exit(status);
	}
	return -1;
}

int mingw_execvp(const char *cmd, char *const *argv)
{
	char *prog = path_lookup(cmd, 0);

	if (prog) {
		mingw_execv(prog, argv);
		free(prog);
	} else
		errno = ENOENT;

	return -1;
}

int mingw_kill(pid_t pid, int sig)
{
	if (pid > 0 && sig == SIGTERM) {
		HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);

		if (TerminateProcess(h, -1)) {
			CloseHandle(h);
			return 0;
		}

		errno = err_win_to_posix(GetLastError());
		CloseHandle(h);
		return -1;
	} else if (pid > 0 && sig == 0) {
		HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
		if (h) {
			CloseHandle(h);
			return 0;
		}
	}

	errno = EINVAL;
	return -1;
}

/*
 * Compare environment entries by key (i.e. stopping at '=' or '\0').
 */
static int compareenv(const void *v1, const void *v2)
{
	const char *e1 = *(const char**)v1;
	const char *e2 = *(const char**)v2;

	for (;;) {
		int c1 = *e1++;
		int c2 = *e2++;
		c1 = (c1 == '=') ? 0 : tolower(c1);
		c2 = (c2 == '=') ? 0 : tolower(c2);
		if (c1 > c2)
			return 1;
		if (c1 < c2)
			return -1;
		if (c1 == 0)
			return 0;
	}
}

static int bsearchenv(char **env, const char *name, size_t size)
{
	unsigned low = 0, high = size;
	while (low < high) {
		unsigned mid = low + ((high - low) >> 1);
		int cmp = compareenv(&env[mid], &name);
		if (cmp < 0)
			low = mid + 1;
		else if (cmp > 0)
			high = mid;
		else
			return mid;
	}
	return ~low; /* not found, return 1's complement of insert position */
}

/*
 * If name contains '=', then sets the variable, otherwise it unsets it
 * Size includes the terminating NULL. Env must have room for size + 1 entries
 * (in case of insert). Returns the new size. Optionally frees removed entries.
 */
static int do_putenv(char **env, const char *name, int size, int free_old)
{
	int i = bsearchenv(env, name, size - 1);

	/* optionally free removed / replaced entry */
	if (i >= 0 && free_old)
		free(env[i]);

	if (strchr(name, '=')) {
		/* if new value ('key=value') is specified, insert or replace entry */
		if (i < 0) {
			i = ~i;
			memmove(&env[i + 1], &env[i], (size - i) * sizeof(char*));
			size++;
		}
		env[i] = (char*) name;
	} else if (i >= 0) {
		/* otherwise ('key') remove existing entry */
		size--;
		memmove(&env[i], &env[i + 1], (size - i) * sizeof(char*));
	}
	return size;
}

char *mingw_getenv(const char *name)
{
	char *value;
	int pos = bsearchenv(environ, name, environ_size - 1);
	if (pos < 0)
		return NULL;
	value = strchr(environ[pos], '=');
	return value ? &value[1] : NULL;
}

int mingw_putenv(const char *namevalue)
{
	ALLOC_GROW(environ, (environ_size + 1) * sizeof(char*), environ_alloc);
	environ_size = do_putenv(environ, namevalue, environ_size, 1);
	return 0;
}

/*
 * Note, this isn't a complete replacement for getaddrinfo. It assumes
 * that service contains a numerical port, or that it is null. It
 * does a simple search using gethostbyname, and returns one IPv4 host
 * if one was found.
 */
static int WSAAPI getaddrinfo_stub(const char *node, const char *service,
				   const struct addrinfo *hints,
				   struct addrinfo **res)
{
	struct hostent *h = NULL;
	struct addrinfo *ai;
	struct sockaddr_in *sin;

	if (node) {
		h = gethostbyname(node);
		if (!h)
			return WSAGetLastError();
	}

	ai = xmalloc(sizeof(struct addrinfo));
	*res = ai;
	ai->ai_flags = 0;
	ai->ai_family = AF_INET;
	ai->ai_socktype = hints ? hints->ai_socktype : 0;
	switch (ai->ai_socktype) {
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
	if (hints && (hints->ai_flags & AI_CANONNAME))
		ai->ai_canonname = h ? xstrdup(h->h_name) : NULL;
	else
		ai->ai_canonname = NULL;

	sin = xcalloc(1, ai->ai_addrlen);
	sin->sin_family = AF_INET;
	/* Note: getaddrinfo is supposed to allow service to be a string,
	 * which should be looked up using getservbyname. This is
	 * currently not implemented */
	if (service)
		sin->sin_port = htons(atoi(service));
	if (h)
		sin->sin_addr = *(struct in_addr *)h->h_addr;
	else if (hints && (hints->ai_flags & AI_PASSIVE))
		sin->sin_addr.s_addr = INADDR_ANY;
	else
		sin->sin_addr.s_addr = INADDR_LOOPBACK;
	ai->ai_addr = (struct sockaddr *)sin;
	ai->ai_next = NULL;
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

#undef gethostname
int mingw_gethostname(char *name, int namelen)
{
    ensure_socket_initialization();
    return gethostname(name, namelen);
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
	SOCKET s;

	ensure_socket_initialization();
	s = WSASocket(domain, type, protocol, NULL, 0, 0);
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

#undef bind
int mingw_bind(int sockfd, struct sockaddr *sa, size_t sz)
{
	SOCKET s = (SOCKET)_get_osfhandle(sockfd);
	return bind(s, sa, sz);
}

#undef setsockopt
int mingw_setsockopt(int sockfd, int lvl, int optname, void *optval, int optlen)
{
	SOCKET s = (SOCKET)_get_osfhandle(sockfd);
	return setsockopt(s, lvl, optname, (const char*)optval, optlen);
}

#undef shutdown
int mingw_shutdown(int sockfd, int how)
{
	SOCKET s = (SOCKET)_get_osfhandle(sockfd);
	return shutdown(s, how);
}

#undef listen
int mingw_listen(int sockfd, int backlog)
{
	SOCKET s = (SOCKET)_get_osfhandle(sockfd);
	return listen(s, backlog);
}

#undef accept
int mingw_accept(int sockfd1, struct sockaddr *sa, socklen_t *sz)
{
	int sockfd2;

	SOCKET s1 = (SOCKET)_get_osfhandle(sockfd1);
	SOCKET s2 = accept(s1, sa, sz);

	/* convert into a file descriptor */
	if ((sockfd2 = _open_osfhandle(s2, O_RDWR|O_BINARY)) < 0) {
		int err = errno;
		closesocket(s2);
		return error("unable to make a socket file descriptor: %s",
			strerror(err));
	}
	return sockfd2;
}

#undef rename
int mingw_rename(const char *pold, const char *pnew)
{
	DWORD attrs, gle;
	int tries = 0;
	wchar_t wpold[MAX_PATH], wpnew[MAX_PATH];
	if (xutftowcs_path(wpold, pold) < 0 || xutftowcs_path(wpnew, pnew) < 0)
		return -1;

	/*
	 * Try native rename() first to get errno right.
	 * It is based on MoveFile(), which cannot overwrite existing files.
	 */
	if (!_wrename(wpold, wpnew))
		return 0;
	if (errno != EEXIST)
		return -1;
repeat:
	if (MoveFileExW(wpold, wpnew, MOVEFILE_REPLACE_EXISTING))
		return 0;
	/* TODO: translate more errors */
	gle = GetLastError();
	if (gle == ERROR_ACCESS_DENIED &&
	    (attrs = GetFileAttributesW(wpnew)) != INVALID_FILE_ATTRIBUTES) {
		if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
			DWORD attrsold = GetFileAttributesW(wpold);
			if (attrsold == INVALID_FILE_ATTRIBUTES ||
			    !(attrsold & FILE_ATTRIBUTE_DIRECTORY))
				errno = EISDIR;
			else if (!_wrmdir(wpnew))
				goto repeat;
			return -1;
		}
		if ((attrs & FILE_ATTRIBUTE_READONLY) &&
		    SetFileAttributesW(wpnew, attrs & ~FILE_ATTRIBUTE_READONLY)) {
			if (MoveFileExW(wpold, wpnew, MOVEFILE_REPLACE_EXISTING))
				return 0;
			gle = GetLastError();
			/* revert file attributes on failure */
			SetFileAttributesW(wpnew, attrs);
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
	if (gle == ERROR_ACCESS_DENIED &&
	       ask_yes_no_if_possible("Rename from '%s' to '%s' failed. "
		       "Should I try again?", pold, pnew))
		goto repeat;

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
static sig_handler_t timer_fn = SIG_DFL, sigint_fn = SIG_DFL;

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
		mingw_raise(SIGALRM);
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
	sig_handler_t old;

	switch (sig) {
	case SIGALRM:
		old = timer_fn;
		timer_fn = handler;
		break;

	case SIGINT:
		old = sigint_fn;
		sigint_fn = handler;
		break;

	default:
		return signal(sig, handler);
	}

	return old;
}

#undef raise
int mingw_raise(int sig)
{
	switch (sig) {
	case SIGALRM:
		if (timer_fn == SIG_DFL) {
			if (isatty(STDERR_FILENO))
				fputs("Alarm clock\n", stderr);
			exit(128 + SIGALRM);
		} else if (timer_fn != SIG_IGN)
			timer_fn(SIGALRM);
		return 0;

	case SIGINT:
		if (sigint_fn == SIG_DFL)
			exit(128 + SIGINT);
		else if (sigint_fn != SIG_IGN)
			sigint_fn(SIGINT);
		return 0;

	default:
		return raise(sig);
	}
}

int link(const char *oldpath, const char *newpath)
{
	typedef BOOL (WINAPI *T)(LPCWSTR, LPCWSTR, LPSECURITY_ATTRIBUTES);
	static T create_hard_link = NULL;
	wchar_t woldpath[MAX_PATH], wnewpath[MAX_PATH];
	if (xutftowcs_path(woldpath, oldpath) < 0 ||
		xutftowcs_path(wnewpath, newpath) < 0)
		return -1;

	if (!create_hard_link) {
		create_hard_link = (T) GetProcAddress(
			GetModuleHandle("kernel32.dll"), "CreateHardLinkW");
		if (!create_hard_link)
			create_hard_link = (T)-1;
	}
	if (create_hard_link == (T)-1) {
		errno = ENOSYS;
		return -1;
	}
	if (!create_hard_link(wnewpath, woldpath, NULL)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	return 0;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
	HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION,
	    FALSE, pid);
	if (!h) {
		errno = ECHILD;
		return -1;
	}

	if (pid > 0 && options & WNOHANG) {
		if (WAIT_OBJECT_0 != WaitForSingleObject(h, 0)) {
			CloseHandle(h);
			return 0;
		}
		options &= ~WNOHANG;
	}

	if (options == 0) {
		struct pinfo_t **ppinfo;
		if (WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0) {
			CloseHandle(h);
			return 0;
		}

		if (status)
			GetExitCodeProcess(h, (LPDWORD)status);

		EnterCriticalSection(&pinfo_cs);

		ppinfo = &pinfo;
		while (*ppinfo) {
			struct pinfo_t *info = *ppinfo;
			if (info->pid == pid) {
				CloseHandle(info->proc);
				*ppinfo = info->next;
				free(info);
				break;
			}
			ppinfo = &info->next;
		}

		LeaveCriticalSection(&pinfo_cs);

		CloseHandle(h);
		return pid;
	}
	CloseHandle(h);

	errno = EINVAL;
	return -1;
}

int mingw_skip_dos_drive_prefix(char **path)
{
	int ret = has_dos_drive_prefix(*path);
	*path += ret;
	return ret;
}

int mingw_offset_1st_component(const char *path)
{
	char *pos = (char *)path;

	/* unc paths */
	if (!skip_dos_drive_prefix(&pos) &&
			is_dir_sep(pos[0]) && is_dir_sep(pos[1])) {
		/* skip server name */
		pos = strpbrk(pos + 2, "\\/");
		if (!pos)
			return 0; /* Error: malformed unc path */

		do {
			pos++;
		} while (*pos && !is_dir_sep(*pos));
	}

	return pos + is_dir_sep(*pos) - path;
}

int xutftowcsn(wchar_t *wcs, const char *utfs, size_t wcslen, int utflen)
{
	int upos = 0, wpos = 0;
	const unsigned char *utf = (const unsigned char*) utfs;
	if (!utf || !wcs || wcslen < 1) {
		errno = EINVAL;
		return -1;
	}
	/* reserve space for \0 */
	wcslen--;
	if (utflen < 0)
		utflen = INT_MAX;

	while (upos < utflen) {
		int c = utf[upos++] & 0xff;
		if (utflen == INT_MAX && c == 0)
			break;

		if (wpos >= wcslen) {
			wcs[wpos] = 0;
			errno = ERANGE;
			return -1;
		}

		if (c < 0x80) {
			/* ASCII */
			wcs[wpos++] = c;
		} else if (c >= 0xc2 && c < 0xe0 && upos < utflen &&
				(utf[upos] & 0xc0) == 0x80) {
			/* 2-byte utf-8 */
			c = ((c & 0x1f) << 6);
			c |= (utf[upos++] & 0x3f);
			wcs[wpos++] = c;
		} else if (c >= 0xe0 && c < 0xf0 && upos + 1 < utflen &&
				!(c == 0xe0 && utf[upos] < 0xa0) && /* over-long encoding */
				(utf[upos] & 0xc0) == 0x80 &&
				(utf[upos + 1] & 0xc0) == 0x80) {
			/* 3-byte utf-8 */
			c = ((c & 0x0f) << 12);
			c |= ((utf[upos++] & 0x3f) << 6);
			c |= (utf[upos++] & 0x3f);
			wcs[wpos++] = c;
		} else if (c >= 0xf0 && c < 0xf5 && upos + 2 < utflen &&
				wpos + 1 < wcslen &&
				!(c == 0xf0 && utf[upos] < 0x90) && /* over-long encoding */
				!(c == 0xf4 && utf[upos] >= 0x90) && /* > \u10ffff */
				(utf[upos] & 0xc0) == 0x80 &&
				(utf[upos + 1] & 0xc0) == 0x80 &&
				(utf[upos + 2] & 0xc0) == 0x80) {
			/* 4-byte utf-8: convert to \ud8xx \udcxx surrogate pair */
			c = ((c & 0x07) << 18);
			c |= ((utf[upos++] & 0x3f) << 12);
			c |= ((utf[upos++] & 0x3f) << 6);
			c |= (utf[upos++] & 0x3f);
			c -= 0x10000;
			wcs[wpos++] = 0xd800 | (c >> 10);
			wcs[wpos++] = 0xdc00 | (c & 0x3ff);
		} else if (c >= 0xa0) {
			/* invalid utf-8 byte, printable unicode char: convert 1:1 */
			wcs[wpos++] = c;
		} else {
			/* invalid utf-8 byte, non-printable unicode: convert to hex */
			static const char *hex = "0123456789abcdef";
			wcs[wpos++] = hex[c >> 4];
			if (wpos < wcslen)
				wcs[wpos++] = hex[c & 0x0f];
		}
	}
	wcs[wpos] = 0;
	return wpos;
}

int xwcstoutf(char *utf, const wchar_t *wcs, size_t utflen)
{
	if (!wcs || !utf || utflen < 1) {
		errno = EINVAL;
		return -1;
	}
	utflen = WideCharToMultiByte(CP_UTF8, 0, wcs, -1, utf, utflen, NULL, NULL);
	if (utflen)
		return utflen - 1;
	errno = ERANGE;
	return -1;
}

static void setup_windows_environment(void)
{
	char *tmp = getenv("TMPDIR");

	/* on Windows it is TMP and TEMP */
	if (!tmp) {
		if (!(tmp = getenv("TMP")))
			tmp = getenv("TEMP");
		if (tmp) {
			setenv("TMPDIR", tmp, 1);
			tmp = getenv("TMPDIR");
		}
	}

	if (tmp) {
		/*
		 * Convert all dir separators to forward slashes,
		 * to help shell commands called from the Git
		 * executable (by not mistaking the dir separators
		 * for escape characters).
		 */
		convert_slashes(tmp);
	}

	/* simulate TERM to enable auto-color (see color.c) */
	if (!getenv("TERM"))
		setenv("TERM", "cygwin", 1);
}

/*
 * Disable MSVCRT command line wildcard expansion (__getmainargs called from
 * mingw startup code, see init.c in mingw runtime).
 */
int _CRT_glob = 0;

typedef struct {
	int newmode;
} _startupinfo;

extern int __wgetmainargs(int *argc, wchar_t ***argv, wchar_t ***env, int glob,
		_startupinfo *si);

static NORETURN void die_startup(void)
{
	fputs("fatal: not enough memory for initialization", stderr);
	exit(128);
}

static void *malloc_startup(size_t size)
{
	void *result = malloc(size);
	if (!result)
		die_startup();
	return result;
}

static char *wcstoutfdup_startup(char *buffer, const wchar_t *wcs, size_t len)
{
	len = xwcstoutf(buffer, wcs, len) + 1;
	return memcpy(malloc_startup(len), buffer, len);
}

void mingw_startup(void)
{
	int i, maxlen, argc;
	char *buffer;
	wchar_t **wenv, **wargv;
	_startupinfo si;

	/* get wide char arguments and environment */
	si.newmode = 0;
	if (__wgetmainargs(&argc, &wargv, &wenv, _CRT_glob, &si) < 0)
		die_startup();

	/* determine size of argv and environ conversion buffer */
	maxlen = wcslen(_wpgmptr);
	for (i = 1; i < argc; i++)
		maxlen = max(maxlen, wcslen(wargv[i]));
	for (i = 0; wenv[i]; i++)
		maxlen = max(maxlen, wcslen(wenv[i]));

	/*
	 * nedmalloc can't free CRT memory, allocate resizable environment
	 * list. Note that xmalloc / xmemdupz etc. call getenv, so we cannot
	 * use it while initializing the environment itself.
	 */
	environ_size = i + 1;
	environ_alloc = alloc_nr(environ_size * sizeof(char*));
	environ = malloc_startup(environ_alloc);

	/* allocate buffer (wchar_t encodes to max 3 UTF-8 bytes) */
	maxlen = 3 * maxlen + 1;
	buffer = malloc_startup(maxlen);

	/* convert command line arguments and environment to UTF-8 */
	__argv[0] = wcstoutfdup_startup(buffer, _wpgmptr, maxlen);
	for (i = 1; i < argc; i++)
		__argv[i] = wcstoutfdup_startup(buffer, wargv[i], maxlen);
	for (i = 0; wenv[i]; i++)
		environ[i] = wcstoutfdup_startup(buffer, wenv[i], maxlen);
	environ[i] = NULL;
	free(buffer);

	/* sort environment for O(log n) getenv / putenv */
	qsort(environ, i, sizeof(char*), compareenv);

	/* fix Windows specific environment settings */
	setup_windows_environment();

	/* initialize critical section for waitpid pinfo_t list */
	InitializeCriticalSection(&pinfo_cs);

	/* set up default file mode and file modes for stdin/out/err */
	_fmode = _O_BINARY;
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
	_setmode(_fileno(stderr), _O_BINARY);

	/* initialize Unicode console */
	winansi_init();
}

int uname(struct utsname *buf)
{
	unsigned v = (unsigned)GetVersion();
	memset(buf, 0, sizeof(*buf));
	xsnprintf(buf->sysname, sizeof(buf->sysname), "Windows");
	xsnprintf(buf->release, sizeof(buf->release),
		 "%u.%u", v & 0xff, (v >> 8) & 0xff);
	/* assuming NT variants only.. */
	xsnprintf(buf->version, sizeof(buf->version),
		  "%u", (v >> 16) & 0x7fff);
	return 0;
}
