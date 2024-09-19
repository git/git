/*
 * By default, use a read/write loop to copy files on POSIX systems.
 * On Linux, use sendfile by default as it's slightly faster.  On
 * macOS, we avoid fcopyfile by default because it's slightly slower.
 */
#undef USE_FCOPYFILE
#define USE_SENDFILE 1

#ifdef _WIN32

#ifdef CLAR_WIN32_LONGPATHS
# define CLAR_MAX_PATH 4096
#else
# define CLAR_MAX_PATH MAX_PATH
#endif

#define RM_RETRY_COUNT	5
#define RM_RETRY_DELAY	10

#ifdef __MINGW32__

/* These security-enhanced functions are not available
 * in MinGW, so just use the vanilla ones */
#define wcscpy_s(a, b, c) wcscpy((a), (c))
#define wcscat_s(a, b, c) wcscat((a), (c))

#endif /* __MINGW32__ */

static int
fs__dotordotdot(WCHAR *_tocheck)
{
	return _tocheck[0] == '.' &&
		(_tocheck[1] == '\0' ||
		 (_tocheck[1] == '.' && _tocheck[2] == '\0'));
}

static int
fs_rmdir_rmdir(WCHAR *_wpath)
{
	unsigned retries = 1;

	while (!RemoveDirectoryW(_wpath)) {
		/* Only retry when we have retries remaining, and the
		 * error was ERROR_DIR_NOT_EMPTY. */
		if (retries++ > RM_RETRY_COUNT ||
			ERROR_DIR_NOT_EMPTY != GetLastError())
			return -1;

		/* Give whatever has a handle to a child item some time
		 * to release it before trying again */
		Sleep(RM_RETRY_DELAY * retries * retries);
	}

	return 0;
}

static void translate_path(WCHAR *path, size_t path_size)
{
    size_t path_len, i;

    if (wcsncmp(path, L"\\\\?\\", 4) == 0)
	return;

    path_len = wcslen(path);
    cl_assert(path_size > path_len + 4);

    for (i = path_len; i > 0; i--) {
	WCHAR c = path[i - 1];

	if (c == L'/')
	    path[i + 3] = L'\\';
	else
	    path[i + 3] = path[i - 1];
    }

    path[0] = L'\\';
    path[1] = L'\\';
    path[2] = L'?';
    path[3] = L'\\';
    path[path_len + 4] = L'\0';
}

static void
fs_rmdir_helper(WCHAR *_wsource)
{
	WCHAR buffer[CLAR_MAX_PATH];
	HANDLE find_handle;
	WIN32_FIND_DATAW find_data;
	size_t buffer_prefix_len;

	/* Set up the buffer and capture the length */
	wcscpy_s(buffer, CLAR_MAX_PATH, _wsource);
	translate_path(buffer, CLAR_MAX_PATH);
	wcscat_s(buffer, CLAR_MAX_PATH, L"\\");
	buffer_prefix_len = wcslen(buffer);

	/* FindFirstFile needs a wildcard to match multiple items */
	wcscat_s(buffer, CLAR_MAX_PATH, L"*");
	find_handle = FindFirstFileW(buffer, &find_data);
	cl_assert(INVALID_HANDLE_VALUE != find_handle);

	do {
		/* FindFirstFile/FindNextFile gives back . and ..
		 * entries at the beginning */
		if (fs__dotordotdot(find_data.cFileName))
			continue;

		wcscpy_s(buffer + buffer_prefix_len, CLAR_MAX_PATH - buffer_prefix_len, find_data.cFileName);

		if (FILE_ATTRIBUTE_DIRECTORY & find_data.dwFileAttributes)
			fs_rmdir_helper(buffer);
		else {
			/* If set, the +R bit must be cleared before deleting */
			if (FILE_ATTRIBUTE_READONLY & find_data.dwFileAttributes)
				cl_assert(SetFileAttributesW(buffer, find_data.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY));

			cl_assert(DeleteFileW(buffer));
		}
	}
	while (FindNextFileW(find_handle, &find_data));

	/* Ensure that we successfully completed the enumeration */
	cl_assert(ERROR_NO_MORE_FILES == GetLastError());

	/* Close the find handle */
	FindClose(find_handle);

	/* Now that the directory is empty, remove it */
	cl_assert(0 == fs_rmdir_rmdir(_wsource));
}

