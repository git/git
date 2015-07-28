#include "../../git-compat-util.h"
#include "../../hashmap.h"
#include "../win32.h"
#include "fscache.h"
#include "../../dir.h"
#include "../../abspath.h"
#include "../../trace.h"
#include "config.h"
#include "../../mem-pool.h"
#include "ntifs.h"

static volatile long initialized;
static DWORD dwTlsIndex;
CRITICAL_SECTION fscache_cs;

/*
 * Store one fscache per thread to avoid thread contention and locking.
 * This is ok because multi-threaded access is 1) uncommon and 2) always
 * splitting up the cache entries across multiple threads so there isn't
 * any overlap between threads anyway.
 */
struct fscache {
	volatile long enabled;
	struct hashmap map;
	struct mem_pool mem_pool;
	unsigned int lstat_requests;
	unsigned int opendir_requests;
	unsigned int fscache_requests;
	unsigned int fscache_misses;
	/*
	 * 32k wide characters translates to 64kB, which is the maximum that
	 * Windows 8.1 and earlier can handle. On network drives, not only
	 * the client's Windows version matters, but also the server's,
	 * therefore we need to keep this to 64kB.
	 */
	WCHAR buffer[32 * 1024];
};
static struct trace_key trace_fscache = TRACE_KEY_INIT(FSCACHE);

/*
 * An entry in the file system cache. Used for both entire directory listings
 * and file entries.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
struct fsentry {
	struct hashmap_entry ent;
	mode_t st_mode;
	ULONG reparse_tag;
	/* Pointer to the directory listing, or NULL for the listing itself. */
	struct fsentry *list;
	/* Pointer to the next file entry of the list. */
	struct fsentry *next;

	union {
		/* Reference count of the directory listing. */
		volatile long refcnt;
		struct {
			/* More stat members (only used for file entries). */
			off64_t st_size;
			struct timespec st_atim;
			struct timespec st_mtim;
			struct timespec st_ctim;
		} s;
	} u;

	/* Length of name. */
	unsigned short len;
	/*
	 * Name of the entry. For directory listings: relative path of the
	 * directory, without trailing '/' (empty for cwd()). For file entries:
	 * name of the file. Typically points to the end of the structure if
	 * the fsentry is allocated on the heap (see fsentry_alloc), or to a
	 * local variable if on the stack (see fsentry_init).
	 */
	struct dirent dirent;
};
#pragma GCC diagnostic pop

struct heap_fsentry {
	union {
		struct fsentry ent;
		char dummy[sizeof(struct fsentry) + MAX_LONG_PATH];
	} u;
};

/*
 * Compares the paths of two fsentry structures for equality.
 */
static int fsentry_cmp(void *cmp_data UNUSED,
		       const struct fsentry *fse1, const struct fsentry *fse2,
		       void *keydata UNUSED)
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
	return fspathncmp(fse1->dirent.d_name, fse2->dirent.d_name, fse1->len);
}

/*
 * Calculates the hash code of an fsentry structure's path.
 */
static unsigned int fsentry_hash(const struct fsentry *fse)
{
	unsigned int hash = fse->list ? fse->list->ent.hash : 0;
	return hash ^ memihash(fse->dirent.d_name, fse->len);
}

/*
 * Initialize an fsentry structure for use by fsentry_hash and fsentry_cmp.
 */
static void fsentry_init(struct fsentry *fse, struct fsentry *list,
			 const char *name, size_t len)
{
	fse->list = list;
	if (len > MAX_LONG_PATH)
		BUG("Trying to allocate fsentry for long path '%.*s'",
		    (int)len, name);
	memcpy(fse->dirent.d_name, name, len);
	fse->dirent.d_name[len] = 0;
	fse->len = len;
	hashmap_entry_init(&fse->ent, fsentry_hash(fse));
}

/*
 * Allocate an fsentry structure on the heap.
 */
static struct fsentry *fsentry_alloc(struct fscache *cache, struct fsentry *list, const char *name,
		size_t len)
{
	/* overallocate fsentry and copy the name to the end */
	struct fsentry *fse =
		mem_pool_alloc(&cache->mem_pool, sizeof(*fse) + len + 1);
	/* init the rest of the structure */
	fsentry_init(fse, list, name, len);
	fse->next = NULL;
	fse->u.refcnt = 1;
	return fse;
}

/*
 * Add a reference to an fsentry.
 */
inline static void fsentry_addref(struct fsentry *fse)
{
	if (fse->list)
		fse = fse->list;

	InterlockedIncrement(&(fse->u.refcnt));
}

