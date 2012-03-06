#include "cache.h"

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

static struct cache_def default_cache;

static inline void reset_lstat_cache(struct cache_def *cache)
{
	cache->path[0] = '\0';
	cache->len = 0;
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
	int save_flags, max_len, ret;
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
			longest_path_match(name, len, cache->path, cache->len,
					   &previous_slash);
		*ret_flags = cache->flags & track_flags & (FL_NOENT|FL_SYMLINK);

		if (!(track_flags & FL_FULLPATH) && match_len == len)
			match_len = last_slash = previous_slash;

		if (*ret_flags && match_len == cache->len)
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
	max_len = len < PATH_MAX ? len : PATH_MAX;
	while (match_len < max_len) {
		do {
			cache->path[match_len] = name[match_len];
			match_len++;
		} while (match_len < max_len && name[match_len] != '/');
		if (match_len >= max_len && !(track_flags & FL_FULLPATH))
			break;
		last_slash = match_len;
		cache->path[last_slash] = '\0';

		if (last_slash <= prefix_len_stat_func)
			ret = stat(cache->path, &st);
		else
			ret = lstat(cache->path, &st);

		if (ret) {
			*ret_flags = FL_LSTATERR;
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
	if (save_flags && last_slash > 0 && last_slash <= PATH_MAX) {
		cache->path[last_slash] = '\0';
		cache->len = last_slash;
		cache->flags = save_flags;
	} else if ((track_flags & FL_DIR) &&
		   last_slash_dir > 0 && last_slash_dir <= PATH_MAX) {
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
		cache->path[last_slash_dir] = '\0';
		cache->len = last_slash_dir;
		cache->flags = FL_DIR;
	} else {
		reset_lstat_cache(cache);
	}
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

/*
 * Return non-zero if path 'name' has a leading symlink component
 */
int has_symlink_leading_path(const char *name, int len)
{
	return threaded_has_symlink_leading_path(&default_cache, name, len);
}

/*
 * Return zero if path 'name' has a leading symlink component or
 * if some leading path component does not exists.
 *
 * Return -1 if leading path exists and is a directory.
 *
 * Return path length if leading path exists and is neither a
 * directory nor a symlink.
 */
int check_leading_path(const char *name, int len)
{
    return threaded_check_leading_path(&default_cache, name, len);
}

/*
 * Return zero if path 'name' has a leading symlink component or
 * if some leading path component does not exists.
 *
 * Return -1 if leading path exists and is a directory.
 *
 * Return path length if leading path exists and is neither a
 * directory nor a symlink.
 */
int threaded_check_leading_path(struct cache_def *cache, const char *name, int len)
{
	int flags;
	int match_len = lstat_cache_matchlen(cache, name, len, &flags,
			   FL_SYMLINK|FL_NOENT|FL_DIR, USE_ONLY_LSTAT);
	if (flags & FL_NOENT)
		return 0;
	else if (flags & FL_DIR)
		return -1;
	else
		return match_len;
}

/*
 * Return non-zero if all path components of 'name' exists as a
 * directory.  If prefix_len > 0, we will test with the stat()
 * function instead of the lstat() function for a prefix length of
 * 'prefix_len', thus we then allow for symlinks in the prefix part as
 * long as those points to real existing directories.
 */
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
int threaded_has_dirs_only_path(struct cache_def *cache, const char *name, int len, int prefix_len)
{
	return lstat_cache(cache, name, len,
			   FL_DIR|FL_FULLPATH, prefix_len) &
		FL_DIR;
}

static struct removal_def {
	char path[PATH_MAX];
	int len;
} removal;

static void do_remove_scheduled_dirs(int new_len)
{
	while (removal.len > new_len) {
		removal.path[removal.len] = '\0';
		if (rmdir(removal.path))
			break;
		do {
			removal.len--;
		} while (removal.len > new_len &&
			 removal.path[removal.len] != '/');
	}
	removal.len = new_len;
}

void schedule_dir_for_removal(const char *name, int len)
{
	int match_len, last_slash, i, previous_slash;

	match_len = last_slash = i =
		longest_path_match(name, len, removal.path, removal.len,
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
	if (match_len < last_slash) {
		memcpy(&removal.path[match_len], &name[match_len],
		       last_slash - match_len);
		removal.len = last_slash;
	}
}

void remove_scheduled_dirs(void)
{
	do_remove_scheduled_dirs(0);
}
