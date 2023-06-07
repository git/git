#include "../git-compat-util.h"
#include "win32.h"
#include <aclapi.h>
#include <sddl.h>
#include <conio.h>
#include <wchar.h>
#include <winioctl.h>
#include "../strbuf.h"
#include "../run-command.h"
#include "../abspath.h"
#include "../alloc.h"
#include "win32/lazyload.h"
#include "../config.h"
#include "../environment.h"
#include "../trace2.h"
#include "../symlinks.h"
#include "../wrapper.h"
#include "dir.h"
#include "gettext.h"
#define SECURITY_WIN32
#include <sspi.h>
#include "../write-or-die.h"
#include "../repository.h"
#include "win32/fscache.h"
#include "../attr.h"
#include "../string-list.h"
#include "win32/wsl.h"

#define HCAST(type, handle) ((type)(intptr_t)handle)

void open_in_gdb(void)
{
	static struct child_process cp = CHILD_PROCESS_INIT;
	extern char *_pgmptr;

	strvec_pushl(&cp.args, "mintty", "gdb", NULL);
	strvec_pushf(&cp.args, "--pid=%d", getpid());
	cp.clean_on_exit = 1;
	if (start_command(&cp) < 0)
		die_errno("Could not start gdb");
	sleep(1);
}

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
	case ERROR_INVALID_REPARSE_DATA: error = EINVAL; break;
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
	case ERROR_NOT_A_REPARSE_POINT: error = EINVAL; break;
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
	case ERROR_REPARSE_ATTRIBUTE_CONFLICT: error = EINVAL; break;
	case ERROR_REPARSE_TAG_INVALID: error = EINVAL; break;
	case ERROR_REPARSE_TAG_MISMATCH: error = EINVAL; break;
	case ERROR_SEEK: error = EIO; break;
	case ERROR_SEEK_ON_DEVICE: error = ESPIPE; break;
	case ERROR_SHARING_BUFFER_EXCEEDED: error = ENFILE; break;
	case ERROR_SHARING_VIOLATION: error = EACCES; break;
	case ERROR_STACK_OVERFLOW: error = ENOMEM; break;
	case ERROR_SUCCESS: BUG("err_win_to_posix() called without an error!");
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

