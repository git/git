#include "cache.h"

static struct cache_def {
	char path[PATH_MAX + 1];
	int len;
	int flags;
	int track_flags;
	int prefix_len_stat_func;
} cache;

/*
 * Returns the length (on a path component basis) of the longest
 * common prefix match of 'name' and the cached path string.
 */
static inline int longest_match_lstat_cache(int len, const char *name)
{
	int max_len, match_len = 0, i = 0;

	max_len = len < cache.len ? len : cache.len;
	while (i < max_len && name[i] == cache.path[i]) {
		if (name[i] == '/')
			match_len = i;
		i++;
	}
	/* Is the cached path string a substring of 'name'? */
	if (i == cache.len && cache.len < len && name[cache.len] == '/')
		match_len = cache.len;
	/* Is 'name' a substring of the cached path string? */
	else if ((i == len && len < cache.len && cache.path[len] == '/') ||
		 (i == len && len == cache.len))
		match_len = len;
	return match_len;
}

static inline void reset_lstat_cache(int track_flags, int prefix_len_stat_func)
{
	cache.path[0] = '\0';
	cache.len = 0;
	cache.flags = 0;
	cache.track_flags = track_flags;
	cache.prefix_len_stat_func = prefix_len_stat_func;
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
static int lstat_cache(int len, const char *name,
		       int track_flags, int prefix_len_stat_func)
{
	int match_len, last_slash, last_slash_dir;
	int match_flags, ret_flags, save_flags, max_len, ret;
	struct stat st;

	if (cache.track_flags != track_flags ||
	    cache.prefix_len_stat_func != prefix_len_stat_func) {
		/*
		 * As a safeguard we clear the cache if the values of
		 * track_flags and/or prefix_len_stat_func does not
		 * match with the last supplied values.
		 */
		reset_lstat_cache(track_flags, prefix_len_stat_func);
		match_len = last_slash = 0;
	} else {
		/*
		 * Check to see if we have a match from the cache for
		 * the 2 "excluding" path types.
		 */
		match_len = last_slash = longest_match_lstat_cache(len, name);
		match_flags = cache.flags & track_flags & (FL_NOENT|FL_SYMLINK);
		if (match_flags && match_len == cache.len)
			return match_flags;
		/*
		 * If we now have match_len > 0, we would know that
		 * the matched part will always be a directory.
		 *
		 * Also, if we are tracking directories and 'name' is
		 * a substring of the cache on a path component basis,
		 * we can return immediately.
		 */
		match_flags = track_flags & FL_DIR;
		if (match_flags && len == match_len)
			return match_flags;
	}

	/*
	 * Okay, no match from the cache so far, so now we have to
	 * check the rest of the path components.
	 */
	ret_flags = FL_DIR;
	last_slash_dir = last_slash;
	max_len = len < PATH_MAX ? len : PATH_MAX;
	while (match_len < max_len) {
		do {
			cache.path[match_len] = name[match_len];
			match_len++;
		} while (match_len < max_len && name[match_len] != '/');
		if (match_len >= max_len && !(track_flags & FL_FULLPATH))
			break;
		last_slash = match_len;
		cache.path[last_slash] = '\0';

		if (last_slash <= prefix_len_stat_func)
			ret = stat(cache.path, &st);
		else
			ret = lstat(cache.path, &st);

		if (ret) {
			ret_flags = FL_LSTATERR;
			if (errno == ENOENT)
				ret_flags |= FL_NOENT;
		} else if (S_ISDIR(st.st_mode)) {
			last_slash_dir = last_slash;
			continue;
		} else if (S_ISLNK(st.st_mode)) {
			ret_flags = FL_SYMLINK;
		} else {
			ret_flags = FL_ERR;
		}
		break;
	}

	/*
	 * At the end update the cache.  Note that max 3 different
	 * path types, FL_NOENT, FL_SYMLINK and FL_DIR, can be cached
	 * for the moment!
	 */
	save_flags = ret_flags & track_flags & (FL_NOENT|FL_SYMLINK);
	if (save_flags && last_slash > 0 && last_slash <= PATH_MAX) {
		cache.path[last_slash] = '\0';
		cache.len = last_slash;
		cache.flags = save_flags;
	} else if (track_flags & FL_DIR &&
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
		cache.path[last_slash_dir] = '\0';
		cache.len = last_slash_dir;
		cache.flags = FL_DIR;
	} else {
		reset_lstat_cache(track_flags, prefix_len_stat_func);
	}
	return ret_flags;
}

#define USE_ONLY_LSTAT  0

/*
 * Return non-zero if path 'name' has a leading symlink component
 */
int has_symlink_leading_path(int len, const char *name)
{
	return lstat_cache(len, name,
			   FL_SYMLINK|FL_DIR, USE_ONLY_LSTAT) &
		FL_SYMLINK;
}

/*
 * Return non-zero if path 'name' has a leading symlink component or
 * if some leading path component does not exists.
 */
int has_symlink_or_noent_leading_path(int len, const char *name)
{
	return lstat_cache(len, name,
			   FL_SYMLINK|FL_NOENT|FL_DIR, USE_ONLY_LSTAT) &
		(FL_SYMLINK|FL_NOENT);
}

/*
 * Return non-zero if all path components of 'name' exists as a
 * directory.  If prefix_len > 0, we will test with the stat()
 * function instead of the lstat() function for a prefix length of
 * 'prefix_len', thus we then allow for symlinks in the prefix part as
 * long as those points to real existing directories.
 */
int has_dirs_only_path(int len, const char *name, int prefix_len)
{
	return lstat_cache(len, name,
			   FL_DIR|FL_FULLPATH, prefix_len) &
		FL_DIR;
}
