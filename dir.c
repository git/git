/*
 * This handles recursive filename detection with exclude
 * files, index knowledge etc..
 *
 * Copyright (C) Linus Torvalds, 2005-2006
 *		 Junio Hamano, 2005-2006
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "abspath.h"
#include "config.h"
#include "convert.h"
#include "dir.h"
#include "environment.h"
#include "gettext.h"
#include "name-hash.h"
#include "object-file.h"
#include "object-store-ll.h"
#include "path.h"
#include "refs.h"
#include "wildmatch.h"
#include "pathspec.h"
#include "utf8.h"
#include "varint.h"
#include "ewah/ewok.h"
#include "fsmonitor-ll.h"
#include "read-cache-ll.h"
#include "setup.h"
#include "sparse-index.h"
#include "submodule-config.h"
#include "symlinks.h"
#include "trace2.h"
#include "tree.h"
#include "hex.h"

 /*
  * The maximum size of a pattern/exclude file. If the file exceeds this size
  * we will ignore it.
  */
#define PATTERN_MAX_FILE_SIZE (100 * 1024 * 1024)

/*
 * Tells read_directory_recursive how a file or directory should be treated.
 * Values are ordered by significance, e.g. if a directory contains both
 * excluded and untracked files, it is listed as untracked because
 * path_untracked > path_excluded.
 */
enum path_treatment {
	path_none = 0,
	path_recurse,
	path_excluded,
	path_untracked
};

/*
 * Support data structure for our opendir/readdir/closedir wrappers
 */
struct cached_dir {
	DIR *fdir;
	struct untracked_cache_dir *untracked;
	int nr_files;
	int nr_dirs;

	const char *d_name;
	int d_type;
	const char *file;
	struct untracked_cache_dir *ucd;
};

static enum path_treatment read_directory_recursive(struct dir_struct *dir,
	struct index_state *istate, const char *path, int len,
	struct untracked_cache_dir *untracked,
	int check_only, int stop_at_first_file, const struct pathspec *pathspec);
static int resolve_dtype(int dtype, struct index_state *istate,
			 const char *path, int len);
struct dirent *readdir_skip_dot_and_dotdot(DIR *dirp)
{
	struct dirent *e;

	while ((e = readdir(dirp)) != NULL) {
		if (!is_dot_or_dotdot(e->d_name))
			break;
	}
	return e;
}

int count_slashes(const char *s)
{
	int cnt = 0;
	while (*s)
		if (*s++ == '/')
			cnt++;
	return cnt;
}

int git_fspathcmp(const char *a, const char *b)
{
	return ignore_case ? strcasecmp(a, b) : strcmp(a, b);
}

int fspatheq(const char *a, const char *b)
{
	return !fspathcmp(a, b);
}

int git_fspathncmp(const char *a, const char *b, size_t count)
{
	return ignore_case ? strncasecmp(a, b, count) : strncmp(a, b, count);
}

int paths_collide(const char *a, const char *b)
{
	size_t len_a = strlen(a), len_b = strlen(b);

	if (len_a == len_b)
		return fspatheq(a, b);

	if (len_a < len_b)
		return is_dir_sep(b[len_a]) && !fspathncmp(a, b, len_a);
	return is_dir_sep(a[len_b]) && !fspathncmp(a, b, len_b);
}

unsigned int fspathhash(const char *str)
{
	return ignore_case ? strihash(str) : strhash(str);
}

int git_fnmatch(const struct pathspec_item *item,
		const char *pattern, const char *string,
		int prefix)
{
	if (prefix > 0) {
		if (ps_strncmp(item, pattern, string, prefix))
			return WM_NOMATCH;
		pattern += prefix;
		string += prefix;
	}
	if (item->flags & PATHSPEC_ONESTAR) {
		int pattern_len = strlen(++pattern);
		int string_len = strlen(string);
		return string_len < pattern_len ||
			ps_strcmp(item, pattern,
				  string + string_len - pattern_len);
	}
	if (item->magic & PATHSPEC_GLOB)
		return wildmatch(pattern, string,
				 WM_PATHNAME |
				 (item->magic & PATHSPEC_ICASE ? WM_CASEFOLD : 0));
	else
		/* wildmatch has not learned no FNM_PATHNAME mode yet */
		return wildmatch(pattern, string,
				 item->magic & PATHSPEC_ICASE ? WM_CASEFOLD : 0);
}

static int fnmatch_icase_mem(const char *pattern, int patternlen,
			     const char *string, int stringlen,
			     int flags)
{
	int match_status;
	struct strbuf pat_buf = STRBUF_INIT;
	struct strbuf str_buf = STRBUF_INIT;
	const char *use_pat = pattern;
	const char *use_str = string;

	if (pattern[patternlen]) {
		strbuf_add(&pat_buf, pattern, patternlen);
		use_pat = pat_buf.buf;
	}
	if (string[stringlen]) {
		strbuf_add(&str_buf, string, stringlen);
		use_str = str_buf.buf;
	}

	if (ignore_case)
		flags |= WM_CASEFOLD;
	match_status = wildmatch(use_pat, use_str, flags);

	strbuf_release(&pat_buf);
	strbuf_release(&str_buf);

	return match_status;
}

static size_t common_prefix_len(const struct pathspec *pathspec)
{
	int n;
	size_t max = 0;

	/*
	 * ":(icase)path" is treated as a pathspec full of
	 * wildcard. In other words, only prefix is considered common
	 * prefix. If the pathspec is abc/foo abc/bar, running in
	 * subdir xyz, the common prefix is still xyz, not xyz/abc as
	 * in non-:(icase).
	 */
	GUARD_PATHSPEC(pathspec,
		       PATHSPEC_FROMTOP |
		       PATHSPEC_MAXDEPTH |
		       PATHSPEC_LITERAL |
		       PATHSPEC_GLOB |
		       PATHSPEC_ICASE |
		       PATHSPEC_EXCLUDE |
		       PATHSPEC_ATTR);

	for (n = 0; n < pathspec->nr; n++) {
		size_t i = 0, len = 0, item_len;
		if (pathspec->items[n].magic & PATHSPEC_EXCLUDE)
			continue;
		if (pathspec->items[n].magic & PATHSPEC_ICASE)
			item_len = pathspec->items[n].prefix;
		else
			item_len = pathspec->items[n].nowildcard_len;
		while (i < item_len && (n == 0 || i < max)) {
			char c = pathspec->items[n].match[i];
			if (c != pathspec->items[0].match[i])
				break;
			if (c == '/')
				len = i + 1;
			i++;
		}
		if (n == 0 || len < max) {
			max = len;
			if (!max)
				break;
		}
	}
	return max;
}

/*
 * Returns a copy of the longest leading path common among all
 * pathspecs.
 */
char *common_prefix(const struct pathspec *pathspec)
{
	unsigned long len = common_prefix_len(pathspec);

	return len ? xmemdupz(pathspec->items[0].match, len) : NULL;
}

int fill_directory(struct dir_struct *dir,
		   struct index_state *istate,
		   const struct pathspec *pathspec)
{
	const char *prefix;
	size_t prefix_len;

	unsigned exclusive_flags = DIR_SHOW_IGNORED | DIR_SHOW_IGNORED_TOO;
	if ((dir->flags & exclusive_flags) == exclusive_flags)
		BUG("DIR_SHOW_IGNORED and DIR_SHOW_IGNORED_TOO are exclusive");

	/*
	 * Calculate common prefix for the pathspec, and
	 * use that to optimize the directory walk
	 */
	prefix_len = common_prefix_len(pathspec);
	prefix = prefix_len ? pathspec->items[0].match : "";

	/* Read the directory and prune it */
	read_directory(dir, istate, prefix, prefix_len, pathspec);

	return prefix_len;
}

int within_depth(const char *name, int namelen,
			int depth, int max_depth)
{
	const char *cp = name, *cpe = name + namelen;

	while (cp < cpe) {
		if (*cp++ != '/')
			continue;
		depth++;
		if (depth > max_depth)
			return 0;
	}
	return 1;
}

/*
 * Read the contents of the blob with the given OID into a buffer.
 * Append a trailing LF to the end if the last line doesn't have one.
 *
 * Returns:
 *    -1 when the OID is invalid or unknown or does not refer to a blob.
 *     0 when the blob is empty.
 *     1 along with { data, size } of the (possibly augmented) buffer
 *       when successful.
 *
 * Optionally updates the given oid_stat with the given OID (when valid).
 */
static int do_read_blob(const struct object_id *oid, struct oid_stat *oid_stat,
			size_t *size_out, char **data_out)
{
	enum object_type type;
	unsigned long sz;
	char *data;

	*size_out = 0;
	*data_out = NULL;

	data = repo_read_object_file(the_repository, oid, &type, &sz);
	if (!data || type != OBJ_BLOB) {
		free(data);
		return -1;
	}

	if (oid_stat) {
		memset(&oid_stat->stat, 0, sizeof(oid_stat->stat));
		oidcpy(&oid_stat->oid, oid);
	}

	if (sz == 0) {
		free(data);
		return 0;
	}

	if (data[sz - 1] != '\n') {
		data = xrealloc(data, st_add(sz, 1));
		data[sz++] = '\n';
	}

	*size_out = xsize_t(sz);
	*data_out = data;

	return 1;
}

#define DO_MATCH_EXCLUDE   (1<<0)
#define DO_MATCH_DIRECTORY (1<<1)
#define DO_MATCH_LEADING_PATHSPEC (1<<2)

/*
 * Does the given pathspec match the given name?  A match is found if
 *
 * (1) the pathspec string is leading directory of 'name' ("RECURSIVELY"), or
 * (2) the pathspec string has a leading part matching 'name' ("LEADING"), or
 * (3) the pathspec string is a wildcard and matches 'name' ("WILDCARD"), or
 * (4) the pathspec string is exactly the same as 'name' ("EXACT").
 *
 * Return value tells which case it was (1-4), or 0 when there is no match.
 *
 * It may be instructive to look at a small table of concrete examples
 * to understand the differences between 1, 2, and 4:
 *
 *                              Pathspecs
 *                |    a/b    |   a/b/    |   a/b/c
 *          ------+-----------+-----------+------------
 *          a/b   |  EXACT    |  EXACT[1] | LEADING[2]
 *  Names   a/b/  | RECURSIVE |   EXACT   | LEADING[2]
 *          a/b/c | RECURSIVE | RECURSIVE |   EXACT
 *
 * [1] Only if DO_MATCH_DIRECTORY is passed; otherwise, this is NOT a match.
 * [2] Only if DO_MATCH_LEADING_PATHSPEC is passed; otherwise, not a match.
 */
static int match_pathspec_item(struct index_state *istate,
			       const struct pathspec_item *item, int prefix,
			       const char *name, int namelen, unsigned flags)
{
	/* name/namelen has prefix cut off by caller */
	const char *match = item->match + prefix;
	int matchlen = item->len - prefix;

	/*
	 * The normal call pattern is:
	 * 1. prefix = common_prefix_len(ps);
	 * 2. prune something, or fill_directory
	 * 3. match_pathspec()
	 *
	 * 'prefix' at #1 may be shorter than the command's prefix and
	 * it's ok for #2 to match extra files. Those extras will be
	 * trimmed at #3.
	 *
	 * Suppose the pathspec is 'foo' and '../bar' running from
	 * subdir 'xyz'. The common prefix at #1 will be empty, thanks
	 * to "../". We may have xyz/foo _and_ XYZ/foo after #2. The
	 * user does not want XYZ/foo, only the "foo" part should be
	 * case-insensitive. We need to filter out XYZ/foo here. In
	 * other words, we do not trust the caller on comparing the
	 * prefix part when :(icase) is involved. We do exact
	 * comparison ourselves.
	 *
	 * Normally the caller (common_prefix_len() in fact) does
	 * _exact_ matching on name[-prefix+1..-1] and we do not need
	 * to check that part. Be defensive and check it anyway, in
	 * case common_prefix_len is changed, or a new caller is
	 * introduced that does not use common_prefix_len.
	 *
	 * If the penalty turns out too high when prefix is really
	 * long, maybe change it to
	 * strncmp(match, name, item->prefix - prefix)
	 */
	if (item->prefix && (item->magic & PATHSPEC_ICASE) &&
	    strncmp(item->match, name - prefix, item->prefix))
		return 0;

	if (item->attr_match_nr &&
	    !match_pathspec_attrs(istate, name - prefix, namelen + prefix, item))
		return 0;

	/* If the match was just the prefix, we matched */
	if (!*match)
		return MATCHED_RECURSIVELY;

	if (matchlen <= namelen && !ps_strncmp(item, match, name, matchlen)) {
		if (matchlen == namelen)
			return MATCHED_EXACTLY;

		if (match[matchlen-1] == '/' || name[matchlen] == '/')
			return MATCHED_RECURSIVELY;
	} else if ((flags & DO_MATCH_DIRECTORY) &&
		   match[matchlen - 1] == '/' &&
		   namelen == matchlen - 1 &&
		   !ps_strncmp(item, match, name, namelen))
		return MATCHED_EXACTLY;

	if (item->nowildcard_len < item->len &&
	    !git_fnmatch(item, match, name,
			 item->nowildcard_len - prefix))
		return MATCHED_FNMATCH;

	/* Perform checks to see if "name" is a leading string of the pathspec */
	if ( (flags & DO_MATCH_LEADING_PATHSPEC) &&
	    !(flags & DO_MATCH_EXCLUDE)) {
		/* name is a literal prefix of the pathspec */
		int offset = name[namelen-1] == '/' ? 1 : 0;
		if ((namelen < matchlen) &&
		    (match[namelen-offset] == '/') &&
		    !ps_strncmp(item, match, name, namelen))
			return MATCHED_RECURSIVELY_LEADING_PATHSPEC;

		/* name doesn't match up to the first wild character */
		if (item->nowildcard_len < item->len &&
		    ps_strncmp(item, match, name,
			       item->nowildcard_len - prefix))
			return 0;

		/*
		 * name has no wildcard, and it didn't match as a leading
		 * pathspec so return.
		 */
		if (item->nowildcard_len == item->len)
			return 0;

		/*
		 * Here is where we would perform a wildmatch to check if
		 * "name" can be matched as a directory (or a prefix) against
		 * the pathspec.  Since wildmatch doesn't have this capability
		 * at the present we have to punt and say that it is a match,
		 * potentially returning a false positive
		 * The submodules themselves will be able to perform more
		 * accurate matching to determine if the pathspec matches.
		 */
		return MATCHED_RECURSIVELY_LEADING_PATHSPEC;
	}

	return 0;
}

/*
 * do_match_pathspec() is meant to ONLY be called by
 * match_pathspec_with_flags(); calling it directly risks pathspecs
 * like ':!unwanted_path' being ignored.
 *
 * Given a name and a list of pathspecs, returns the nature of the
 * closest (i.e. most specific) match of the name to any of the
 * pathspecs.
 *
 * The caller typically calls this multiple times with the same
 * pathspec and seen[] array but with different name/namelen
 * (e.g. entries from the index) and is interested in seeing if and
 * how each pathspec matches all the names it calls this function
 * with.  A mark is left in the seen[] array for each pathspec element
 * indicating the closest type of match that element achieved, so if
 * seen[n] remains zero after multiple invocations, that means the nth
 * pathspec did not match any names, which could indicate that the
 * user mistyped the nth pathspec.
 */
