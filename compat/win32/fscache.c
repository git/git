#include "../../cache.h"
#include "../../hashmap.h"
#include "../win32.h"
#include "fscache.h"

static int initialized;
static volatile long enabled;
static struct hashmap map;
static CRITICAL_SECTION mutex;

/*
 * An entry in the file system cache. Used for both entire directory listings
 * and file entries.
 */
struct fsentry {
	struct hashmap_entry ent;
	mode_t st_mode;
	/* Length of name. */
	unsigned short len;
	/*
	 * Name of the entry. For directory listings: relative path of the
	 * directory, without trailing '/' (empty for cwd()). For file entries:
	 * name of the file. Typically points to the end of the structure if
	 * the fsentry is allocated on the heap (see fsentry_alloc), or to a
	 * local variable if on the stack (see fsentry_init).
	 */
	const char *name;
	/* Pointer to the directory listing, or NULL for the listing itself. */
	struct fsentry *list;
	/* Pointer to the next file entry of the list. */
	struct fsentry *next;

	union {
		/* Reference count of the directory listing. */
		volatile long refcnt;
		/* Handle to wait on the loading thread. */
		HANDLE hwait;
		struct {
			/* More stat members (only used for file entries). */
			off64_t st_size;
			struct timespec st_atim;
			struct timespec st_mtim;
			struct timespec st_ctim;
		};
	};
};

/*
 * Compares the paths of two fsentry structures for equality.
 */
static int fsentry_cmp(void *unused_cmp_data,
		       const struct fsentry *fse1, const struct fsentry *fse2,
		       void *unused_keydata)
{
	int res;
	if (fse1 == fse2)
		return 0;

	/* compare the list parts first */
	if (fse1->list != fse2->list &&
	    (res = fsentry_cmp(NULL, fse1->list ? fse1->list : fse1,
			       fse2->list ? fse2->list	: fse2, NULL)))
		return res;

	/* if list parts are equal, compare len and name */
	if (fse1->len != fse2->len)
		return fse1->len - fse2->len;
	return strnicmp(fse1->name, fse2->name, fse1->len);
}

/*
 * Calculates the hash code of an fsentry structure's path.
 */
static unsigned int fsentry_hash(const struct fsentry *fse)
{
	unsigned int hash = fse->list ? fse->list->ent.hash : 0;
	return hash ^ memihash(fse->name, fse->len);
}

/*
 * Initialize an fsentry structure for use by fsentry_hash and fsentry_cmp.
 */
static void fsentry_init(struct fsentry *fse, struct fsentry *list,
		const char *name, size_t len)
{
	fse->list = list;
	fse->name = name;
	fse->len = len;
	hashmap_entry_init(fse, fsentry_hash(fse));
}

/*
 * Allocate an fsentry structure on the heap.
 */
static struct fsentry *fsentry_alloc(struct fsentry *list, const char *name,
		size_t len)
{
	/* overallocate fsentry and copy the name to the end */
	struct fsentry *fse = xmalloc(sizeof(struct fsentry) + len + 1);
	char *nm = ((char*) fse) + sizeof(struct fsentry);
	memcpy(nm, name, len);
	nm[len] = 0;
	/* init the rest of the structure */
	fsentry_init(fse, list, nm, len);
	fse->next = NULL;
	fse->refcnt = 1;
	return fse;
}

/*
 * Add a reference to an fsentry.
 */
inline static void fsentry_addref(struct fsentry *fse)
{
	if (fse->list)
		fse = fse->list;

	InterlockedIncrement(&(fse->refcnt));
}

/*
 * Release the reference to an fsentry, frees the memory if its the last ref.
 */
static void fsentry_release(struct fsentry *fse)
{
	if (fse->list)
		fse = fse->list;

	if (InterlockedDecrement(&(fse->refcnt)))
		return;

	while (fse) {
		struct fsentry *next = fse->next;
		free(fse);
		fse = next;
	}
}

/*
 * Allocate and initialize an fsentry from a WIN32_FIND_DATA structure.
 */
static struct fsentry *fseentry_create_entry(struct fsentry *list,
		const WIN32_FIND_DATAW *fdata)
{
	char buf[MAX_PATH * 3];
	int len;
	struct fsentry *fse;
	len = xwcstoutf(buf, fdata->cFileName, ARRAY_SIZE(buf));

	fse = fsentry_alloc(list, buf, len);

	fse->st_mode = file_attr_to_st_mode(fdata->dwFileAttributes,
			fdata->dwReserved0);
	fse->st_size = S_ISLNK(fse->st_mode) ? MAX_LONG_PATH :
			fdata->nFileSizeLow | (((off_t) fdata->nFileSizeHigh) << 32);
	filetime_to_timespec(&(fdata->ftLastAccessTime), &(fse->st_atim));
	filetime_to_timespec(&(fdata->ftLastWriteTime), &(fse->st_mtim));
	filetime_to_timespec(&(fdata->ftCreationTime), &(fse->st_ctim));

	return fse;
}