/*
 * Release the reference to an fsentry.
 */
static void fsentry_release(struct fsentry *fse)
{
	if (fse->list)
		fse = fse->list;

	InterlockedDecrement(&(fse->u.refcnt));
}

static int xwcstoutfn(char *utf, int utflen, const wchar_t *wcs, int wcslen)
{
	if (!wcs || !utf || utflen < 1) {
		errno = EINVAL;
		return -1;
	}
	utflen = WideCharToMultiByte(CP_UTF8, 0, wcs, wcslen, utf, utflen, NULL, NULL);
	if (utflen)
		return utflen;
	errno = ERANGE;
	return -1;
}

/*
 * Allocate and initialize an fsentry from a FILE_FULL_DIR_INFORMATION structure.
 */
static struct fsentry *fseentry_create_entry(struct fscache *cache,
					     struct fsentry *list,
					     PFILE_FULL_DIR_INFORMATION fdata)
{
	char buf[MAX_PATH * 3];
	int len;
	struct fsentry *fse;

	len = xwcstoutfn(buf, ARRAY_SIZE(buf), fdata->FileName, fdata->FileNameLength / sizeof(wchar_t));

	fse = fsentry_alloc(cache, list, buf, len);

	fse->reparse_tag =
		fdata->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ?
		fdata->EaSize : 0;

	fse->st_mode = file_attr_to_st_mode(fdata->FileAttributes);
	fse->dirent.d_type = S_ISDIR(fse->st_mode) ? DT_DIR : DT_REG;
	fse->u.s.st_size = fdata->EndOfFile.LowPart |
		(((off_t)fdata->EndOfFile.HighPart) << 32);
	filetime_to_timespec((FILETIME *)&(fdata->LastAccessTime),
			     &(fse->u.s.st_atim));
	filetime_to_timespec((FILETIME *)&(fdata->LastWriteTime),
			     &(fse->u.s.st_mtim));
	filetime_to_timespec((FILETIME *)&(fdata->CreationTime),
			     &(fse->u.s.st_ctim));

	return fse;
}

/*
 * Create an fsentry-based directory listing (similar to opendir / readdir).
 * Dir should not contain trailing '/'. Use an empty string for the current
 * directory (not "."!).
 */
static struct fsentry *fsentry_create_list(struct fscache *cache, const struct fsentry *dir,
					   int *dir_not_found)
{
	wchar_t pattern[MAX_LONG_PATH];
	NTSTATUS status;
	IO_STATUS_BLOCK iosb;
	PFILE_FULL_DIR_INFORMATION di;
	HANDLE h;
	int wlen;
	struct fsentry *list, **phead;
	DWORD err;

	*dir_not_found = 0;

	/* convert name to UTF-16 and check length */
	if ((wlen = xutftowcs_path_ex(pattern, dir->dirent.d_name,
				      MAX_LONG_PATH, dir->len, MAX_PATH - 2,
				      are_long_paths_enabled())) < 0)
		return NULL;

	/* handle CWD */
	if (!wlen) {
		wlen = GetCurrentDirectoryW(ARRAY_SIZE(pattern), pattern);
		if (!wlen || wlen >= ARRAY_SIZE(pattern)) {
			errno = wlen ? ENAMETOOLONG : err_win_to_posix(GetLastError());
			return NULL;
		}
	}

	h = CreateFileW(pattern, FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		err = GetLastError();
		*dir_not_found = 1; /* or empty directory */
		errno = (err == ERROR_DIRECTORY) ? ENOTDIR : err_win_to_posix(err);
		trace_printf_key(&trace_fscache, "fscache: error(%d) '%s'\n",
						 errno, dir->dirent.d_name);
		return NULL;
	}

	/* allocate object to hold directory listing */
	list = fsentry_alloc(cache, NULL, dir->dirent.d_name, dir->len);
	list->st_mode = S_IFDIR;
	list->dirent.d_type = DT_DIR;

	/* walk directory and build linked list of fsentry structures */
	phead = &list->next;
	status = NtQueryDirectoryFile(h, NULL, 0, 0, &iosb, cache->buffer,
		sizeof(cache->buffer), FileFullDirectoryInformation, FALSE, NULL, FALSE);
	if (!NT_SUCCESS(status)) {
		/*
		 * NtQueryDirectoryFile returns STATUS_INVALID_PARAMETER when
		 * asked to enumerate an invalid directory (ie it is a file
		 * instead of a directory).  Verify that is the actual cause
		 * of the error.
		*/
		if (status == STATUS_INVALID_PARAMETER) {
			DWORD attributes = GetFileAttributesW(pattern);
			if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
				status = ERROR_DIRECTORY;
		}
		goto Error;
	}
	di = (PFILE_FULL_DIR_INFORMATION)(cache->buffer);
	for (;;) {

		*phead = fseentry_create_entry(cache, list, di);
		phead = &(*phead)->next;

		/* If there is no offset in the entry, the buffer has been exhausted. */
		if (di->NextEntryOffset == 0) {
			status = NtQueryDirectoryFile(h, NULL, 0, 0, &iosb, cache->buffer,
				sizeof(cache->buffer), FileFullDirectoryInformation, FALSE, NULL, FALSE);
			if (!NT_SUCCESS(status)) {
				if (status == STATUS_NO_MORE_FILES)
					break;
				goto Error;
			}

			di = (PFILE_FULL_DIR_INFORMATION)(cache->buffer);
			continue;
		}

		/* Advance to the next entry. */
		di = (PFILE_FULL_DIR_INFORMATION)(((PUCHAR)di) + di->NextEntryOffset);
	}

	CloseHandle(h);
	return list;

Error:
	trace_printf_key(&trace_fscache,
			 "fscache: status(%ld) unable to query directory "
			 "contents '%s'\n", status, dir->dirent.d_name);
	CloseHandle(h);
	fsentry_release(list);
	return NULL;
}