static int do_match_pathspec(struct index_state *istate,
			     const struct pathspec *ps,
			     const char *name, int namelen,
			     int prefix, char *seen,
			     unsigned flags)
{
	int i, retval = 0, exclude = flags & DO_MATCH_EXCLUDE;

	GUARD_PATHSPEC(ps,
		       PATHSPEC_FROMTOP |
		       PATHSPEC_MAXDEPTH |
		       PATHSPEC_LITERAL |
		       PATHSPEC_GLOB |
		       PATHSPEC_ICASE |
		       PATHSPEC_EXCLUDE |
		       PATHSPEC_ATTR);

	if (!ps->nr) {
		if (!ps->recursive ||
		    !(ps->magic & PATHSPEC_MAXDEPTH) ||
		    ps->max_depth == -1)
			return MATCHED_RECURSIVELY;

		if (within_depth(name, namelen, 0, ps->max_depth))
			return MATCHED_EXACTLY;
		else
			return 0;
	}

	name += prefix;
	namelen -= prefix;

	for (i = ps->nr - 1; i >= 0; i--) {
		int how;

		if ((!exclude &&   ps->items[i].magic & PATHSPEC_EXCLUDE) ||
		    ( exclude && !(ps->items[i].magic & PATHSPEC_EXCLUDE)))
			continue;

		if (seen && seen[i] == MATCHED_EXACTLY)
			continue;
		/*
		 * Make exclude patterns optional and never report
		 * "pathspec ':(exclude)foo' matches no files"
		 */
		if (seen && ps->items[i].magic & PATHSPEC_EXCLUDE)
			seen[i] = MATCHED_FNMATCH;
		how = match_pathspec_item(istate, ps->items+i, prefix, name,
					  namelen, flags);
		if (ps->recursive &&
		    (ps->magic & PATHSPEC_MAXDEPTH) &&
		    ps->max_depth != -1 &&
		    how && how != MATCHED_FNMATCH) {
			int len = ps->items[i].len;
			if (name[len] == '/')
				len++;
			if (within_depth(name+len, namelen-len, 0, ps->max_depth))
				how = MATCHED_EXACTLY;
			else
				how = 0;
		}
		if (how) {
			if (retval < how)
				retval = how;
			if (seen && seen[i] < how)
				seen[i] = how;
		}
	}
	return retval;
}

static int match_pathspec_with_flags(struct index_state *istate,
				     const struct pathspec *ps,
				     const char *name, int namelen,
				     int prefix, char *seen, unsigned flags)
{
	int positive, negative;
	positive = do_match_pathspec(istate, ps, name, namelen,
				     prefix, seen, flags);
	if (!(ps->magic & PATHSPEC_EXCLUDE) || !positive)
		return positive;
	negative = do_match_pathspec(istate, ps, name, namelen,
				     prefix, seen,
				     flags | DO_MATCH_EXCLUDE);
	return negative ? 0 : positive;
}

int match_pathspec(struct index_state *istate,
		   const struct pathspec *ps,
		   const char *name, int namelen,
		   int prefix, char *seen, int is_dir)
{
	unsigned flags = is_dir ? DO_MATCH_DIRECTORY : 0;
	return match_pathspec_with_flags(istate, ps, name, namelen,
					 prefix, seen, flags);
}

/**
 * Check if a submodule is a superset of the pathspec
 */
int submodule_path_match(struct index_state *istate,
			 const struct pathspec *ps,
			 const char *submodule_name,
			 char *seen)
{
	int matched = match_pathspec_with_flags(istate, ps, submodule_name,
						strlen(submodule_name),
						0, seen,
						DO_MATCH_DIRECTORY |
						DO_MATCH_LEADING_PATHSPEC);
	return matched;
}

int report_path_error(const char *ps_matched,
		      const struct pathspec *pathspec)
{
	/*
	 * Make sure all pathspec matched; otherwise it is an error.
	 */
	int num, errors = 0;
	for (num = 0; num < pathspec->nr; num++) {
		int other, found_dup;

		if (ps_matched[num])
			continue;
		/*
		 * The caller might have fed identical pathspec
		 * twice.  Do not barf on such a mistake.
		 * FIXME: parse_pathspec should have eliminated
		 * duplicate pathspec.
		 */
		for (found_dup = other = 0;
		     !found_dup && other < pathspec->nr;
		     other++) {
			if (other == num || !ps_matched[other])
				continue;
			if (!strcmp(pathspec->items[other].original,
				    pathspec->items[num].original))
				/*
				 * Ok, we have a match already.
				 */
				found_dup = 1;
		}
		if (found_dup)
			continue;

		error(_("pathspec '%s' did not match any file(s) known to git"),
		      pathspec->items[num].original);
		errors++;
	}
	return errors;
}

/*
 * Return the length of the "simple" part of a path match limiter.
 */
int simple_length(const char *match)
{
	int len = -1;

	for (;;) {
		unsigned char c = *match++;
		len++;
		if (c == '\0' || is_glob_special(c))
			return len;
	}
}

int no_wildcard(const char *string)
{
	return string[simple_length(string)] == '\0';
}

void parse_path_pattern(const char **pattern,
			   int *patternlen,
			   unsigned *flags,
			   int *nowildcardlen)
{
	const char *p = *pattern;
	size_t i, len;

	*flags = 0;
	if (*p == '!') {
		*flags |= PATTERN_FLAG_NEGATIVE;
		p++;
	}
	len = strlen(p);
	if (len && p[len - 1] == '/') {
		len--;
		*flags |= PATTERN_FLAG_MUSTBEDIR;
	}
	for (i = 0; i < len; i++) {
		if (p[i] == '/')
			break;
	}
	if (i == len)
		*flags |= PATTERN_FLAG_NODIR;
	*nowildcardlen = simple_length(p);
	/*
	 * we should have excluded the trailing slash from 'p' too,
	 * but that's one more allocation. Instead just make sure
	 * nowildcardlen does not exceed real patternlen
	 */
	if (*nowildcardlen > len)
		*nowildcardlen = len;
	if (*p == '*' && no_wildcard(p + 1))
		*flags |= PATTERN_FLAG_ENDSWITH;
	*pattern = p;
	*patternlen = len;
}

int pl_hashmap_cmp(const void *cmp_data UNUSED,
		   const struct hashmap_entry *a,
		   const struct hashmap_entry *b,
		   const void *key UNUSED)
{
	const struct pattern_entry *ee1 =
			container_of(a, struct pattern_entry, ent);
	const struct pattern_entry *ee2 =
			container_of(b, struct pattern_entry, ent);

	size_t min_len = ee1->patternlen <= ee2->patternlen
			 ? ee1->patternlen
			 : ee2->patternlen;

	return fspathncmp(ee1->pattern, ee2->pattern, min_len);
}

static char *dup_and_filter_pattern(const char *pattern)
{
	char *set, *read;
	size_t count  = 0;
	char *result = xstrdup(pattern);

	set = result;
	read = result;

	while (*read) {
		/* skip escape characters (once) */
		if (*read == '\\')
			read++;

		*set = *read;

		set++;
		read++;
		count++;
	}
	*set = 0;

	if (count > 2 &&
	    *(set - 1) == '*' &&
	    *(set - 2) == '/')
		*(set - 2) = 0;

	return result;
}

static void clear_pattern_entry_hashmap(struct hashmap *map)
{
	struct hashmap_iter iter;
	struct pattern_entry *entry;

	hashmap_for_each_entry(map, &iter, entry, ent) {
		free(entry->pattern);
	}
	hashmap_clear_and_free(map, struct pattern_entry, ent);
}

static void add_pattern_to_hashsets(struct pattern_list *pl, struct path_pattern *given)
{
	struct pattern_entry *translated;
	char *truncated;
	char *data = NULL;
	const char *prev, *cur, *next;

	if (!pl->use_cone_patterns)
		return;

	if (given->flags & PATTERN_FLAG_NEGATIVE &&
	    given->flags & PATTERN_FLAG_MUSTBEDIR &&
	    !strcmp(given->pattern, "/*")) {
		pl->full_cone = 0;
		return;
	}

	if (!given->flags && !strcmp(given->pattern, "/*")) {
		pl->full_cone = 1;
		return;
	}

	if (given->patternlen < 2 ||
	    *given->pattern != '/' ||
	    strstr(given->pattern, "**")) {
		/* Not a cone pattern. */
		warning(_("unrecognized pattern: '%s'"), given->pattern);
		goto clear_hashmaps;
	}

	if (!(given->flags & PATTERN_FLAG_MUSTBEDIR) &&
	    strcmp(given->pattern, "/*")) {
		/* Not a cone pattern. */
		warning(_("unrecognized pattern: '%s'"), given->pattern);
		goto clear_hashmaps;
	}

	prev = given->pattern;
	cur = given->pattern + 1;
	next = given->pattern + 2;

	while (*cur) {
		/* Watch for glob characters '*', '\', '[', '?' */
		if (!is_glob_special(*cur))
			goto increment;

		/* But only if *prev != '\\' */
		if (*prev == '\\')
			goto increment;

		/* But allow the initial '\' */
		if (*cur == '\\' &&
		    is_glob_special(*next))
			goto increment;

		/* But a trailing '/' then '*' is fine */
		if (*prev == '/' &&
		    *cur == '*' &&
		    *next == 0)
			goto increment;

		/* Not a cone pattern. */
		warning(_("unrecognized pattern: '%s'"), given->pattern);
		goto clear_hashmaps;

	increment:
		prev++;
		cur++;
		next++;
	}

	if (given->patternlen > 2 &&
	    !strcmp(given->pattern + given->patternlen - 2, "/*")) {
		struct pattern_entry *old;

		if (!(given->flags & PATTERN_FLAG_NEGATIVE)) {
			/* Not a cone pattern. */
			warning(_("unrecognized pattern: '%s'"), given->pattern);
			goto clear_hashmaps;
		}

		truncated = dup_and_filter_pattern(given->pattern);

		translated = xmalloc(sizeof(struct pattern_entry));
		translated->pattern = truncated;
		translated->patternlen = given->patternlen - 2;
		hashmap_entry_init(&translated->ent,
				   fspathhash(translated->pattern));

		if (!hashmap_get_entry(&pl->recursive_hashmap,
				       translated, ent, NULL)) {
			/* We did not see the "parent" included */
			warning(_("unrecognized negative pattern: '%s'"),
				given->pattern);
			free(truncated);
			free(translated);
			goto clear_hashmaps;
		}

		hashmap_add(&pl->parent_hashmap, &translated->ent);
		old = hashmap_remove_entry(&pl->recursive_hashmap, translated, ent, &data);
		if (old) {
			free(old->pattern);
			free(old);
		}
		free(data);
		return;
	}

	if (given->flags & PATTERN_FLAG_NEGATIVE) {
		warning(_("unrecognized negative pattern: '%s'"),
			given->pattern);
		goto clear_hashmaps;
	}

	translated = xmalloc(sizeof(struct pattern_entry));

	translated->pattern = dup_and_filter_pattern(given->pattern);
	translated->patternlen = given->patternlen;
	hashmap_entry_init(&translated->ent,
			   fspathhash(translated->pattern));

	hashmap_add(&pl->recursive_hashmap, &translated->ent);

	if (hashmap_get_entry(&pl->parent_hashmap, translated, ent, NULL)) {
		/* we already included this at the parent level */
		warning(_("your sparse-checkout file may have issues: pattern '%s' is repeated"),
			given->pattern);
		goto clear_hashmaps;
	}

	return;

clear_hashmaps:
	warning(_("disabling cone pattern matching"));
	clear_pattern_entry_hashmap(&pl->recursive_hashmap);
	clear_pattern_entry_hashmap(&pl->parent_hashmap);
	pl->use_cone_patterns = 0;
}

static int hashmap_contains_path(struct hashmap *map,
				 struct strbuf *pattern)
{
	struct pattern_entry p;

	/* Check straight mapping */
	p.pattern = pattern->buf;
	p.patternlen = pattern->len;
	hashmap_entry_init(&p.ent, fspathhash(p.pattern));
	return !!hashmap_get_entry(map, &p, ent, NULL);
}

int hashmap_contains_parent(struct hashmap *map,
			    const char *path,
			    struct strbuf *buffer)
{
	char *slash_pos;

	strbuf_setlen(buffer, 0);

	if (path[0] != '/')
		strbuf_addch(buffer, '/');

	strbuf_addstr(buffer, path);

	slash_pos = strrchr(buffer->buf, '/');

	while (slash_pos > buffer->buf) {
		strbuf_setlen(buffer, slash_pos - buffer->buf);

		if (hashmap_contains_path(map, buffer))
			return 1;

		slash_pos = strrchr(buffer->buf, '/');
	}

	return 0;
}

void add_pattern(const char *string, const char *base,
		 int baselen, struct pattern_list *pl, int srcpos)
{
	struct path_pattern *pattern;
	int patternlen;
	unsigned flags;
	int nowildcardlen;

	parse_path_pattern(&string, &patternlen, &flags, &nowildcardlen);
	FLEX_ALLOC_MEM(pattern, pattern, string, patternlen);
	pattern->patternlen = patternlen;
	pattern->nowildcardlen = nowildcardlen;
	pattern->base = base;
	pattern->baselen = baselen;
	pattern->flags = flags;
	pattern->srcpos = srcpos;
	ALLOC_GROW(pl->patterns, pl->nr + 1, pl->alloc);
	pl->patterns[pl->nr++] = pattern;
	pattern->pl = pl;

	add_pattern_to_hashsets(pl, pattern);
}

static int read_skip_worktree_file_from_index(struct index_state *istate,
					      const char *path,
					      size_t *size_out, char **data_out,
					      struct oid_stat *oid_stat)
{
	int pos, len;

	len = strlen(path);
	pos = index_name_pos(istate, path, len);
	if (pos < 0)
		return -1;
	if (!ce_skip_worktree(istate->cache[pos]))
		return -1;

	return do_read_blob(&istate->cache[pos]->oid, oid_stat, size_out, data_out);
}

/*
 * Frees memory within pl which was allocated for exclude patterns and
 * the file buffer.  Does not free pl itself.
 */
void clear_pattern_list(struct pattern_list *pl)
{
	int i;

	for (i = 0; i < pl->nr; i++)
		free(pl->patterns[i]);
	free(pl->patterns);
	clear_pattern_entry_hashmap(&pl->recursive_hashmap);
	clear_pattern_entry_hashmap(&pl->parent_hashmap);

	memset(pl, 0, sizeof(*pl));
}

static void trim_trailing_spaces(char *buf)
{
	char *p, *last_space = NULL;

	for (p = buf; *p; p++)
		switch (*p) {
		case ' ':
			if (!last_space)
				last_space = p;
			break;
		case '\\':
			p++;
			if (!*p)
				return;
			/* fallthrough */
		default:
			last_space = NULL;
		}

	if (last_space)
		*last_space = '\0';
}

/*
 * Given a subdirectory name and "dir" of the current directory,
 * search the subdir in "dir" and return it, or create a new one if it
 * does not exist in "dir".
 *
 * If "name" has the trailing slash, it'll be excluded in the search.
 */
static struct untracked_cache_dir *lookup_untracked(struct untracked_cache *uc,
						    struct untracked_cache_dir *dir,
						    const char *name, int len)
{
	int first, last;
	struct untracked_cache_dir *d;
	if (!dir)
		return NULL;
	if (len && name[len - 1] == '/')
		len--;
	first = 0;
	last = dir->dirs_nr;
	while (last > first) {
		int cmp, next = first + ((last - first) >> 1);
		d = dir->dirs[next];
		cmp = strncmp(name, d->name, len);
		if (!cmp && strlen(d->name) > len)
			cmp = -1;
		if (!cmp)
			return d;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}

	uc->dir_created++;
	FLEX_ALLOC_MEM(d, name, name, len);

	ALLOC_GROW(dir->dirs, dir->dirs_nr + 1, dir->dirs_alloc);
	MOVE_ARRAY(dir->dirs + first + 1, dir->dirs + first,
		   dir->dirs_nr - first);
	dir->dirs_nr++;
	dir->dirs[first] = d;
	return d;
}

static void do_invalidate_gitignore(struct untracked_cache_dir *dir)
{
	int i;
	dir->valid = 0;
	dir->untracked_nr = 0;
	for (i = 0; i < dir->dirs_nr; i++)
		do_invalidate_gitignore(dir->dirs[i]);
}

static void invalidate_gitignore(struct untracked_cache *uc,
				 struct untracked_cache_dir *dir)
{
	uc->gitignore_invalidated++;
	do_invalidate_gitignore(dir);
}

static void invalidate_directory(struct untracked_cache *uc,
				 struct untracked_cache_dir *dir)
{
	int i;

	/*
	 * Invalidation increment here is just roughly correct. If
	 * untracked_nr or any of dirs[].recurse is non-zero, we
	 * should increment dir_invalidated too. But that's more
	 * expensive to do.
	 */
	if (dir->valid)
		uc->dir_invalidated++;

	dir->valid = 0;
	dir->untracked_nr = 0;
	for (i = 0; i < dir->dirs_nr; i++)
		dir->dirs[i]->recurse = 0;
}

static int add_patterns_from_buffer(char *buf, size_t size,
				    const char *base, int baselen,
				    struct pattern_list *pl);

/* Flags for add_patterns() */
#define PATTERN_NOFOLLOW (1<<0)