/*
 * Create an fsentry-based directory listing (similar to opendir / readdir).
 * Dir should not contain trailing '/'. Use an empty string for the current
 * directory (not "."!).
 */
static struct fsentry *fsentry_create_list(const struct fsentry *dir)
{
	wchar_t pattern[MAX_LONG_PATH + 2]; /* + 2 for "\*" */
	WIN32_FIND_DATAW fdata;
	HANDLE h;
	int wlen;
	struct fsentry *list, **phead;
	DWORD err;

	/* convert name to UTF-16 and check length */
	if ((wlen = xutftowcs_path_ex(pattern, dir->name, MAX_LONG_PATH,
			dir->len, MAX_PATH - 2, core_long_paths)) < 0)
		return NULL;

	/*
	 * append optional '\' and wildcard '*'. Note: we need to use '\' as
	 * Windows doesn't translate '/' to '\' for "\\?\"-prefixed paths.
	 */
	if (wlen)
		pattern[wlen++] = '\\';
	pattern[wlen++] = '*';
	pattern[wlen] = 0;

	/* open find handle */
	h = FindFirstFileW(pattern, &fdata);
	if (h == INVALID_HANDLE_VALUE) {
		err = GetLastError();
		errno = (err == ERROR_DIRECTORY) ? ENOTDIR : err_win_to_posix(err);
		return NULL;
	}

	/* allocate object to hold directory listing */
	list = fsentry_alloc(NULL, dir->name, dir->len);

	/* walk directory and build linked list of fsentry structures */
	phead = &list->next;
	do {
		*phead = fseentry_create_entry(list, &fdata);
		phead = &(*phead)->next;
	} while (FindNextFileW(h, &fdata));

	/* remember result of last FindNextFile, then close find handle */
	err = GetLastError();
	FindClose(h);

	/* return the list if we've got all the files */
	if (err == ERROR_NO_MORE_FILES)
		return list;

	/* otherwise free the list and return error */
	fsentry_release(list);
	errno = err_win_to_posix(err);
	return NULL;
}

/*
 * Adds a directory listing to the cache.
 */
static void fscache_add(struct fsentry *fse)
{
	if (fse->list)
		fse = fse->list;

	for (; fse; fse = fse->next)
		hashmap_add(&map, fse);
}

/*
 * Clears the cache.
 */
static void fscache_clear(void)
{
	hashmap_free(&map, 1);
	hashmap_init(&map, (hashmap_cmp_fn)fsentry_cmp, NULL, 0);
}

/*
 * Checks if the cache is enabled for the given path.
 */
static inline int fscache_enabled(const char *path)
{
	return enabled > 0 && !is_absolute_path(path);
}

/*
 * Looks up a cache entry, waits if its being loaded by another thread.
 * The mutex must be owned by the calling thread.
 */
static struct fsentry *fscache_get_wait(struct fsentry *key)
{
	struct fsentry *fse = hashmap_get(&map, key, NULL);

	/* return if its a 'real' entry (future entries have refcnt == 0) */
	if (!fse || fse->list || fse->refcnt)
		return fse;

	/* create an event and link our key to the future entry */
	key->hwait = CreateEvent(NULL, TRUE, FALSE, NULL);
	key->next = fse->next;
	fse->next = key;

	/* wait for the loading thread to signal us */
	LeaveCriticalSection(&mutex);
	WaitForSingleObject(key->hwait, INFINITE);
	CloseHandle(key->hwait);
	EnterCriticalSection(&mutex);

	/* repeat cache lookup */
	return hashmap_get(&map, key, NULL);
}

/*
 * Looks up or creates a cache entry for the specified key.
 */
static struct fsentry *fscache_get(struct fsentry *key)
{
	struct fsentry *fse, *future, *waiter;

	EnterCriticalSection(&mutex);
	/* check if entry is in cache */
	fse = fscache_get_wait(key);
	if (fse) {
		fsentry_addref(fse);
		LeaveCriticalSection(&mutex);
		return fse;
	}
	/* if looking for a file, check if directory listing is in cache */
	if (!fse && key->list) {
		fse = fscache_get_wait(key->list);
		if (fse) {
			LeaveCriticalSection(&mutex);
			/* dir entry without file entry -> file doesn't exist */
			errno = ENOENT;
			return NULL;
		}
	}

	/* add future entry to indicate that we're loading it */
	future = key->list ? key->list : key;
	future->next = NULL;
	future->refcnt = 0;
	hashmap_add(&map, future);

	/* create the directory listing (outside mutex!) */
	LeaveCriticalSection(&mutex);
	fse = fsentry_create_list(future);
	EnterCriticalSection(&mutex);

	/* remove future entry and signal waiting threads */
	hashmap_remove(&map, future, NULL);
	waiter = future->next;
	while (waiter) {
		HANDLE h = waiter->hwait;
		waiter = waiter->next;
		SetEvent(h);
	}