static int
fs_rm_wait(WCHAR *_wpath)
{
	unsigned retries = 1;
	DWORD last_error;

	do {
		if (INVALID_FILE_ATTRIBUTES == GetFileAttributesW(_wpath))
			last_error = GetLastError();
		else
			last_error = ERROR_SUCCESS;

		/* Is the item gone? */
		if (ERROR_FILE_NOT_FOUND == last_error ||
			ERROR_PATH_NOT_FOUND == last_error)
			return 0;

		Sleep(RM_RETRY_DELAY * retries * retries);
	}
	while (retries++ <= RM_RETRY_COUNT);

	return -1;
}

static void
fs_rm(const char *_source)
{
	WCHAR wsource[CLAR_MAX_PATH];
	DWORD attrs;

	/* The input path is UTF-8. Convert it to wide characters
	 * for use with the Windows API */
	cl_assert(MultiByteToWideChar(CP_UTF8,
				MB_ERR_INVALID_CHARS,
				_source,
				-1, /* Indicates NULL termination */
				wsource,
				CLAR_MAX_PATH));

	translate_path(wsource, CLAR_MAX_PATH);

	/* Does the item exist? If not, we have no work to do */
	attrs = GetFileAttributesW(wsource);

	if (INVALID_FILE_ATTRIBUTES == attrs)
		return;

	if (FILE_ATTRIBUTE_DIRECTORY & attrs)
		fs_rmdir_helper(wsource);
	else {
		/* The item is a file. Strip the +R bit */
		if (FILE_ATTRIBUTE_READONLY & attrs)
			cl_assert(SetFileAttributesW(wsource, attrs & ~FILE_ATTRIBUTE_READONLY));

		cl_assert(DeleteFileW(wsource));
	}

	/* Wait for the DeleteFile or RemoveDirectory call to complete */
	cl_assert(0 == fs_rm_wait(wsource));
}

static void
fs_copydir_helper(WCHAR *_wsource, WCHAR *_wdest)
{
	WCHAR buf_source[CLAR_MAX_PATH], buf_dest[CLAR_MAX_PATH];
	HANDLE find_handle;
	WIN32_FIND_DATAW find_data;
	size_t buf_source_prefix_len, buf_dest_prefix_len;

	wcscpy_s(buf_source, CLAR_MAX_PATH, _wsource);
	wcscat_s(buf_source, CLAR_MAX_PATH, L"\\");
	translate_path(buf_source, CLAR_MAX_PATH);
	buf_source_prefix_len = wcslen(buf_source);

	wcscpy_s(buf_dest, CLAR_MAX_PATH, _wdest);
	wcscat_s(buf_dest, CLAR_MAX_PATH, L"\\");
	translate_path(buf_dest, CLAR_MAX_PATH);
	buf_dest_prefix_len = wcslen(buf_dest);

	/* Get an enumerator for the items in the source. */
	wcscat_s(buf_source, CLAR_MAX_PATH, L"*");
	find_handle = FindFirstFileW(buf_source, &find_data);
	cl_assert(INVALID_HANDLE_VALUE != find_handle);

	/* Create the target directory. */
	cl_assert(CreateDirectoryW(_wdest, NULL));

	do {
		/* FindFirstFile/FindNextFile gives back . and ..
		 * entries at the beginning */
		if (fs__dotordotdot(find_data.cFileName))
			continue;

		wcscpy_s(buf_source + buf_source_prefix_len, CLAR_MAX_PATH - buf_source_prefix_len, find_data.cFileName);
		wcscpy_s(buf_dest + buf_dest_prefix_len, CLAR_MAX_PATH - buf_dest_prefix_len, find_data.cFileName);

		if (FILE_ATTRIBUTE_DIRECTORY & find_data.dwFileAttributes)
			fs_copydir_helper(buf_source, buf_dest);
		else
			cl_assert(CopyFileW(buf_source, buf_dest, TRUE));
	}
	while (FindNextFileW(find_handle, &find_data));

	/* Ensure that we successfully completed the enumeration */
	cl_assert(ERROR_NO_MORE_FILES == GetLastError());

	/* Close the find handle */
	FindClose(find_handle);
}