/*
 * Given a file with name "fname", read it (either from disk, or from
 * an index if 'istate' is non-null), parse it and store the
 * exclude rules in "pl".
 *
 * If "oid_stat" is not NULL, compute oid of the exclude file and fill
 * stat data from disk (only valid if add_patterns returns zero). If
 * oid_stat.valid is non-zero, "oid_stat" must contain good value as input.
 */
static int add_patterns(const char *fname, const char *base, int baselen,
			struct pattern_list *pl, struct index_state *istate,
			unsigned flags, struct oid_stat *oid_stat)
{
	struct stat st;
	int r;
	int fd;
	size_t size = 0;
	char *buf;

	if (is_fscache_enabled(fname)) {
		if (lstat(fname, &st) < 0) {
			fd = -1;
		} else {
			fd = open(fname, O_RDONLY);
			if (fd < 0)
				warn_on_fopen_errors(fname);
		}
	} else {
		if (flags & PATTERN_NOFOLLOW)
			fd = open_nofollow(fname, O_RDONLY);
		else
			fd = open(fname, O_RDONLY);

		if (fd < 0 || fstat(fd, &st) < 0) {
			if (fd < 0)
				warn_on_fopen_errors(fname);
			else {
				close(fd);
				fd = -1;
			}
		}
	}

	if (fd < 0) {
		if (!istate)
			return -1;
		r = read_skip_worktree_file_from_index(istate, fname,
						       &size, &buf,
						       oid_stat);
		if (r != 1)
			return r;
	} else {
		size = xsize_t(st.st_size);
		if (size == 0) {
			if (oid_stat) {
				fill_stat_data(&oid_stat->stat, &st);
				oidcpy(&oid_stat->oid, the_hash_algo->empty_blob);
				oid_stat->valid = 1;
			}
			close(fd);
			return 0;
		}
		buf = xmallocz(size);
		if (read_in_full(fd, buf, size) != size) {
			free(buf);
			close(fd);
			return -1;
		}
		buf[size++] = '\n';
		close(fd);
		if (oid_stat) {
			int pos;
			if (oid_stat->valid &&
			    !match_stat_data_racy(istate, &oid_stat->stat, &st))
				; /* no content change, oid_stat->oid still good */
			else if (istate &&
				 (pos = index_name_pos(istate, fname, strlen(fname))) >= 0 &&
				 !ce_stage(istate->cache[pos]) &&
				 ce_uptodate(istate->cache[pos]) &&
				 !would_convert_to_git(istate, fname))
				oidcpy(&oid_stat->oid,
				       &istate->cache[pos]->oid);
			else
				hash_object_file(the_hash_algo, buf, size,
						 OBJ_BLOB, &oid_stat->oid);
			fill_stat_data(&oid_stat->stat, &st);
			oid_stat->valid = 1;
		}
	}

	if (size > PATTERN_MAX_FILE_SIZE) {
		warning("ignoring excessively large pattern file: %s", fname);
		free(buf);
		return -1;
	}

	add_patterns_from_buffer(buf, size, base, baselen, pl);
	free(buf);
	return 0;
}

static int add_patterns_from_buffer(char *buf, size_t size,
				    const char *base, int baselen,
				    struct pattern_list *pl)
{
	char *orig = buf;
	int i, lineno = 1;
	char *entry;

	hashmap_init(&pl->recursive_hashmap, pl_hashmap_cmp, NULL, 0);
	hashmap_init(&pl->parent_hashmap, pl_hashmap_cmp, NULL, 0);

	if (skip_utf8_bom(&buf, size))
		size -= buf - orig;

	entry = buf;

	for (i = 0; i < size; i++) {
		if (buf[i] == '\n') {
			if (entry != buf + i && entry[0] != '#') {
				buf[i - (i && buf[i-1] == '\r')] = 0;
				trim_trailing_spaces(entry);
				add_pattern(entry, base, baselen, pl, lineno);
			}
			lineno++;
			entry = buf + i + 1;
		}
	}
	return 0;
}

int add_patterns_from_file_to_list(const char *fname, const char *base,
				   int baselen, struct pattern_list *pl,
				   struct index_state *istate,
				   unsigned flags)
{
	return add_patterns(fname, base, baselen, pl, istate, flags, NULL);
}

int add_patterns_from_blob_to_list(
	struct object_id *oid,
	const char *base, int baselen,
	struct pattern_list *pl)
{
	char *buf;
	size_t size;
	int r;

	r = do_read_blob(oid, NULL, &size, &buf);
	if (r != 1)
		return r;

	if (size > PATTERN_MAX_FILE_SIZE) {
		warning("ignoring excessively large pattern blob: %s",
			oid_to_hex(oid));
		free(buf);
		return -1;
	}

	add_patterns_from_buffer(buf, size, base, baselen, pl);
	free(buf);
	return 0;
}

struct pattern_list *add_pattern_list(struct dir_struct *dir,
				      int group_type, const char *src)
{
	struct pattern_list *pl;
	struct exclude_list_group *group;

	group = &dir->internal.exclude_list_group[group_type];
	ALLOC_GROW(group->pl, group->nr + 1, group->alloc);
	pl = &group->pl[group->nr++];
	memset(pl, 0, sizeof(*pl));
	pl->src = src;
	return pl;
}

/*
 * Used to set up core.excludesfile and .git/info/exclude lists.
 */
static void add_patterns_from_file_1(struct dir_struct *dir, const char *fname,
				     struct oid_stat *oid_stat)
{
	struct pattern_list *pl;
	/*
	 * catch setup_standard_excludes() that's called before
	 * dir->untracked is assigned. That function behaves
	 * differently when dir->untracked is non-NULL.
	 */
	if (!dir->untracked)
		dir->internal.unmanaged_exclude_files++;
	pl = add_pattern_list(dir, EXC_FILE, fname);
	if (add_patterns(fname, "", 0, pl, NULL, 0, oid_stat) < 0)
		die(_("cannot use %s as an exclude file"), fname);
}

void add_patterns_from_file(struct dir_struct *dir, const char *fname)
{
	dir->internal.unmanaged_exclude_files++; /* see validate_untracked_cache() */
	add_patterns_from_file_1(dir, fname, NULL);
}

int match_basename(const char *basename, int basenamelen,
		   const char *pattern, int prefix, int patternlen,
		   unsigned flags)
{
	if (prefix == patternlen) {
		if (patternlen == basenamelen &&
		    !fspathncmp(pattern, basename, basenamelen))
			return 1;
	} else if (flags & PATTERN_FLAG_ENDSWITH) {
		/* "*literal" matching against "fooliteral" */
		if (patternlen - 1 <= basenamelen &&
		    !fspathncmp(pattern + 1,
				   basename + basenamelen - (patternlen - 1),
				   patternlen - 1))
			return 1;
	} else {
		if (fnmatch_icase_mem(pattern, patternlen,
				      basename, basenamelen,
				      0) == 0)
			return 1;
	}
	return 0;
}

int match_pathname(const char *pathname, int pathlen,
		   const char *base, int baselen,
		   const char *pattern, int prefix, int patternlen)
{
	const char *name;
	int namelen;

	/*
	 * match with FNM_PATHNAME; the pattern has base implicitly
	 * in front of it.
	 */
	if (*pattern == '/') {
		pattern++;
		patternlen--;
		prefix--;
	}

	/*
	 * baselen does not count the trailing slash. base[] may or
	 * may not end with a trailing slash though.
	 */
	if (pathlen < baselen + 1 ||
	    (baselen && pathname[baselen] != '/') ||
	    fspathncmp(pathname, base, baselen))
		return 0;

	namelen = baselen ? pathlen - baselen - 1 : pathlen;
	name = pathname + pathlen - namelen;

	if (prefix) {
		/*
		 * if the non-wildcard part is longer than the
		 * remaining pathname, surely it cannot match.
		 */
		if (prefix > namelen)
			return 0;

		if (fspathncmp(pattern, name, prefix))
			return 0;
		pattern += prefix;
		patternlen -= prefix;
		name    += prefix;
		namelen -= prefix;

		/*
		 * If the whole pattern did not have a wildcard,
		 * then our prefix match is all we need; we
		 * do not need to call fnmatch at all.
		 */
		if (!patternlen && !namelen)
			return 1;
	}

	return fnmatch_icase_mem(pattern, patternlen,
				 name, namelen,
				 WM_PATHNAME) == 0;
}

/*
 * Scan the given exclude list in reverse to see whether pathname
 * should be ignored.  The first match (i.e. the last on the list), if
 * any, determines the fate.  Returns the exclude_list element which
 * matched, or NULL for undecided.
 */
static struct path_pattern *last_matching_pattern_from_list(const char *pathname,
						       int pathlen,
						       const char *basename,
						       int *dtype,
						       struct pattern_list *pl,
						       struct index_state *istate)
{
	struct path_pattern *res = NULL; /* undecided */
	int i;

	if (!pl->nr)
		return NULL;	/* undefined */

	for (i = pl->nr - 1; 0 <= i; i--) {
		struct path_pattern *pattern = pl->patterns[i];
		const char *exclude = pattern->pattern;
		int prefix = pattern->nowildcardlen;

		if (pattern->flags & PATTERN_FLAG_MUSTBEDIR) {
			*dtype = resolve_dtype(*dtype, istate, pathname, pathlen);
			if (*dtype != DT_DIR)
				continue;
		}

		if (pattern->flags & PATTERN_FLAG_NODIR) {
			if (match_basename(basename,
					   pathlen - (basename - pathname),
					   exclude, prefix, pattern->patternlen,
					   pattern->flags)) {
				res = pattern;
				break;
			}
			continue;
		}

		assert(pattern->baselen == 0 ||
		       pattern->base[pattern->baselen - 1] == '/');
		if (match_pathname(pathname, pathlen,
				   pattern->base,
				   pattern->baselen ? pattern->baselen - 1 : 0,
				   exclude, prefix, pattern->patternlen)) {
			res = pattern;
			break;
		}
	}
	return res;
}

/*
 * Scan the list of patterns to determine if the ordered list
 * of patterns matches on 'pathname'.
 *
 * Return 1 for a match, 0 for not matched and -1 for undecided.
 */
enum pattern_match_result path_matches_pattern_list(
				const char *pathname, int pathlen,
				const char *basename, int *dtype,
				struct pattern_list *pl,
				struct index_state *istate)
{
	struct path_pattern *pattern;
	struct strbuf parent_pathname = STRBUF_INIT;
	int result = NOT_MATCHED;
	size_t slash_pos;

	if (!pl->use_cone_patterns) {
		pattern = last_matching_pattern_from_list(pathname, pathlen, basename,
							dtype, pl, istate);
		if (pattern) {
			if (pattern->flags & PATTERN_FLAG_NEGATIVE)
				return NOT_MATCHED;
			else
				return MATCHED;
		}

		return UNDECIDED;
	}

	if (pl->full_cone)
		return MATCHED;

	strbuf_addch(&parent_pathname, '/');
	strbuf_add(&parent_pathname, pathname, pathlen);

	/*
	 * Directory entries are matched if and only if a file
	 * contained immediately within them is matched. For the
	 * case of a directory entry, modify the path to create
	 * a fake filename within this directory, allowing us to
	 * use the file-base matching logic in an equivalent way.
	 */
	if (parent_pathname.len > 0 &&
	    parent_pathname.buf[parent_pathname.len - 1] == '/') {
		slash_pos = parent_pathname.len - 1;
		strbuf_add(&parent_pathname, "-", 1);
	} else {
		const char *slash_ptr = strrchr(parent_pathname.buf, '/');
		slash_pos = slash_ptr ? slash_ptr - parent_pathname.buf : 0;
	}

	if (hashmap_contains_path(&pl->recursive_hashmap,
				  &parent_pathname)) {
		result = MATCHED_RECURSIVE;
		goto done;
	}

	if (!slash_pos) {
		/* include every file in root */
		result = MATCHED;
		goto done;
	}

	strbuf_setlen(&parent_pathname, slash_pos);

	if (hashmap_contains_path(&pl->parent_hashmap, &parent_pathname)) {
		result = MATCHED;
		goto done;
	}

	if (hashmap_contains_parent(&pl->recursive_hashmap,
				    pathname,
				    &parent_pathname))
		result = MATCHED_RECURSIVE;

done:
	strbuf_release(&parent_pathname);
	return result;
}

int init_sparse_checkout_patterns(struct index_state *istate)
{
	if (!core_apply_sparse_checkout)
		return 1;
	if (istate->sparse_checkout_patterns)
		return 0;

	CALLOC_ARRAY(istate->sparse_checkout_patterns, 1);

	if (get_sparse_checkout_patterns(istate->sparse_checkout_patterns) < 0) {
		FREE_AND_NULL(istate->sparse_checkout_patterns);
		return -1;
	}

	return 0;
}

static int path_in_sparse_checkout_1(const char *path,
				     struct index_state *istate,
				     int require_cone_mode)
{
	int dtype = DT_REG;
	enum pattern_match_result match = UNDECIDED;
	const char *end, *slash;

	/*
	 * We default to accepting a path if the path is empty, there are no
	 * patterns, or the patterns are of the wrong type.
	 */
	if (!*path ||
	    init_sparse_checkout_patterns(istate) ||
	    (require_cone_mode &&
	     !istate->sparse_checkout_patterns->use_cone_patterns))
		return 1;

	/*
	 * If UNDECIDED, use the match from the parent dir (recursively), or
	 * fall back to NOT_MATCHED at the topmost level. Note that cone mode
	 * never returns UNDECIDED, so we will execute only one iteration in
	 * this case.
	 */
	for (end = path + strlen(path);
	     end > path && match == UNDECIDED;
	     end = slash) {

		for (slash = end - 1; slash > path && *slash != '/'; slash--)
			; /* do nothing */

		match = path_matches_pattern_list(path, end - path,
				slash > path ? slash + 1 : path, &dtype,
				istate->sparse_checkout_patterns, istate);

		/* We are going to match the parent dir now */
		dtype = DT_DIR;
	}
	return match > 0;
}

int path_in_sparse_checkout(const char *path,
			    struct index_state *istate)
{
	return path_in_sparse_checkout_1(path, istate, 0);
}

int path_in_cone_mode_sparse_checkout(const char *path,
				     struct index_state *istate)
{
	return path_in_sparse_checkout_1(path, istate, 1);
}

static struct path_pattern *last_matching_pattern_from_lists(
		struct dir_struct *dir, struct index_state *istate,
		const char *pathname, int pathlen,
		const char *basename, int *dtype_p)
{
	int i, j;
	struct exclude_list_group *group;
	struct path_pattern *pattern;
	for (i = EXC_CMDL; i <= EXC_FILE; i++) {
		group = &dir->internal.exclude_list_group[i];
		for (j = group->nr - 1; j >= 0; j--) {
			pattern = last_matching_pattern_from_list(
				pathname, pathlen, basename, dtype_p,
				&group->pl[j], istate);
			if (pattern)
				return pattern;
		}
	}
	return NULL;
}

/*
 * Loads the per-directory exclude list for the substring of base
 * which has a char length of baselen.
 */