/*
 * Adds a directory listing to the cache.
 */
static void fscache_add(struct fscache *cache, struct fsentry *fse)
{
	if (fse->list)
		fse = fse->list;

	for (; fse; fse = fse->next)
		hashmap_add(&cache->map, &fse->ent);
}

/*
 * Clears the cache.
 */
static void fscache_clear(struct fscache *cache)
{
	mem_pool_discard(&cache->mem_pool, 0);
	mem_pool_init(&cache->mem_pool, 0);
	hashmap_clear(&cache->map);
	hashmap_init(&cache->map, (hashmap_cmp_fn)fsentry_cmp, NULL, 0);
	cache->lstat_requests = cache->opendir_requests = 0;
	cache->fscache_misses = cache->fscache_requests = 0;
}

/*
 * Checks if the cache is enabled for the given path.
 */
static int do_fscache_enabled(struct fscache *cache, const char *path)
{
	return cache->enabled > 0 && !is_absolute_path(path);
}

int fscache_enabled(const char *path)
{
	struct fscache *cache = fscache_getcache();

	return cache ? do_fscache_enabled(cache, path) : 0;
}

/*
 * Looks up or creates a cache entry for the specified key.
 */
static struct fsentry *fscache_get(struct fscache *cache, struct fsentry *key)
{
	struct fsentry *fse;
	int dir_not_found;

	cache->fscache_requests++;
	/* check if entry is in cache */
	fse = hashmap_get_entry(&cache->map, key, ent, NULL);
	if (fse) {
		if (fse->st_mode)
			fsentry_addref(fse);
		else
			fse = NULL; /* non-existing directory */
		return fse;
	}
	/* if looking for a file, check if directory listing is in cache */
	if (!fse && key->list) {
		fse = hashmap_get_entry(&cache->map, key->list, ent, NULL);
		if (fse) {
			/*
			 * dir entry without file entry, or dir does not
			 * exist -> file doesn't exist
			 */
			errno = ENOENT;
			return NULL;
		}
	}

	/* create the directory listing */
	fse = fsentry_create_list(cache, key->list ? key->list : key, &dir_not_found);

	/* leave on error (errno set by fsentry_create_list) */
	if (!fse) {
		if (dir_not_found && key->list) {
			/*
			 * Record that the directory does not exist (or is
			 * empty, which for all practical matters is the same
			 * thing as far as fscache is concerned).
			 */
			fse = fsentry_alloc(cache, key->list->list,
					    key->list->dirent.d_name,
					    key->list->len);
			fse->st_mode = 0;
			hashmap_add(&cache->map, &fse->ent);
		}
		return NULL;
	}

	/* add directory listing to the cache */
	cache->fscache_misses++;
	fscache_add(cache, fse);

	/* lookup file entry if requested (fse already points to directory) */
	if (key->list)
		fse = hashmap_get_entry(&cache->map, key, ent, NULL);

	if (fse && !fse->st_mode)
		fse = NULL; /* non-existing directory */

	/* return entry or ENOENT */
	if (fse)
		fsentry_addref(fse);
	else
		errno = ENOENT;

	return fse;
}

/*
 * Enables the cache. Note that the cache is read-only, changes to
 * the working directory are NOT reflected in the cache while enabled.
 */
