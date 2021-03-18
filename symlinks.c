#include "cache.h"

static int threaded_check_leading_path(struct cache_def *cache, const char *name,
				       int len, int warn_on_lstat_err);
static int threaded_has_dirs_only_path(struct cache_def *cache, const char *name, int len, int prefix_len);

/*
 * Returns the length (on a path component basis) of the longest
 * common prefix match of 'name_a' and 'name_b'.
 */
static int longest_path_match(const char *name_a, int len_a,
			      const char *name_b, int len_b,
			      int *previous_slash)
{
	int max_len, match_len = 0, match_len_prev = 0, i = 0;

	max_len = len_a < len_b ? len_a : len_b;
	while (i < max_len && name_a[i] == name_b[i]) {
		if (name_a[i] == '/') {
			match_len_prev = match_len;
			match_len = i;
		}
		i++;
	}
	/*
	 * Is 'name_b' a substring of 'name_a', the other way around,
	 * or is 'name_a' and 'name_b' the exact same string?
	 */
	if (i >= max_len && ((len_a > len_b && name_a[len_b] == '/') ||
			     (len_a < len_b && name_b[len_a] == '/') ||
			     (len_a == len_b))) {
		match_len_prev = match_len;
		match_len = i;
	}
	*previous_slash = match_len_prev;
	return match_len;
}

static struct cache_def default_cache = CACHE_DEF_INIT;

static inline void reset_lstat_cache(struct cache_def *cache)
{
	strbuf_reset(&cache->path);
	cache->flags = 0;
	/*
	 * The track_flags and prefix_len_stat_func members is only
	 * set by the safeguard rule inside lstat_cache()
	 */
}

#define FL_DIR      (1 << 0)
#define FL_NOENT    (1 << 1)
#define FL_SYMLINK  (1 << 2)
#define FL_LSTATERR (1 << 3)
#define FL_ERR      (1 << 4)
#define FL_FULLPATH (1 << 5)

/*
 * Check if name 'name' of length 'len' has a symlink leading
 * component, or if the directory exists and is real, or not.
 *
 * To speed up the check, some information is allowed to be cached.
 * This can be indicated by the 'track_flags' argument, which also can
 * be used to indicate that we should check the full path.
 *
 * The 'prefix_len_stat_func' parameter can be used to set the length
 * of the prefix, where the cache should use the stat() function
 * instead of the lstat() function to test each path component.
 */
static int lstat_cache_matchlen(struct cache_def *cache,
				const char *name, int len,
				int *ret_flags, int track_flags,
				int prefix_len_stat_func)
{
	int match_len, last_slash, last_slash_dir, previous_slash;
	int save_flags, ret, saved_errno = 0;
	struct stat st;

	if (cache->track_flags != track_flags ||
	    cache->prefix_len_stat_func != prefix_len_stat_func) {
		/*
		 * As a safeguard rule we clear the cache if the
		 * values of track_flags and/or prefix_len_stat_func
		 * does not match with the last supplied values.
		 */
		reset_lstat_cache(cache);
		cache->track_flags = track_flags;
		cache->prefix_len_stat_func = prefix_len_stat_func;
		match_len = last_slash = 0;
	} else {
		/*
		 * Check to see if we have a match from the cache for
		 * the 2 "excluding" path types.
		 */
		match_len = last_slash =
			longest_path_match(name, len, cache->path.buf,
					   cache->path.len, &previous_slash);
		*ret_flags = cache->flags & track_flags & (FL_NOENT|FL_SYMLINK);

		if (!(track_flags & FL_FULLPATH) && match_len == len)
			match_len = last_slash = previous_slash;

		if (*ret_flags && match_len == cache->path.len)
			return match_len;
		/*
		 * If we now have match_len > 0, we would know that
		 * the matched part will always be a directory.
		 *
		 * Also, if we are tracking directories and 'name' is
		 * a substring of the cache on a path component basis,
		 * we can return immediately.
		 */
		*ret_flags = track_flags & FL_DIR;
		if (*ret_flags && len == match_len)
			return match_len;
	}

	/*
	 * Okay, no match from the cache so far, so now we have to
	 * check the rest of the path components.
	 */
	*ret_flags = FL_DIR;
	last_slash_dir = last_slash;
	if (len > cache->path.len)
		strbuf_grow(&cache->path, len - cache->path.len);
	while (match_len < len) {
		do {
			cache->path.buf[match_len] = name[match_len];
			match_len++;
		} while (match_len < len && name[match_len] != '/');
		if (match_len >= len && !(track_flags & FL_FULLPATH))
			break;
		last_slash = match_len;
		cache->path.buf[last_slash] = '\0';

		if (last_slash <= prefix_len_stat_func)
			ret = stat(cache->path.buf, &st);
		else
			ret = lstat(cache->path.buf, &st);

		if (ret) {
			*ret_flags = FL_LSTATERR;
			saved_errno = errno;
			if (errno == ENOENT)
				*ret_flags |= FL_NOENT;
		} else if (S_ISDIR(st.st_mode)) {
			last_slash_dir = last_slash;
			continue;
		} else if (S_ISLNK(st.st_mode)) {
			*ret_flags = FL_SYMLINK;
		} else {
			*ret_flags = FL_ERR;
		}
		break;
	}

	/*
	 * At the end update the cache.  Note that max 3 different
	 * path types, FL_NOENT, FL_SYMLINK and FL_DIR, can be cached
	 * for the moment!
	 */
	save_flags = *ret_flags & track_flags & (FL_NOENT|FL_SYMLINK);
	if (save_flags && last_slash > 0) {
		cache->path.buf[last_slash] = '\0';
		cache->path.len = last_slash;
		cache->flags = save_flags;
	} else if ((track_flags & FL_DIR) && last_slash_dir > 0) {
		/*
		 * We have a separate test for the directory case,
		 * since it could be that we have found a symlink or a
		 * non-existing directory and the track_flags says
		 * that we cannot cache this fact, so the cache would
		 * then have been left empty in this case.
		 *
		 * But if we are allowed to track real directories, we
		 * can still cache the path components before the last
		 * one (the found symlink or non-existing component).
		 */
		cache->path.buf[last_slash_dir] = '\0';
		cache->path.len = last_slash_dir;
		cache->flags = FL_DIR;
	} else {
		reset_lstat_cache(cache);
	}
	if (saved_errno)
		errno = saved_errno;
	return match_len;
}