static void prep_exclude(struct dir_struct *dir,
			 struct index_state *istate,
			 const char *base, int baselen)
{
	struct exclude_list_group *group;
	struct pattern_list *pl;
	struct exclude_stack *stk = NULL;
	struct untracked_cache_dir *untracked;
	int current;

	group = &dir->internal.exclude_list_group[EXC_DIRS];

	/*
	 * Pop the exclude lists from the EXCL_DIRS exclude_list_group
	 * which originate from directories not in the prefix of the
	 * path being checked.
	 */
	while ((stk = dir->internal.exclude_stack) != NULL) {
		if (stk->baselen <= baselen &&
		    !strncmp(dir->internal.basebuf.buf, base, stk->baselen))
			break;
		pl = &group->pl[dir->internal.exclude_stack->exclude_ix];
		dir->internal.exclude_stack = stk->prev;
		dir->internal.pattern = NULL;
		free((char *)pl->src); /* see strbuf_detach() below */
		clear_pattern_list(pl);
		free(stk);
		group->nr--;
	}

	/* Skip traversing into sub directories if the parent is excluded */
	if (dir->internal.pattern)
		return;

	/*
	 * Lazy initialization. All call sites currently just
	 * memset(dir, 0, sizeof(*dir)) before use. Changing all of
	 * them seems lots of work for little benefit.
	 */
	if (!dir->internal.basebuf.buf)
		strbuf_init(&dir->internal.basebuf, PATH_MAX);

	/* Read from the parent directories and push them down. */
	current = stk ? stk->baselen : -1;
	strbuf_setlen(&dir->internal.basebuf, current < 0 ? 0 : current);
	if (dir->untracked)
		untracked = stk ? stk->ucd : dir->untracked->root;
	else
		untracked = NULL;

	while (current < baselen) {
		const char *cp;
		struct oid_stat oid_stat;

		CALLOC_ARRAY(stk, 1);
		if (current < 0) {
			cp = base;
			current = 0;
		} else {
			cp = strchr(base + current + 1, '/');
			if (!cp)
				die("oops in prep_exclude");
			cp++;
			untracked =
				lookup_untracked(dir->untracked,
						 untracked,
						 base + current,
						 cp - base - current);
		}
		stk->prev = dir->internal.exclude_stack;
		stk->baselen = cp - base;
		stk->exclude_ix = group->nr;
		stk->ucd = untracked;
		pl = add_pattern_list(dir, EXC_DIRS, NULL);
		strbuf_add(&dir->internal.basebuf, base + current, stk->baselen - current);
		assert(stk->baselen == dir->internal.basebuf.len);

		/* Abort if the directory is excluded */
		if (stk->baselen) {
			int dt = DT_DIR;
			dir->internal.basebuf.buf[stk->baselen - 1] = 0;
			dir->internal.pattern = last_matching_pattern_from_lists(dir,
									istate,
				dir->internal.basebuf.buf, stk->baselen - 1,
				dir->internal.basebuf.buf + current, &dt);
			dir->internal.basebuf.buf[stk->baselen - 1] = '/';
			if (dir->internal.pattern &&
			    dir->internal.pattern->flags & PATTERN_FLAG_NEGATIVE)
				dir->internal.pattern = NULL;
			if (dir->internal.pattern) {
				dir->internal.exclude_stack = stk;
				return;
			}
		}

		/* Try to read per-directory file */
		oidclr(&oid_stat.oid, the_repository->hash_algo);
		oid_stat.valid = 0;
		if (dir->exclude_per_dir &&
		    /*
		     * If we know that no files have been added in
		     * this directory (i.e. valid_cached_dir() has
		     * been executed and set untracked->valid) ..
		     */
		    (!untracked || !untracked->valid ||
		     /*
		      * .. and .gitignore does not exist before
		      * (i.e. null exclude_oid). Then we can skip
		      * loading .gitignore, which would result in
		      * ENOENT anyway.
		      */
		     !is_null_oid(&untracked->exclude_oid))) {
			/*
			 * dir->internal.basebuf gets reused by the traversal,
			 * but we need fname to remain unchanged to ensure the
			 * src member of each struct path_pattern correctly
			 * back-references its source file.  Other invocations
			 * of add_pattern_list provide stable strings, so we
			 * strbuf_detach() and free() here in the caller.
			 */
			struct strbuf sb = STRBUF_INIT;
			strbuf_addbuf(&sb, &dir->internal.basebuf);
			strbuf_addstr(&sb, dir->exclude_per_dir);
			pl->src = strbuf_detach(&sb, NULL);
			add_patterns(pl->src, pl->src, stk->baselen, pl, istate,
				     PATTERN_NOFOLLOW,
				     untracked ? &oid_stat : NULL);
		}
		/*
		 * NEEDSWORK: when untracked cache is enabled, prep_exclude()
		 * will first be called in valid_cached_dir() then maybe many
		 * times more in last_matching_pattern(). When the cache is
		 * used, last_matching_pattern() will not be called and
		 * reading .gitignore content will be a waste.
		 *
		 * So when it's called by valid_cached_dir() and we can get
		 * .gitignore SHA-1 from the index (i.e. .gitignore is not
		 * modified on work tree), we could delay reading the
		 * .gitignore content until we absolutely need it in
		 * last_matching_pattern(). Be careful about ignore rule
		 * order, though, if you do that.
		 */
		if (untracked &&
		    !oideq(&oid_stat.oid, &untracked->exclude_oid)) {
			invalidate_gitignore(dir->untracked, untracked);
			oidcpy(&untracked->exclude_oid, &oid_stat.oid);
		}
		dir->internal.exclude_stack = stk;
		current = stk->baselen;
	}
	strbuf_setlen(&dir->internal.basebuf, baselen);
}

/*
 * Loads the exclude lists for the directory containing pathname, then
 * scans all exclude lists to determine whether pathname is excluded.
 * Returns the exclude_list element which matched, or NULL for
 * undecided.
 */
struct path_pattern *last_matching_pattern(struct dir_struct *dir,
				      struct index_state *istate,
				      const char *pathname,
				      int *dtype_p)
{
	int pathlen = strlen(pathname);
	const char *basename = strrchr(pathname, '/');
	basename = (basename) ? basename+1 : pathname;

	prep_exclude(dir, istate, pathname, basename-pathname);

	if (dir->internal.pattern)
		return dir->internal.pattern;

	return last_matching_pattern_from_lists(dir, istate, pathname, pathlen,
			basename, dtype_p);
}

/*
 * Loads the exclude lists for the directory containing pathname, then
 * scans all exclude lists to determine whether pathname is excluded.
 * Returns 1 if true, otherwise 0.
 */
int is_excluded(struct dir_struct *dir, struct index_state *istate,
		const char *pathname, int *dtype_p)
{
	struct path_pattern *pattern =
		last_matching_pattern(dir, istate, pathname, dtype_p);
	if (pattern)
		return pattern->flags & PATTERN_FLAG_NEGATIVE ? 0 : 1;
	return 0;
}

static struct dir_entry *dir_entry_new(const char *pathname, int len)
{
	struct dir_entry *ent;

	FLEX_ALLOC_MEM(ent, name, pathname, len);
	ent->len = len;
	return ent;
}

static struct dir_entry *dir_add_name(struct dir_struct *dir,
				      struct index_state *istate,
				      const char *pathname, int len)
{
	if (index_file_exists(istate, pathname, len, ignore_case))
		return NULL;

	ALLOC_GROW(dir->entries, dir->nr+1, dir->internal.alloc);
	return dir->entries[dir->nr++] = dir_entry_new(pathname, len);
}

struct dir_entry *dir_add_ignored(struct dir_struct *dir,
				  struct index_state *istate,
				  const char *pathname, int len)
{
	if (!index_name_is_other(istate, pathname, len))
		return NULL;

	ALLOC_GROW(dir->ignored, dir->ignored_nr+1, dir->internal.ignored_alloc);
	return dir->ignored[dir->ignored_nr++] = dir_entry_new(pathname, len);
}

enum exist_status {
	index_nonexistent = 0,
	index_directory,
	index_gitdir
};

/*
 * Do not use the alphabetically sorted index to look up
 * the directory name; instead, use the case insensitive
 * directory hash.
 */
static enum exist_status directory_exists_in_index_icase(struct index_state *istate,
							 const char *dirname, int len)
{
	struct cache_entry *ce;

	if (index_dir_exists(istate, dirname, len))
		return index_directory;

	ce = index_file_exists(istate, dirname, len, ignore_case);
	if (ce && S_ISGITLINK(ce->ce_mode))
		return index_gitdir;

	return index_nonexistent;
}

/*
 * The index sorts alphabetically by entry name, which
 * means that a gitlink sorts as '\0' at the end, while
 * a directory (which is defined not as an entry, but as
 * the files it contains) will sort with the '/' at the
 * end.
 */
static enum exist_status directory_exists_in_index(struct index_state *istate,
						   const char *dirname, int len)
{
	int pos;

	if (ignore_case)
		return directory_exists_in_index_icase(istate, dirname, len);

	pos = index_name_pos(istate, dirname, len);
	if (pos < 0)
		pos = -pos-1;
	while (pos < istate->cache_nr) {
		const struct cache_entry *ce = istate->cache[pos++];
		unsigned char endchar;

		if (strncmp(ce->name, dirname, len))
			break;
		endchar = ce->name[len];
		if (endchar > '/')
			break;
		if (endchar == '/')
			return index_directory;
		if (!endchar && S_ISGITLINK(ce->ce_mode))
			return index_gitdir;
	}
	return index_nonexistent;
}

/*
 * When we find a directory when traversing the filesystem, we
 * have three distinct cases:
 *
 *  - ignore it
 *  - see it as a directory
 *  - recurse into it
 *
 * and which one we choose depends on a combination of existing
 * git index contents and the flags passed into the directory
 * traversal routine.
 *
 * Case 1: If we *already* have entries in the index under that
 * directory name, we always recurse into the directory to see
 * all the files.
 *
 * Case 2: If we *already* have that directory name as a gitlink,
 * we always continue to see it as a gitlink, regardless of whether
 * there is an actual git directory there or not (it might not
 * be checked out as a subproject!)
 *
 * Case 3: if we didn't have it in the index previously, we
 * have a few sub-cases:
 *
 *  (a) if DIR_SHOW_OTHER_DIRECTORIES flag is set, we show it as
 *      just a directory, unless DIR_HIDE_EMPTY_DIRECTORIES is
 *      also true, in which case we need to check if it contains any
 *      untracked and / or ignored files.
 *  (b) if it looks like a git directory and we don't have the
 *      DIR_NO_GITLINKS flag, then we treat it as a gitlink, and
 *      show it as a directory.
 *  (c) otherwise, we recurse into it.
 */
static enum path_treatment treat_directory(struct dir_struct *dir,
	struct index_state *istate,
	struct untracked_cache_dir *untracked,
	const char *dirname, int len, int baselen, int excluded,
	const struct pathspec *pathspec)
{
	/*
	 * WARNING: From this function, you can return path_recurse or you
	 *          can call read_directory_recursive() (or neither), but
	 *          you CAN'T DO BOTH.
	 */
	enum path_treatment state;
	int matches_how = 0;
	int check_only, stop_early;
	int old_ignored_nr, old_untracked_nr;
	/* The "len-1" is to strip the final '/' */
	enum exist_status status = directory_exists_in_index(istate, dirname, len-1);

	if (status == index_directory)
		return path_recurse;
	if (status == index_gitdir)
		return path_none;
	if (status != index_nonexistent)
		BUG("Unhandled value for directory_exists_in_index: %d\n", status);

	/*
	 * We don't want to descend into paths that don't match the necessary
	 * patterns.  Clearly, if we don't have a pathspec, then we can't check
	 * for matching patterns.  Also, if (excluded) then we know we matched
	 * the exclusion patterns so as an optimization we can skip checking
	 * for matching patterns.
	 */
	if (pathspec && !excluded) {
		matches_how = match_pathspec_with_flags(istate, pathspec,
							dirname, len,
							0 /* prefix */,
							NULL /* seen */,
							DO_MATCH_LEADING_PATHSPEC);
		if (!matches_how)
			return path_none;
	}


	if ((dir->flags & DIR_SKIP_NESTED_GIT) ||
		!(dir->flags & DIR_NO_GITLINKS)) {
		/*
		 * Determine if `dirname` is a nested repo by confirming that:
		 * 1) we are in a nonbare repository, and
		 * 2) `dirname` is not an immediate parent of `the_repository->gitdir`,
		 *    which could occur if the git_dir or worktree location was
		 *    manually configured by the user; see t2205 testcases 1-3 for
		 *    examples where this matters
		 */
		int nested_repo;
		struct strbuf sb = STRBUF_INIT;
		strbuf_addstr(&sb, dirname);
		nested_repo = is_nonbare_repository_dir(&sb);

		if (nested_repo) {
			char *real_dirname, *real_gitdir;
			strbuf_addstr(&sb, ".git");
			real_dirname = real_pathdup(sb.buf, 1);
			real_gitdir = real_pathdup(the_repository->gitdir, 1);

			nested_repo = !!strcmp(real_dirname, real_gitdir);
			free(real_gitdir);
			free(real_dirname);
		}
		strbuf_release(&sb);

		if (nested_repo) {
			if ((dir->flags & DIR_SKIP_NESTED_GIT) ||
				(matches_how == MATCHED_RECURSIVELY_LEADING_PATHSPEC))
				return path_none;
			return excluded ? path_excluded : path_untracked;
		}
	}

	if (!(dir->flags & DIR_SHOW_OTHER_DIRECTORIES)) {
		if (excluded &&
		    (dir->flags & DIR_SHOW_IGNORED_TOO) &&
		    (dir->flags & DIR_SHOW_IGNORED_TOO_MODE_MATCHING)) {

			/*
			 * This is an excluded directory and we are
			 * showing ignored paths that match an exclude
			 * pattern.  (e.g. show directory as ignored
			 * only if it matches an exclude pattern).
			 * This path will either be 'path_excluded`
			 * (if we are showing empty directories or if
			 * the directory is not empty), or will be
			 * 'path_none' (empty directory, and we are
			 * not showing empty directories).
			 */
			if (!(dir->flags & DIR_HIDE_EMPTY_DIRECTORIES))
				return path_excluded;

			if (read_directory_recursive(dir, istate, dirname, len,
						     untracked, 1, 1, pathspec) == path_excluded)
				return path_excluded;

			return path_none;
		}
		return path_recurse;
	}

	assert(dir->flags & DIR_SHOW_OTHER_DIRECTORIES);

	/*
	 * If we have a pathspec which could match something _below_ this
	 * directory (e.g. when checking 'subdir/' having a pathspec like
	 * 'subdir/some/deep/path/file' or 'subdir/widget-*.c'), then we
	 * need to recurse.
	 */
	if (matches_how == MATCHED_RECURSIVELY_LEADING_PATHSPEC)
		return path_recurse;

	/* Special cases for where this directory is excluded/ignored */
	if (excluded) {
		/*
		 * If DIR_SHOW_OTHER_DIRECTORIES is set and we're not
		 * hiding empty directories, there is no need to
		 * recurse into an ignored directory.
		 */
		if (!(dir->flags & DIR_HIDE_EMPTY_DIRECTORIES))
			return path_excluded;

		/*
		 * Even if we are hiding empty directories, we can still avoid
		 * recursing into ignored directories for DIR_SHOW_IGNORED_TOO
		 * if DIR_SHOW_IGNORED_TOO_MODE_MATCHING is also set.
		 */
		if ((dir->flags & DIR_SHOW_IGNORED_TOO) &&
		    (dir->flags & DIR_SHOW_IGNORED_TOO_MODE_MATCHING))
			return path_excluded;
	}

	/*
	 * Other than the path_recurse case above, we only need to
	 * recurse into untracked directories if any of the following
	 * bits is set:
	 *   - DIR_SHOW_IGNORED (because then we need to determine if
	 *                       there are ignored entries below)
	 *   - DIR_SHOW_IGNORED_TOO (same as above)
	 *   - DIR_HIDE_EMPTY_DIRECTORIES (because we have to determine if
	 *                                 the directory is empty)
	 */
	if (!excluded &&
	    !(dir->flags & (DIR_SHOW_IGNORED |
			    DIR_SHOW_IGNORED_TOO |
			    DIR_HIDE_EMPTY_DIRECTORIES))) {
		return path_untracked;
	}

	/*
	 * Even if we don't want to know all the paths under an untracked or
	 * ignored directory, we may still need to go into the directory to
	 * determine if it is empty (because with DIR_HIDE_EMPTY_DIRECTORIES,
	 * an empty directory should be path_none instead of path_excluded or
	 * path_untracked).
	 */
	check_only = ((dir->flags & DIR_HIDE_EMPTY_DIRECTORIES) &&
		      !(dir->flags & DIR_SHOW_IGNORED_TOO));

	/*
	 * However, there's another optimization possible as a subset of
	 * check_only, based on the cases we have to consider:
	 *   A) Directory matches no exclude patterns:
	 *     * Directory is empty => path_none
	 *     * Directory has an untracked file under it => path_untracked
	 *     * Directory has only ignored files under it => path_excluded
	 *   B) Directory matches an exclude pattern:
	 *     * Directory is empty => path_none
	 *     * Directory has an untracked file under it => path_excluded
	 *     * Directory has only ignored files under it => path_excluded
	 * In case A, we can exit as soon as we've found an untracked
	 * file but otherwise have to walk all files.  In case B, though,
	 * we can stop at the first file we find under the directory.
	 */
	stop_early = check_only && excluded;

	/*
	 * If /every/ file within an untracked directory is ignored, then
	 * we want to treat the directory as ignored (for e.g. status
	 * --porcelain), without listing the individual ignored files
	 * underneath.  To do so, we'll save the current ignored_nr, and
	 * pop all the ones added after it if it turns out the entire
	 * directory is ignored.  Also, when DIR_SHOW_IGNORED_TOO and
	 * !DIR_KEEP_UNTRACKED_CONTENTS then we don't want to show
	 * untracked paths so will need to pop all those off the last
	 * after we traverse.
	 */
	old_ignored_nr = dir->ignored_nr;
	old_untracked_nr = dir->nr;

	/* Actually recurse into dirname now, we'll fixup the state later. */
	untracked = lookup_untracked(dir->untracked, untracked,
				     dirname + baselen, len - baselen);
	state = read_directory_recursive(dir, istate, dirname, len, untracked,
					 check_only, stop_early, pathspec);

	/* There are a variety of reasons we may need to fixup the state... */
	if (state == path_excluded) {
		/* state == path_excluded implies all paths under
		 * dirname were ignored...
		 *
		 * if running e.g. `git status --porcelain --ignored=matching`,
		 * then we want to see the subpaths that are ignored.
		 *
		 * if running e.g. just `git status --porcelain`, then
		 * we just want the directory itself to be listed as ignored
		 * and not the individual paths underneath.
		 */
		int want_ignored_subpaths =
			((dir->flags & DIR_SHOW_IGNORED_TOO) &&
			 (dir->flags & DIR_SHOW_IGNORED_TOO_MODE_MATCHING));

		if (want_ignored_subpaths) {
			/*
			 * with --ignored=matching, we want the subpaths
			 * INSTEAD of the directory itself.
			 */
			state = path_none;
		} else {
			int i;
			for (i = old_ignored_nr + 1; i<dir->ignored_nr; ++i)
				FREE_AND_NULL(dir->ignored[i]);
			dir->ignored_nr = old_ignored_nr;
		}
	}

	/*
	 * We may need to ignore some of the untracked paths we found while
	 * traversing subdirectories.
	 */
	if ((dir->flags & DIR_SHOW_IGNORED_TOO) &&
	    !(dir->flags & DIR_KEEP_UNTRACKED_CONTENTS)) {
		int i;
		for (i = old_untracked_nr + 1; i<dir->nr; ++i)
			FREE_AND_NULL(dir->entries[i]);
		dir->nr = old_untracked_nr;
	}

	/*
	 * If there is nothing under the current directory and we are not
	 * hiding empty directories, then we need to report on the
	 * untracked or ignored status of the directory itself.
	 */
	if (state == path_none && !(dir->flags & DIR_HIDE_EMPTY_DIRECTORIES))
		state = excluded ? path_excluded : path_untracked;

	return state;
}

