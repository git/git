#include "../git-compat-util.h"
#include "dirent.h"

struct DIR {
	struct dirent dd_dir; /* includes d_type */
	HANDLE dd_handle;     /* FindFirstFile handle */
	int dd_stat;          /* 0-based index */
	char dd_name[1];      /* extend struct */
};

DIR *opendir(const char *name)
{
	DWORD attrs = GetFileAttributesA(name);
	int len;
	DIR *p;

	/* check for valid path */
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		errno = ENOENT;
		return NULL;
	}

	/* check if it's a directory */
	if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
		errno = ENOTDIR;
		return NULL;
	}

	/* check that the pattern won't be too long for FindFirstFileA */
	len = strlen(name);
	if (is_dir_sep(name[len - 1]))
		len--;
	if (len + 2 >= MAX_PATH) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	p = malloc(sizeof(DIR) + len + 2);
	if (!p)
		return NULL;

	memset(p, 0, sizeof(DIR) + len + 2);
	strcpy(p->dd_name, name);
	p->dd_name[len] = '/';
	p->dd_name[len+1] = '*';

	p->dd_handle = INVALID_HANDLE_VALUE;
	return p;
}

struct dirent *readdir(DIR *dir)
{
	WIN32_FIND_DATAA buf;
	HANDLE handle;

	if (!dir || !dir->dd_handle) {
		errno = EBADF; /* No set_errno for mingw */
		return NULL;
	}

	if (dir->dd_handle == INVALID_HANDLE_VALUE && dir->dd_stat == 0) {
		DWORD lasterr;
		handle = FindFirstFileA(dir->dd_name, &buf);
		lasterr = GetLastError();
		dir->dd_handle = handle;
		if (handle == INVALID_HANDLE_VALUE && (lasterr != ERROR_NO_MORE_FILES)) {
			errno = err_win_to_posix(lasterr);
			return NULL;
		}
	} else if (dir->dd_handle == INVALID_HANDLE_VALUE) {
		return NULL;
	} else if (!FindNextFileA(dir->dd_handle, &buf)) {
		DWORD lasterr = GetLastError();
		FindClose(dir->dd_handle);
		dir->dd_handle = INVALID_HANDLE_VALUE;
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
	dir->dd_dir.d_type = 0;
	if (buf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		dir->dd_dir.d_type |= DT_DIR;
	else
		dir->dd_dir.d_type |= DT_REG;

	return &dir->dd_dir;
}

int closedir(DIR *dir)
{
	if (!dir) {
		errno = EBADF;
		return -1;
	}

	if (dir->dd_handle != INVALID_HANDLE_VALUE)
		FindClose(dir->dd_handle);
	free(dir);
	return 0;
}