static int lstat_cache(struct cache_def *cache, const char *name, int len,
		       int track_flags, int prefix_len_stat_func)
{
	int flags;
	(void)lstat_cache_matchlen(cache, name, len, &flags, track_flags,
			prefix_len_stat_func);
	return flags;
}

#define USE_ONLY_LSTAT  0

/*
 * Return non-zero if path 'name' has a leading symlink component
 */
int threaded_has_symlink_leading_path(struct cache_def *cache, const char *name, int len)
{
	return lstat_cache(cache, name, len, FL_SYMLINK|FL_DIR, USE_ONLY_LSTAT) & FL_SYMLINK;
}

int has_symlink_leading_path(const char *name, int len)
{
	return threaded_has_symlink_leading_path(&default_cache, name, len);
}

int check_leading_path(const char *name, int len, int warn_on_lstat_err)
{
	return threaded_check_leading_path(&default_cache, name, len,
					   warn_on_lstat_err);
}

/*
 * Return zero if some leading path component of 'name' does not exist.
 *
 * Return -1 if leading path exists and is a directory.
 *
 * Return the length of a leading component if it either exists but it's not a
 * directory, or if we were unable to lstat() it. If warn_on_lstat_err is true,
 * also emit a warning for this error.
 */
static int threaded_check_leading_path(struct cache_def *cache, const char *name,
				       int len, int warn_on_lstat_err)
{
	int flags;
	int match_len = lstat_cache_matchlen(cache, name, len, &flags,
			   FL_SYMLINK|FL_NOENT|FL_DIR, USE_ONLY_LSTAT);
	int saved_errno = errno;

	if (flags & FL_NOENT)
		return 0;
	else if (flags & FL_DIR)
		return -1;
	if (warn_on_lstat_err && (flags & FL_LSTATERR)) {
		char *path = xmemdupz(name, match_len);
		errno = saved_errno;
		warning_errno(_("failed to lstat '%s'"), path);
		free(path);
	}
	return match_len;
}

int has_dirs_only_path(const char *name, int len, int prefix_len)
{
	return threaded_has_dirs_only_path(&default_cache, name, len, prefix_len);
}

/*
 * Return non-zero if all path components of 'name' exists as a
 * directory.  If prefix_len > 0, we will test with the stat()
 * function instead of the lstat() function for a prefix length of
 * 'prefix_len', thus we then allow for symlinks in the prefix part as
 * long as those points to real existing directories.
 */
static int threaded_has_dirs_only_path(struct cache_def *cache, const char *name, int len, int prefix_len)
{
	/*
	 * Note: this function is used by the checkout machinery, which also
	 * takes care to properly reset the cache when it performs an operation
	 * that would leave the cache outdated. If this function starts caching
	 * anything else besides FL_DIR, remember to also invalidate the cache
	 * when creating or deleting paths that might be in the cache.
	 */
	return lstat_cache(cache, name, len,
			   FL_DIR|FL_FULLPATH, prefix_len) &
		FL_DIR;
}

static struct strbuf removal = STRBUF_INIT;

static void do_remove_scheduled_dirs(int new_len)
{
	while (removal.len > new_len) {
		removal.buf[removal.len] = '\0';
		if (rmdir(removal.buf))
			break;
		do {
			removal.len--;
		} while (removal.len > new_len &&
			 removal.buf[removal.len] != '/');
	}
	removal.len = new_len;
}

void schedule_dir_for_removal(const char *name, int len)
{
	int match_len, last_slash, i, previous_slash;

	match_len = last_slash = i =
		longest_path_match(name, len, removal.buf, removal.len,
				   &previous_slash);
	/* Find last slash inside 'name' */
	while (i < len) {
		if (name[i] == '/')
			last_slash = i;
		i++;
	}

	/*
	 * If we are about to go down the directory tree, we check if
	 * we must first go upwards the tree, such that we then can
	 * remove possible empty directories as we go upwards.
	 */
	if (match_len < last_slash && match_len < removal.len)
		do_remove_scheduled_dirs(match_len);
	/*
	 * If we go deeper down the directory tree, we only need to
	 * save the new path components as we go down.
	 */
	if (match_len < last_slash)
		strbuf_add(&removal, &name[match_len], last_slash - match_len);
}

void remove_scheduled_dirs(void)
{
	do_remove_scheduled_dirs(0);
}

void invalidate_lstat_cache(void)
{
	reset_lstat_cache(&default_cache);
}

#undef rmdir
int lstat_cache_aware_rmdir(const char *path)
{
	/* Any change in this function must be made also in `mingw_rmdir()` */
	int ret = rmdir(path);

	if (!ret)
		invalidate_lstat_cache();

	return ret;
}