/*
 * This is an inexact early pruning of any recursive directory
 * reading - if the path cannot possibly be in the pathspec,
 * return true, and we'll skip it early.
 */
static int simplify_away(const char *path, int pathlen,
			 const struct pathspec *pathspec)
{
	int i;

	if (!pathspec || !pathspec->nr)
		return 0;

	GUARD_PATHSPEC(pathspec,
		       PATHSPEC_FROMTOP |
		       PATHSPEC_MAXDEPTH |
		       PATHSPEC_LITERAL |
		       PATHSPEC_GLOB |
		       PATHSPEC_ICASE |
		       PATHSPEC_EXCLUDE |
		       PATHSPEC_ATTR);

	for (i = 0; i < pathspec->nr; i++) {
		const struct pathspec_item *item = &pathspec->items[i];
		int len = item->nowildcard_len;

		if (len > pathlen)
			len = pathlen;
		if (!ps_strncmp(item, item->match, path, len))
			return 0;
	}

	return 1;
}

/*
 * This function tells us whether an excluded path matches a
 * list of "interesting" pathspecs. That is, whether a path matched
 * by any of the pathspecs could possibly be ignored by excluding
 * the specified path. This can happen if:
 *
 *   1. the path is mentioned explicitly in the pathspec
 *
 *   2. the path is a directory prefix of some element in the
 *      pathspec
 */
static int exclude_matches_pathspec(const char *path, int pathlen,
				    const struct pathspec *pathspec)
{
	int i;

	if (!pathspec || !pathspec->nr)
		return 0;

	GUARD_PATHSPEC(pathspec,
		       PATHSPEC_FROMTOP |
		       PATHSPEC_MAXDEPTH |
		       PATHSPEC_LITERAL |
		       PATHSPEC_GLOB |
		       PATHSPEC_ICASE |
		       PATHSPEC_EXCLUDE |
		       PATHSPEC_ATTR);

	for (i = 0; i < pathspec->nr; i++) {
		const struct pathspec_item *item = &pathspec->items[i];
		int len = item->nowildcard_len;

		if (len == pathlen &&
		    !ps_strncmp(item, item->match, path, pathlen))
			return 1;
		if (len > pathlen &&
		    item->match[pathlen] == '/' &&
		    !ps_strncmp(item, item->match, path, pathlen))
			return 1;
	}
	return 0;
}

static int get_index_dtype(struct index_state *istate,
			   const char *path, int len)
{
	int pos;
	const struct cache_entry *ce;

	ce = index_file_exists(istate, path, len, 0);
	if (ce) {
		if (!ce_uptodate(ce))
			return DT_UNKNOWN;
		if (S_ISGITLINK(ce->ce_mode))
			return DT_DIR;
		/*
		 * Nobody actually cares about the
		 * difference between DT_LNK and DT_REG
		 */
		return DT_REG;
	}

	/* Try to look it up as a directory */
	pos = index_name_pos(istate, path, len);
	if (pos >= 0)
		return DT_UNKNOWN;
	pos = -pos-1;
	while (pos < istate->cache_nr) {
		ce = istate->cache[pos++];
		if (strncmp(ce->name, path, len))
			break;
		if (ce->name[len] > '/')
			break;
		if (ce->name[len] < '/')
			continue;
		if (!ce_uptodate(ce))
			break;	/* continue? */
		return DT_DIR;
	}
	return DT_UNKNOWN;
}

unsigned char get_dtype(struct dirent *e, struct strbuf *path,
			int follow_symlink)
{
	struct stat st;
	unsigned char dtype = DTYPE(e);
	size_t base_path_len;

	if (dtype != DT_UNKNOWN && !(follow_symlink && dtype == DT_LNK))
		return dtype;

	/*
	 * d_type unknown or unfollowed symlink, try to fall back on [l]stat
	 * results. If [l]stat fails, explicitly set DT_UNKNOWN.
	 */
	base_path_len = path->len;
	strbuf_addstr(path, e->d_name);
	if ((follow_symlink && stat(path->buf, &st)) ||
	    (!follow_symlink && lstat(path->buf, &st)))
		goto cleanup;

	/* determine d_type from st_mode */
	if (S_ISREG(st.st_mode))
		dtype = DT_REG;
	else if (S_ISDIR(st.st_mode))
		dtype = DT_DIR;
	else if (S_ISLNK(st.st_mode))
		dtype = DT_LNK;

cleanup:
	strbuf_setlen(path, base_path_len);
	return dtype;
}

static int resolve_dtype(int dtype, struct index_state *istate,
			 const char *path, int len)
{
	struct stat st;

	if (dtype != DT_UNKNOWN)
		return dtype;
	dtype = get_index_dtype(istate, path, len);
	if (dtype != DT_UNKNOWN)
		return dtype;
	if (lstat(path, &st))
		return dtype;
	if (S_ISREG(st.st_mode))
		return DT_REG;
	if (S_ISDIR(st.st_mode))
		return DT_DIR;
	if (S_ISLNK(st.st_mode))
		return DT_LNK;
	return dtype;
}

static enum path_treatment treat_path_fast(struct dir_struct *dir,
					   struct cached_dir *cdir,
					   struct index_state *istate,
					   struct strbuf *path,
					   int baselen,
					   const struct pathspec *pathspec)
{
	/*
	 * WARNING: From this function, you can return path_recurse or you
	 *          can call read_directory_recursive() (or neither), but
	 *          you CAN'T DO BOTH.
	 */
	strbuf_setlen(path, baselen);
	if (!cdir->ucd) {
		strbuf_addstr(path, cdir->file);
		return path_untracked;
	}
	strbuf_addstr(path, cdir->ucd->name);
	/* treat_one_path() does this before it calls treat_directory() */
	strbuf_complete(path, '/');
	if (cdir->ucd->check_only)
		/*
		 * check_only is set as a result of treat_directory() getting
		 * to its bottom. Verify again the same set of directories
		 * with check_only set.
		 */
		return read_directory_recursive(dir, istate, path->buf, path->len,
						cdir->ucd, 1, 0, pathspec);
	/*
	 * We get path_recurse in the first run when
	 * directory_exists_in_index() returns index_nonexistent. We
	 * are sure that new changes in the index does not impact the
	 * outcome. Return now.
	 */
	return path_recurse;
}

static enum path_treatment treat_path(struct dir_struct *dir,
				      struct untracked_cache_dir *untracked,
				      struct cached_dir *cdir,
				      struct index_state *istate,
				      struct strbuf *path,
				      int baselen,
				      const struct pathspec *pathspec)
{
	int has_path_in_index, dtype, excluded;

	if (!cdir->d_name)
		return treat_path_fast(dir, cdir, istate, path,
				       baselen, pathspec);
	if (is_dot_or_dotdot(cdir->d_name) || !fspathcmp(cdir->d_name, ".git"))
		return path_none;
	strbuf_setlen(path, baselen);
	strbuf_addstr(path, cdir->d_name);
	if (simplify_away(path->buf, path->len, pathspec))
		return path_none;

	dtype = resolve_dtype(cdir->d_type, istate, path->buf, path->len);

	/* Always exclude indexed files */
	has_path_in_index = !!index_file_exists(istate, path->buf, path->len,
						ignore_case);
	if (dtype != DT_DIR && has_path_in_index)
		return path_none;

	/*
	 * When we are looking at a directory P in the working tree,
	 * there are three cases:
	 *
	 * (1) P exists in the index.  Everything inside the directory P in
	 * the working tree needs to go when P is checked out from the
	 * index.
	 *
	 * (2) P does not exist in the index, but there is P/Q in the index.
	 * We know P will stay a directory when we check out the contents
	 * of the index, but we do not know yet if there is a directory
	 * P/Q in the working tree to be killed, so we need to recurse.
	 *
	 * (3) P does not exist in the index, and there is no P/Q in the index
	 * to require P to be a directory, either.  Only in this case, we
	 * know that everything inside P will not be killed without
	 * recursing.
	 */
	if ((dir->flags & DIR_COLLECT_KILLED_ONLY) &&
	    (dtype == DT_DIR) &&
	    !has_path_in_index &&
	    (directory_exists_in_index(istate, path->buf, path->len) == index_nonexistent))
		return path_none;

	excluded = is_excluded(dir, istate, path->buf, &dtype);

	/*
	 * Excluded? If we don't explicitly want to show
	 * ignored files, ignore it
	 */
	if (excluded && !(dir->flags & (DIR_SHOW_IGNORED|DIR_SHOW_IGNORED_TOO)))
		return path_excluded;

	switch (dtype) {
	default:
		return path_none;
	case DT_DIR:
		/*
		 * WARNING: Do not ignore/amend the return value from
		 * treat_directory(), and especially do not change it to return
		 * path_recurse as that can cause exponential slowdown.
		 * Instead, modify treat_directory() to return the right value.
		 */
		strbuf_addch(path, '/');
		return treat_directory(dir, istate, untracked,
				       path->buf, path->len,
				       baselen, excluded, pathspec);
	case DT_REG:
	case DT_LNK:
		if (pathspec &&
		    !match_pathspec(istate, pathspec, path->buf, path->len,
				    0 /* prefix */, NULL /* seen */,
				    0 /* is_dir */))
			return path_none;
		if (excluded)
			return path_excluded;
		return path_untracked;
	}
}

static void add_untracked(struct untracked_cache_dir *dir, const char *name)
{
	if (!dir)
		return;
	ALLOC_GROW(dir->untracked, dir->untracked_nr + 1,
		   dir->untracked_alloc);
	dir->untracked[dir->untracked_nr++] = xstrdup(name);
}

static int valid_cached_dir(struct dir_struct *dir,
			    struct untracked_cache_dir *untracked,
			    struct index_state *istate,
			    struct strbuf *path,
			    int check_only)
{
	struct stat st;

	if (!untracked)
		return 0;

	/*
	 * With fsmonitor, we can trust the untracked cache's valid field.
	 */
	refresh_fsmonitor(istate);
	if (!(dir->untracked->use_fsmonitor && untracked->valid)) {
		if (lstat(path->len ? path->buf : ".", &st)) {
			memset(&untracked->stat_data, 0, sizeof(untracked->stat_data));
			return 0;
		}
		if (!untracked->valid ||
			match_stat_data_racy(istate, &untracked->stat_data, &st)) {
			fill_stat_data(&untracked->stat_data, &st);
			return 0;
		}
	}

	if (untracked->check_only != !!check_only)
		return 0;

	/*
	 * prep_exclude will be called eventually on this directory,
	 * but it's called much later in last_matching_pattern(). We
	 * need it now to determine the validity of the cache for this
	 * path. The next calls will be nearly no-op, the way
	 * prep_exclude() is designed.
	 */
	if (path->len && path->buf[path->len - 1] != '/') {
		strbuf_addch(path, '/');
		prep_exclude(dir, istate, path->buf, path->len);
		strbuf_setlen(path, path->len - 1);
	} else
		prep_exclude(dir, istate, path->buf, path->len);

	/* hopefully prep_exclude() haven't invalidated this entry... */
	return untracked->valid;
}

static int open_cached_dir(struct cached_dir *cdir,
			   struct dir_struct *dir,
			   struct untracked_cache_dir *untracked,
			   struct index_state *istate,
			   struct strbuf *path,
			   int check_only)
{
	const char *c_path;

	memset(cdir, 0, sizeof(*cdir));
	cdir->untracked = untracked;
	if (valid_cached_dir(dir, untracked, istate, path, check_only))
		return 0;
	c_path = path->len ? path->buf : ".";
	cdir->fdir = opendir(c_path);
	if (!cdir->fdir)
		warning_errno(_("could not open directory '%s'"), c_path);
	if (dir->untracked) {
		invalidate_directory(dir->untracked, untracked);
		dir->untracked->dir_opened++;
	}
	if (!cdir->fdir)
		return -1;
	return 0;
}

static int read_cached_dir(struct cached_dir *cdir)
{
	struct dirent *de;

	if (cdir->fdir) {
		de = readdir_skip_dot_and_dotdot(cdir->fdir);
		if (!de) {
			cdir->d_name = NULL;
			cdir->d_type = DT_UNKNOWN;
			return -1;
		}
		cdir->d_name = de->d_name;
		cdir->d_type = DTYPE(de);
		return 0;
	}
	while (cdir->nr_dirs < cdir->untracked->dirs_nr) {
		struct untracked_cache_dir *d = cdir->untracked->dirs[cdir->nr_dirs];
		if (!d->recurse) {
			cdir->nr_dirs++;
			continue;
		}
		cdir->ucd = d;
		cdir->nr_dirs++;
		return 0;
	}
	cdir->ucd = NULL;
	if (cdir->nr_files < cdir->untracked->untracked_nr) {
		struct untracked_cache_dir *d = cdir->untracked;
		cdir->file = d->untracked[cdir->nr_files++];
		return 0;
	}
	return -1;
}

static void close_cached_dir(struct cached_dir *cdir)
{
	if (cdir->fdir)
		closedir(cdir->fdir);
	/*
	 * We have gone through this directory and found no untracked
	 * entries. Mark it valid.
	 */
	if (cdir->untracked) {
		cdir->untracked->valid = 1;
		cdir->untracked->recurse = 1;
	}
}

static void add_path_to_appropriate_result_list(struct dir_struct *dir,
	struct untracked_cache_dir *untracked,
	struct cached_dir *cdir,
	struct index_state *istate,
	struct strbuf *path,
	int baselen,
	const struct pathspec *pathspec,
	enum path_treatment state)
{
	/* add the path to the appropriate result list */
	switch (state) {
	case path_excluded:
		if (dir->flags & DIR_SHOW_IGNORED)
			dir_add_name(dir, istate, path->buf, path->len);
		else if ((dir->flags & DIR_SHOW_IGNORED_TOO) ||
			((dir->flags & DIR_COLLECT_IGNORED) &&
			exclude_matches_pathspec(path->buf, path->len,
						 pathspec)))
			dir_add_ignored(dir, istate, path->buf, path->len);
		break;

	case path_untracked:
		if (dir->flags & DIR_SHOW_IGNORED)
			break;
		dir_add_name(dir, istate, path->buf, path->len);
		if (cdir->fdir)
			add_untracked(untracked, path->buf + baselen);
		break;

	default:
		break;
	}
}