static void
fs_copy(const char *_source, const char *_dest)
{
	WCHAR wsource[CLAR_MAX_PATH], wdest[CLAR_MAX_PATH];
	DWORD source_attrs, dest_attrs;
	HANDLE find_handle;
	WIN32_FIND_DATAW find_data;

	/* The input paths are UTF-8. Convert them to wide characters
	 * for use with the Windows API. */
	cl_assert(MultiByteToWideChar(CP_UTF8,
				MB_ERR_INVALID_CHARS,
				_source,
				-1,
				wsource,
				CLAR_MAX_PATH));

	cl_assert(MultiByteToWideChar(CP_UTF8,
				MB_ERR_INVALID_CHARS,
				_dest,
				-1,
				wdest,
				CLAR_MAX_PATH));

	translate_path(wsource, CLAR_MAX_PATH);
	translate_path(wdest, CLAR_MAX_PATH);

	/* Check the source for existence */
	source_attrs = GetFileAttributesW(wsource);
	cl_assert(INVALID_FILE_ATTRIBUTES != source_attrs);

	/* Check the target for existence */
	dest_attrs = GetFileAttributesW(wdest);

	if (INVALID_FILE_ATTRIBUTES != dest_attrs) {
		/* Target exists; append last path part of source to target.
		 * Use FindFirstFile to parse the path */
		find_handle = FindFirstFileW(wsource, &find_data);
		cl_assert(INVALID_HANDLE_VALUE != find_handle);
		wcscat_s(wdest, CLAR_MAX_PATH, L"\\");
		wcscat_s(wdest, CLAR_MAX_PATH, find_data.cFileName);
		FindClose(find_handle);

		/* Check the new target for existence */
		cl_assert(INVALID_FILE_ATTRIBUTES == GetFileAttributesW(wdest));
	}

	if (FILE_ATTRIBUTE_DIRECTORY & source_attrs)
		fs_copydir_helper(wsource, wdest);
	else
		cl_assert(CopyFileW(wsource, wdest, TRUE));
}

void
cl_fs_cleanup(void)
{
#ifdef CLAR_FIXTURE_PATH
	fs_rm(fixture_path(_clar_path, "*"));
#else
	((void)fs_copy); /* unused */
#endif
}

#else

#include <errno.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(__linux__)
# include <sys/sendfile.h>
#endif

#if defined(__APPLE__)
# include <copyfile.h>
#endif

static void basename_r(const char **out, int *out_len, const char *in)
{
	size_t in_len = strlen(in), start_pos;

	for (in_len = strlen(in); in_len; in_len--) {
		if (in[in_len - 1] != '/')
			break;
	}

	for (start_pos = in_len; start_pos; start_pos--) {
		if (in[start_pos - 1] == '/')
			break;
	}

	cl_assert(in_len - start_pos < INT_MAX);

	if (in_len - start_pos > 0) {
		*out = &in[start_pos];
		*out_len = (in_len - start_pos);
	} else {
		*out = "/";
		*out_len = 1;
	}
}

static char *joinpath(const char *dir, const char *base, int base_len)
{
	char *out;
	int len;

	if (base_len == -1) {
		size_t bl = strlen(base);

		cl_assert(bl < INT_MAX);
		base_len = (int)bl;
	}

	len = strlen(dir) + base_len + 2;
	cl_assert(len > 0);

	cl_assert(out = malloc(len));
	cl_assert(snprintf(out, len, "%s/%.*s", dir, base_len, base) < len);

	return out;
}

