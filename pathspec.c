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

/*
 * Magic pathspec
 *
 * NEEDSWORK: These need to be moved to dir.h or even to a new
 * pathspec.h when we restructure get_pathspec() users to use the
 * "struct pathspec" interface.
 *
 * Possible future magic semantics include stuff like:
 *
 *	{ PATHSPEC_NOGLOB, '!', "noglob" },
 *	{ PATHSPEC_ICASE, '\0', "icase" },
 *	{ PATHSPEC_RECURSIVE, '*', "recursive" },
 *	{ PATHSPEC_REGEXP, '\0', "regexp" },
 *
 */
#define PATHSPEC_FROMTOP    (1<<0)

static struct pathspec_magic {
	unsigned bit;
	char mnemonic; /* this cannot be ':'! */
	const char *name;
} pathspec_magic[] = {
	{ PATHSPEC_FROMTOP, '/', "top" },
};

/*
 * Take an element of a pathspec and check for magic signatures.
 * Append the result to the prefix.
 *
 * For now, we only parse the syntax and throw out anything other than
 * "top" magic.
 *
 * NEEDSWORK: This needs to be rewritten when we start migrating
 * get_pathspec() users to use the "struct pathspec" interface.  For
 * example, a pathspec element may be marked as case-insensitive, but
 * the prefix part must always match literally, and a single stupid
 * string cannot express such a case.
 */
static const char *prefix_pathspec(const char *prefix, int prefixlen, const char *elt)
{
	unsigned magic = 0;
	const char *copyfrom = elt;
	int i;

	if (elt[0] != ':') {
		; /* nothing to do */
	} else if (elt[1] == '(') {
		/* longhand */
		const char *nextat;
		for (copyfrom = elt + 2;
		     *copyfrom && *copyfrom != ')';
		     copyfrom = nextat) {
			size_t len = strcspn(copyfrom, ",)");
			if (copyfrom[len] == ',')
				nextat = copyfrom + len + 1;
			else
				/* handle ')' and '\0' */
				nextat = copyfrom + len;
			if (!len)
				continue;
			for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++)
				if (strlen(pathspec_magic[i].name) == len &&
				    !strncmp(pathspec_magic[i].name, copyfrom, len)) {
					magic |= pathspec_magic[i].bit;
					break;
				}
			if (ARRAY_SIZE(pathspec_magic) <= i)
				die(_("Invalid pathspec magic '%.*s' in '%s'"),
				    (int) len, copyfrom, elt);
		}
		if (*copyfrom != ')')
			die(_("Missing ')' at the end of pathspec magic in '%s'"), elt);
		copyfrom++;
	} else {
		/* shorthand */
		for (copyfrom = elt + 1;
		     *copyfrom && *copyfrom != ':';
		     copyfrom++) {
			char ch = *copyfrom;

			if (!is_pathspec_magic(ch))
				break;
			for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++)
				if (pathspec_magic[i].mnemonic == ch) {
					magic |= pathspec_magic[i].bit;
					break;
				}
			if (ARRAY_SIZE(pathspec_magic) <= i)
				die(_("Unimplemented pathspec magic '%c' in '%s'"),
				    ch, elt);
		}
		if (*copyfrom == ':')
			copyfrom++;
	}

	if (magic & PATHSPEC_FROMTOP)
		return xstrdup(copyfrom);
	else
		return prefix_path(prefix, prefixlen, copyfrom);
}

/*
 * N.B. get_pathspec() is deprecated in favor of the "struct pathspec"
 * based interface - see pathspec_magic above.
 *
 * Arguments:
 *  - prefix - a path relative to the root of the working tree
 *  - pathspec - a list of paths underneath the prefix path
 *
 * Iterates over pathspec, prepending each path with prefix,
 * and return the resulting list.
 *
 * If pathspec is empty, return a singleton list containing prefix.
 *
 * If pathspec and prefix are both empty, return an empty list.
 *
 * This is typically used by built-in commands such as add.c, in order
 * to normalize argv arguments provided to the built-in into a list of
 * paths to process, all relative to the root of the working tree.
 */
const char **get_pathspec(const char *prefix, const char **pathspec)
{
	const char *entry = *pathspec;
	const char **src, **dst;
	int prefixlen;

	if (!prefix && !entry)
		return NULL;

	if (!entry) {
		static const char *spec[2];
		spec[0] = prefix;
		spec[1] = NULL;
		return spec;
	}

	/* Otherwise we have to re-write the entries.. */
	src = pathspec;
	dst = pathspec;
	prefixlen = prefix ? strlen(prefix) : 0;
	while (*src) {
		*(dst++) = prefix_pathspec(prefix, prefixlen, *src);
		src++;
	}
	*dst = NULL;
	if (!*pathspec)
		return NULL;
	return pathspec;
}

void copy_pathspec(struct pathspec *dst, const struct pathspec *src)
{
	*dst = *src;
	dst->items = xmalloc(sizeof(struct pathspec_item) * dst->nr);
	memcpy(dst->items, src->items,
	       sizeof(struct pathspec_item) * dst->nr);
}