int fscache_enable(size_t initial_size)
{
	int fscache;
	struct fscache *cache;
	int result = 0;

	/* allow the cache to be disabled entirely */
	fscache = git_env_bool("GIT_TEST_FSCACHE", -1);
	if (fscache != -1)
		core_fscache = fscache;
	if (!core_fscache)
		return 0;

	/*
	 * refcount the global fscache initialization so that the
	 * opendir and lstat function pointers are redirected if
	 * any threads are using the fscache.
	 */
	EnterCriticalSection(&fscache_cs);
	if (!initialized) {
		if (!dwTlsIndex) {
			dwTlsIndex = TlsAlloc();
			if (dwTlsIndex == TLS_OUT_OF_INDEXES) {
				LeaveCriticalSection(&fscache_cs);
				return 0;
			}
		}

		/* redirect opendir and lstat to the fscache implementations */
		opendir = fscache_opendir;
		lstat = fscache_lstat;
		win32_is_mount_point = fscache_is_mount_point;
	}
	initialized++;
	LeaveCriticalSection(&fscache_cs);

	/* refcount the thread specific initialization */
	cache = fscache_getcache();
	if (cache) {
		cache->enabled++;
	} else {
		cache = (struct fscache *)xcalloc(1, sizeof(*cache));
		cache->enabled = 1;
		/*
		 * avoid having to rehash by leaving room for the parent dirs.
		 * '4' was determined empirically by testing several repos
		 */
		hashmap_init(&cache->map, (hashmap_cmp_fn)fsentry_cmp, NULL, initial_size * 4);
		mem_pool_init(&cache->mem_pool, 0);
		if (!TlsSetValue(dwTlsIndex, cache))
			BUG("TlsSetValue error");
	}

	trace_printf_key(&trace_fscache, "fscache: enable\n");
	return result;
}

/*
 * Disables the cache.
 */
void fscache_disable(void)
{
	struct fscache *cache;

	if (!core_fscache)
		return;

	/* update the thread specific fscache initialization */
	cache = fscache_getcache();
	if (!cache)
		BUG("fscache_disable() called on a thread where fscache has not been initialized");
	if (!cache->enabled)
		BUG("fscache_disable() called on an fscache that is already disabled");
	cache->enabled--;
	if (!cache->enabled) {
		TlsSetValue(dwTlsIndex, NULL);
		trace_printf_key(&trace_fscache, "fscache_disable: lstat %u, opendir %u, "
			"total requests/misses %u/%u\n",
			cache->lstat_requests, cache->opendir_requests,
			cache->fscache_requests, cache->fscache_misses);
		mem_pool_discard(&cache->mem_pool, 0);
		hashmap_clear(&cache->map);
		free(cache);
	}

	/* update the global fscache initialization */
	EnterCriticalSection(&fscache_cs);
	initialized--;
	if (!initialized) {
		/* reset opendir and lstat to the original implementations */
		opendir = dirent_opendir;
		lstat = mingw_lstat;
		win32_is_mount_point = mingw_is_mount_point;
	}
	LeaveCriticalSection(&fscache_cs);

	trace_printf_key(&trace_fscache, "fscache: disable\n");
	return;
}

/*
 * Flush cached stats result when fscache is enabled.
 */
void fscache_flush(void)
{
	struct fscache *cache = fscache_getcache();

	if (cache && cache->enabled) {
		fscache_clear(cache);
	}
}

/*
 * Lstat replacement, uses the cache if enabled, otherwise redirects to
 * mingw_lstat.
 */
int fscache_lstat(const char *filename, struct stat *st)
{
	int dirlen, base, len;
	struct heap_fsentry key[2];
	struct fsentry *fse;
	struct fscache *cache = fscache_getcache();

	if (!cache || !do_fscache_enabled(cache, filename))
		return mingw_lstat(filename, st);

	cache->lstat_requests++;
	/* split filename into path + name */
	len = strlen(filename);
	if (len && is_dir_sep(filename[len - 1]))
		len--;
	base = len;
	while (base && !is_dir_sep(filename[base - 1]))
		base--;
	dirlen = base ? base - 1 : 0;

	/* lookup entry for path + name in cache */
	fsentry_init(&key[0].u.ent, NULL, filename, dirlen);
	fsentry_init(&key[1].u.ent, &key[0].u.ent, filename + base, len - base);
	fse = fscache_get(cache, &key[1].u.ent);
	if (!fse) {
		errno = ENOENT;
		return -1;
	}

	/* copy stat data */
	st->st_ino = 0;
	st->st_gid = 0;
	st->st_uid = 0;
	st->st_dev = 0;
	st->st_rdev = 0;
	st->st_nlink = 1;
	st->st_mode = fse->st_mode;
	st->st_size = fse->u.s.st_size;
	st->st_atim = fse->u.s.st_atim;
	st->st_mtim = fse->u.s.st_mtim;
	st->st_ctim = fse->u.s.st_ctim;

	/* don't forget to release fsentry */
	fsentry_release(fse);
	return 0;
}