/*
 * Read a directory tree. We currently ignore anything but
 * directories, regular files and symlinks. That's because git
 * doesn't handle them at all yet. Maybe that will change some
 * day.
 *
 * Also, we ignore the name ".git" (even if it is not a directory).
 * That likely will not change.
 *
 * If 'stop_at_first_file' is specified, 'path_excluded' is returned
 * to signal that a file was found. This is the least significant value that
 * indicates that a file was encountered that does not depend on the order of
 * whether an untracked or excluded path was encountered first.
 *
 * Returns the most significant path_treatment value encountered in the scan.
 * If 'stop_at_first_file' is specified, `path_excluded` is the most
 * significant path_treatment value that will be returned.
 */

static enum path_treatment read_directory_recursive(struct dir_struct *dir,
	struct index_state *istate, const char *base, int baselen,
	struct untracked_cache_dir *untracked, int check_only,
	int stop_at_first_file, const struct pathspec *pathspec)
{
	/*
	 * WARNING: Do NOT recurse unless path_recurse is returned from
	 *          treat_path().  Recursing on any other return value
	 *          can result in exponential slowdown.
	 */
	struct cached_dir cdir;
	enum path_treatment state, subdir_state, dir_state = path_none;
	struct strbuf path = STRBUF_INIT;

	strbuf_add(&path, base, baselen);

	if (open_cached_dir(&cdir, dir, untracked, istate, &path, check_only))
		goto out;
	dir->internal.visited_directories++;

	if (untracked)
		untracked->check_only = !!check_only;

	while (!read_cached_dir(&cdir)) {
		/* check how the file or directory should be treated */
		state = treat_path(dir, untracked, &cdir, istate, &path,
				   baselen, pathspec);
		dir->internal.visited_paths++;

		if (state > dir_state)
			dir_state = state;

		/* recurse into subdir if instructed by treat_path */
		if (state == path_recurse) {
			struct untracked_cache_dir *ud;
			ud = lookup_untracked(dir->untracked,
					      untracked,
					      path.buf + baselen,
					      path.len - baselen);
			subdir_state =
				read_directory_recursive(dir, istate, path.buf,
							 path.len, ud,
							 check_only, stop_at_first_file, pathspec);
			if (subdir_state > dir_state)
				dir_state = subdir_state;

			if (pathspec &&
			    !match_pathspec(istate, pathspec, path.buf, path.len,
					    0 /* prefix */, NULL,
					    0 /* do NOT special case dirs */))
				state = path_none;
		}

		if (check_only) {
			if (stop_at_first_file) {
				/*
				 * If stopping at first file, then
				 * signal that a file was found by
				 * returning `path_excluded`. This is
				 * to return a consistent value
				 * regardless of whether an ignored or
				 * excluded file happened to be
				 * encountered 1st.
				 *
				 * In current usage, the
				 * `stop_at_first_file` is passed when
				 * an ancestor directory has matched
				 * an exclude pattern, so any found
				 * files will be excluded.
				 */
				if (dir_state >= path_excluded) {
					dir_state = path_excluded;
					break;
				}
			}

			/* abort early if maximum state has been reached */
			if (dir_state == path_untracked) {
				if (cdir.fdir)
					add_untracked(untracked, path.buf + baselen);
				break;
			}
			/* skip the add_path_to_appropriate_result_list() */
			continue;
		}

		add_path_to_appropriate_result_list(dir, untracked, &cdir,
						    istate, &path, baselen,
						    pathspec, state);
	}
	close_cached_dir(&cdir);
 out:
	strbuf_release(&path);

	return dir_state;
}

int cmp_dir_entry(const void *p1, const void *p2)
{
	const struct dir_entry *e1 = *(const struct dir_entry **)p1;
	const struct dir_entry *e2 = *(const struct dir_entry **)p2;

	return name_compare(e1->name, e1->len, e2->name, e2->len);
}

/* check if *out lexically strictly contains *in */
int check_dir_entry_contains(const struct dir_entry *out, const struct dir_entry *in)
{
	return (out->len < in->len) &&
		(out->name[out->len - 1] == '/') &&
		!memcmp(out->name, in->name, out->len);
}

static int treat_leading_path(struct dir_struct *dir,
			      struct index_state *istate,
			      const char *path, int len,
			      const struct pathspec *pathspec)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf subdir = STRBUF_INIT;
	int prevlen, baselen;
	const char *cp;
	struct cached_dir cdir;
	enum path_treatment state = path_none;

	/*
	 * For each directory component of path, we are going to check whether
	 * that path is relevant given the pathspec.  For example, if path is
	 *    foo/bar/baz/
	 * then we will ask treat_path() whether we should go into foo, then
	 * whether we should go into bar, then whether baz is relevant.
	 * Checking each is important because e.g. if path is
	 *    .git/info/
	 * then we need to check .git to know we shouldn't traverse it.
	 * If the return from treat_path() is:
	 *    * path_none, for any path, we return false.
	 *    * path_recurse, for all path components, we return true
	 *    * <anything else> for some intermediate component, we make sure
	 *        to add that path to the relevant list but return false
	 *        signifying that we shouldn't recurse into it.
	 */

	while (len && path[len - 1] == '/')
		len--;
	if (!len)
		return 1;

	memset(&cdir, 0, sizeof(cdir));
	cdir.d_type = DT_DIR;
	baselen = 0;
	prevlen = 0;
	while (1) {
		prevlen = baselen + !!baselen;
		cp = path + prevlen;
		cp = memchr(cp, '/', path + len - cp);
		if (!cp)
			baselen = len;
		else
			baselen = cp - path;
		strbuf_reset(&sb);
		strbuf_add(&sb, path, baselen);
		if (!is_directory(sb.buf))
			break;
		strbuf_reset(&sb);
		strbuf_add(&sb, path, prevlen);
		strbuf_reset(&subdir);
		strbuf_add(&subdir, path+prevlen, baselen-prevlen);
		cdir.d_name = subdir.buf;
		state = treat_path(dir, NULL, &cdir, istate, &sb, prevlen, pathspec);

		if (state != path_recurse)
			break; /* do not recurse into it */
		if (len <= baselen)
			break; /* finished checking */
	}
	add_path_to_appropriate_result_list(dir, NULL, &cdir, istate,
					    &sb, baselen, pathspec,
					    state);

	strbuf_release(&subdir);
	strbuf_release(&sb);
	return state == path_recurse;
}

static const char *get_ident_string(void)
{
	static struct strbuf sb = STRBUF_INIT;
	struct utsname uts;

	if (sb.len)
		return sb.buf;
	if (uname(&uts) < 0)
		die_errno(_("failed to get kernel name and information"));
	strbuf_addf(&sb, "Location %s, system %s", get_git_work_tree(),
		    uts.sysname);
	return sb.buf;
}

static int ident_in_untracked(const struct untracked_cache *uc)
{
	/*
	 * Previous git versions may have saved many NUL separated
	 * strings in the "ident" field, but it is insane to manage
	 * many locations, so just take care of the first one.
	 */

	return !strcmp(uc->ident.buf, get_ident_string());
}

static void set_untracked_ident(struct untracked_cache *uc)
{
	strbuf_reset(&uc->ident);
	strbuf_addstr(&uc->ident, get_ident_string());

	/*
	 * This strbuf used to contain a list of NUL separated
	 * strings, so save NUL too for backward compatibility.
	 */
	strbuf_addch(&uc->ident, 0);
}

static unsigned new_untracked_cache_flags(struct index_state *istate)
{
	struct repository *repo = istate->repo;
	char *val;

	/*
	 * This logic is coordinated with the setting of these flags in
	 * wt-status.c#wt_status_collect_untracked(), and the evaluation
	 * of the config setting in commit.c#git_status_config()
	 */
	if (!repo_config_get_string(repo, "status.showuntrackedfiles", &val) &&
	    !strcmp(val, "all"))
		return 0;

	/*
	 * The default, if "all" is not set, is "normal" - leading us here.
	 * If the value is "none" then it really doesn't matter.
	 */
	return DIR_SHOW_OTHER_DIRECTORIES | DIR_HIDE_EMPTY_DIRECTORIES;
}

static void new_untracked_cache(struct index_state *istate, int flags)
{
	struct untracked_cache *uc = xcalloc(1, sizeof(*uc));
	strbuf_init(&uc->ident, 100);
	uc->exclude_per_dir = ".gitignore";
	uc->dir_flags = flags >= 0 ? flags : new_untracked_cache_flags(istate);
	set_untracked_ident(uc);
	istate->untracked = uc;
	istate->cache_changed |= UNTRACKED_CHANGED;
}

void add_untracked_cache(struct index_state *istate)
{
	if (!istate->untracked) {
		new_untracked_cache(istate, -1);
	} else {
		if (!ident_in_untracked(istate->untracked)) {
			free_untracked_cache(istate->untracked);
			new_untracked_cache(istate, -1);
		}
	}
}

void remove_untracked_cache(struct index_state *istate)
{
	if (istate->untracked) {
		free_untracked_cache(istate->untracked);
		istate->untracked = NULL;
		istate->cache_changed |= UNTRACKED_CHANGED;
	}
}

static struct untracked_cache_dir *validate_untracked_cache(struct dir_struct *dir,
						      int base_len,
						      const struct pathspec *pathspec,
						      struct index_state *istate)
{
	struct untracked_cache_dir *root;
	static int untracked_cache_disabled = -1;

	if (!dir->untracked)
		return NULL;
	if (untracked_cache_disabled < 0)
		untracked_cache_disabled = git_env_bool("GIT_DISABLE_UNTRACKED_CACHE", 0);
	if (untracked_cache_disabled)
		return NULL;

	/*
	 * We only support $GIT_DIR/info/exclude and core.excludesfile
	 * as the global ignore rule files. Any other additions
	 * (e.g. from command line) invalidate the cache. This
	 * condition also catches running setup_standard_excludes()
	 * before setting dir->untracked!
	 */
	if (dir->internal.unmanaged_exclude_files)
		return NULL;

	/*
	 * Optimize for the main use case only: whole-tree git
	 * status. More work involved in treat_leading_path() if we
	 * use cache on just a subset of the worktree. pathspec
	 * support could make the matter even worse.
	 */
	if (base_len || (pathspec && pathspec->nr))
		return NULL;

	/* We don't support collecting ignore files */
	if (dir->flags & (DIR_SHOW_IGNORED | DIR_SHOW_IGNORED_TOO |
			DIR_COLLECT_IGNORED))
		return NULL;

	/*
	 * If we use .gitignore in the cache and now you change it to
	 * .gitexclude, everything will go wrong.
	 */
	if (dir->exclude_per_dir != dir->untracked->exclude_per_dir &&
	    strcmp(dir->exclude_per_dir, dir->untracked->exclude_per_dir))
		return NULL;

	/*
	 * EXC_CMDL is not considered in the cache. If people set it,
	 * skip the cache.
	 */
	if (dir->internal.exclude_list_group[EXC_CMDL].nr)
		return NULL;

	if (!ident_in_untracked(dir->untracked)) {
		warning(_("untracked cache is disabled on this system or location"));
		return NULL;
	}

	/*
	 * If the untracked structure we received does not have the same flags
	 * as requested in this run, we're going to need to either discard the
	 * existing structure (and potentially later recreate), or bypass the
	 * untracked cache mechanism for this run.
	 */
	if (dir->flags != dir->untracked->dir_flags) {
		/*
		 * If the untracked structure we received does not have the same flags
		 * as configured, then we need to reset / create a new "untracked"
		 * structure to match the new config.
		 *
		 * Keeping the saved and used untracked cache consistent with the
		 * configuration provides an opportunity for frequent users of
		 * "git status -uall" to leverage the untracked cache by aligning their
		 * configuration - setting "status.showuntrackedfiles" to "all" or
		 * "normal" as appropriate.
		 *
		 * Previously using -uall (or setting "status.showuntrackedfiles" to
		 * "all") was incompatible with untracked cache and *consistently*
		 * caused surprisingly bad performance (with fscache and fsmonitor
		 * enabled) on Windows.
		 *
		 * IMPROVEMENT OPPORTUNITY: If we reworked the untracked cache storage
		 * to not be as bound up with the desired output in a given run,
		 * and instead iterated through and stored enough information to
		 * correctly serve both "modes", then users could get peak performance
		 * with or without '-uall' regardless of their
		 * "status.showuntrackedfiles" config.
		 */
		if (dir->untracked->dir_flags != new_untracked_cache_flags(istate)) {
			free_untracked_cache(istate->untracked);
			new_untracked_cache(istate, dir->flags);
			dir->untracked = istate->untracked;
		}
		else {
			/*
			 * Current untracked cache data is consistent with config, but not
			 * usable in this request/run; just bypass untracked cache.
			 */
			return NULL;
		}
	}

	if (!dir->untracked->root) {
		/* Untracked cache existed but is not initialized; fix that */
		FLEX_ALLOC_STR(dir->untracked->root, name, "");
		istate->cache_changed |= UNTRACKED_CHANGED;
	}

	/* Validate $GIT_DIR/info/exclude and core.excludesfile */
	root = dir->untracked->root;
	if (!oideq(&dir->internal.ss_info_exclude.oid,
		   &dir->untracked->ss_info_exclude.oid)) {
		invalidate_gitignore(dir->untracked, root);
		dir->untracked->ss_info_exclude = dir->internal.ss_info_exclude;
	}
	if (!oideq(&dir->internal.ss_excludes_file.oid,
		   &dir->untracked->ss_excludes_file.oid)) {
		invalidate_gitignore(dir->untracked, root);
		dir->untracked->ss_excludes_file = dir->internal.ss_excludes_file;
	}

	/* Make sure this directory is not dropped out at saving phase */
	root->recurse = 1;
	return root;
}

static void emit_traversal_statistics(struct dir_struct *dir,
				      struct repository *repo,
				      const char *path,
				      int path_len)
{
	if (!trace2_is_enabled())
		return;

	if (!path_len) {
		trace2_data_string("read_directory", repo, "path", "");
	} else {
		struct strbuf tmp = STRBUF_INIT;
		strbuf_add(&tmp, path, path_len);
		trace2_data_string("read_directory", repo, "path", tmp.buf);
		strbuf_release(&tmp);
	}

	trace2_data_intmax("read_directory", repo,
			   "directories-visited", dir->internal.visited_directories);
	trace2_data_intmax("read_directory", repo,
			   "paths-visited", dir->internal.visited_paths);

	if (!dir->untracked)
		return;
	trace2_data_intmax("read_directory", repo,
			   "node-creation", dir->untracked->dir_created);
	trace2_data_intmax("read_directory", repo,
			   "gitignore-invalidation",
			   dir->untracked->gitignore_invalidated);
	trace2_data_intmax("read_directory", repo,
			   "directory-invalidation",
			   dir->untracked->dir_invalidated);
	trace2_data_intmax("read_directory", repo,
			   "opendir", dir->untracked->dir_opened);
}

int read_directory(struct dir_struct *dir, struct index_state *istate,
		   const char *path, int len, const struct pathspec *pathspec)
{
	struct untracked_cache_dir *untracked;

	trace2_region_enter("dir", "read_directory", istate->repo);
	dir->internal.visited_paths = 0;
	dir->internal.visited_directories = 0;

	if (has_symlink_leading_path(path, len)) {
		trace2_region_leave("dir", "read_directory", istate->repo);
		return dir->nr;
	}

	untracked = validate_untracked_cache(dir, len, pathspec, istate);
	if (!untracked)
		/*
		 * make sure untracked cache code path is disabled,
		 * e.g. prep_exclude()
		 */
		dir->untracked = NULL;
	if (!len || treat_leading_path(dir, istate, path, len, pathspec))
		read_directory_recursive(dir, istate, path, len, untracked, 0, 0, pathspec);
	QSORT(dir->entries, dir->nr, cmp_dir_entry);
	QSORT(dir->ignored, dir->ignored_nr, cmp_dir_entry);

	emit_traversal_statistics(dir, istate->repo, path, len);

	trace2_region_leave("dir", "read_directory", istate->repo);
	if (dir->untracked) {
		static int force_untracked_cache = -1;

		if (force_untracked_cache < 0)
			force_untracked_cache =
				git_env_bool("GIT_FORCE_UNTRACKED_CACHE", -1);
		if (force_untracked_cache < 0)
			force_untracked_cache = (istate->repo->settings.core_untracked_cache == UNTRACKED_CACHE_WRITE);
		if (force_untracked_cache &&
			dir->untracked == istate->untracked &&
		    (dir->untracked->dir_opened ||
		     dir->untracked->gitignore_invalidated ||
		     dir->untracked->dir_invalidated))
			istate->cache_changed |= UNTRACKED_CHANGED;
		if (dir->untracked != istate->untracked) {
			FREE_AND_NULL(dir->untracked);
		}
	}

	return dir->nr;
}

