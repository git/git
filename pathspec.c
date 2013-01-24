#include "cache.h"
#include "dir.h"
#include "pathspec.h"

/*
 * Finds which of the given pathspecs match items in the index.
 *
 * For each pathspec, sets the corresponding entry in the seen[] array
 * (which should be specs items long, i.e. the same size as pathspec)
 * to the nature of the "closest" (i.e. most specific) match found for
 * that pathspec in the index, if it was a closer type of match than
 * the existing entry.  As an optimization, matching is skipped
 * altogether if seen[] already only contains non-zero entries.
 *
 * If seen[] has not already been written to, it may make sense
 * to use find_pathspecs_matching_against_index() instead.
 */
void add_pathspec_matches_against_index(const char **pathspec,
					char *seen, int specs)
{
	int num_unmatched = 0, i;

	/*
	 * Since we are walking the index as if we were walking the directory,
	 * we have to mark the matched pathspec as seen; otherwise we will
	 * mistakenly think that the user gave a pathspec that did not match
	 * anything.
	 */
	for (i = 0; i < specs; i++)
		if (!seen[i])
			num_unmatched++;
	if (!num_unmatched)
		return;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, seen);
	}
}

/*
 * Finds which of the given pathspecs match items in the index.
 *
 * This is a one-shot wrapper around add_pathspec_matches_against_index()
 * which allocates, populates, and returns a seen[] array indicating the
 * nature of the "closest" (i.e. most specific) matches which each of the
 * given pathspecs achieves against all items in the index.
 */
char *find_pathspecs_matching_against_index(const char **pathspec)
{
	char *seen;
	int i;

	for (i = 0; pathspec[i];  i++)
		; /* just counting */
	seen = xcalloc(i, 1);
	add_pathspec_matches_against_index(pathspec, seen, i);
	return seen;
}

/*
 * Check the index to see whether path refers to a submodule, or
 * something inside a submodule.  If the former, returns the path with
 * any trailing slash stripped.  If the latter, dies with an error
 * message.
 */
const char *check_path_for_gitlink(const char *path)
{
	int i, path_len = strlen(path);
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (S_ISGITLINK(ce->ce_mode)) {
			int ce_len = ce_namelen(ce);
			if (path_len <= ce_len || path[ce_len] != '/' ||
			    memcmp(ce->name, path, ce_len))
				/* path does not refer to this
				 * submodule or anything inside it */
				continue;
			if (path_len == ce_len + 1) {
				/* path refers to submodule;
				 * strip trailing slash */
				return xstrndup(ce->name, ce_len);
			} else {
				die (_("Path '%s' is in submodule '%.*s'"),
				     path, ce_len, ce->name);
			}
		}
	}
	return path;
}

/*
 * Dies if the given path refers to a file inside a symlinked
 * directory in the index.
 */
void die_if_path_beyond_symlink(const char *path, const char *prefix)
{
	if (has_symlink_leading_path(path, strlen(path))) {
		int len = prefix ? strlen(prefix) : 0;
		die(_("'%s' is beyond a symbolic link"), path + len);
	}
}