static int ask_yes_no_if_possible(const char *format, va_list args)
{
	char question[4096];
	const char *retry_hook;

	vsnprintf(question, sizeof(question), format, args);

	retry_hook = mingw_getenv("GIT_ASK_YESNO");
	if (retry_hook) {
		struct child_process cmd = CHILD_PROCESS_INIT;

		strvec_pushl(&cmd.args, retry_hook, question, NULL);
		return !run_command(&cmd);
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

static int retry_ask_yes_no(int *tries, const char *format, ...)
{
	static const int delay[] = { 0, 1, 10, 20, 40 };
	va_list args;
	int result, saved_errno = errno;

	if ((*tries) < ARRAY_SIZE(delay)) {
		/*
		 * We assume that some other process had the file open at the wrong
		 * moment and retry. In order to give the other process a higher
		 * chance to complete its operation, we give up our time slice now.
		 * If we have to retry again, we do sleep a bit.
		 */
		Sleep(delay[*tries]);
		(*tries)++;
		return 1;
	}

	va_start(args, format);
	result = ask_yes_no_if_possible(format, args);
	va_end(args);
	errno = saved_errno;
	return result;
}

/* Windows only */
enum hide_dotfiles_type {
	HIDE_DOTFILES_FALSE = 0,
	HIDE_DOTFILES_TRUE,
	HIDE_DOTFILES_DOTGITONLY
};

static int core_restrict_inherited_handles = -1;
static enum hide_dotfiles_type hide_dotfiles = HIDE_DOTFILES_DOTGITONLY;
static char *unset_environment_variables;
int core_fscache;

int are_long_paths_enabled(void)
{
	/* default to `false` during initialization */
	static const int fallback = 0;

	static int enabled = -1;

	if (enabled < 0) {
		/* avoid infinite recursion */
		if (!the_repository)
			return fallback;

		if (the_repository->config &&
		    the_repository->config->hash_initialized &&
		    git_config_get_bool("core.longpaths", &enabled) < 0)
			enabled = 0;
	}

	return enabled < 0 ? fallback : enabled;
}

int mingw_core_config(const char *var, const char *value,
		      const struct config_context *ctx, void *cb)
{
	if (!strcmp(var, "core.hidedotfiles")) {
		if (value && !strcasecmp(value, "dotgitonly"))
			hide_dotfiles = HIDE_DOTFILES_DOTGITONLY;
		else
			hide_dotfiles = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.fscache")) {
		core_fscache = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "core.unsetenvvars")) {
		if (!value)
			return config_error_nonbool(var);
		free(unset_environment_variables);
		unset_environment_variables = xstrdup(value);
		return 0;
	}

	if (!strcmp(var, "core.restrictinheritedhandles")) {
		if (value && !strcasecmp(value, "auto"))
			core_restrict_inherited_handles = -1;
		else
			core_restrict_inherited_handles =
				git_config_bool(var, value);
		return 0;
	}

	return 0;
}

static inline int is_wdir_sep(wchar_t wchar)
{
	return wchar == L'/' || wchar == L'\\';
}

static const wchar_t *make_relative_to(const wchar_t *path,
				       const wchar_t *relative_to, wchar_t *out,
				       size_t size)
{
	size_t i = wcslen(relative_to), len;

	/* Is `path` already absolute? */
	if (is_wdir_sep(path[0]) ||
	    (iswalpha(path[0]) && path[1] == L':' && is_wdir_sep(path[2])))
		return path;

	while (i > 0 && !is_wdir_sep(relative_to[i - 1]))
		i--;

	/* Is `relative_to` in the current directory? */
	if (!i)
		return path;

	len = wcslen(path);
	if (i + len + 1 > size) {
		error("Could not make '%ls' relative to '%ls' (too large)",
		      path, relative_to);
		return NULL;
	}

	memcpy(out, relative_to, i * sizeof(wchar_t));
	wcscpy(out + i, path);
	return out;
}

static DWORD symlink_file_flags = 0, symlink_directory_flags = 1;

enum phantom_symlink_result {
	PHANTOM_SYMLINK_RETRY,
	PHANTOM_SYMLINK_DONE,
	PHANTOM_SYMLINK_DIRECTORY
};

/*
 * Changes a file symlink to a directory symlink if the target exists and is a
 * directory.
 */
static enum phantom_symlink_result
process_phantom_symlink(const wchar_t *wtarget, const wchar_t *wlink)
{
	HANDLE hnd;
	BY_HANDLE_FILE_INFORMATION fdata;
	wchar_t relative[MAX_LONG_PATH];
	const wchar_t *rel;

	/* check that wlink is still a file symlink */
	if ((GetFileAttributesW(wlink)
			& (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
			!= FILE_ATTRIBUTE_REPARSE_POINT)
		return PHANTOM_SYMLINK_DONE;

	/* make it relative, if necessary */
	rel = make_relative_to(wtarget, wlink, relative, ARRAY_SIZE(relative));
	if (!rel)
		return PHANTOM_SYMLINK_DONE;

	/* let Windows resolve the link by opening it */
	hnd = CreateFileW(rel, 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hnd == INVALID_HANDLE_VALUE) {
		errno = err_win_to_posix(GetLastError());
		return PHANTOM_SYMLINK_RETRY;
	}

	if (!GetFileInformationByHandle(hnd, &fdata)) {
		errno = err_win_to_posix(GetLastError());
		CloseHandle(hnd);
		return PHANTOM_SYMLINK_RETRY;
	}
	CloseHandle(hnd);

	/* if target exists and is a file, we're done */
	if (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		return PHANTOM_SYMLINK_DONE;

	/* otherwise recreate the symlink with directory flag */
	if (DeleteFileW(wlink) &&
	    CreateSymbolicLinkW(wlink, wtarget, symlink_directory_flags))
		return PHANTOM_SYMLINK_DIRECTORY;

	errno = err_win_to_posix(GetLastError());
	return PHANTOM_SYMLINK_RETRY;
}

/* keep track of newly created symlinks to non-existing targets */
struct phantom_symlink_info {
	struct phantom_symlink_info *next;
	wchar_t *wlink;
	wchar_t *wtarget;
};

static struct phantom_symlink_info *phantom_symlinks = NULL;
static CRITICAL_SECTION phantom_symlinks_cs;

static void process_phantom_symlinks(void)
{
	struct phantom_symlink_info *current, **psi;
	EnterCriticalSection(&phantom_symlinks_cs);
	/* process phantom symlinks list */
	psi = &phantom_symlinks;
	while ((current = *psi)) {
		enum phantom_symlink_result result = process_phantom_symlink(
				current->wtarget, current->wlink);
		if (result == PHANTOM_SYMLINK_RETRY) {
			psi = &current->next;
		} else {
			/* symlink was processed, remove from list */
			*psi = current->next;
			free(current);
			/* if symlink was a directory, start over */
			if (result == PHANTOM_SYMLINK_DIRECTORY)
				psi = &phantom_symlinks;
		}
	}
	LeaveCriticalSection(&phantom_symlinks_cs);
}

static int create_phantom_symlink(wchar_t *wtarget, wchar_t *wlink)
{
	int len;

	/* create file symlink */
	if (!CreateSymbolicLinkW(wlink, wtarget, symlink_file_flags)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}

	/* convert to directory symlink if target exists */
	switch (process_phantom_symlink(wtarget, wlink)) {
	case PHANTOM_SYMLINK_RETRY: {
		/* if target doesn't exist, add to phantom symlinks list */
		wchar_t wfullpath[MAX_LONG_PATH];
		struct phantom_symlink_info *psi;

		/* convert to absolute path to be independent of cwd */
		len = GetFullPathNameW(wlink, MAX_LONG_PATH, wfullpath, NULL);
		if (!len || len >= MAX_LONG_PATH) {
			errno = err_win_to_posix(GetLastError());
			return -1;
		}

		/* over-allocate and fill phantom_symlink_info structure */
		psi = xmalloc(sizeof(struct phantom_symlink_info) +
			      sizeof(wchar_t) * (len + wcslen(wtarget) + 2));
		psi->wlink = (wchar_t *)(psi + 1);
		wcscpy(psi->wlink, wfullpath);
		psi->wtarget = psi->wlink + len + 1;
		wcscpy(psi->wtarget, wtarget);

		EnterCriticalSection(&phantom_symlinks_cs);
		psi->next = phantom_symlinks;
		phantom_symlinks = psi;
		LeaveCriticalSection(&phantom_symlinks_cs);
		break;
	}
	case PHANTOM_SYMLINK_DIRECTORY:
		/* if we created a dir symlink, process other phantom symlinks */
		process_phantom_symlinks();
		break;
	default:
		break;
	}
	return 0;
}

/* Normalizes NT paths as returned by some low-level APIs. */
static wchar_t *normalize_ntpath(wchar_t *wbuf)
{
	int i;
	/* fix absolute path prefixes */
	if (wbuf[0] == '\\') {
		/* strip NT namespace prefixes */
		if (!wcsncmp(wbuf, L"\\??\\", 4) ||
		    !wcsncmp(wbuf, L"\\\\?\\", 4))
			wbuf += 4;
		else if (!wcsnicmp(wbuf, L"\\DosDevices\\", 12))
			wbuf += 12;
		/* replace remaining '...UNC\' with '\\' */
		if (!wcsnicmp(wbuf, L"UNC\\", 4)) {
			wbuf += 2;
			*wbuf = '\\';
		}
	}
	/* convert backslashes to slashes */
	for (i = 0; wbuf[i]; i++)
		if (wbuf[i] == '\\')
			wbuf[i] = '/';
	return wbuf;
}

int mingw_unlink(const char *pathname)
{
	int tries = 0;
	wchar_t wpathname[MAX_LONG_PATH];
	if (xutftowcs_long_path(wpathname, pathname) < 0)
		return -1;

	if (DeleteFileW(wpathname))
		return 0;

	do {
		/* read-only files cannot be removed */
		_wchmod(wpathname, 0666);
		if (!_wunlink(wpathname))
			return 0;
		if (!is_file_in_use_error(GetLastError()))
			break;
		/*
		 * _wunlink() / DeleteFileW() for directory symlinks fails with
		 * ERROR_ACCESS_DENIED (EACCES), so try _wrmdir() as well. This is the
		 * same error we get if a file is in use (already checked above).
		 */
		if (!_wrmdir(wpathname))
			return 0;
	} while (retry_ask_yes_no(&tries, "Unlink of file '%s' failed. "
			"Should I try again?", pathname));
	return -1;
}

static int is_dir_empty(const wchar_t *wpath)
{
	WIN32_FIND_DATAW findbuf;
	HANDLE handle;
	wchar_t wbuf[MAX_LONG_PATH + 2];
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
	int tries = 0;
	wchar_t wpathname[MAX_LONG_PATH];
	struct stat st;

	/*
	 * Contrary to Linux' `rmdir()`, Windows' _wrmdir() and _rmdir()
	 * (and `RemoveDirectoryW()`) will attempt to remove the target of a
	 * symbolic link (if it points to a directory).
	 *
	 * This behavior breaks the assumption of e.g. `remove_path()` which
	 * upon successful deletion of a file will attempt to remove its parent
	 * directories recursively until failure (which usually happens when
	 * the directory is not empty).
	 *
	 * Therefore, before calling `_wrmdir()`, we first check if the path is
	 * a symbolic link. If it is, we exit and return the same error as
	 * Linux' `rmdir()` would, i.e. `ENOTDIR`.
	 */
	if (!mingw_lstat(pathname, &st) && S_ISLNK(st.st_mode)) {
		errno = ENOTDIR;
		return -1;
	}

	if (xutftowcs_long_path(wpathname, pathname) < 0)
		return -1;

	do {
		if (!_wrmdir(wpathname)) {
			invalidate_lstat_cache();
			return 0;
		}
		if (!is_file_in_use_error(GetLastError()))
			errno = err_win_to_posix(GetLastError());
		if (errno != EACCES)
			break;
		if (!is_dir_empty(wpathname)) {
			errno = ENOTEMPTY;
			break;
		}
	} while (retry_ask_yes_no(&tries, "Deletion of directory '%s' failed. "
			"Should I try again?", pathname));
	return -1;
}

static inline int needs_hiding(const char *path)
{
	const char *basename;

	if (hide_dotfiles == HIDE_DOTFILES_FALSE)
		return 0;

	/* We cannot use basename(), as it would remove trailing slashes */
	win32_skip_dos_drive_prefix((char **)&path);
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
			else
				break;
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
	wchar_t wpath[MAX_LONG_PATH];

	if (!is_valid_win32_path(path, 0)) {
		errno = EINVAL;
		return -1;
	}

	/* CreateDirectoryW path limit is 248 (MAX_PATH - 8.3 file name) */
	if (xutftowcs_path_ex(wpath, path, MAX_LONG_PATH, -1, 248,
			      are_long_paths_enabled()) < 0)
		return -1;

	ret = _wmkdir(wpath);
	if (!ret)
		process_phantom_symlinks();
	if (!ret && needs_hiding(path))
		return set_hidden_flag(wpath, 1);
	return ret;
}

/*
 * Calling CreateFile() using FILE_APPEND_DATA and without FILE_WRITE_DATA
 * is documented in [1] as opening a writable file handle in append mode.
 * (It is believed that) this is atomic since it is maintained by the
 * kernel unlike the O_APPEND flag which is racily maintained by the CRT.
 *
 * [1] https://docs.microsoft.com/en-us/windows/desktop/fileio/file-access-rights-constants
 *
 * This trick does not appear to work for named pipes.  Instead it creates
 * a named pipe client handle that cannot be written to.  Callers should
 * just use the regular _wopen() for them.  (And since client handle gets
 * bound to a unique server handle, it isn't really an issue.)
 */
static int mingw_open_append(wchar_t const *wfilename, int oflags, ...)
{
	HANDLE handle;
	int fd;
	DWORD create = (oflags & O_CREAT) ? OPEN_ALWAYS : OPEN_EXISTING;

	/* only these flags are supported */
	if ((oflags & ~O_CREAT) != (O_WRONLY | O_APPEND))
		return errno = ENOSYS, -1;

	/*
	 * FILE_SHARE_WRITE is required to permit child processes
	 * to append to the file.
	 */
	handle = CreateFileW(wfilename, FILE_APPEND_DATA,
			FILE_SHARE_WRITE | FILE_SHARE_READ,
			NULL, create, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();

		/*
		 * Some network storage solutions (e.g. Isilon) might return
		 * ERROR_INVALID_PARAMETER instead of expected error
		 * ERROR_PATH_NOT_FOUND, which results in an unknown error. If
		 * so, let's turn the error to ERROR_PATH_NOT_FOUND instead.
		 */
		if (err == ERROR_INVALID_PARAMETER)
			err = ERROR_PATH_NOT_FOUND;

		errno = err_win_to_posix(err);
		return -1;
	}

	/*
	 * No O_APPEND here, because the CRT uses it only to reset the
	 * file pointer to EOF before each write(); but that is not
	 * necessary (and may lead to races) for a file created with
	 * FILE_APPEND_DATA.
	 */
	fd = _open_osfhandle((intptr_t)handle, O_BINARY);
	if (fd < 0)
		CloseHandle(handle);
	return fd;
}

/*
 * Does the pathname map to the local named pipe filesystem?
 * That is, does it have a "//./pipe/" prefix?
 */
static int is_local_named_pipe_path(const char *filename)
{
	return (is_dir_sep(filename[0]) &&
		is_dir_sep(filename[1]) &&
		filename[2] == '.'  &&
		is_dir_sep(filename[3]) &&
		!strncasecmp(filename+4, "pipe", 4) &&
		is_dir_sep(filename[8]) &&
		filename[9]);
}

int mingw_open (const char *filename, int oflags, ...)
{
	static int append_atomically = -1;
	typedef int (*open_fn_t)(wchar_t const *wfilename, int oflags, ...);
	va_list args;
	unsigned mode;
	int fd, create = (oflags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL);
	wchar_t wfilename[MAX_LONG_PATH];
	open_fn_t open_fn;

	va_start(args, oflags);
	mode = va_arg(args, int);
	va_end(args);

	if (!is_valid_win32_path(filename, !create)) {
		errno = create ? EINVAL : ENOENT;
		return -1;
	}

	/*
	 * Only set append_atomically to default value(1) when repo is initialized
	 * and fail to get config value
	 */
	if (append_atomically < 0 && the_repository && the_repository->commondir &&
		git_config_get_bool("windows.appendatomically", &append_atomically))
		append_atomically = 1;

	if (append_atomically && (oflags & O_APPEND) &&
		!is_local_named_pipe_path(filename))
		open_fn = mingw_open_append;
	else
		open_fn = _wopen;

	if (filename && !strcmp(filename, "/dev/null"))
		wcscpy(wfilename, L"nul");
	else if (xutftowcs_long_path(wfilename, filename) < 0)
		return -1;

	fd = open_fn(wfilename, oflags, mode);

	if ((oflags & O_CREAT) && fd >= 0 && are_wsl_compatible_mode_bits_enabled()) {
		_mode_t wsl_mode = S_IFREG | (mode&0777);
		set_wsl_mode_bits_by_handle((HANDLE)_get_osfhandle(fd), wsl_mode);
	}

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
			fd = open_fn(wfilename, oflags & ~O_CREAT, mode);
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
	wchar_t wfilename[MAX_LONG_PATH], wotype[4];
	if (filename && !strcmp(filename, "/dev/null"))
		wcscpy(wfilename, L"nul");
	else if (!is_valid_win32_path(filename, 1)) {
		int create = otype && strchr(otype, 'w');
		errno = create ? EINVAL : ENOENT;
		return NULL;
	} else if (xutftowcs_long_path(wfilename, filename) < 0)
		return NULL;

	if (xutftowcs(wotype, otype, ARRAY_SIZE(wotype)) < 0)
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
	wchar_t wfilename[MAX_LONG_PATH], wotype[4];
	if (filename && !strcmp(filename, "/dev/null"))
		wcscpy(wfilename, L"nul");
	else if (!is_valid_win32_path(filename, 1)) {
		int create = otype && strchr(otype, 'w');
		errno = create ? EINVAL : ENOENT;
		return NULL;
	} else if (xutftowcs_long_path(wfilename, filename) < 0)
		return NULL;

	if (xutftowcs(wotype, otype, ARRAY_SIZE(wotype)) < 0)
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

	if (result < 0 && (errno == EINVAL || errno == EBADF || errno == ENOSPC) && buf) {
		int orig = errno;

		/* check if fd is a pipe */
		HANDLE h = (HANDLE) _get_osfhandle(fd);
		if (GetFileType(h) != FILE_TYPE_PIPE) {
			if (orig == EINVAL) {
				wchar_t path[MAX_LONG_PATH];
				DWORD ret = GetFinalPathNameByHandleW(h, path,
								ARRAY_SIZE(path), 0);
				UINT drive_type = ret > 0 && ret < ARRAY_SIZE(path) ?
					GetDriveTypeW(path) : DRIVE_UNKNOWN;

				/*
				 * The default atomic append causes such an error on
				 * network file systems, in such a case, it should be
				 * turned off via config.
				 *
				 * `drive_type` of UNC path: DRIVE_NO_ROOT_DIR
				 */
				if (DRIVE_NO_ROOT_DIR == drive_type || DRIVE_REMOTE == drive_type)
					warning("invalid write operation detected; you may try:\n"
						"\n\tgit config windows.appendAtomically false");
			}

			errno = orig;
		} else if (orig == EINVAL || errno == EBADF)
			errno = EPIPE;
		else {
			DWORD buf_size;

			if (!GetNamedPipeInfo(h, NULL, NULL, &buf_size, NULL))
				buf_size = 4096;
			if (len > buf_size)
				return write(fd, buf, buf_size);
			errno = orig;
		}
	}

	return result;
}

int mingw_access(const char *filename, int mode)
{
	wchar_t wfilename[MAX_LONG_PATH];
	if (!strcmp("nul", filename) || !strcmp("/dev/null", filename))
		return 0;
	if (xutftowcs_long_path(wfilename, filename) < 0)
		return -1;
	/* X_OK is not supported by the MSVCRT version */
	return _waccess(wfilename, mode & ~X_OK);
}

/* cached length of current directory for handle_long_path */
static int current_directory_len = 0;

int mingw_chdir(const char *dirname)
{
	int result;
	wchar_t wdirname[MAX_LONG_PATH];
	if (xutftowcs_long_path(wdirname, dirname) < 0)
		return -1;

	if (has_symlinks) {
		HANDLE hnd = CreateFileW(wdirname, 0,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
				OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
		if (hnd == INVALID_HANDLE_VALUE) {
			errno = err_win_to_posix(GetLastError());
			return -1;
		}
		if (!GetFinalPathNameByHandleW(hnd, wdirname, ARRAY_SIZE(wdirname), 0)) {
			errno = err_win_to_posix(GetLastError());
			CloseHandle(hnd);
			return -1;
		}
		CloseHandle(hnd);
	}

	result = _wchdir(normalize_ntpath(wdirname));
	current_directory_len = GetCurrentDirectoryW(0, NULL);
	return result;
}

int mingw_chmod(const char *filename, int mode)
{
	wchar_t wfilename[MAX_LONG_PATH];
	if (xutftowcs_long_path(wfilename, filename) < 0)
		return -1;
	return _wchmod(wfilename, mode);
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
		if (attributes &
		    (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))
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

static int readlink_1(const WCHAR *wpath, BOOL fail_on_unknown_tag,
		      char *tmpbuf, int *plen, DWORD *ptag);

int mingw_lstat(const char *file_name, struct stat *buf)
{
	WIN32_FILE_ATTRIBUTE_DATA fdata;
	DWORD reparse_tag = 0;
	int link_len = 0;
	wchar_t wfilename[MAX_LONG_PATH];
	int wlen = xutftowcs_long_path(wfilename, file_name);
	if (wlen < 0)
		return -1;

	/* strip trailing '/', or GetFileAttributes will fail */
	while (wlen && is_dir_sep(wfilename[wlen - 1]))
		wfilename[--wlen] = 0;
	if (!wlen) {
		errno = ENOENT;
		return -1;
	}

	if (GetFileAttributesExW(wfilename, GetFileExInfoStandard, &fdata)) {
		/* for reparse points, get the link tag and length */
		if (fdata.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
			char tmpbuf[MAX_LONG_PATH];

			if (readlink_1(wfilename, FALSE, tmpbuf, &link_len,
				       &reparse_tag) < 0)
				return -1;
		}
		buf->st_ino = 0;
		buf->st_gid = 0;
		buf->st_uid = 0;
		buf->st_nlink = 1;
		buf->st_mode = file_attr_to_st_mode(fdata.dwFileAttributes,
				reparse_tag);
		buf->st_size = S_ISLNK(buf->st_mode) ? link_len :
			fdata.nFileSizeLow | (((off_t) fdata.nFileSizeHigh) << 32);
		buf->st_dev = buf->st_rdev = 0; /* not used by Git */
		filetime_to_timespec(&(fdata.ftLastAccessTime), &(buf->st_atim));
		filetime_to_timespec(&(fdata.ftLastWriteTime), &(buf->st_mtim));
		filetime_to_timespec(&(fdata.ftCreationTime), &(buf->st_ctim));
		if (S_ISREG(buf->st_mode) &&
		    are_wsl_compatible_mode_bits_enabled()) {
			copy_wsl_mode_bits_from_disk(wfilename, -1,
						     &buf->st_mode);
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

int (*lstat)(const char *file_name, struct stat *buf) = mingw_lstat;

static int get_file_info_by_handle(HANDLE hnd, struct stat *buf)
{
	BY_HANDLE_FILE_INFORMATION fdata;

	if (!GetFileInformationByHandle(hnd, &fdata)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}

	buf->st_ino = 0;
	buf->st_gid = 0;
	buf->st_uid = 0;
	buf->st_nlink = 1;
	buf->st_mode = file_attr_to_st_mode(fdata.dwFileAttributes, 0);
	buf->st_size = fdata.nFileSizeLow |
		(((off_t)fdata.nFileSizeHigh)<<32);
	buf->st_dev = buf->st_rdev = 0; /* not used by Git */
	filetime_to_timespec(&(fdata.ftLastAccessTime), &(buf->st_atim));
	filetime_to_timespec(&(fdata.ftLastWriteTime), &(buf->st_mtim));
	filetime_to_timespec(&(fdata.ftCreationTime), &(buf->st_ctim));
	if (are_wsl_compatible_mode_bits_enabled())
	    get_wsl_mode_bits_by_handle(hnd, &buf->st_mode);
	return 0;
}

int mingw_stat(const char *file_name, struct stat *buf)
{
	wchar_t wfile_name[MAX_LONG_PATH];
	HANDLE hnd;
	int result;

	/* open the file and let Windows resolve the links */
	if (xutftowcs_long_path(wfile_name, file_name) < 0)
		return -1;
	hnd = CreateFileW(wfile_name, 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hnd == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();

		if (err == ERROR_ACCESS_DENIED &&
		    !mingw_lstat(file_name, buf) &&
		    !S_ISLNK(buf->st_mode))
			/*
			 * POSIX semantics state to still try to fill
			 * information, even if permission is denied to create
			 * a file handle.
			 */
			return 0;

		errno = err_win_to_posix(err);
		return -1;
	}
	result = get_file_info_by_handle(hnd, buf);
	CloseHandle(hnd);
	return result;
}

int mingw_fstat(int fd, struct stat *buf)
{
	HANDLE fh = (HANDLE)_get_osfhandle(fd);
	DWORD avail, type = GetFileType(fh) & ~FILE_TYPE_REMOTE;

	switch (type) {
	case FILE_TYPE_DISK:
		return get_file_info_by_handle(fh, buf);

	case FILE_TYPE_CHAR:
	case FILE_TYPE_PIPE:
		/* initialize stat fields */
		memset(buf, 0, sizeof(*buf));
		buf->st_nlink = 1;

		if (type == FILE_TYPE_CHAR) {
			buf->st_mode = _S_IFCHR;
		} else {
			buf->st_mode = _S_IFIFO;
			if (PeekNamedPipe(fh, NULL, 0, NULL, &avail, NULL))
				buf->st_size = avail;
		}
		return 0;

	default:
		errno = EBADF;
		return -1;
	}
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
	int rc;
	DWORD attrs;
	wchar_t wfilename[MAX_LONG_PATH];
	HANDLE osfilehandle;

	if (xutftowcs_long_path(wfilename, file_name) < 0)
		return -1;

	/* must have write permission */
	attrs = GetFileAttributesW(wfilename);
	if (attrs != INVALID_FILE_ATTRIBUTES &&
	    (attrs & FILE_ATTRIBUTE_READONLY)) {
		/* ignore errors here; open() will report them */
		SetFileAttributesW(wfilename, attrs & ~FILE_ATTRIBUTE_READONLY);
	}

	osfilehandle = CreateFileW(wfilename,
				   FILE_WRITE_ATTRIBUTES,
				   0 /*FileShare.None*/,
				   NULL,
				   OPEN_EXISTING,
				   (attrs != INVALID_FILE_ATTRIBUTES &&
					(attrs & FILE_ATTRIBUTE_DIRECTORY)) ?
					FILE_FLAG_BACKUP_SEMANTICS : 0,
				   NULL);
	if (osfilehandle == INVALID_HANDLE_VALUE) {
		errno = err_win_to_posix(GetLastError());
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

	if (!SetFileTime(osfilehandle, NULL, &aft, &mft)) {
		errno = EINVAL;
		rc = -1;
	} else
		rc = 0;

	if (osfilehandle != INVALID_HANDLE_VALUE)
		CloseHandle(osfilehandle);

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
	/* a pointer to the original strftime in case we can't find the UCRT version */
	static size_t (*fallback)(char *, size_t, const char *, const struct tm *) = strftime;
	size_t ret;
	DECLARE_PROC_ADDR(ucrtbase.dll, size_t, __cdecl, strftime, char *, size_t,
		const char *, const struct tm *);

	if (INIT_PROC_ADDR(strftime))
		ret = strftime(s, max, format, tm);
	else
		ret = fallback(s, max, format, tm);

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
	int offset = 0;

	/* we need to return the path, thus no long paths here! */
	if (xutftowcs_path(wtemplate, template) < 0)
		return NULL;

	if (is_dir_sep(template[0]) && !is_dir_sep(template[1]) &&
	    iswalpha(wtemplate[0]) && wtemplate[1] == L':') {
		/* We have an absolute path missing the drive prefix */
		offset = 2;
	}
	if (!_wmktemp(wtemplate))
		return NULL;
	if (xwcstoutf(template, wtemplate + offset, strlen(template) + 1) < 0)
		return NULL;
	return template;
}

int mkstemp(char *template)
{
	return git_mkstemp_mode(template, 0600);
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

#ifndef __MINGW64__
struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
	if (gmtime_s(result, timep) == 0)
		return result;
	return NULL;
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
	if (localtime_s(result, timep) == 0)
		return result;
	return NULL;
}
#endif

char *mingw_strbuf_realpath(struct strbuf *resolved, const char *path)
{
	wchar_t wpath[MAX_PATH];
	HANDLE h;
	DWORD ret;
	int len;
	const char *last_component = NULL;
	char *append = NULL;

	if (xutftowcs_path(wpath, path) < 0)
		return NULL;

	h = CreateFileW(wpath, 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	/*
	 * strbuf_realpath() allows the last path component to not exist. If
	 * that is the case, now it's time to try without last component.
	 */
	if (h == INVALID_HANDLE_VALUE &&
	    GetLastError() == ERROR_FILE_NOT_FOUND) {
		/* cut last component off of `wpath` */
		wchar_t *p = wpath + wcslen(wpath);

		while (p != wpath)
			if (*(--p) == L'/' || *p == L'\\')
				break; /* found start of last component */

		if (p != wpath && (last_component = find_last_dir_sep(path))) {
			append = xstrdup(last_component + 1); /* skip directory separator */
			/*
			 * Do not strip the trailing slash at the drive root, otherwise
			 * the path would be e.g. `C:` (which resolves to the
			 * _current_ directory on that drive).
			 */
			if (p[-1] == L':')
				p[1] = L'\0';
			else
				*p = L'\0';
			h = CreateFileW(wpath, 0, FILE_SHARE_READ |
					FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					NULL, OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS, NULL);
		}
	}

	if (h == INVALID_HANDLE_VALUE) {
realpath_failed:
		FREE_AND_NULL(append);
		return NULL;
	}

	ret = GetFinalPathNameByHandleW(h, wpath, ARRAY_SIZE(wpath), 0);
	CloseHandle(h);
	if (!ret || ret >= ARRAY_SIZE(wpath))
		goto realpath_failed;

	len = wcslen(wpath) * 3;
	strbuf_grow(resolved, len);
	len = xwcstoutf(resolved->buf, normalize_ntpath(wpath), len);
	if (len < 0)
		goto realpath_failed;
	resolved->len = len;

	if (append) {
		/* Use forward-slash, like `normalize_ntpath()` */
		strbuf_complete(resolved, '/');
		strbuf_addstr(resolved, append);
		FREE_AND_NULL(append);
	}

	return resolved->buf;

}

char *mingw_getcwd(char *pointer, int len)
{
	wchar_t cwd[MAX_PATH], wpointer[MAX_PATH];
	DWORD ret = GetCurrentDirectoryW(ARRAY_SIZE(cwd), cwd);
	HANDLE hnd;

	if (!ret || ret >= ARRAY_SIZE(cwd)) {
		errno = ret ? ENAMETOOLONG : err_win_to_posix(GetLastError());
		return NULL;
	}
	hnd = CreateFileW(cwd, 0,
			  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hnd != INVALID_HANDLE_VALUE) {
		ret = GetFinalPathNameByHandleW(hnd, wpointer, ARRAY_SIZE(wpointer), 0);
		CloseHandle(hnd);
		if (!ret || ret >= ARRAY_SIZE(wpointer)) {
			ret = GetLongPathNameW(cwd, wpointer, ARRAY_SIZE(wpointer));
			if (!ret || ret >= ARRAY_SIZE(wpointer)) {
				errno = ret ? ENAMETOOLONG : err_win_to_posix(GetLastError());
				return NULL;
			}
		}
		if (xwcstoutf(pointer, normalize_ntpath(wpointer), len) < 0)
			return NULL;
		return pointer;
	}
	if (GetFileAttributesW(cwd) == INVALID_FILE_ATTRIBUTES) {
		errno = ENOENT;
		return NULL;
	}
	if (xwcstoutf(pointer, cwd, len) < 0)
		return NULL;
	convert_slashes(pointer);
	return pointer;
}

/*
 * See "Parsing C++ Command-Line Arguments" at Microsoft's Docs:
 * https://docs.microsoft.com/en-us/cpp/cpp/parsing-cpp-command-line-arguments
 */
static const char *quote_arg_msvc(const char *arg)
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
			if (*p == '"' || !*p)
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
			if (*arg == '"' || !*arg) {
				while (count-- > 0)
					*d++ = '\\';
				/* don't escape the surrounding end quote */
				if (!*arg)
					break;
				*d++ = '\\';
			}
		}
		*d++ = *arg++;
	}
	*d++ = '"';
	*d++ = '\0';
	return q;
}

#include "quote.h"

static const char *quote_arg_msys2(const char *arg)
{
	struct strbuf buf = STRBUF_INIT;
	const char *p2 = arg, *p;

	for (p = arg; *p; p++) {
		int ws = isspace(*p);
		if (!ws && *p != '\\' && *p != '"' && *p != '{' && *p != '\'' &&
		    *p != '?' && *p != '*' && *p != '~')
			continue;
		if (!buf.len)
			strbuf_addch(&buf, '"');
		if (p != p2)
			strbuf_add(&buf, p2, p - p2);
		if (*p == '\\' || *p == '"')
			strbuf_addch(&buf, '\\');
		p2 = p;
	}

	if (p == arg)
		strbuf_addch(&buf, '"');
	else if (!buf.len)
		return arg;
	else
		strbuf_add(&buf, p2, p - p2);

	strbuf_addch(&buf, '"');
	return strbuf_detach(&buf, 0);
}

static const char *parse_interpreter(const char *cmd)
{
	static char buf[MAX_PATH];
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
	wchar_t wpath[MAX_PATH];
	snprintf(path, sizeof(path), "%.*s\\%s.exe", dirlen, dir, cmd);

	if (xutftowcs_path(wpath, path) < 0)
		return NULL;

	if (!isexe && _waccess(wpath, F_OK) == 0)
		return xstrdup(path);
	wpath[wcslen(wpath)-4] = '\0';
	if ((!exe_only || isexe) && _waccess(wpath, F_OK) == 0) {
		if (!(GetFileAttributesW(wpath) & FILE_ATTRIBUTE_DIRECTORY)) {
			path[strlen(path)-4] = '\0';
			return xstrdup(path);
		}
	}
	return NULL;
}

static char *path_lookup(const char *cmd, int exe_only);

static char *is_busybox_applet(const char *cmd)
{
	static struct string_list applets = STRING_LIST_INIT_DUP;
	static char *busybox_path;
	static int busybox_path_initialized;

	/* Avoid infinite loop */
	if (!strncasecmp(cmd, "busybox", 7) &&
	    (!cmd[7] || !strcasecmp(cmd + 7, ".exe")))
		return NULL;

	if (!busybox_path_initialized) {
		busybox_path = path_lookup("busybox.exe", 1);
		busybox_path_initialized = 1;
	}

	/* Assume that sh is compiled in... */
	if (!busybox_path || !strcasecmp(cmd, "sh"))
		return xstrdup_or_null(busybox_path);

	if (!applets.nr) {
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf buf = STRBUF_INIT;
		char *p;

		strvec_pushl(&cp.args, busybox_path, "--help", NULL);

		if (capture_command(&cp, &buf, 2048)) {
			string_list_append(&applets, "");
			return NULL;
		}

		/* parse output */
		p = strstr(buf.buf, "Currently defined functions:\n");
		if (!p) {
			warning("Could not parse output of busybox --help");
			string_list_append(&applets, "");
			return NULL;
		}
		p = strchrnul(p, '\n');
		for (;;) {
			size_t len;

			p += strspn(p, "\n\t ,");
			len = strcspn(p, "\n\t ,");
			if (!len)
				break;
			p[len] = '\0';
			string_list_insert(&applets, p);
			p = p + len + 1;
		}
	}

	return string_list_has_string(&applets, cmd) ?
		xstrdup(busybox_path) : NULL;
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

	if (strpbrk(cmd, "/\\"))
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

	if (!prog && !isexe)
		prog = is_busybox_applet(cmd);

	return prog;
}

char *mingw_locate_in_PATH(const char *cmd)
{
	return path_lookup(cmd, 0);
}

static const wchar_t *wcschrnul(const wchar_t *s, wchar_t c)
{
	while (*s && *s != c)
		s++;
	return s;
}

/* Compare only keys */
static int wenvcmp(const void *a, const void *b)
{
	wchar_t *p = *(wchar_t **)a, *q = *(wchar_t **)b;
	size_t p_len, q_len;

	/* Find the keys */
	p_len = wcschrnul(p, L'=') - p;
	q_len = wcschrnul(q, L'=') - q;

	/* If the length differs, include the shorter key's NUL */
	if (p_len < q_len)
		p_len++;
	else if (p_len > q_len)
		p_len = q_len + 1;

	return _wcsnicmp(p, q, p_len);
}

/*
 * Build an environment block combining the inherited environment
 * merged with the given list of settings.
 *
 * Values of the form "KEY=VALUE" in deltaenv override inherited values.
 * Values of the form "KEY" in deltaenv delete inherited values.
 *
 * Multiple entries in deltaenv for the same key are explicitly allowed.
 *
 * We return a contiguous block of UNICODE strings with a final trailing
 * zero word.
 */
static wchar_t *make_environment_block(char **deltaenv)
{
	wchar_t *wenv = GetEnvironmentStringsW(), *wdeltaenv, *result, *p;
	size_t wlen, s, delta_size, size;

	wchar_t **array = NULL;
	size_t alloc = 0, nr = 0, i;

	size = 1; /* for extra NUL at the end */

	/* If there is no deltaenv to apply, simply return a copy. */
	if (!deltaenv || !*deltaenv) {
		for (p = wenv; p && *p; ) {
			size_t s = wcslen(p) + 1;
			size += s;
			p += s;
		}

		DUP_ARRAY(result, wenv, size);
		FreeEnvironmentStringsW(wenv);
		return result;
	}

	/*
	 * If there is a deltaenv, let's accumulate all keys into `array`,
	 * sort them using the stable git_stable_qsort() and then copy,
	 * skipping duplicate keys
	 */
	for (p = wenv; p && *p; ) {
		ALLOC_GROW(array, nr + 1, alloc);
		s = wcslen(p) + 1;
		array[nr++] = p;
		p += s;
		size += s;
	}

	/* (over-)assess size needed for wchar version of deltaenv */
	for (delta_size = 0, i = 0; deltaenv[i]; i++)
		delta_size += strlen(deltaenv[i]) * 2 + 1;
	ALLOC_ARRAY(wdeltaenv, delta_size);

	/* convert the deltaenv, appending to array */
	for (i = 0, p = wdeltaenv; deltaenv[i]; i++) {
		ALLOC_GROW(array, nr + 1, alloc);
		wlen = xutftowcs(p, deltaenv[i], wdeltaenv + delta_size - p);
		array[nr++] = p;
		p += wlen + 1;
	}

	git_stable_qsort(array, nr, sizeof(*array), wenvcmp);
	ALLOC_ARRAY(result, size + delta_size);

	for (p = result, i = 0; i < nr; i++) {
		/* Skip any duplicate keys; last one wins */
		while (i + 1 < nr && !wenvcmp(array + i, array + i + 1))
		       i++;

		/* Skip "to delete" entry */
		if (!wcschr(array[i], L'='))
			continue;

		size = wcslen(array[i]) + 1;
		COPY_ARRAY(p, array[i], size);
		p += size;
	}
	*p = L'\0';

	free(array);
	free(wdeltaenv);
	FreeEnvironmentStringsW(wenv);
	return result;
}

static void do_unset_environment_variables(void)
{
	static int done;
	char *p = unset_environment_variables;

	if (done || !p)
		return;
	done = 1;

	for (;;) {
		char *comma = strchr(p, ',');

		if (comma)
			*comma = '\0';
		unsetenv(p);
		if (!comma)
			break;
		p = comma + 1;
	}
}

struct pinfo_t {
	struct pinfo_t *next;
	pid_t pid;
	HANDLE proc;
};
static struct pinfo_t *pinfo = NULL;
CRITICAL_SECTION pinfo_cs;

/* Used to match and chomp off path components */
static inline int match_last_path_component(const char *path, size_t *len,
					    const char *component)
{
	size_t component_len = strlen(component);
	if (*len < component_len + 1 ||
	    !is_dir_sep(path[*len - component_len - 1]) ||
	    fspathncmp(path + *len - component_len, component, component_len))
		return 0;
	*len -= component_len + 1;
	/* chomp off repeated dir separators */
	while (*len > 0 && is_dir_sep(path[*len - 1]))
		(*len)--;
	return 1;
}

static int is_msys2_sh(const char *cmd)
{
	if (!cmd)
		return 0;

	if (!strcmp(cmd, "sh")) {
		static int ret = -1;
		char *p;

		if (ret >= 0)
			return ret;

		p = path_lookup(cmd, 0);
		if (!p)
			ret = 0;
		else {
			size_t len = strlen(p);

			ret = match_last_path_component(p, &len, "sh.exe") &&
				match_last_path_component(p, &len, "bin") &&
				match_last_path_component(p, &len, "usr");
			free(p);
		}
		return ret;
	}

	if (ends_with(cmd, "\\sh.exe")) {
		static char *sh;

		if (!sh)
			sh = path_lookup("sh", 0);

		return !fspathcmp(cmd, sh);
	}

	return 0;
}

static pid_t mingw_spawnve_fd(const char *cmd, const char **argv, char **deltaenv,
			      const char *dir, const char *prepend_cmd,
			      int fhin, int fhout, int fherr)
{
	static int restrict_handle_inheritance = -1;
	STARTUPINFOEXW si;
	PROCESS_INFORMATION pi;
	LPPROC_THREAD_ATTRIBUTE_LIST attr_list = NULL;
	HANDLE stdhandles[3];
	DWORD stdhandles_count = 0;
	SIZE_T size;
	struct strbuf args;
	wchar_t wcmd[MAX_PATH], wdir[MAX_PATH], *wargs, *wenvblk = NULL;
	unsigned flags = CREATE_UNICODE_ENVIRONMENT;
	BOOL ret;
	HANDLE cons;
	const char *(*quote_arg)(const char *arg) =
		is_msys2_sh(cmd ? cmd : *argv) ?
		quote_arg_msys2 : quote_arg_msvc;
	const char *strace_env;

	/* Make sure to override previous errors, if any */
	errno = 0;

	if (restrict_handle_inheritance < 0)
		restrict_handle_inheritance = core_restrict_inherited_handles;
	/*
	 * The following code to restrict which handles are inherited seems
	 * to work properly only on Windows 7 and later, so let's disable it
	 * on Windows Vista and 2008.
	 */
	if (restrict_handle_inheritance < 0)
		restrict_handle_inheritance = GetVersion() >> 16 >= 7601;

	do_unset_environment_variables();

	/* Determine whether or not we are associated to a console */
	cons = CreateFileW(L"CONOUT$", GENERIC_WRITE,
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
	si.StartupInfo.cb = sizeof(si);
	si.StartupInfo.hStdInput = winansi_get_osfhandle(fhin);
	si.StartupInfo.hStdOutput = winansi_get_osfhandle(fhout);
	si.StartupInfo.hStdError = winansi_get_osfhandle(fherr);

	/* The list of handles cannot contain duplicates */
	if (si.StartupInfo.hStdInput != INVALID_HANDLE_VALUE)
		stdhandles[stdhandles_count++] = si.StartupInfo.hStdInput;
	if (si.StartupInfo.hStdOutput != INVALID_HANDLE_VALUE &&
	    si.StartupInfo.hStdOutput != si.StartupInfo.hStdInput)
		stdhandles[stdhandles_count++] = si.StartupInfo.hStdOutput;
	if (si.StartupInfo.hStdError != INVALID_HANDLE_VALUE &&
	    si.StartupInfo.hStdError != si.StartupInfo.hStdInput &&
	    si.StartupInfo.hStdError != si.StartupInfo.hStdOutput)
		stdhandles[stdhandles_count++] = si.StartupInfo.hStdError;
	if (stdhandles_count)
		si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;

	if (*argv && !strcmp(cmd, *argv))
		wcmd[0] = L'\0';
	/*
	 * Paths to executables and to the current directory do not support
	 * long paths, therefore we cannot use xutftowcs_long_path() here.
	 */
	else if (xutftowcs_path(wcmd, cmd) < 0)
		return -1;
	if (dir && xutftowcs_path(wdir, dir) < 0)
		return -1;

	/* concatenate argv, quoting args as we go */
	strbuf_init(&args, 0);
	if (prepend_cmd) {
		char *quoted = (char *)quote_arg(prepend_cmd);
		strbuf_addstr(&args, quoted);
		if (quoted != prepend_cmd)
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

	strace_env = getenv("GIT_STRACE_COMMANDS");
	if (strace_env) {
		char *p = path_lookup("strace.exe", 1);
		if (!p)
			return error("strace not found!");
		if (xutftowcs_path(wcmd, p) < 0) {
			free(p);
			return -1;
		}
		free(p);
		if (!strcmp("1", strace_env) ||
		    !strcasecmp("yes", strace_env) ||
		    !strcasecmp("true", strace_env))
			strbuf_insert(&args, 0, "strace ", 7);
		else {
			const char *quoted = quote_arg(strace_env);
			struct strbuf buf = STRBUF_INIT;
			strbuf_addf(&buf, "strace -o %s ", quoted);
			if (quoted != strace_env)
				free((char *)quoted);
			strbuf_insert(&args, 0, buf.buf, buf.len);
			strbuf_release(&buf);
		}
	}

	ALLOC_ARRAY(wargs, st_add(st_mult(2, args.len), 1));
	xutftowcs(wargs, args.buf, 2 * args.len + 1);
	strbuf_release(&args);

	wenvblk = make_environment_block(deltaenv);

	memset(&pi, 0, sizeof(pi));
	if (restrict_handle_inheritance && stdhandles_count &&
	    (InitializeProcThreadAttributeList(NULL, 1, 0, &size) ||
	     GetLastError() == ERROR_INSUFFICIENT_BUFFER) &&
	    (attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)
			(HeapAlloc(GetProcessHeap(), 0, size))) &&
	    InitializeProcThreadAttributeList(attr_list, 1, 0, &size) &&
	    UpdateProcThreadAttribute(attr_list, 0,
				      PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
				      stdhandles,
				      stdhandles_count * sizeof(HANDLE),
				      NULL, NULL)) {
		si.lpAttributeList = attr_list;
		flags |= EXTENDED_STARTUPINFO_PRESENT;
	}

	ret = CreateProcessW(*wcmd ? wcmd : NULL, wargs, NULL, NULL,
			     stdhandles_count ? TRUE : FALSE,
			     flags, wenvblk, dir ? wdir : NULL,
			     &si.StartupInfo, &pi);

	/*
	 * On Windows 2008 R2, it seems that specifying certain types of handles
	 * (such as FILE_TYPE_CHAR or FILE_TYPE_PIPE) will always produce an
	 * error. Rather than playing finicky and fragile games, let's just try
	 * to detect this situation and simply try again without restricting any
	 * handle inheritance. This is still better than failing to create
	 * processes.
	 */
	if (!ret && restrict_handle_inheritance && stdhandles_count) {
		DWORD err = GetLastError();
		struct strbuf buf = STRBUF_INIT;

		if (err != ERROR_NO_SYSTEM_RESOURCES &&
		    /*
		     * On Windows 7 and earlier, handles on pipes and character
		     * devices are inherited automatically, and cannot be
		     * specified in the thread handle list. Rather than trying
		     * to catch each and every corner case (and running the
		     * chance of *still* forgetting a few), let's just fall
		     * back to creating the process without trying to limit the
		     * handle inheritance.
		     */
		    !(err == ERROR_INVALID_PARAMETER &&
		      GetVersion() >> 16 < 9200) &&
		    !getenv("SUPPRESS_HANDLE_INHERITANCE_WARNING")) {
			DWORD fl = 0;
			int i;

			setenv("SUPPRESS_HANDLE_INHERITANCE_WARNING", "1", 1);

			for (i = 0; i < stdhandles_count; i++) {
				HANDLE h = stdhandles[i];
				strbuf_addf(&buf, "handle #%d: %p (type %lx, "
					    "handle info (%d) %lx\n", i, h,
					    GetFileType(h),
					    GetHandleInformation(h, &fl),
					    fl);
			}
			strbuf_addstr(&buf, "\nThis is a bug; please report it "
				      "at\nhttps://github.com/git-for-windows/"
				      "git/issues/new\n\n"
				      "To suppress this warning, please set "
				      "the environment variable\n\n"
				      "\tSUPPRESS_HANDLE_INHERITANCE_WARNING=1"
				      "\n");
		}
		restrict_handle_inheritance = 0;
		flags &= ~EXTENDED_STARTUPINFO_PRESENT;
		ret = CreateProcessW(*wcmd ? wcmd : NULL, wargs, NULL, NULL,
				     TRUE, flags, wenvblk, dir ? wdir : NULL,
				     &si.StartupInfo, &pi);
		if (!ret)
			errno = err_win_to_posix(GetLastError());
		if (ret && buf.len) {
			warning("failed to restrict file handles (%ld)\n\n%s",
				err, buf.buf);
		}
		strbuf_release(&buf);
	} else if (!ret)
		errno = err_win_to_posix(GetLastError());

	if (si.lpAttributeList)
		DeleteProcThreadAttributeList(si.lpAttributeList);
	if (attr_list)
		HeapFree(GetProcessHeap(), 0, attr_list);

	free(wenvblk);
	free(wargs);

	if (!ret)
		return -1;

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

static pid_t mingw_spawnv(const char *cmd, const char **argv,
			  const char *prepend_cmd)
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
				pid = mingw_spawnve_fd(iprog, argv, deltaenv, dir, interpr,
						       fhin, fhout, fherr);
				free(iprog);
			}
			argv[0] = argv0;
		}
		else
			pid = mingw_spawnve_fd(prog, argv, deltaenv, dir, NULL,
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
		int exec_id;
		int argc = 0;
		char **argv2;
		while (argv[argc]) argc++;
		ALLOC_ARRAY(argv2, argc + 1);
		argv2[0] = (char *)cmd;	/* full path to the script file */
		COPY_ARRAY(&argv2[1], &argv[1], argc);
		exec_id = trace2_exec(prog, (const char **)argv2);
		pid = mingw_spawnv(prog, (const char **)argv2, interpr);
		if (pid >= 0) {
			int status;
			if (waitpid(pid, &status, 0) < 0)
				status = 255;
			trace2_exec_result(exec_id, status);
			exit(status);
		}
		trace2_exec_result(exec_id, -1);
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
		int exec_id;

		exec_id = trace2_exec(cmd, (const char **)argv);
		pid = mingw_spawnv(cmd, (const char **)argv, NULL);
		if (pid < 0) {
			trace2_exec_result(exec_id, -1);
			return -1;
		}
		if (waitpid(pid, &status, 0) < 0)
			status = 255;
		trace2_exec_result(exec_id, status);
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
 * UTF-8 versions of getenv(), putenv() and unsetenv().
 * Internally, they use the CRT's stock UNICODE routines
 * to avoid data loss.
 */
char *mingw_getenv(const char *name)
{
#define GETENV_MAX_RETAIN 64
	static char *values[GETENV_MAX_RETAIN];
	static int value_counter;
	int len_key, len_value;
	wchar_t *w_key;
	char *value;
	wchar_t w_value[32768];

	if (!name || !*name)
		return NULL;

	len_key = strlen(name) + 1;
	/* We cannot use xcalloc() here because that uses getenv() itself */
	w_key = calloc(len_key, sizeof(wchar_t));
	if (!w_key)
		die("Out of memory, (tried to allocate %u wchar_t's)", len_key);
	xutftowcs(w_key, name, len_key);
	/* GetEnvironmentVariableW() only sets the last error upon failure */
	SetLastError(ERROR_SUCCESS);
	len_value = GetEnvironmentVariableW(w_key, w_value, ARRAY_SIZE(w_value));
	if (!len_value && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
		free(w_key);
		return NULL;
	}
	free(w_key);

	len_value = len_value * 3 + 1;
	/* We cannot use xcalloc() here because that uses getenv() itself */
	value = calloc(len_value, sizeof(char));
	if (!value)
		die("Out of memory, (tried to allocate %u bytes)", len_value);
	xwcstoutf(value, w_value, len_value);

	/*
	 * We return `value` which is an allocated value and the caller is NOT
	 * expecting to have to free it, so we keep a round-robin array,
	 * invalidating the buffer after GETENV_MAX_RETAIN getenv() calls.
	 */
	free(values[value_counter]);
	values[value_counter++] = value;
	if (value_counter >= ARRAY_SIZE(values))
		value_counter = 0;

	return value;
}

int mingw_putenv(const char *namevalue)
{
	int size;
	wchar_t *wide, *equal;
	BOOL result;

	if (!namevalue || !*namevalue)
		return 0;

	size = strlen(namevalue) * 2 + 1;
	wide = calloc(size, sizeof(wchar_t));
	if (!wide)
		die("Out of memory, (tried to allocate %u wchar_t's)", size);
	xutftowcs(wide, namevalue, size);
	equal = wcschr(wide, L'=');
	if (!equal)
		result = SetEnvironmentVariableW(wide, NULL);
	else {
		*equal = L'\0';
		result = SetEnvironmentVariableW(wide, equal + 1);
	}
	free(wide);

	if (!result)
		errno = err_win_to_posix(GetLastError());

	return result ? 0 : -1;
}

static void ensure_socket_initialization(void)
{
	WSADATA wsa;
	static int initialized = 0;

	if (initialized)
		return;

	if (WSAStartup(MAKEWORD(2,2), &wsa))
		die("unable to initialize winsock subsystem, error %d",
			WSAGetLastError());

	atexit((void(*)(void)) WSACleanup);
	initialized = 1;
}

static int winsock_error_to_errno(DWORD err)
{
	switch (err) {
	case WSAEINTR: return EINTR;
	case WSAEBADF: return EBADF;
	case WSAEACCES: return EACCES;
	case WSAEFAULT: return EFAULT;
	case WSAEINVAL: return EINVAL;
	case WSAEMFILE: return EMFILE;
	case WSAEWOULDBLOCK: return EWOULDBLOCK;
	case WSAEINPROGRESS: return EINPROGRESS;
	case WSAEALREADY: return EALREADY;
	case WSAENOTSOCK: return ENOTSOCK;
	case WSAEDESTADDRREQ: return EDESTADDRREQ;
	case WSAEMSGSIZE: return EMSGSIZE;
	case WSAEPROTOTYPE: return EPROTOTYPE;
	case WSAENOPROTOOPT: return ENOPROTOOPT;
	case WSAEPROTONOSUPPORT: return EPROTONOSUPPORT;
	case WSAEOPNOTSUPP: return EOPNOTSUPP;
	case WSAEAFNOSUPPORT: return EAFNOSUPPORT;
	case WSAEADDRINUSE: return EADDRINUSE;
	case WSAEADDRNOTAVAIL: return EADDRNOTAVAIL;
	case WSAENETDOWN: return ENETDOWN;
	case WSAENETUNREACH: return ENETUNREACH;
	case WSAENETRESET: return ENETRESET;
	case WSAECONNABORTED: return ECONNABORTED;
	case WSAECONNRESET: return ECONNRESET;
	case WSAENOBUFS: return ENOBUFS;
	case WSAEISCONN: return EISCONN;
	case WSAENOTCONN: return ENOTCONN;
	case WSAETIMEDOUT: return ETIMEDOUT;
	case WSAECONNREFUSED: return ECONNREFUSED;
	case WSAELOOP: return ELOOP;
	case WSAENAMETOOLONG: return ENAMETOOLONG;
	case WSAEHOSTUNREACH: return EHOSTUNREACH;
	case WSAENOTEMPTY: return ENOTEMPTY;
	/* No errno equivalent; default to EIO */
	case WSAESOCKTNOSUPPORT:
	case WSAEPFNOSUPPORT:
	case WSAESHUTDOWN:
	case WSAETOOMANYREFS:
	case WSAEHOSTDOWN:
	case WSAEPROCLIM:
	case WSAEUSERS:
	case WSAEDQUOT:
	case WSAESTALE:
	case WSAEREMOTE:
	case WSASYSNOTREADY:
	case WSAVERNOTSUPPORTED:
	case WSANOTINITIALISED:
	case WSAEDISCON:
	case WSAENOMORE:
	case WSAECANCELLED:
	case WSAEINVALIDPROCTABLE:
	case WSAEINVALIDPROVIDER:
	case WSAEPROVIDERFAILEDINIT:
	case WSASYSCALLFAILURE:
	case WSASERVICE_NOT_FOUND:
	case WSATYPE_NOT_FOUND:
	case WSA_E_NO_MORE:
	case WSA_E_CANCELLED:
	case WSAEREFUSED:
	case WSAHOST_NOT_FOUND:
	case WSATRY_AGAIN:
	case WSANO_RECOVERY:
	case WSANO_DATA:
	case WSA_QOS_RECEIVERS:
	case WSA_QOS_SENDERS:
	case WSA_QOS_NO_SENDERS:
	case WSA_QOS_NO_RECEIVERS:
	case WSA_QOS_REQUEST_CONFIRMED:
	case WSA_QOS_ADMISSION_FAILURE:
	case WSA_QOS_POLICY_FAILURE:
	case WSA_QOS_BAD_STYLE:
	case WSA_QOS_BAD_OBJECT:
	case WSA_QOS_TRAFFIC_CTRL_ERROR:
	case WSA_QOS_GENERIC_ERROR:
	case WSA_QOS_ESERVICETYPE:
	case WSA_QOS_EFLOWSPEC:
	case WSA_QOS_EPROVSPECBUF:
	case WSA_QOS_EFILTERSTYLE:
	case WSA_QOS_EFILTERTYPE:
	case WSA_QOS_EFILTERCOUNT:
	case WSA_QOS_EOBJLENGTH:
	case WSA_QOS_EFLOWCOUNT:
#ifndef _MSC_VER
	case WSA_QOS_EUNKNOWNPSOBJ:
#endif
	case WSA_QOS_EPOLICYOBJ:
	case WSA_QOS_EFLOWDESC:
	case WSA_QOS_EPSFLOWSPEC:
	case WSA_QOS_EPSFILTERSPEC:
	case WSA_QOS_ESDMODEOBJ:
	case WSA_QOS_ESHAPERATEOBJ:
	case WSA_QOS_RESERVED_PETYPE:
	default: return EIO;
	}
}

/*
 * On Windows, `errno` is a global macro to a function call.
 * This makes it difficult to debug and single-step our mappings.
 */
static inline void set_wsa_errno(void)
{
	DWORD wsa = WSAGetLastError();
	int e = winsock_error_to_errno(wsa);
	errno = e;

#ifdef DEBUG_WSA_ERRNO
	fprintf(stderr, "winsock error: %d -> %d\n", wsa, e);
	fflush(stderr);
#endif
}

static inline int winsock_return(int ret)
{
	if (ret < 0)
		set_wsa_errno();

	return ret;
}

#define WINSOCK_RETURN(x) do { return winsock_return(x); } while (0)

#undef gethostname
int mingw_gethostname(char *name, int namelen)
{
	ensure_socket_initialization();
	WINSOCK_RETURN(gethostname(name, namelen));
}

#undef gethostbyname
struct hostent *mingw_gethostbyname(const char *host)
{
	struct hostent *ret;

	ensure_socket_initialization();

	ret = gethostbyname(host);
	if (!ret)
		set_wsa_errno();

	return ret;
}

#undef getaddrinfo
int mingw_getaddrinfo(const char *node, const char *service,
		      const struct addrinfo *hints, struct addrinfo **res)
{
	ensure_socket_initialization();
	WINSOCK_RETURN(getaddrinfo(node, service, hints, res));
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
		set_wsa_errno();
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
	WINSOCK_RETURN(connect(s, sa, sz));
}

#undef bind
int mingw_bind(int sockfd, struct sockaddr *sa, size_t sz)
{
	SOCKET s = (SOCKET)_get_osfhandle(sockfd);
	WINSOCK_RETURN(bind(s, sa, sz));
}

#undef setsockopt
int mingw_setsockopt(int sockfd, int lvl, int optname, void *optval, int optlen)
{
	SOCKET s = (SOCKET)_get_osfhandle(sockfd);
	WINSOCK_RETURN(setsockopt(s, lvl, optname, (const char*)optval, optlen));
}

#undef shutdown
int mingw_shutdown(int sockfd, int how)
{
	SOCKET s = (SOCKET)_get_osfhandle(sockfd);
	WINSOCK_RETURN(shutdown(s, how));
}

#undef listen
int mingw_listen(int sockfd, int backlog)
{
	SOCKET s = (SOCKET)_get_osfhandle(sockfd);
	WINSOCK_RETURN(listen(s, backlog));
}

#undef accept
int mingw_accept(int sockfd1, struct sockaddr *sa, socklen_t *sz)
{
	int sockfd2;

	SOCKET s1 = (SOCKET)_get_osfhandle(sockfd1);
	SOCKET s2 = accept(s1, sa, sz);

	if (s2 == INVALID_SOCKET) {
		set_wsa_errno();
		return -1;
	}

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
	DWORD attrs = INVALID_FILE_ATTRIBUTES, gle;
	int tries = 0;
	wchar_t wpold[MAX_LONG_PATH], wpnew[MAX_LONG_PATH];
	if (xutftowcs_long_path(wpold, pold) < 0 ||
	    xutftowcs_long_path(wpnew, pnew) < 0)
		return -1;

repeat:
	if (MoveFileExW(wpold, wpnew,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
		return 0;
	gle = GetLastError();

	/* revert file attributes on failure */
	if (attrs != INVALID_FILE_ATTRIBUTES)
		SetFileAttributesW(wpnew, attrs);

	if (!is_file_in_use_error(gle)) {
		errno = err_win_to_posix(gle);
		return -1;
	}

	if (attrs == INVALID_FILE_ATTRIBUTES &&
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
		    SetFileAttributesW(wpnew, attrs & ~FILE_ATTRIBUTE_READONLY))
			goto repeat;
	}
	if (retry_ask_yes_no(&tries, "Rename from '%s' to '%s' failed. "
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

/* See https://msdn.microsoft.com/en-us/library/windows/desktop/ms724435.aspx */
enum EXTENDED_NAME_FORMAT {
	NameDisplay = 3,
	NameUserPrincipal = 8
};

static char *get_extended_user_info(enum EXTENDED_NAME_FORMAT type)
{
	DECLARE_PROC_ADDR(secur32.dll, BOOL, SEC_ENTRY, GetUserNameExW,
		enum EXTENDED_NAME_FORMAT, LPCWSTR, PULONG);
	static wchar_t wbuffer[1024];
	DWORD len;

	if (!INIT_PROC_ADDR(GetUserNameExW))
		return NULL;

	len = ARRAY_SIZE(wbuffer);
	if (GetUserNameExW(type, wbuffer, &len)) {
		char *converted = xmalloc((len *= 3));
		if (xwcstoutf(converted, wbuffer, len) >= 0)
			return converted;
		free(converted);
	}

	return NULL;
}

char *mingw_query_user_email(void)
{
	return get_extended_user_info(NameUserPrincipal);
}

struct passwd *getpwuid(int uid)
{
	static unsigned initialized;
	static char user_name[100];
	static struct passwd *p;
	wchar_t buf[100];
	DWORD len;

	if (initialized)
		return p;

	len = ARRAY_SIZE(buf);
	if (!GetUserNameW(buf, &len)) {
		initialized = 1;
		return NULL;
	}

	if (xwcstoutf(user_name, buf, sizeof(user_name)) < 0) {
		initialized = 1;
		return NULL;
	}

	p = xmalloc(sizeof(*p));
	p->pw_name = user_name;
	p->pw_gecos = get_extended_user_info(NameDisplay);
	if (!p->pw_gecos)
		p->pw_gecos = "unknown";
	p->pw_dir = NULL;

	initialized = 1;
	return p;
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
		int rc = WaitForSingleObject(timer_thread, 10000);
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

	if (out)
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
	if (out)
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

#if defined(_MSC_VER)
	case SIGILL:
	case SIGFPE:
	case SIGSEGV:
	case SIGTERM:
	case SIGBREAK:
	case SIGABRT:
	case SIGABRT_COMPAT:
		/*
		 * The <signal.h> header in the MS C Runtime defines 8 signals
		 * as being supported on the platform. Anything else causes an
		 * "Invalid signal or error" (which in DEBUG builds causes the
		 * Abort/Retry/Ignore dialog). We by-pass the CRT for things we
		 * already know will fail.
		 */
		return raise(sig);
	default:
		errno = EINVAL;
		return -1;

#else

	default:
		return raise(sig);

#endif

	}
}

int link(const char *oldpath, const char *newpath)
{
	wchar_t woldpath[MAX_LONG_PATH], wnewpath[MAX_LONG_PATH];
	if (xutftowcs_long_path(woldpath, oldpath) < 0 ||
	    xutftowcs_long_path(wnewpath, newpath) < 0)
		return -1;

	if (!CreateHardLinkW(wnewpath, woldpath, NULL)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	return 0;
}

enum symlink_type {
	SYMLINK_TYPE_UNSPECIFIED = 0,
	SYMLINK_TYPE_FILE,
	SYMLINK_TYPE_DIRECTORY,
};

static enum symlink_type check_symlink_attr(struct index_state *index, const char *link)
{
	static struct attr_check *check;
	const char *value;

	if (!index)
		return SYMLINK_TYPE_UNSPECIFIED;

	if (!check)
		check = attr_check_initl("symlink", NULL);

	git_check_attr(index, link, check);

	value = check->items[0].value;
	if (ATTR_UNSET(value))
		return SYMLINK_TYPE_UNSPECIFIED;
	if (!strcmp(value, "file"))
		return SYMLINK_TYPE_FILE;
	if (!strcmp(value, "dir") || !strcmp(value, "directory"))
		return SYMLINK_TYPE_DIRECTORY;

	warning(_("ignoring invalid symlink type '%s' for '%s'"), value, link);
	return SYMLINK_TYPE_UNSPECIFIED;
}

int mingw_create_symlink(struct index_state *index, const char *target, const char *link)
{
	wchar_t wtarget[MAX_LONG_PATH], wlink[MAX_LONG_PATH];
	int len;

	/* fail if symlinks are disabled or API is not supported (WinXP) */
	if (!has_symlinks) {
		errno = ENOSYS;
		return -1;
	}

	if ((len = xutftowcs_long_path(wtarget, target)) < 0
			|| xutftowcs_long_path(wlink, link) < 0)
		return -1;

	/* convert target dir separators to backslashes */
	while (len--)
		if (wtarget[len] == '/')
			wtarget[len] = '\\';

	switch (check_symlink_attr(index, link)) {
	case SYMLINK_TYPE_UNSPECIFIED:
		/* Create a phantom symlink: it is initially created as a file
		 * symlink, but may change to a directory symlink later if/when
		 * the target exists. */
		return create_phantom_symlink(wtarget, wlink);
	case SYMLINK_TYPE_FILE:
		if (!CreateSymbolicLinkW(wlink, wtarget, symlink_file_flags))
			break;
		return 0;
	case SYMLINK_TYPE_DIRECTORY:
		if (!CreateSymbolicLinkW(wlink, wtarget,
					 symlink_directory_flags))
			break;
		/* There may be dangling phantom symlinks that point at this
		 * one, which should now morph into directory symlinks. */
		process_phantom_symlinks();
		return 0;
	default:
		BUG("unhandled symlink type");
	}

	/* CreateSymbolicLinkW failed. */
	errno = err_win_to_posix(GetLastError());
	return -1;
}

#ifndef _WINNT_H
/*
 * The REPARSE_DATA_BUFFER structure is defined in the Windows DDK (in
 * ntifs.h) and in MSYS1's winnt.h (which defines _WINNT_H). So define
 * it ourselves if we are on MSYS2 (whose winnt.h defines _WINNT_).
 */
typedef struct _REPARSE_DATA_BUFFER {
	DWORD  ReparseTag;
	WORD   ReparseDataLength;
	WORD   Reserved;
#ifndef _MSC_VER
	_ANONYMOUS_UNION
#endif
	union {
		struct {
			WORD   SubstituteNameOffset;
			WORD   SubstituteNameLength;
			WORD   PrintNameOffset;
			WORD   PrintNameLength;
			ULONG  Flags;
			WCHAR PathBuffer[1];
		} SymbolicLinkReparseBuffer;
		struct {
			WORD   SubstituteNameOffset;
			WORD   SubstituteNameLength;
			WORD   PrintNameOffset;
			WORD   PrintNameLength;
			WCHAR PathBuffer[1];
		} MountPointReparseBuffer;
		struct {
			BYTE   DataBuffer[1];
		} GenericReparseBuffer;
	} DUMMYUNIONNAME;
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#endif

static int readlink_1(const WCHAR *wpath, BOOL fail_on_unknown_tag,
		      char *tmpbuf, int *plen, DWORD *ptag)
{
	HANDLE handle;
	WCHAR *wbuf;
	REPARSE_DATA_BUFFER *b = alloca(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
	DWORD dummy;

	/* read reparse point data */
	handle = CreateFileW(wpath, 0,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0, b,
			MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &dummy, NULL)) {
		errno = err_win_to_posix(GetLastError());
		CloseHandle(handle);
		return -1;
	}
	CloseHandle(handle);

	/* get target path for symlinks or mount points (aka 'junctions') */
	switch ((*ptag = b->ReparseTag)) {
	case IO_REPARSE_TAG_SYMLINK:
		wbuf = (WCHAR*) (((char*) b->SymbolicLinkReparseBuffer.PathBuffer)
				+ b->SymbolicLinkReparseBuffer.SubstituteNameOffset);
		*(WCHAR*) (((char*) wbuf)
				+ b->SymbolicLinkReparseBuffer.SubstituteNameLength) = 0;
		break;
	case IO_REPARSE_TAG_MOUNT_POINT:
		wbuf = (WCHAR*) (((char*) b->MountPointReparseBuffer.PathBuffer)
				+ b->MountPointReparseBuffer.SubstituteNameOffset);
		*(WCHAR*) (((char*) wbuf)
				+ b->MountPointReparseBuffer.SubstituteNameLength) = 0;
		break;
	default:
		if (fail_on_unknown_tag) {
			errno = EINVAL;
			return -1;
		} else {
			*plen = MAX_LONG_PATH;
			return 0;
		}
	}

	if ((*plen =
	     xwcstoutf(tmpbuf, normalize_ntpath(wbuf), MAX_LONG_PATH)) <  0)
		return -1;
	return 0;
}

int readlink(const char *path, char *buf, size_t bufsiz)
{
	WCHAR wpath[MAX_LONG_PATH];
	char tmpbuf[MAX_LONG_PATH];
	int len;
	DWORD tag;

	if (xutftowcs_long_path(wpath, path) < 0)
		return -1;

	if (readlink_1(wpath, TRUE, tmpbuf, &len, &tag) < 0)
		return -1;

	/*
	 * Adapt to strange readlink() API: Copy up to bufsiz *bytes*, potentially
	 * cutting off a UTF-8 sequence. Insufficient bufsize is *not* a failure
	 * condition. There is no conversion function that produces invalid UTF-8,
	 * so convert to a (hopefully large enough) temporary buffer, then memcpy
	 * the requested number of bytes (including '\0' for robustness).
	 */
	memcpy(buf, tmpbuf, min(bufsiz, len + 1));
	return min(bufsiz, len);
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

int (*win32_is_mount_point)(struct strbuf *path) = mingw_is_mount_point;

int mingw_is_mount_point(struct strbuf *path)
{
	WIN32_FIND_DATAW findbuf = { 0 };
	HANDLE handle;
	wchar_t wfilename[MAX_LONG_PATH];
	int wlen = xutftowcs_long_path(wfilename, path->buf);
	if (wlen < 0)
		die(_("could not get long path for '%s'"), path->buf);

	/* remove trailing slash, if any */
	if (wlen > 0 && wfilename[wlen - 1] == L'/')
		wfilename[--wlen] = L'\0';

	handle = FindFirstFileW(wfilename, &findbuf);
	if (handle == INVALID_HANDLE_VALUE)
		return 0;
	FindClose(handle);

	return (findbuf.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
		(findbuf.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
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

#ifdef ENSURE_MSYSTEM_IS_SET
static size_t append_system_bin_dirs(char *path, size_t size)
{
#if !defined(RUNTIME_PREFIX) || !defined(HAVE_WPGMPTR)
	return 0;
#else
	char prefix[32768];
	const char *slash;
	size_t len = xwcstoutf(prefix, _wpgmptr, sizeof(prefix)), off = 0;

	if (len == 0 || len >= sizeof(prefix) ||
	    !(slash = find_last_dir_sep(prefix)))
		return 0;
	/* strip trailing `git.exe` */
	len = slash - prefix;

	/* strip trailing `cmd` or `mingw64\bin` or `mingw32\bin` or `bin` or `libexec\git-core` */
	if (strip_suffix_mem(prefix, &len, "\\mingw64\\libexec\\git-core") ||
	    strip_suffix_mem(prefix, &len, "\\mingw64\\bin"))
		off += xsnprintf(path + off, size - off,
				 "%.*s\\mingw64\\bin;", (int)len, prefix);
	else if (strip_suffix_mem(prefix, &len, "\\mingw32\\libexec\\git-core") ||
		 strip_suffix_mem(prefix, &len, "\\mingw32\\bin"))
		off += xsnprintf(path + off, size - off,
				 "%.*s\\mingw32\\bin;", (int)len, prefix);
	else if (strip_suffix_mem(prefix, &len, "\\cmd") ||
		 strip_suffix_mem(prefix, &len, "\\bin") ||
		 strip_suffix_mem(prefix, &len, "\\libexec\\git-core"))
		off += xsnprintf(path + off, size - off,
				 "%.*s\\mingw%d\\bin;", (int)len, prefix,
				 (int)(sizeof(void *) * 8));
	else
		return 0;

	off += xsnprintf(path + off, size - off,
			 "%.*s\\usr\\bin;", (int)len, prefix);
	return off;
#endif
}
#endif

static int is_system32_path(const char *path)
{
	WCHAR system32[MAX_LONG_PATH], wpath[MAX_LONG_PATH];

	if (xutftowcs_long_path(wpath, path) < 0 ||
	    !GetSystemDirectoryW(system32, ARRAY_SIZE(system32)) ||
	    _wcsicmp(system32, wpath))
		return 0;

	return 1;
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


	/*
	 * Make sure TERM is set up correctly to enable auto-color
	 * (see color.c .) Use "cygwin" for older OS releases which
	 * works correctly with MSYS2 utilities on older consoles.
	 */
	if (!getenv("TERM")) {
		if ((GetVersion() >> 16) < 15063)
			setenv("TERM", "cygwin", 0);
		else {
			setenv("TERM", "xterm-256color", 0);
			setenv("COLORTERM", "truecolor", 0);
		}
	}

	/* calculate HOME if not set */
	if (!getenv("HOME")) {
		/*
		 * try $HOMEDRIVE$HOMEPATH - the home share may be a network
		 * location, thus also check if the path exists (i.e. is not
		 * disconnected)
		 */
		if ((tmp = getenv("HOMEDRIVE"))) {
			struct strbuf buf = STRBUF_INIT;
			strbuf_addstr(&buf, tmp);
			if ((tmp = getenv("HOMEPATH"))) {
				strbuf_addstr(&buf, tmp);
				if (!is_system32_path(buf.buf) &&
				    is_directory(buf.buf))
					setenv("HOME", buf.buf, 1);
				else
					tmp = NULL; /* use $USERPROFILE */
			}
			strbuf_release(&buf);
		}
		/* use $USERPROFILE if the home share is not available */
		if (!tmp && (tmp = getenv("USERPROFILE")))
			setenv("HOME", tmp, 1);
	}

	if (!getenv("PLINK_PROTOCOL"))
		setenv("PLINK_PROTOCOL", "ssh", 0);

#ifdef ENSURE_MSYSTEM_IS_SET
	if (!(tmp = getenv("MSYSTEM")) || !tmp[0]) {
		const char *home = getenv("HOME"), *path = getenv("PATH");
		char buf[32768];
		size_t off = 0;

		xsnprintf(buf, sizeof(buf),
			  "MINGW%d", (int)(sizeof(void *) * 8));
		setenv("MSYSTEM", buf, 1);

		if (home)
			off += xsnprintf(buf + off, sizeof(buf) - off,
					 "%s\\bin;", home);
		off += append_system_bin_dirs(buf + off, sizeof(buf) - off);
		if (path)
			off += xsnprintf(buf + off, sizeof(buf) - off,
					 "%s", path);
		else if (off > 0)
			buf[off - 1] = '\0';
		else
			buf[0] = '\0';
		setenv("PATH", buf, 1);
	}
#endif

	if (!getenv("LC_ALL") && !getenv("LC_CTYPE") && !getenv("LANG"))
		setenv("LC_CTYPE", "C.UTF-8", 1);

	/*
	 * Change 'core.symlinks' default to false, unless native symlinks are
	 * enabled in MSys2 (via 'MSYS=winsymlinks:nativestrict'). Thus we can
	 * run the test suite (which doesn't obey config files) with or without
	 * symlink support.
	 */
	if (!(tmp = getenv("MSYS")) || !strstr(tmp, "winsymlinks:nativestrict"))
		has_symlinks = 0;
}

static PSID get_current_user_sid(void)
{
	HANDLE token;
	DWORD len = 0;
	PSID result = NULL;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return NULL;

	if (!GetTokenInformation(token, TokenUser, NULL, 0, &len)) {
		TOKEN_USER *info = xmalloc((size_t)len);
		if (GetTokenInformation(token, TokenUser, info, len, &len)) {
			len = GetLengthSid(info->User.Sid);
			result = xmalloc(len);
			if (!CopySid(len, result, info->User.Sid)) {
				error(_("failed to copy SID (%ld)"),
				      GetLastError());
				FREE_AND_NULL(result);
			}
		}
		FREE_AND_NULL(info);
	}
	CloseHandle(token);

	return result;
}

static BOOL user_sid_to_user_name(PSID sid, LPSTR *str)
{
	SID_NAME_USE pe_use;
	DWORD len_user = 0, len_domain = 0;
	BOOL translate_sid_to_user;

	/*
	 * returns only FALSE, because the string pointers are NULL
	 */
	LookupAccountSidA(NULL, sid, NULL, &len_user, NULL, &len_domain,
			  &pe_use);
	/*
	 * Alloc needed space of the strings
	 */
	ALLOC_ARRAY((*str), (size_t)len_domain + (size_t)len_user);
	translate_sid_to_user = LookupAccountSidA(NULL, sid,
	    (*str) + len_domain, &len_user, *str, &len_domain, &pe_use);
	if (!translate_sid_to_user)
		FREE_AND_NULL(*str);
	else
		(*str)[len_domain] = '/';
	return translate_sid_to_user;
}

static int acls_supported(const char *path)
{
	size_t offset = offset_1st_component(path);
	WCHAR wroot[MAX_PATH];
	DWORD file_system_flags;

	if (offset &&
	    xutftowcsn(wroot, path, MAX_PATH, offset) > 0 &&
	    GetVolumeInformationW(wroot, NULL, 0, NULL, NULL,
				  &file_system_flags, NULL, 0))
		return !!(file_system_flags & FILE_PERSISTENT_ACLS);

	return 0;
}

int is_path_owned_by_current_sid(const char *path, struct strbuf *report)
{
	WCHAR wpath[MAX_PATH];
	PSID sid = NULL;
	PSECURITY_DESCRIPTOR descriptor = NULL;
	DWORD err;

	static wchar_t home[MAX_PATH];

	int result = 0;

	if (xutftowcs_path(wpath, path) < 0)
		return 0;

	/*
	 * On Windows, the home directory is owned by the administrator, but for
	 * all practical purposes, it belongs to the user. Do pretend that it is
	 * owned by the user.
	 */
	if (!*home) {
		DWORD size = ARRAY_SIZE(home);
		DWORD len = GetEnvironmentVariableW(L"HOME", home, size);
		if (!len || len > size)
			wcscpy(home, L"::N/A::");
	}
	if (!wcsicmp(wpath, home))
		return 1;

	/* Get the owner SID */
	err = GetNamedSecurityInfoW(wpath, SE_FILE_OBJECT,
				    OWNER_SECURITY_INFORMATION |
				    DACL_SECURITY_INFORMATION,
				    &sid, NULL, NULL, NULL, &descriptor);

	if (err == ERROR_SUCCESS && sid && IsValidSid(sid)) {
		/* Now, verify that the SID matches the current user's */
		static PSID current_user_sid;
		BOOL is_member;

		if (!current_user_sid)
			current_user_sid = get_current_user_sid();

		if (current_user_sid &&
		    IsValidSid(current_user_sid) &&
		    EqualSid(sid, current_user_sid))
			result = 1;
		else if (IsWellKnownSid(sid, WinBuiltinAdministratorsSid) &&
			 CheckTokenMembership(NULL, sid, &is_member) &&
			 is_member)
			/*
			 * If owned by the Administrators group, and the
			 * current user is an administrator, we consider that
			 * okay, too.
			 */
			result = 1;
		else if (report &&
			 IsWellKnownSid(sid, WinWorldSid) &&
			 !acls_supported(path)) {
			/*
			 * On FAT32 volumes, ownership is not actually recorded.
			 */
			strbuf_addf(report, "'%s' is on a file system that does "
				    "not record ownership\n", path);
		} else if (report) {
			LPSTR str1, str2, str3, str4, to_free1 = NULL,
			    to_free3 = NULL, to_local_free2 = NULL,
			    to_local_free4 = NULL;

			if (user_sid_to_user_name(sid, &str1))
				to_free1 = str1;
			else
				str1 = "(inconvertible)";
			if (ConvertSidToStringSidA(sid, &str2))
				to_local_free2 = str2;
			else
				str2 = "(inconvertible)";

			if (!current_user_sid) {
				str3 = "(none)";
				str4 = "(none)";
			}
			else if (!IsValidSid(current_user_sid)) {
				str3 = "(invalid)";
				str4 = "(invalid)";
			} else {
				if (user_sid_to_user_name(current_user_sid,
							  &str3))
					to_free3 = str3;
				else
					str3 = "(inconvertible)";
				if (ConvertSidToStringSidA(current_user_sid,
							   &str4))
					to_local_free4 = str4;
				else
					str4 = "(inconvertible)";
			}
			strbuf_addf(report,
				    "'%s' is owned by:\n"
				    "\t%s (%s)\nbut the current user is:\n"
				    "\t%s (%s)\n",
				    path, str1, str2, str3, str4);
			free(to_free1);
			LocalFree(to_local_free2);
			free(to_free3);
			LocalFree(to_local_free4);
		}
	}

	/*
	 * We can release the security descriptor struct only now because `sid`
	 * actually points into this struct.
	 */
	if (descriptor)
		LocalFree(descriptor);

	return result;
}

int is_valid_win32_path(const char *path, int allow_literal_nul)
{
	const char *p = path;
	int preceding_space_or_period = 0, i = 0, periods = 0;

	if (!protect_ntfs)
		return 1;

	skip_dos_drive_prefix((char **)&path);
	goto segment_start;

	for (;;) {
		char c = *(path++);
		switch (c) {
		case '\0':
		case '/': case '\\':
			/* cannot end in ` ` or `.`, except for `.` and `..` */
			if (preceding_space_or_period &&
			    (i != periods || periods > 2))
				return 0;
			if (!c)
				return 1;

			i = periods = preceding_space_or_period = 0;

segment_start:
			switch (*path) {
			case 'a': case 'A': /* AUX */
				if (((c = path[++i]) != 'u' && c != 'U') ||
				    ((c = path[++i]) != 'x' && c != 'X')) {
not_a_reserved_name:
					path += i;
					continue;
				}
				break;
			case 'c': case 'C':
				/* COM1 ... COM9, CON, CONIN$, CONOUT$ */
				if ((c = path[++i]) != 'o' && c != 'O')
					goto not_a_reserved_name;
				c = path[++i];
				if (c == 'm' || c == 'M') { /* COM1 ... COM9 */
					c = path[++i];
					if (c < '1' || c > '9')
						goto not_a_reserved_name;
				} else if (c == 'n' || c == 'N') { /* CON */
					c = path[i + 1];
					if ((c == 'i' || c == 'I') &&
					    ((c = path[i + 2]) == 'n' ||
					     c == 'N') &&
					    path[i + 3] == '$')
						i += 3; /* CONIN$ */
					else if ((c == 'o' || c == 'O') &&
						 ((c = path[i + 2]) == 'u' ||
						  c == 'U') &&
						 ((c = path[i + 3]) == 't' ||
						  c == 'T') &&
						 path[i + 4] == '$')
						i += 4; /* CONOUT$ */
				} else
					goto not_a_reserved_name;
				break;
			case 'l': case 'L': /* LPT<N> */
				if (((c = path[++i]) != 'p' && c != 'P') ||
				    ((c = path[++i]) != 't' && c != 'T') ||
				    !isdigit(path[++i]))
					goto not_a_reserved_name;
				break;
			case 'n': case 'N': /* NUL */
				if (((c = path[++i]) != 'u' && c != 'U') ||
				    ((c = path[++i]) != 'l' && c != 'L') ||
				    (allow_literal_nul &&
				     !path[i + 1] && p == path))
					goto not_a_reserved_name;
				break;
			case 'p': case 'P': /* PRN */
				if (((c = path[++i]) != 'r' && c != 'R') ||
				    ((c = path[++i]) != 'n' && c != 'N'))
					goto not_a_reserved_name;
				break;
			default:
				continue;
			}

			/*
			 * So far, this looks like a reserved name. Let's see
			 * whether it actually is one: trailing spaces, a file
			 * extension, or an NTFS Alternate Data Stream do not
			 * matter, the name is still reserved if any of those
			 * follow immediately after the actual name.
			 */
			i++;
			if (path[i] == ' ') {
				preceding_space_or_period = 1;
				while (path[++i] == ' ')
					; /* skip all spaces */
			}

			c = path[i];
			if (c && c != '.' && c != ':' && !is_xplatform_dir_sep(c))
				goto not_a_reserved_name;

			/* contains reserved name */
			return 0;
		case '.':
			periods++;
			/* fallthru */
		case ' ':
			preceding_space_or_period = 1;
			i++;
			continue;
		case ':': /* DOS drive prefix was already skipped */
		case '<': case '>': case '"': case '|': case '?': case '*':
			/* illegal character */
			return 0;
		default:
			if (c > '\0' && c < '\x20')
				/* illegal character */
				return 0;
		}
		preceding_space_or_period = 0;
		i++;
	}
}

int handle_long_path(wchar_t *path, int len, int max_path, int expand)
{
	int result;
	wchar_t buf[MAX_LONG_PATH];

	/*
	 * we don't need special handling if path is relative to the current
	 * directory, and current directory + path don't exceed the desired
	 * max_path limit. This should cover > 99 % of cases with minimal
	 * performance impact (git almost always uses relative paths).
	 */
	if ((len < 2 || (!is_dir_sep(path[0]) && path[1] != ':')) &&
	    (current_directory_len + len < max_path))
		return len;

	/*
	 * handle everything else:
	 * - absolute paths: "C:\dir\file"
	 * - absolute UNC paths: "\\server\share\dir\file"
	 * - absolute paths on current drive: "\dir\file"
	 * - relative paths on other drive: "X:file"
	 * - prefixed paths: "\\?\...", "\\.\..."
	 */

	/* convert to absolute path using GetFullPathNameW */
	result = GetFullPathNameW(path, MAX_LONG_PATH, buf, NULL);
	if (!result) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}

	/*
	 * return absolute path if it fits within max_path (even if
	 * "cwd + path" doesn't due to '..' components)
	 */
	if (result < max_path) {
		/* Be careful not to add a drive prefix if there was none */
		if (is_wdir_sep(path[0]) &&
		    !is_wdir_sep(buf[0]) && buf[1] == L':' && is_wdir_sep(buf[2]))
			wcscpy(path, buf + 2);
		else
			wcscpy(path, buf);
		return result;
	}

	/* error out if we shouldn't expand the path or buf is too small */
	if (!expand || result >= MAX_LONG_PATH - 6) {
		errno = ENAMETOOLONG;
		return -1;
	}

	/* prefix full path with "\\?\" or "\\?\UNC\" */
	if (buf[0] == '\\') {
		/* ...unless already prefixed */
		if (buf[1] == '\\' && (buf[2] == '?' || buf[2] == '.'))
			return len;

		wcscpy(path, L"\\\\?\\UNC\\");
		wcscpy(path + 8, buf + 2);
		return result + 6;
	} else {
		wcscpy(path, L"\\\\?\\");
		wcscpy(path + 4, buf);
		return result + 4;
	}
}

#if !defined(_MSC_VER)
/*
 * Disable MSVCRT command line wildcard expansion (__getmainargs called from
 * mingw startup code, see init.c in mingw runtime).
 */
int _CRT_glob = 0;
#endif

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

static void maybe_redirect_std_handle(const wchar_t *key, DWORD std_id, int fd,
				      DWORD desired_access, DWORD flags)
{
	DWORD create_flag = fd ? OPEN_ALWAYS : OPEN_EXISTING;
	wchar_t buf[MAX_PATH];
	DWORD max = ARRAY_SIZE(buf);
	HANDLE handle;
	DWORD ret = GetEnvironmentVariableW(key, buf, max);

	if (!ret || ret >= max)
		return;

	/* make sure this does not leak into child processes */
	SetEnvironmentVariableW(key, NULL);
	if (!wcscmp(buf, L"off")) {
		close(fd);
		handle = GetStdHandle(std_id);
		if (handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
		return;
	}
	if (std_id == STD_ERROR_HANDLE && !wcscmp(buf, L"2>&1")) {
		handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (handle == INVALID_HANDLE_VALUE) {
			close(fd);
			handle = GetStdHandle(std_id);
			if (handle != INVALID_HANDLE_VALUE)
				CloseHandle(handle);
		} else {
			int new_fd = _open_osfhandle((intptr_t)handle, O_BINARY);
			SetStdHandle(std_id, handle);
			dup2(new_fd, fd);
			/* do *not* close the new_fd: that would close stdout */
		}
		return;
	}
	handle = CreateFileW(buf, desired_access, 0, NULL, create_flag,
			     flags, NULL);
	if (handle != INVALID_HANDLE_VALUE) {
		int new_fd = _open_osfhandle((intptr_t)handle, O_BINARY);
		SetStdHandle(std_id, handle);
		dup2(new_fd, fd);
		close(new_fd);
	}
}

static void maybe_redirect_std_handles(void)
{
	maybe_redirect_std_handle(L"GIT_REDIRECT_STDIN", STD_INPUT_HANDLE, 0,
				  GENERIC_READ, FILE_ATTRIBUTE_NORMAL);
	maybe_redirect_std_handle(L"GIT_REDIRECT_STDOUT", STD_OUTPUT_HANDLE, 1,
				  GENERIC_WRITE, FILE_ATTRIBUTE_NORMAL);
	maybe_redirect_std_handle(L"GIT_REDIRECT_STDERR", STD_ERROR_HANDLE, 2,
				  GENERIC_WRITE, FILE_FLAG_NO_BUFFERING);
}

static void adjust_symlink_flags(void)
{
	/*
	 * Starting with Windows 10 Build 14972, symbolic links can be created
	 * using CreateSymbolicLink() without elevation by passing the flag
	 * SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE (0x02) as last
	 * parameter, provided the Developer Mode has been enabled. Some
	 * earlier Windows versions complain about this flag with an
	 * ERROR_INVALID_PARAMETER, hence we have to test the build number
	 * specifically.
	 */
	if (GetVersion() >= 14972 << 16) {
		symlink_file_flags |= 2;
		symlink_directory_flags |= 2;
	}

}

#ifdef _MSC_VER
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#endif

/*
 * We implement wmain() and compile with -municode, which would
 * normally ignore main(), but we call the latter from the former
 * so that we can handle non-ASCII command-line parameters
 * appropriately.
 *
 * To be more compatible with the core git code, we convert
 * argv into UTF8 and pass them directly to main().
 */
int wmain(int argc, const wchar_t **wargv)
{
	int i, maxlen, exit_status;
	char *buffer, **save;
	const char **argv;

	trace2_initialize_clock();

#ifdef _MSC_VER
#ifdef _DEBUG
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif

#ifdef USE_MSVC_CRTDBG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
#endif

	maybe_redirect_std_handles();
	adjust_symlink_flags();
	fsync_object_files = 1;

	/* determine size of argv and environ conversion buffer */
	maxlen = wcslen(wargv[0]);
	for (i = 1; i < argc; i++)
		maxlen = max(maxlen, wcslen(wargv[i]));

	/* allocate buffer (wchar_t encodes to max 3 UTF-8 bytes) */
	maxlen = 3 * maxlen + 1;
	buffer = malloc_startup(maxlen);

	/*
	 * Create a UTF-8 version of w_argv. Also create a "save" copy
	 * to remember all the string pointers because parse_options()
	 * will remove claimed items from the argv that we pass down.
	 */
	ALLOC_ARRAY(argv, argc + 1);
	ALLOC_ARRAY(save, argc + 1);
	for (i = 0; i < argc; i++)
		argv[i] = save[i] = wcstoutfdup_startup(buffer, wargv[i], maxlen);
	argv[i] = save[i] = NULL;
	free(buffer);

	/* fix Windows specific environment settings */
	setup_windows_environment();

	unset_environment_variables = xstrdup("PERL5LIB");

	/* initialize critical section for waitpid pinfo_t list */
	InitializeCriticalSection(&pinfo_cs);
	InitializeCriticalSection(&phantom_symlinks_cs);

	/* initialize critical section for fscache */
	InitializeCriticalSection(&fscache_cs);

	/* set up default file mode and file modes for stdin/out/err */
	_fmode = _O_BINARY;
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
	_setmode(_fileno(stderr), _O_BINARY);

	/* initialize Unicode console */
	winansi_init();

	/* init length of current directory for handle_long_path */
	current_directory_len = GetCurrentDirectoryW(0, NULL);

	/* invoke the real main() using our utf8 version of argv. */
	exit_status = main(argc, argv);

	for (i = 0; i < argc; i++)
		free(save[i]);
	free(save);
	free(argv);

	return exit_status;
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

int mingw_have_unix_sockets(void)
{
	SC_HANDLE scm, srvc;
	SERVICE_STATUS_PROCESS status;
	DWORD bytes;
	int ret = 0;
	scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
	if (scm) {
		srvc = OpenServiceA(scm, "afunix", SERVICE_QUERY_STATUS);
		if (srvc) {
			if(QueryServiceStatusEx(srvc, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(SERVICE_STATUS_PROCESS), &bytes))
				ret = status.dwCurrentState == SERVICE_RUNNING;
			CloseServiceHandle(srvc);
		}
		CloseServiceHandle(scm);
	}
	return ret;
}