int file_exists(const char *f)
{
	struct stat sb;
	return lstat(f, &sb) == 0;
}

int repo_file_exists(struct repository *repo, const char *path)
{
	if (repo != the_repository)
		BUG("do not know how to check file existence in arbitrary repo");

	return file_exists(path);
}

static int cmp_icase(char a, char b)
{
	if (a == b)
		return 0;
	if (ignore_case)
		return toupper(a) - toupper(b);
	return a - b;
}

/*
 * Given two normalized paths (a trailing slash is ok), if subdir is
 * outside dir, return -1.  Otherwise return the offset in subdir that
 * can be used as relative path to dir.
 */
int dir_inside_of(const char *subdir, const char *dir)
{
	int offset = 0;

	assert(dir && subdir && *dir && *subdir);

	while (*dir && *subdir && !cmp_icase(*dir, *subdir)) {
		dir++;
		subdir++;
		offset++;
	}

	/* hel[p]/me vs hel[l]/yeah */
	if (*dir && *subdir)
		return -1;

	if (!*subdir)
		return !*dir ? offset : -1; /* same dir */

	/* foo/[b]ar vs foo/[] */
	if (is_dir_sep(dir[-1]))
		return is_dir_sep(subdir[-1]) ? offset : -1;

	/* foo[/]bar vs foo[] */
	return is_dir_sep(*subdir) ? offset + 1 : -1;
}

int is_inside_dir(const char *dir)
{
	char *cwd;
	int rc;

	if (!dir)
		return 0;

	cwd = xgetcwd();
	rc = (dir_inside_of(cwd, dir) >= 0);
	free(cwd);
	return rc;
}

int is_empty_dir(const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *e;
	int ret = 1;

	if (!dir)
		return 0;

	e = readdir_skip_dot_and_dotdot(dir);
	if (e)
		ret = 0;

	closedir(dir);
	return ret;
}

char *git_url_basename(const char *repo, int is_bundle, int is_bare)
{
	const char *end = repo + strlen(repo), *start, *ptr;
	size_t len;
	char *dir;

	/*
	 * Skip scheme.
	 */
	start = strstr(repo, "://");
	if (!start)
		start = repo;
	else
		start += 3;

	/*
	 * Skip authentication data. The stripping does happen
	 * greedily, such that we strip up to the last '@' inside
	 * the host part.
	 */
	for (ptr = start; ptr < end && !is_dir_sep(*ptr); ptr++) {
		if (*ptr == '@')
			start = ptr + 1;
	}

	/*
	 * Strip trailing spaces, slashes and /.git
	 */
	while (start < end && (is_dir_sep(end[-1]) || isspace(end[-1])))
		end--;
	if (end - start > 5 && is_dir_sep(end[-5]) &&
	    !strncmp(end - 4, ".git", 4)) {
		end -= 5;
		while (start < end && is_dir_sep(end[-1]))
			end--;
	}

	/*
	 * It should not be possible to overflow `ptrdiff_t` by passing in an
	 * insanely long URL, but GCC does not know that and will complain
	 * without this check.
	 */
	if (end - start < 0)
		die(_("No directory name could be guessed.\n"
		      "Please specify a directory on the command line"));

	/*
	 * Strip trailing port number if we've got only a
	 * hostname (that is, there is no dir separator but a
	 * colon). This check is required such that we do not
	 * strip URI's like '/foo/bar:2222.git', which should
	 * result in a dir '2222' being guessed due to backwards
	 * compatibility.
	 */
	if (memchr(start, '/', end - start) == NULL
	    && memchr(start, ':', end - start) != NULL) {
		ptr = end;
		while (start < ptr && isdigit(ptr[-1]) && ptr[-1] != ':')
			ptr--;
		if (start < ptr && ptr[-1] == ':')
			end = ptr - 1;
	}

	/*
	 * Find last component. To remain backwards compatible we
	 * also regard colons as path separators, such that
	 * cloning a repository 'foo:bar.git' would result in a
	 * directory 'bar' being guessed.
	 */
	ptr = end;
	while (start < ptr && !is_dir_sep(ptr[-1]) && ptr[-1] != ':')
		ptr--;
	start = ptr;

	/*
	 * Strip .{bundle,git}.
	 */
	len = end - start;
	strip_suffix_mem(start, &len, is_bundle ? ".bundle" : ".git");

	if (!len || (len == 1 && *start == '/'))
		die(_("No directory name could be guessed.\n"
		      "Please specify a directory on the command line"));

	if (is_bare)
		dir = xstrfmt("%.*s.git", (int)len, start);
	else
		dir = xstrndup(start, len);
	/*
	 * Replace sequences of 'control' characters and whitespace
	 * with one ascii space, remove leading and trailing spaces.
	 */
	if (*dir) {
		char *out = dir;
		int prev_space = 1 /* strip leading whitespace */;
		for (end = dir; *end; ++end) {
			char ch = *end;
			if ((unsigned char)ch < '\x20')
				ch = '\x20';
			if (isspace(ch)) {
				if (prev_space)
					continue;
				prev_space = 1;
			} else
				prev_space = 0;
			*out++ = ch;
		}
		*out = '\0';
		if (out > dir && prev_space)
			out[-1] = '\0';
	}
	return dir;
}

void strip_dir_trailing_slashes(char *dir)
{
	char *end = dir + strlen(dir);

	while (dir < end - 1 && is_dir_sep(end[-1]))
		end--;
	*end = '\0';
}

static int remove_dir_recurse(struct strbuf *path, int flag, int *kept_up)
{
	DIR *dir;
	struct dirent *e;
	int ret = 0, original_len = path->len, len, kept_down = 0;
	int only_empty = (flag & REMOVE_DIR_EMPTY_ONLY);
	int keep_toplevel = (flag & REMOVE_DIR_KEEP_TOPLEVEL);
	int purge_original_cwd = (flag & REMOVE_DIR_PURGE_ORIGINAL_CWD);
	struct object_id submodule_head;

	if ((flag & REMOVE_DIR_KEEP_NESTED_GIT) &&
	    !repo_resolve_gitlink_ref(the_repository, path->buf,
				      "HEAD", &submodule_head)) {
		/* Do not descend and nuke a nested git work tree. */
		if (kept_up)
			*kept_up = 1;
		return 0;
	}

	flag &= ~REMOVE_DIR_KEEP_TOPLEVEL;
	dir = opendir(path->buf);
	if (!dir) {
		if (errno == ENOENT)
			return keep_toplevel ? -1 : 0;
		else if (errno == EACCES && !keep_toplevel)
			/*
			 * An empty dir could be removable even if it
			 * is unreadable:
			 */
			return rmdir(path->buf);
		else
			return -1;
	}
	strbuf_complete(path, '/');

	len = path->len;
	while ((e = readdir_skip_dot_and_dotdot(dir)) != NULL) {
		struct stat st;

		strbuf_setlen(path, len);
		strbuf_addstr(path, e->d_name);
		if (lstat(path->buf, &st)) {
			if (errno == ENOENT)
				/*
				 * file disappeared, which is what we
				 * wanted anyway
				 */
				continue;
			/* fall through */
		} else if (S_ISDIR(st.st_mode)) {
			if (!remove_dir_recurse(path, flag, &kept_down))
				continue; /* happy */
		} else if (!only_empty &&
			   (!unlink(path->buf) || errno == ENOENT)) {
			continue; /* happy, too */
		}

		/* path too long, stat fails, or non-directory still exists */
		ret = -1;
		break;
	}
	closedir(dir);

	strbuf_setlen(path, original_len);
	if (!ret && !keep_toplevel && !kept_down) {
		if (!purge_original_cwd &&
		    startup_info->original_cwd &&
		    !strcmp(startup_info->original_cwd, path->buf))
			ret = -1; /* Do not remove current working directory */
		else
			ret = (!rmdir(path->buf) || errno == ENOENT) ? 0 : -1;
	} else if (kept_up)
		/*
		 * report the uplevel that it is not an error that we
		 * did not rmdir() our directory.
		 */
		*kept_up = !ret;
	return ret;
}

int remove_dir_recursively(struct strbuf *path, int flag)
{
	return remove_dir_recurse(path, flag, NULL);
}

static GIT_PATH_FUNC(git_path_info_exclude, "info/exclude")

void setup_standard_excludes(struct dir_struct *dir)
{
	dir->exclude_per_dir = ".gitignore";

	/* core.excludesfile defaulting to $XDG_CONFIG_HOME/git/ignore */
	if (!excludes_file)
		excludes_file = xdg_config_home("ignore");
	if (excludes_file && !access_or_warn(excludes_file, R_OK, 0))
		add_patterns_from_file_1(dir, excludes_file,
					 dir->untracked ? &dir->internal.ss_excludes_file : NULL);

	/* per repository user preference */
	if (startup_info->have_repository) {
		const char *path = git_path_info_exclude();
		if (!access_or_warn(path, R_OK, 0))
			add_patterns_from_file_1(dir, path,
						 dir->untracked ? &dir->internal.ss_info_exclude : NULL);
	}
}

char *get_sparse_checkout_filename(void)
{
	return git_pathdup("info/sparse-checkout");
}

int get_sparse_checkout_patterns(struct pattern_list *pl)
{
	int res;
	char *sparse_filename = get_sparse_checkout_filename();

	pl->use_cone_patterns = core_sparse_checkout_cone;
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, pl, NULL, 0);

	free(sparse_filename);
	return res;
}

int remove_path(const char *name)
{
	char *slash;

	if (unlink(name) && !is_missing_file_error(errno))
		return -1;

	slash = strrchr(name, '/');
	if (slash) {
		char *dirs = xstrdup(name);
		slash = dirs + (slash - name);
		do {
			*slash = '\0';
			if (startup_info->original_cwd &&
			    !strcmp(startup_info->original_cwd, dirs))
				break;
		} while (rmdir(dirs) == 0 && (slash = strrchr(dirs, '/')));
		free(dirs);
	}
	return 0;
}

/*
 * Frees memory within dir which was allocated, and resets fields for further
 * use.  Does not free dir itself.
 */
void dir_clear(struct dir_struct *dir)
{
	int i, j;
	struct exclude_list_group *group;
	struct pattern_list *pl;
	struct exclude_stack *stk;
	struct dir_struct new = DIR_INIT;

	for (i = EXC_CMDL; i <= EXC_FILE; i++) {
		group = &dir->internal.exclude_list_group[i];
		for (j = 0; j < group->nr; j++) {
			pl = &group->pl[j];
			if (i == EXC_DIRS)
				free((char *)pl->src);
			clear_pattern_list(pl);
		}
		free(group->pl);
	}

	for (i = 0; i < dir->ignored_nr; i++)
		free(dir->ignored[i]);
	for (i = 0; i < dir->nr; i++)
		free(dir->entries[i]);
	free(dir->ignored);
	free(dir->entries);

	stk = dir->internal.exclude_stack;
	while (stk) {
		struct exclude_stack *prev = stk->prev;
		free(stk);
		stk = prev;
	}
	strbuf_release(&dir->internal.basebuf);

	memcpy(dir, &new, sizeof(*dir));
}

struct ondisk_untracked_cache {
	struct stat_data info_exclude_stat;
	struct stat_data excludes_file_stat;
	uint32_t dir_flags;
};

#define ouc_offset(x) offsetof(struct ondisk_untracked_cache, x)

struct write_data {
	int index;	   /* number of written untracked_cache_dir */
	struct ewah_bitmap *check_only; /* from untracked_cache_dir */
	struct ewah_bitmap *valid;	/* from untracked_cache_dir */
	struct ewah_bitmap *sha1_valid; /* set if exclude_sha1 is not null */
	struct strbuf out;
	struct strbuf sb_stat;
	struct strbuf sb_sha1;
};

static void stat_data_to_disk(struct stat_data *to, const struct stat_data *from)
{
	to->sd_ctime.sec  = htonl(from->sd_ctime.sec);
	to->sd_ctime.nsec = htonl(from->sd_ctime.nsec);
	to->sd_mtime.sec  = htonl(from->sd_mtime.sec);
	to->sd_mtime.nsec = htonl(from->sd_mtime.nsec);
	to->sd_dev	  = htonl(from->sd_dev);
	to->sd_ino	  = htonl(from->sd_ino);
	to->sd_uid	  = htonl(from->sd_uid);
	to->sd_gid	  = htonl(from->sd_gid);
	to->sd_size	  = htonl(from->sd_size);
}

static void write_one_dir(struct untracked_cache_dir *untracked,
			  struct write_data *wd)
{
	struct stat_data stat_data;
	struct strbuf *out = &wd->out;
	unsigned char intbuf[16];
	unsigned int intlen, value;
	int i = wd->index++;

	/*
	 * untracked_nr should be reset whenever valid is clear, but
	 * for safety..
	 */
	if (!untracked->valid) {
		untracked->untracked_nr = 0;
		untracked->check_only = 0;
	}

	if (untracked->check_only)
		ewah_set(wd->check_only, i);
	if (untracked->valid) {
		ewah_set(wd->valid, i);
		stat_data_to_disk(&stat_data, &untracked->stat_data);
		strbuf_add(&wd->sb_stat, &stat_data, sizeof(stat_data));
	}
	if (!is_null_oid(&untracked->exclude_oid)) {
		ewah_set(wd->sha1_valid, i);
		strbuf_add(&wd->sb_sha1, untracked->exclude_oid.hash,
			   the_hash_algo->rawsz);
	}

	intlen = encode_varint(untracked->untracked_nr, intbuf);
	strbuf_add(out, intbuf, intlen);

	/* skip non-recurse directories */
	for (i = 0, value = 0; i < untracked->dirs_nr; i++)
		if (untracked->dirs[i]->recurse)
			value++;
	intlen = encode_varint(value, intbuf);
	strbuf_add(out, intbuf, intlen);

	strbuf_add(out, untracked->name, strlen(untracked->name) + 1);

	for (i = 0; i < untracked->untracked_nr; i++)
		strbuf_add(out, untracked->untracked[i],
			   strlen(untracked->untracked[i]) + 1);

	for (i = 0; i < untracked->dirs_nr; i++)
		if (untracked->dirs[i]->recurse)
			write_one_dir(untracked->dirs[i], wd);
}

void write_untracked_extension(struct strbuf *out, struct untracked_cache *untracked)
{
	struct ondisk_untracked_cache *ouc;
	struct write_data wd;
	unsigned char varbuf[16];
	int varint_len;
	const unsigned hashsz = the_hash_algo->rawsz;

	CALLOC_ARRAY(ouc, 1);
	stat_data_to_disk(&ouc->info_exclude_stat, &untracked->ss_info_exclude.stat);
	stat_data_to_disk(&ouc->excludes_file_stat, &untracked->ss_excludes_file.stat);
	ouc->dir_flags = htonl(untracked->dir_flags);

	varint_len = encode_varint(untracked->ident.len, varbuf);
	strbuf_add(out, varbuf, varint_len);
	strbuf_addbuf(out, &untracked->ident);

	strbuf_add(out, ouc, sizeof(*ouc));
	strbuf_add(out, untracked->ss_info_exclude.oid.hash, hashsz);
	strbuf_add(out, untracked->ss_excludes_file.oid.hash, hashsz);
	strbuf_add(out, untracked->exclude_per_dir, strlen(untracked->exclude_per_dir) + 1);
	FREE_AND_NULL(ouc);

	if (!untracked->root) {
		varint_len = encode_varint(0, varbuf);
		strbuf_add(out, varbuf, varint_len);
		return;
	}

	wd.index      = 0;
	wd.check_only = ewah_new();
	wd.valid      = ewah_new();
	wd.sha1_valid = ewah_new();
	strbuf_init(&wd.out, 1024);
	strbuf_init(&wd.sb_stat, 1024);
	strbuf_init(&wd.sb_sha1, 1024);
	write_one_dir(untracked->root, &wd);

	varint_len = encode_varint(wd.index, varbuf);
	strbuf_add(out, varbuf, varint_len);
	strbuf_addbuf(out, &wd.out);
	ewah_serialize_strbuf(wd.valid, out);
	ewah_serialize_strbuf(wd.check_only, out);
	ewah_serialize_strbuf(wd.sha1_valid, out);
	strbuf_addbuf(out, &wd.sb_stat);
	strbuf_addbuf(out, &wd.sb_sha1);
	strbuf_addch(out, '\0'); /* safe guard for string lists */

	ewah_free(wd.valid);
	ewah_free(wd.check_only);
	ewah_free(wd.sha1_valid);
	strbuf_release(&wd.out);
	strbuf_release(&wd.sb_stat);
	strbuf_release(&wd.sb_sha1);
}