static void
fs_copydir_helper(const char *source, const char *dest, int dest_mode)
{
	DIR *source_dir;
	struct dirent *d;

	mkdir(dest, dest_mode);

	cl_assert_(source_dir = opendir(source), "Could not open source dir");
	while ((d = (errno = 0, readdir(source_dir))) != NULL) {
		char *child;

		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;

		child = joinpath(source, d->d_name, -1);
		fs_copy(child, dest);
		free(child);
	}

	cl_assert_(errno == 0, "Failed to iterate source dir");

	closedir(source_dir);
}

static void
fs_copyfile_helper(const char *source, size_t source_len, const char *dest, int dest_mode)
{
	int in, out;

	cl_must_pass((in = open(source, O_RDONLY)));
	cl_must_pass((out = open(dest, O_WRONLY|O_CREAT|O_TRUNC, dest_mode)));

#if USE_FCOPYFILE && defined(__APPLE__)
	((void)(source_len)); /* unused */
	cl_must_pass(fcopyfile(in, out, 0, COPYFILE_DATA));
#elif USE_SENDFILE && defined(__linux__)
	{
		ssize_t ret = 0;

		while (source_len && (ret = sendfile(out, in, NULL, source_len)) > 0) {
			source_len -= (size_t)ret;
		}
		cl_assert(ret >= 0);
	}
#else
	{
		char buf[131072];
		ssize_t ret;

		((void)(source_len)); /* unused */

		while ((ret = read(in, buf, sizeof(buf))) > 0) {
			size_t len = (size_t)ret;

			while (len && (ret = write(out, buf, len)) > 0) {
				cl_assert(ret <= (ssize_t)len);
				len -= ret;
			}
			cl_assert(ret >= 0);
		}
		cl_assert(ret == 0);
	}
#endif

	close(in);
	close(out);
}

static void
fs_copy(const char *source, const char *_dest)
{
	char *dbuf = NULL;
	const char *dest = NULL;
	struct stat source_st, dest_st;

	cl_must_pass_(lstat(source, &source_st), "Failed to stat copy source");

	if (lstat(_dest, &dest_st) == 0) {
		const char *base;
		int base_len;

		/* Target exists and is directory; append basename */
		cl_assert(S_ISDIR(dest_st.st_mode));

		basename_r(&base, &base_len, source);
		cl_assert(base_len < INT_MAX);

		dbuf = joinpath(_dest, base, base_len);
		dest = dbuf;
	} else if (errno != ENOENT) {
		cl_fail("Cannot copy; cannot stat destination");
	} else {
		dest = _dest;
	}

	if (S_ISDIR(source_st.st_mode)) {
		fs_copydir_helper(source, dest, source_st.st_mode);
	} else {
		fs_copyfile_helper(source, source_st.st_size, dest, source_st.st_mode);
	}

	free(dbuf);
}

static void
fs_rmdir_helper(const char *path)
{
	DIR *dir;
	struct dirent *d;

	cl_assert_(dir = opendir(path), "Could not open dir");
	while ((d = (errno = 0, readdir(dir))) != NULL) {
		char *child;

		if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
			continue;

		child = joinpath(path, d->d_name, -1);
		fs_rm(child);
		free(child);
	}

	cl_assert_(errno == 0, "Failed to iterate source dir");
	closedir(dir);

	cl_must_pass_(rmdir(path), "Could not remove directory");
}

static void
fs_rm(const char *path)
{
	struct stat st;

	if (lstat(path, &st)) {
		if (errno == ENOENT)
			return;

		cl_fail("Cannot copy; cannot stat destination");
	}

	if (S_ISDIR(st.st_mode)) {
		fs_rmdir_helper(path);
	} else {
		cl_must_pass(unlink(path));
	}
}

void
cl_fs_cleanup(void)
{
	clar_unsandbox();
	clar_sandbox();
}
#endif