	/* leave on error (errno set by fsentry_create_list) */
	if (!fse) {
		LeaveCriticalSection(&mutex);
		return NULL;
	}

	/* add directory listing to the cache */
	fscache_add(fse);

	/* lookup file entry if requested (fse already points to directory) */
	if (key->list)
		fse = hashmap_get(&map, key, NULL);

	/* return entry or ENOENT */
	if (fse)
		fsentry_addref(fse);
	else
		errno = ENOENT;

	LeaveCriticalSection(&mutex);
	return fse;
}

/*
 * Enables or disables the cache. Note that the cache is read-only, changes to
 * the working directory are NOT reflected in the cache while enabled.
 */
int fscache_enable(int enable)
{
	int result;

	if (!initialized) {
		/* allow the cache to be disabled entirely */
		if (!core_fscache)
			return 0;

		InitializeCriticalSection(&mutex);
		hashmap_init(&map, (hashmap_cmp_fn) fsentry_cmp, NULL, 0);
		initialized = 1;
	}

	result = enable ? InterlockedIncrement(&enabled)
			: InterlockedDecrement(&enabled);

	if (enable && result == 1) {
		/* redirect opendir and lstat to the fscache implementations */
		opendir = fscache_opendir;
		lstat = fscache_lstat;
	} else if (!enable && !result) {
		/* reset opendir and lstat to the original implementations */
		opendir = dirent_opendir;
		lstat = mingw_lstat;
		EnterCriticalSection(&mutex);
		fscache_clear();
		LeaveCriticalSection(&mutex);
	}
	return result;
}

/*
 * Lstat replacement, uses the cache if enabled, otherwise redirects to
 * mingw_lstat.
 */
int fscache_lstat(const char *filename, struct stat *st)
{
	int dirlen, base, len;
	struct fsentry key[2], *fse;

	if (!fscache_enabled(filename))
		return mingw_lstat(filename, st);

	/* split filename into path + name */
	len = strlen(filename);
	if (len && is_dir_sep(filename[len - 1]))
		len--;
	base = len;
	while (base && !is_dir_sep(filename[base - 1]))
		base--;
	dirlen = base ? base - 1 : 0;

	/* lookup entry for path + name in cache */
	fsentry_init(key, NULL, filename, dirlen);
	fsentry_init(key + 1, key, filename + base, len - base);
	fse = fscache_get(key + 1);
	if (!fse)
		return -1;

	/* copy stat data */
	st->st_ino = 0;
	st->st_gid = 0;
	st->st_uid = 0;
	st->st_dev = 0;
	st->st_rdev = 0;
	st->st_nlink = 1;
	st->st_mode = fse->st_mode;
	st->st_size = fse->st_size;
	st->st_atim = fse->st_atim;
	st->st_mtim = fse->st_mtim;
	st->st_ctim = fse->st_ctim;

	/* don't forget to release fsentry */
	fsentry_release(fse);
	return 0;
}

typedef struct fscache_DIR {
	struct DIR base_dir; /* extend base struct DIR */
	struct fsentry *pfsentry;
	struct dirent dirent;
} fscache_DIR;

/*
 * Readdir replacement.
 */
static struct dirent *fscache_readdir(DIR *base_dir)
{
	fscache_DIR *dir = (fscache_DIR*) base_dir;
	struct fsentry *next = dir->pfsentry->next;
	if (!next)
		return NULL;
	dir->pfsentry = next;
	dir->dirent.d_type = S_ISREG(next->st_mode) ? DT_REG :
			S_ISDIR(next->st_mode) ? DT_DIR : DT_LNK;
	dir->dirent.d_name = (char*) next->name;
	return &(dir->dirent);
}

/*
 * Closedir replacement.
 */
static int fscache_closedir(DIR *base_dir)
{
	fscache_DIR *dir = (fscache_DIR*) base_dir;
	fsentry_release(dir->pfsentry);
	free(dir);
	return 0;
}

/*
 * Opendir replacement, uses a directory listing from the cache if enabled,
 * otherwise calls original dirent implementation.
 */
DIR *fscache_opendir(const char *dirname)
{
	struct fsentry key, *list;
	fscache_DIR *dir;
	int len;

	if (!fscache_enabled(dirname))
		return dirent_opendir(dirname);

	/* prepare name (strip trailing '/', replace '.') */
	len = strlen(dirname);
	if ((len == 1 && dirname[0] == '.') ||
	    (len && is_dir_sep(dirname[len - 1])))
		len--;

	/* get directory listing from cache */
	fsentry_init(&key, NULL, dirname, len);
	list = fscache_get(&key);
	if (!list)
		return NULL;

	/* alloc and return DIR structure */
	dir = (fscache_DIR*) xmalloc(sizeof(fscache_DIR));
	dir->base_dir.preaddir = fscache_readdir;
	dir->base_dir.pclosedir = fscache_closedir;
	dir->pfsentry = list;
	return (DIR*) dir;
}