static void free_untracked(struct untracked_cache_dir *ucd)
{
	int i;
	if (!ucd)
		return;
	for (i = 0; i < ucd->dirs_nr; i++)
		free_untracked(ucd->dirs[i]);
	for (i = 0; i < ucd->untracked_nr; i++)
		free(ucd->untracked[i]);
	free(ucd->untracked);
	free(ucd->dirs);
	free(ucd);
}

void free_untracked_cache(struct untracked_cache *uc)
{
	if (!uc)
		return;

	free(uc->exclude_per_dir_to_free);
	strbuf_release(&uc->ident);
	free_untracked(uc->root);
	free(uc);
}

struct read_data {
	int index;
	struct untracked_cache_dir **ucd;
	struct ewah_bitmap *check_only;
	struct ewah_bitmap *valid;
	struct ewah_bitmap *sha1_valid;
	const unsigned char *data;
	const unsigned char *end;
};

static void stat_data_from_disk(struct stat_data *to, const unsigned char *data)
{
	memcpy(to, data, sizeof(*to));
	to->sd_ctime.sec  = ntohl(to->sd_ctime.sec);
	to->sd_ctime.nsec = ntohl(to->sd_ctime.nsec);
	to->sd_mtime.sec  = ntohl(to->sd_mtime.sec);
	to->sd_mtime.nsec = ntohl(to->sd_mtime.nsec);
	to->sd_dev	  = ntohl(to->sd_dev);
	to->sd_ino	  = ntohl(to->sd_ino);
	to->sd_uid	  = ntohl(to->sd_uid);
	to->sd_gid	  = ntohl(to->sd_gid);
	to->sd_size	  = ntohl(to->sd_size);
}

static int read_one_dir(struct untracked_cache_dir **untracked_,
			struct read_data *rd)
{
	struct untracked_cache_dir ud, *untracked;
	const unsigned char *data = rd->data, *end = rd->end;
	const unsigned char *eos;
	unsigned int value;
	int i;

	memset(&ud, 0, sizeof(ud));

	value = decode_varint(&data);
	if (data > end)
		return -1;
	ud.recurse	   = 1;
	ud.untracked_alloc = value;
	ud.untracked_nr	   = value;
	if (ud.untracked_nr)
		ALLOC_ARRAY(ud.untracked, ud.untracked_nr);

	ud.dirs_alloc = ud.dirs_nr = decode_varint(&data);
	if (data > end)
		return -1;
	ALLOC_ARRAY(ud.dirs, ud.dirs_nr);

	eos = memchr(data, '\0', end - data);
	if (!eos || eos == end)
		return -1;

	*untracked_ = untracked = xmalloc(st_add3(sizeof(*untracked), eos - data, 1));
	memcpy(untracked, &ud, sizeof(ud));
	memcpy(untracked->name, data, eos - data + 1);
	data = eos + 1;

	for (i = 0; i < untracked->untracked_nr; i++) {
		eos = memchr(data, '\0', end - data);
		if (!eos || eos == end)
			return -1;
		untracked->untracked[i] = xmemdupz(data, eos - data);
		data = eos + 1;
	}

	rd->ucd[rd->index++] = untracked;
	rd->data = data;

	for (i = 0; i < untracked->dirs_nr; i++) {
		if (read_one_dir(untracked->dirs + i, rd) < 0)
			return -1;
	}
	return 0;
}

static void set_check_only(size_t pos, void *cb)
{
	struct read_data *rd = cb;
	struct untracked_cache_dir *ud = rd->ucd[pos];
	ud->check_only = 1;
}

static void read_stat(size_t pos, void *cb)
{
	struct read_data *rd = cb;
	struct untracked_cache_dir *ud = rd->ucd[pos];
	if (rd->data + sizeof(struct stat_data) > rd->end) {
		rd->data = rd->end + 1;
		return;
	}
	stat_data_from_disk(&ud->stat_data, rd->data);
	rd->data += sizeof(struct stat_data);
	ud->valid = 1;
}

static void read_oid(size_t pos, void *cb)
{
	struct read_data *rd = cb;
	struct untracked_cache_dir *ud = rd->ucd[pos];
	if (rd->data + the_hash_algo->rawsz > rd->end) {
		rd->data = rd->end + 1;
		return;
	}
	oidread(&ud->exclude_oid, rd->data, the_repository->hash_algo);
	rd->data += the_hash_algo->rawsz;
}

static void load_oid_stat(struct oid_stat *oid_stat, const unsigned char *data,
			  const unsigned char *sha1)
{
	stat_data_from_disk(&oid_stat->stat, data);
	oidread(&oid_stat->oid, sha1, the_repository->hash_algo);
	oid_stat->valid = 1;
}

struct untracked_cache *read_untracked_extension(const void *data, unsigned long sz)
{
	struct untracked_cache *uc;
	struct read_data rd;
	const unsigned char *next = data, *end = (const unsigned char *)data + sz;
	const char *ident;
	int ident_len;
	ssize_t len;
	const char *exclude_per_dir;
	const unsigned hashsz = the_hash_algo->rawsz;
	const unsigned offset = sizeof(struct ondisk_untracked_cache);
	const unsigned exclude_per_dir_offset = offset + 2 * hashsz;

	if (sz <= 1 || end[-1] != '\0')
		return NULL;
	end--;

	ident_len = decode_varint(&next);
	if (next + ident_len > end)
		return NULL;
	ident = (const char *)next;
	next += ident_len;

	if (next + exclude_per_dir_offset + 1 > end)
		return NULL;

	CALLOC_ARRAY(uc, 1);
	strbuf_init(&uc->ident, ident_len);
	strbuf_add(&uc->ident, ident, ident_len);
	load_oid_stat(&uc->ss_info_exclude,
		      next + ouc_offset(info_exclude_stat),
		      next + offset);
	load_oid_stat(&uc->ss_excludes_file,
		      next + ouc_offset(excludes_file_stat),
		      next + offset + hashsz);
	uc->dir_flags = get_be32(next + ouc_offset(dir_flags));
	exclude_per_dir = (const char *)next + exclude_per_dir_offset;
	uc->exclude_per_dir = uc->exclude_per_dir_to_free = xstrdup(exclude_per_dir);
	/* NUL after exclude_per_dir is covered by sizeof(*ouc) */
	next += exclude_per_dir_offset + strlen(exclude_per_dir) + 1;
	if (next >= end)
		goto done2;

	len = decode_varint(&next);
	if (next > end || len == 0)
		goto done2;

	rd.valid      = ewah_new();
	rd.check_only = ewah_new();
	rd.sha1_valid = ewah_new();
	rd.data	      = next;
	rd.end	      = end;
	rd.index      = 0;
	ALLOC_ARRAY(rd.ucd, len);

	if (read_one_dir(&uc->root, &rd) || rd.index != len)
		goto done;

	next = rd.data;
	len = ewah_read_mmap(rd.valid, next, end - next);
	if (len < 0)
		goto done;

	next += len;
	len = ewah_read_mmap(rd.check_only, next, end - next);
	if (len < 0)
		goto done;

	next += len;
	len = ewah_read_mmap(rd.sha1_valid, next, end - next);
	if (len < 0)
		goto done;

	ewah_each_bit(rd.check_only, set_check_only, &rd);
	rd.data = next + len;
	ewah_each_bit(rd.valid, read_stat, &rd);
	ewah_each_bit(rd.sha1_valid, read_oid, &rd);
	next = rd.data;

done:
	free(rd.ucd);
	ewah_free(rd.valid);
	ewah_free(rd.check_only);
	ewah_free(rd.sha1_valid);
done2:
	if (next != end) {
		free_untracked_cache(uc);
		uc = NULL;
	}
	return uc;
}

static void invalidate_one_directory(struct untracked_cache *uc,
				     struct untracked_cache_dir *ucd)
{
	uc->dir_invalidated++;
	ucd->valid = 0;
	ucd->untracked_nr = 0;
}

/*
 * Normally when an entry is added or removed from a directory,
 * invalidating that directory is enough. No need to touch its
 * ancestors. When a directory is shown as "foo/bar/" in git-status
 * however, deleting or adding an entry may have cascading effect.
 *
 * Say the "foo/bar/file" has become untracked, we need to tell the
 * untracked_cache_dir of "foo" that "bar/" is not an untracked
 * directory any more (because "bar" is managed by foo as an untracked
 * "file").
 *
 * Similarly, if "foo/bar/file" moves from untracked to tracked and it
 * was the last untracked entry in the entire "foo", we should show
 * "foo/" instead. Which means we have to invalidate past "bar" up to
 * "foo".
 *
 * This function traverses all directories from root to leaf. If there
 * is a chance of one of the above cases happening, we invalidate back
 * to root. Otherwise we just invalidate the leaf. There may be a more
 * sophisticated way than checking for SHOW_OTHER_DIRECTORIES to
 * detect these cases and avoid unnecessary invalidation, for example,
 * checking for the untracked entry named "bar/" in "foo", but for now
 * stick to something safe and simple.
 */
static int invalidate_one_component(struct untracked_cache *uc,
				    struct untracked_cache_dir *dir,
				    const char *path, int len)
{
	const char *rest = strchr(path, '/');

	if (rest) {
		int component_len = rest - path;
		struct untracked_cache_dir *d =
			lookup_untracked(uc, dir, path, component_len);
		int ret =
			invalidate_one_component(uc, d, rest + 1,
						 len - (component_len + 1));
		if (ret)
			invalidate_one_directory(uc, dir);
		return ret;
	}

	invalidate_one_directory(uc, dir);
	return uc->dir_flags & DIR_SHOW_OTHER_DIRECTORIES;
}

void untracked_cache_invalidate_path(struct index_state *istate,
				     const char *path, int safe_path)
{
	if (!istate->untracked || !istate->untracked->root)
		return;
	if (!safe_path && !verify_path(path, 0))
		return;
	invalidate_one_component(istate->untracked, istate->untracked->root,
				 path, strlen(path));
}

void untracked_cache_invalidate_trimmed_path(struct index_state *istate,
					     const char *path,
					     int safe_path)
{
	size_t len = strlen(path);

	if (!len)
		BUG("untracked_cache_invalidate_trimmed_path given zero length path");

	if (path[len - 1] != '/') {
		untracked_cache_invalidate_path(istate, path, safe_path);
	} else {
		struct strbuf tmp = STRBUF_INIT;

		strbuf_add(&tmp, path, len - 1);
		untracked_cache_invalidate_path(istate, tmp.buf, safe_path);
		strbuf_release(&tmp);
	}
}

void untracked_cache_remove_from_index(struct index_state *istate,
				       const char *path)
{
	untracked_cache_invalidate_path(istate, path, 1);
}

void untracked_cache_add_to_index(struct index_state *istate,
				  const char *path)
{
	untracked_cache_invalidate_path(istate, path, 1);
}

static void connect_wt_gitdir_in_nested(const char *sub_worktree,
					const char *sub_gitdir)
{
	int i;
	struct repository subrepo;
	struct strbuf sub_wt = STRBUF_INIT;
	struct strbuf sub_gd = STRBUF_INIT;

	const struct submodule *sub;

	/* If the submodule has no working tree, we can ignore it. */
	if (repo_init(&subrepo, sub_gitdir, sub_worktree))
		return;

	if (repo_read_index(&subrepo) < 0)
		die(_("index file corrupt in repo %s"), subrepo.gitdir);

	/* TODO: audit for interaction with sparse-index. */
	ensure_full_index(subrepo.index);
	for (i = 0; i < subrepo.index->cache_nr; i++) {
		const struct cache_entry *ce = subrepo.index->cache[i];

		if (!S_ISGITLINK(ce->ce_mode))
			continue;

		while (i + 1 < subrepo.index->cache_nr &&
		       !strcmp(ce->name, subrepo.index->cache[i + 1]->name))
			/*
			 * Skip entries with the same name in different stages
			 * to make sure an entry is returned only once.
			 */
			i++;

		sub = submodule_from_path(&subrepo, null_oid(), ce->name);
		if (!sub || !is_submodule_active(&subrepo, ce->name))
			/* .gitmodules broken or inactive sub */
			continue;

		strbuf_reset(&sub_wt);
		strbuf_reset(&sub_gd);
		strbuf_addf(&sub_wt, "%s/%s", sub_worktree, sub->path);
		submodule_name_to_gitdir(&sub_gd, &subrepo, sub->name);

		connect_work_tree_and_git_dir(sub_wt.buf, sub_gd.buf, 1);
	}
	strbuf_release(&sub_wt);
	strbuf_release(&sub_gd);
	repo_clear(&subrepo);
}

void connect_work_tree_and_git_dir(const char *work_tree_,
				   const char *git_dir_,
				   int recurse_into_nested)
{
	struct strbuf gitfile_sb = STRBUF_INIT;
	struct strbuf cfg_sb = STRBUF_INIT;
	struct strbuf rel_path = STRBUF_INIT;
	char *git_dir, *work_tree;

	/* Prepare .git file */
	strbuf_addf(&gitfile_sb, "%s/.git", work_tree_);
	if (safe_create_leading_directories_const(gitfile_sb.buf))
		die(_("could not create directories for %s"), gitfile_sb.buf);

	/* Prepare config file */
	strbuf_addf(&cfg_sb, "%s/config", git_dir_);
	if (safe_create_leading_directories_const(cfg_sb.buf))
		die(_("could not create directories for %s"), cfg_sb.buf);

	git_dir = real_pathdup(git_dir_, 1);
	work_tree = real_pathdup(work_tree_, 1);

	/* Write .git file */
	write_file(gitfile_sb.buf, "gitdir: %s",
		   relative_path(git_dir, work_tree, &rel_path));
	/* Update core.worktree setting */
	git_config_set_in_file(cfg_sb.buf, "core.worktree",
			       relative_path(work_tree, git_dir, &rel_path));

	strbuf_release(&gitfile_sb);
	strbuf_release(&cfg_sb);
	strbuf_release(&rel_path);

	if (recurse_into_nested)
		connect_wt_gitdir_in_nested(work_tree, git_dir);

	free(work_tree);
	free(git_dir);
}

/*
 * Migrate the git directory of the given path from old_git_dir to new_git_dir.
 */
void relocate_gitdir(const char *path, const char *old_git_dir, const char *new_git_dir)
{
	if (rename(old_git_dir, new_git_dir) < 0)
		die_errno(_("could not migrate git directory from '%s' to '%s'"),
			old_git_dir, new_git_dir);

	connect_work_tree_and_git_dir(path, new_git_dir, 0);
}

int path_match_flags(const char *const str, const enum path_match_flags flags)
{
	const char *p = str;

	if (flags & PATH_MATCH_NATIVE &&
	    flags & PATH_MATCH_XPLATFORM)
		BUG("path_match_flags() must get one match kind, not multiple!");
	else if (!(flags & PATH_MATCH_KINDS_MASK))
		BUG("path_match_flags() must get at least one match kind!");

	if (flags & PATH_MATCH_STARTS_WITH_DOT_SLASH &&
	    flags & PATH_MATCH_STARTS_WITH_DOT_DOT_SLASH)
		BUG("path_match_flags() must get one platform kind, not multiple!");
	else if (!(flags & PATH_MATCH_PLATFORM_MASK))
		BUG("path_match_flags() must get at least one platform kind!");

	if (*p++ != '.')
		return 0;
	if (flags & PATH_MATCH_STARTS_WITH_DOT_DOT_SLASH &&
	    *p++ != '.')
		return 0;

	if (flags & PATH_MATCH_NATIVE)
		return is_dir_sep(*p);
	else if (flags & PATH_MATCH_XPLATFORM)
		return is_xplatform_dir_sep(*p);
	BUG("unreachable");
}