/*
 * is_mount_point() replacement, uses cache if enabled, otherwise falls
 * back to mingw_is_mount_point().
 */
int fscache_is_mount_point(struct strbuf *path)
{
	int dirlen, base, len;
	struct heap_fsentry key[2];
	struct fsentry *fse;
	struct fscache *cache = fscache_getcache();

	if (!cache || !do_fscache_enabled(cache, path->buf))
		return mingw_is_mount_point(path);

	cache->lstat_requests++;
	/* split path into path + name */
	len = path->len;
	if (len && is_dir_sep(path->buf[len - 1]))
		len--;
	base = len;
	while (base && !is_dir_sep(path->buf[base - 1]))
		base--;
	dirlen = base ? base - 1 : 0;

	/* lookup entry for path + name in cache */
	fsentry_init(&key[0].u.ent, NULL, path->buf, dirlen);
	fsentry_init(&key[1].u.ent, &key[0].u.ent, path->buf + base, len - base);
	fse = fscache_get(cache, &key[1].u.ent);
	if (!fse)
		return mingw_is_mount_point(path);
	return fse->reparse_tag == IO_REPARSE_TAG_MOUNT_POINT;
}

typedef struct fscache_DIR {
	struct DIR base_dir; /* extend base struct DIR */
	struct fsentry *pfsentry;
	struct dirent *dirent;
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
	dir->dirent = &next->dirent;
	return dir->dirent;
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
	struct heap_fsentry key;
	struct fsentry *list;
	fscache_DIR *dir;
	int len;
	struct fscache *cache = fscache_getcache();

	if (!cache || !do_fscache_enabled(cache, dirname))
		return dirent_opendir(dirname);

	cache->opendir_requests++;
	/* prepare name (strip trailing '/', replace '.') */
	len = strlen(dirname);
	if ((len == 1 && dirname[0] == '.') ||
	    (len && is_dir_sep(dirname[len - 1])))
		len--;

	/* get directory listing from cache */
	fsentry_init(&key.u.ent, NULL, dirname, len);
	list = fscache_get(cache, &key.u.ent);
	if (!list)
		return NULL;

	/* alloc and return DIR structure */
	dir = (fscache_DIR*) xmalloc(sizeof(fscache_DIR));
	dir->base_dir.preaddir = fscache_readdir;
	dir->base_dir.pclosedir = fscache_closedir;
	dir->pfsentry = list;
	return (DIR*) dir;
}

struct fscache *fscache_getcache(void)
{
	return (struct fscache *)TlsGetValue(dwTlsIndex);
}

void fscache_merge(struct fscache *dest)
{
	struct hashmap_iter iter;
	struct hashmap_entry *e;
	struct fscache *cache = fscache_getcache();

	/*
	 * Only do the merge if fscache was enabled and we have a dest
	 * cache to merge into.
	 */
	if (!dest) {
		fscache_enable(0);
		return;
	}
	if (!cache)
		BUG("fscache_merge() called on a thread where fscache has not been initialized");

	TlsSetValue(dwTlsIndex, NULL);
	trace_printf_key(&trace_fscache, "fscache_merge: lstat %u, opendir %u, "
		"total requests/misses %u/%u\n",
		cache->lstat_requests, cache->opendir_requests,
		cache->fscache_requests, cache->fscache_misses);

	/*
	 * This is only safe because the primary thread we're merging into
	 * isn't being used so the critical section only needs to prevent
	 * the the child threads from stomping on each other.
	 */
	EnterCriticalSection(&fscache_cs);

	hashmap_iter_init(&cache->map, &iter);
	while ((e = hashmap_iter_next(&iter)))
		hashmap_add(&dest->map, e);

	mem_pool_combine(&dest->mem_pool, &cache->mem_pool);

	dest->lstat_requests += cache->lstat_requests;
	dest->opendir_requests += cache->opendir_requests;
	dest->fscache_requests += cache->fscache_requests;
	dest->fscache_misses += cache->fscache_misses;
	initialized--;
	LeaveCriticalSection(&fscache_cs);

	free(cache);

}
