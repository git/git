#include "../../git-compat-util.h"

typedef struct dirent_DIR {
	struct DIR base_dir;  /* extend base struct DIR */
	HANDLE dd_handle;     /* FindFirstFile handle */
	int dd_stat;          /* 0-based index */
	struct dirent dd_dir; /* includes d_type */
} dirent_DIR;

DIR *(*opendir)(const char *dirname) = dirent_opendir;

static inline void finddata2dirent(struct dirent *ent, WIN32_FIND_DATAW *fdata)
{
	/* convert UTF-16 name to UTF-8 (d_name points to dirent_DIR.dd_name) */
	xwcstoutf(ent->d_name, fdata->cFileName, MAX_PATH * 3);

	/* Set file type, based on WIN32_FIND_DATA */
	if (fdata->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		ent->d_type = DT_DIR;
	else
		ent->d_type = DT_REG;
}

static struct dirent *dirent_readdir(dirent_DIR *dir)
{
	if (!dir) {
		errno = EBADF; /* No set_errno for mingw */
		return NULL;
	}

	/* if first entry, dirent has already been set up by opendir */
	if (dir->dd_stat) {
		/* get next entry and convert from WIN32_FIND_DATA to dirent */
		WIN32_FIND_DATAW fdata;
		if (FindNextFileW(dir->dd_handle, &fdata)) {
			finddata2dirent(&dir->dd_dir, &fdata);
		} else {
			DWORD lasterr = GetLastError();
			/* POSIX says you shouldn't set errno when readdir can't
			   find any more files; so, if another error we leave it set. */
			if (lasterr != ERROR_NO_MORE_FILES)
				errno = err_win_to_posix(lasterr);
			return NULL;
		}
	}

	++dir->dd_stat;
	return &dir->dd_dir;
}

static int dirent_closedir(dirent_DIR *dir)
{
	if (!dir) {
		errno = EBADF;
		return -1;
	}

	FindClose(dir->dd_handle);
	free(dir);
	return 0;
}

DIR *dirent_opendir(const char *name)
{
	wchar_t pattern[MAX_PATH + 2]; /* + 2 for '/' '*' */
	WIN32_FIND_DATAW fdata;
	HANDLE h;
	int len;
	dirent_DIR *dir;

	/* convert name to UTF-16 and check length < MAX_PATH */
	if ((len = xutftowcs_path(pattern, name)) < 0)
		return NULL;

	/* append optional '/' and wildcard '*' */
	if (len && !is_dir_sep(pattern[len - 1]))
		pattern[len++] = '/';
	pattern[len++] = '*';
	pattern[len] = 0;

	/* open find handle */
	h = FindFirstFileW(pattern, &fdata);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		errno = (err == ERROR_DIRECTORY) ? ENOTDIR : err_win_to_posix(err);
		return NULL;
	}

	/* initialize DIR structure and copy first dir entry */
	dir = xmalloc(sizeof(dirent_DIR) + MAX_LONG_PATH);
	dir->base_dir.preaddir = (struct dirent *(*)(DIR *dir)) dirent_readdir;
	dir->base_dir.pclosedir = (int (*)(DIR *dir)) dirent_closedir;
	dir->dd_handle = h;
	dir->dd_stat = 0;
	finddata2dirent(&dir->dd_dir, &fdata);
	return (DIR*) dir;
}
