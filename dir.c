/*
 * This handles recursive filename detection with exclude
 * files, index knowledge etc..
 *
 * Copyright (C) Linus Torvalds, 2005-2006
 *		 Junio Hamano, 2005-2006
 */
#include "cache.h"
#include "dir.h"
#include "refs.h"

struct path_simplify {
	int len;
	const char *path;
};

static int read_directory_recursive(struct dir_struct *dir, const char *path, int len,
	int check_only, const struct path_simplify *simplify);
static int get_dtype(struct dirent *de, const char *path, int len);

/* helper string functions with support for the ignore_case flag */
int strcmp_icase(const char *a, const char *b)
{
	return ignore_case ? strcasecmp(a, b) : strcmp(a, b);
}

int strncmp_icase(const char *a, const char *b, size_t count)
{
	return ignore_case ? strncasecmp(a, b, count) : strncmp(a, b, count);
}

int fnmatch_icase(const char *pattern, const char *string, int flags)
{
	return fnmatch(pattern, string, flags | (ignore_case ? FNM_CASEFOLD : 0));
}

static size_t common_prefix_len(const char **pathspec)
{
	const char *n, *first;
	size_t max = 0;

	if (!pathspec)
		return max;

	first = *pathspec;
	while ((n = *pathspec++)) {
		size_t i, len = 0;
		for (i = 0; first == n || i < max; i++) {
			char c = n[i];
			if (!c || c != first[i] || is_glob_special(c))
				break;
			if (c == '/')
				len = i + 1;
		}
		if (first == n || len < max) {
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
char *common_prefix(const char **pathspec)
{
	unsigned long len = common_prefix_len(pathspec);

	return len ? xmemdupz(*pathspec, len) : NULL;
}

int fill_directory(struct dir_struct *dir, const char **pathspec)
{
	size_t len;

	/*
	 * Calculate common prefix for the pathspec, and
	 * use that to optimize the directory walk
	 */
	len = common_prefix_len(pathspec);

	/* Read the directory and prune it */
	read_directory(dir, pathspec ? *pathspec : "", len, pathspec);
	return len;
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
 * Does 'match' match the given name?
 * A match is found if
 *
 * (1) the 'match' string is leading directory of 'name', or
 * (2) the 'match' string is a wildcard and matches 'name', or
 * (3) the 'match' string is exactly the same as 'name'.
 *
 * and the return value tells which case it was.
 *
 * It returns 0 when there is no match.
 */
static int match_one(const char *match, const char *name, int namelen)
{
	int matchlen;

	/* If the match was just the prefix, we matched */
	if (!*match)
		return MATCHED_RECURSIVELY;

	if (ignore_case) {
		for (;;) {
			unsigned char c1 = tolower(*match);
			unsigned char c2 = tolower(*name);
			if (c1 == '\0' || is_glob_special(c1))
				break;
			if (c1 != c2)
				return 0;
			match++;
			name++;
			namelen--;
		}
	} else {
		for (;;) {
			unsigned char c1 = *match;
			unsigned char c2 = *name;
			if (c1 == '\0' || is_glob_special(c1))
				break;
			if (c1 != c2)
				return 0;
			match++;
			name++;
			namelen--;
		}
	}


	/*
	 * If we don't match the matchstring exactly,
	 * we need to match by fnmatch
	 */
	matchlen = strlen(match);
	if (strncmp_icase(match, name, matchlen))
		return !fnmatch_icase(match, name, 0) ? MATCHED_FNMATCH : 0;

	if (namelen == matchlen)
		return MATCHED_EXACTLY;
	if (match[matchlen-1] == '/' || name[matchlen] == '/')
		return MATCHED_RECURSIVELY;
	return 0;
}

/*
 * Given a name and a list of pathspecs, see if the name matches
 * any of the pathspecs.  The caller is also interested in seeing
 * all pathspec matches some names it calls this function with
 * (otherwise the user could have mistyped the unmatched pathspec),
 * and a mark is left in seen[] array for pathspec element that
 * actually matched anything.
 */
int match_pathspec(const char **pathspec, const char *name, int namelen,
		int prefix, char *seen)
{
	int i, retval = 0;

	if (!pathspec)
		return 1;

	name += prefix;
	namelen -= prefix;

	for (i = 0; pathspec[i] != NULL; i++) {
		int how;
		const char *match = pathspec[i] + prefix;
		if (seen && seen[i] == MATCHED_EXACTLY)
			continue;
		how = match_one(match, name, namelen);
		if (how) {
			if (retval < how)
				retval = how;
			if (seen && seen[i] < how)
				seen[i] = how;
		}
	}
	return retval;
}

/*
 * Does 'match' match the given name?
 * A match is found if
 *
 * (1) the 'match' string is leading directory of 'name', or
 * (2) the 'match' string is a wildcard and matches 'name', or
 * (3) the 'match' string is exactly the same as 'name'.
 *
 * and the return value tells which case it was.
 *
 * It returns 0 when there is no match.
 */
static int match_pathspec_item(const struct pathspec_item *item, int prefix,
			       const char *name, int namelen)
{
	/* name/namelen has prefix cut off by caller */
	const char *match = item->match + prefix;
	int matchlen = item->len - prefix;

	/* If the match was just the prefix, we matched */
	if (!*match)
		return MATCHED_RECURSIVELY;

	if (matchlen <= namelen && !strncmp(match, name, matchlen)) {
		if (matchlen == namelen)
			return MATCHED_EXACTLY;

		if (match[matchlen-1] == '/' || name[matchlen] == '/')
			return MATCHED_RECURSIVELY;
	}

	if (item->use_wildcard && !fnmatch(match, name, 0))
		return MATCHED_FNMATCH;

	return 0;
}

/*
 * Given a name and a list of pathspecs, see if the name matches
 * any of the pathspecs.  The caller is also interested in seeing
 * all pathspec matches some names it calls this function with
 * (otherwise the user could have mistyped the unmatched pathspec),
 * and a mark is left in seen[] array for pathspec element that
 * actually matched anything.
 */
int match_pathspec_depth(const struct pathspec *ps,
			 const char *name, int namelen,
			 int prefix, char *seen)
{
	int i, retval = 0;

	if (!ps->nr) {
		if (!ps->recursive || ps->max_depth == -1)
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
		if (seen && seen[i] == MATCHED_EXACTLY)
			continue;
		how = match_pathspec_item(ps->items+i, prefix, name, namelen);
		if (ps->recursive && ps->max_depth != -1 &&
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

/*
 * Return the length of the "simple" part of a path match limiter.
 */
static int simple_length(const char *match)
{
	int len = -1;

	for (;;) {
		unsigned char c = *match++;
		len++;
		if (c == '\0' || is_glob_special(c))
			return len;
	}
}

static int no_wildcard(const char *string)
{
	return string[simple_length(string)] == '\0';
}

void add_exclude(const char *string, const char *base,
		 int baselen, struct exclude_list *which)
{
	struct exclude *x;
	size_t len;
	int to_exclude = 1;
	int flags = 0;

	if (*string == '!') {
		to_exclude = 0;
		string++;
	}
	len = strlen(string);
	if (len && string[len - 1] == '/') {
		char *s;
		x = xmalloc(sizeof(*x) + len);
		s = (char *)(x+1);
		memcpy(s, string, len - 1);
		s[len - 1] = '\0';
		string = s;
		x->pattern = s;
		flags = EXC_FLAG_MUSTBEDIR;
	} else {
		x = xmalloc(sizeof(*x));
		x->pattern = string;
	}
	x->to_exclude = to_exclude;
	x->patternlen = strlen(string);
	x->base = base;
	x->baselen = baselen;
	x->flags = flags;
	if (!strchr(string, '/'))
		x->flags |= EXC_FLAG_NODIR;
	x->nowildcardlen = simple_length(string);
	if (*string == '*' && no_wildcard(string+1))
		x->flags |= EXC_FLAG_ENDSWITH;
	ALLOC_GROW(which->excludes, which->nr + 1, which->alloc);
	which->excludes[which->nr++] = x;
}

static void *read_skip_worktree_file_from_index(const char *path, size_t *size)
{
	int pos, len;
	unsigned long sz;
	enum object_type type;
	void *data;
	struct index_state *istate = &the_index;

	len = strlen(path);
	pos = index_name_pos(istate, path, len);
	if (pos < 0)
		return NULL;
	if (!ce_skip_worktree(istate->cache[pos]))
		return NULL;
	data = read_sha1_file(istate->cache[pos]->sha1, &type, &sz);
	if (!data || type != OBJ_BLOB) {
		free(data);
		return NULL;
	}
	*size = xsize_t(sz);
	return data;
}

void free_excludes(struct exclude_list *el)
{
	int i;

	for (i = 0; i < el->nr; i++)
		free(el->excludes[i]);
	free(el->excludes);

	el->nr = 0;
	el->excludes = NULL;
}

int add_excludes_from_file_to_list(const char *fname,
				   const char *base,
				   int baselen,
				   char **buf_p,
				   struct exclude_list *which,
				   int check_index)
{
	struct stat st;
	int fd, i;
	size_t size = 0;
	char *buf, *entry;

	fd = open(fname, O_RDONLY);
	if (fd < 0 || fstat(fd, &st) < 0) {
		if (errno != ENOENT)
			warn_on_inaccessible(fname);
		if (0 <= fd)
			close(fd);
		if (!check_index ||
		    (buf = read_skip_worktree_file_from_index(fname, &size)) == NULL)
			return -1;
		if (size == 0) {
			free(buf);
			return 0;
		}
		if (buf[size-1] != '\n') {
			buf = xrealloc(buf, size+1);
			buf[size++] = '\n';
		}
	}
	else {
		size = xsize_t(st.st_size);
		if (size == 0) {
			close(fd);
			return 0;
		}
		buf = xmalloc(size+1);
		if (read_in_full(fd, buf, size) != size) {
			free(buf);
			close(fd);
			return -1;
		}
		buf[size++] = '\n';
		close(fd);
	}

	if (buf_p)
		*buf_p = buf;
	entry = buf;
	for (i = 0; i < size; i++) {
		if (buf[i] == '\n') {
			if (entry != buf + i && entry[0] != '#') {
				buf[i - (i && buf[i-1] == '\r')] = 0;
				add_exclude(entry, base, baselen, which);
			}
			entry = buf + i + 1;
		}
	}
	return 0;
}

void add_excludes_from_file(struct dir_struct *dir, const char *fname)
{
	if (add_excludes_from_file_to_list(fname, "", 0, NULL,
					   &dir->exclude_list[EXC_FILE], 0) < 0)
		die("cannot use %s as an exclude file", fname);
}

static void prep_exclude(struct dir_struct *dir, const char *base, int baselen)
{
	struct exclude_list *el;
	struct exclude_stack *stk = NULL;
	int current;

	if ((!dir->exclude_per_dir) ||
	    (baselen + strlen(dir->exclude_per_dir) >= PATH_MAX))
		return; /* too long a path -- ignore */

	/* Pop the ones that are not the prefix of the path being checked. */
	el = &dir->exclude_list[EXC_DIRS];
	while ((stk = dir->exclude_stack) != NULL) {
		if (stk->baselen <= baselen &&
		    !strncmp(dir->basebuf, base, stk->baselen))
			break;
		dir->exclude_stack = stk->prev;
		while (stk->exclude_ix < el->nr)
			free(el->excludes[--el->nr]);
		free(stk->filebuf);
		free(stk);
	}

	/* Read from the parent directories and push them down. */
	current = stk ? stk->baselen : -1;
	while (current < baselen) {
		struct exclude_stack *stk = xcalloc(1, sizeof(*stk));
		const char *cp;

		if (current < 0) {
			cp = base;
			current = 0;
		}
		else {
			cp = strchr(base + current + 1, '/');
			if (!cp)
				die("oops in prep_exclude");
			cp++;
		}
		stk->prev = dir->exclude_stack;
		stk->baselen = cp - base;
		stk->exclude_ix = el->nr;
		memcpy(dir->basebuf + current, base + current,
		       stk->baselen - current);
		strcpy(dir->basebuf + stk->baselen, dir->exclude_per_dir);
		add_excludes_from_file_to_list(dir->basebuf,
					       dir->basebuf, stk->baselen,
					       &stk->filebuf, el, 1);
		dir->exclude_stack = stk;
		current = stk->baselen;
	}
	dir->basebuf[baselen] = '\0';
}

/* Scan the list and let the last match determine the fate.
 * Return 1 for exclude, 0 for include and -1 for undecided.
 */
int excluded_from_list(const char *pathname,
		       int pathlen, const char *basename, int *dtype,
		       struct exclude_list *el)
{
	int i;

	if (!el->nr)
		return -1;	/* undefined */

	for (i = el->nr - 1; 0 <= i; i--) {
		struct exclude *x = el->excludes[i];
		const char *name, *exclude = x->pattern;
		int to_exclude = x->to_exclude;
		int namelen, prefix = x->nowildcardlen;

		if (x->flags & EXC_FLAG_MUSTBEDIR) {
			if (*dtype == DT_UNKNOWN)
				*dtype = get_dtype(NULL, pathname, pathlen);
			if (*dtype != DT_DIR)
				continue;
		}

		if (x->flags & EXC_FLAG_NODIR) {
			/* match basename */
			if (prefix == x->patternlen) {
				if (!strcmp_icase(exclude, basename))
					return to_exclude;
			} else if (x->flags & EXC_FLAG_ENDSWITH) {
				if (x->patternlen - 1 <= pathlen &&
				    !strcmp_icase(exclude + 1, pathname + pathlen - x->patternlen + 1))
					return to_exclude;
			} else {
				if (fnmatch_icase(exclude, basename, 0) == 0)
					return to_exclude;
			}
			continue;
		}

		/* match with FNM_PATHNAME:
		 * exclude has base (baselen long) implicitly in front of it.
		 */
		if (*exclude == '/') {
			exclude++;
			prefix--;
		}

		if (pathlen < x->baselen ||
		    (x->baselen && pathname[x->baselen-1] != '/') ||
		    strncmp_icase(pathname, x->base, x->baselen))
			continue;

		namelen = x->baselen ? pathlen - x->baselen : pathlen;
		name = pathname + pathlen  - namelen;

		/* if the non-wildcard part is longer than the
		   remaining pathname, surely it cannot match */
		if (prefix > namelen)
			continue;

		if (prefix) {
			if (strncmp_icase(exclude, name, prefix))
				continue;
			exclude += prefix;
			name    += prefix;
			namelen -= prefix;
		}

		if (!namelen || !fnmatch_icase(exclude, name, FNM_PATHNAME))
			return to_exclude;
	}
	return -1; /* undecided */
}

static int excluded(struct dir_struct *dir, const char *pathname, int *dtype_p)
{
	int pathlen = strlen(pathname);
	int st;
	const char *basename = strrchr(pathname, '/');
	basename = (basename) ? basename+1 : pathname;

	prep_exclude(dir, pathname, basename-pathname);
	for (st = EXC_CMDL; st <= EXC_FILE; st++) {
		switch (excluded_from_list(pathname, pathlen, basename,
					   dtype_p, &dir->exclude_list[st])) {
		case 0:
			return 0;
		case 1:
			return 1;
		}
	}
	return 0;
}

void path_exclude_check_init(struct path_exclude_check *check,
			     struct dir_struct *dir)
{
	check->dir = dir;
	strbuf_init(&check->path, 256);
}

void path_exclude_check_clear(struct path_exclude_check *check)
{
	strbuf_release(&check->path);
}

/*
 * Is this name excluded?  This is for a caller like show_files() that
 * do not honor directory hierarchy and iterate through paths that are
 * possibly in an ignored directory.
 *
 * A path to a directory known to be excluded is left in check->path to
 * optimize for repeated checks for files in the same excluded directory.
 */
int path_excluded(struct path_exclude_check *check,
		  const char *name, int namelen, int *dtype)
{
	int i;
	struct strbuf *path = &check->path;

	/*
	 * we allow the caller to pass namelen as an optimization; it
	 * must match the length of the name, as we eventually call
	 * excluded() on the whole name string.
	 */
	if (namelen < 0)
		namelen = strlen(name);

	if (path->len &&
	    path->len <= namelen &&
	    !memcmp(name, path->buf, path->len) &&
	    (!name[path->len] || name[path->len] == '/'))
		return 1;

	strbuf_setlen(path, 0);
	for (i = 0; name[i]; i++) {
		int ch = name[i];

		if (ch == '/') {
			int dt = DT_DIR;
			if (excluded(check->dir, path->buf, &dt))
				return 1;
		}
		strbuf_addch(path, ch);
	}

	/* An entry in the index; cannot be a directory with subentries */
	strbuf_setlen(path, 0);

	return excluded(check->dir, name, dtype);
}

static struct dir_entry *dir_entry_new(const char *pathname, int len)
{
	struct dir_entry *ent;

	ent = xmalloc(sizeof(*ent) + len + 1);
	ent->len = len;
	memcpy(ent->name, pathname, len);
	ent->name[len] = 0;
	return ent;
}

static struct dir_entry *dir_add_name(struct dir_struct *dir, const char *pathname, int len)
{
	if (cache_name_exists(pathname, len, ignore_case))
		return NULL;

	ALLOC_GROW(dir->entries, dir->nr+1, dir->alloc);
	return dir->entries[dir->nr++] = dir_entry_new(pathname, len);
}

struct dir_entry *dir_add_ignored(struct dir_struct *dir, const char *pathname, int len)
{
	if (!cache_name_is_other(pathname, len))
		return NULL;

	ALLOC_GROW(dir->ignored, dir->ignored_nr+1, dir->ignored_alloc);
	return dir->ignored[dir->ignored_nr++] = dir_entry_new(pathname, len);
}

enum exist_status {
	index_nonexistent = 0,
	index_directory,
	index_gitdir
};

/*
 * Do not use the alphabetically stored index to look up
 * the directory name; instead, use the case insensitive
 * name hash.
 */
static enum exist_status directory_exists_in_index_icase(const char *dirname, int len)
{
	struct cache_entry *ce = index_name_exists(&the_index, dirname, len + 1, ignore_case);
	unsigned char endchar;

	if (!ce)
		return index_nonexistent;
	endchar = ce->name[len];

	/*
	 * The cache_entry structure returned will contain this dirname
	 * and possibly additional path components.
	 */
	if (endchar == '/')
		return index_directory;

	/*
	 * If there are no additional path components, then this cache_entry
	 * represents a submodule.  Submodules, despite being directories,
	 * are stored in the cache without a closing slash.
	 */
	if (!endchar && S_ISGITLINK(ce->ce_mode))
		return index_gitdir;

	/* This should never be hit, but it exists just in case. */
	return index_nonexistent;
}

/*
 * The index sorts alphabetically by entry name, which
 * means that a gitlink sorts as '\0' at the end, while
 * a directory (which is defined not as an entry, but as
 * the files it contains) will sort with the '/' at the
 * end.
 */
static enum exist_status directory_exists_in_index(const char *dirname, int len)
{
	int pos;

	if (ignore_case)
		return directory_exists_in_index_icase(dirname, len);

	pos = cache_name_pos(dirname, len);
	if (pos < 0)
		pos = -pos-1;
	while (pos < active_nr) {
		struct cache_entry *ce = active_cache[pos++];
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
 *  (a) if "show_other_directories" is true, we show it as
 *      just a directory, unless "hide_empty_directories" is
 *      also true and the directory is empty, in which case
 *      we just ignore it entirely.
 *  (b) if it looks like a git directory, and we don't have
 *      'no_gitlinks' set we treat it as a gitlink, and show it
 *      as a directory.
 *  (c) otherwise, we recurse into it.
 */
enum directory_treatment {
	show_directory,
	ignore_directory,
	recurse_into_directory
};

static enum directory_treatment treat_directory(struct dir_struct *dir,
	const char *dirname, int len,
	const struct path_simplify *simplify)
{
	/* The "len-1" is to strip the final '/' */
	switch (directory_exists_in_index(dirname, len-1)) {
	case index_directory:
		return recurse_into_directory;

	case index_gitdir:
		if (dir->flags & DIR_SHOW_OTHER_DIRECTORIES)
			return ignore_directory;
		return show_directory;

	case index_nonexistent:
		if (dir->flags & DIR_SHOW_OTHER_DIRECTORIES)
			break;
		if (!(dir->flags & DIR_NO_GITLINKS)) {
			unsigned char sha1[20];
			if (resolve_gitlink_ref(dirname, "HEAD", sha1) == 0)
				return show_directory;
		}
		return recurse_into_directory;
	}

	/* This is the "show_other_directories" case */
	if (!(dir->flags & DIR_HIDE_EMPTY_DIRECTORIES))
		return show_directory;
	if (!read_directory_recursive(dir, dirname, len, 1, simplify))
		return ignore_directory;
	return show_directory;
}

/*
 * This is an inexact early pruning of any recursive directory
 * reading - if the path cannot possibly be in the pathspec,
 * return true, and we'll skip it early.
 */
static int simplify_away(const char *path, int pathlen, const struct path_simplify *simplify)
{
	if (simplify) {
		for (;;) {
			const char *match = simplify->path;
			int len = simplify->len;

			if (!match)
				break;
			if (len > pathlen)
				len = pathlen;
			if (!memcmp(path, match, len))
				return 0;
			simplify++;
		}
		return 1;
	}
	return 0;
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
static int exclude_matches_pathspec(const char *path, int len,
		const struct path_simplify *simplify)
{
	if (simplify) {
		for (; simplify->path; simplify++) {
			if (len == simplify->len
			    && !memcmp(path, simplify->path, len))
				return 1;
			if (len < simplify->len
			    && simplify->path[len] == '/'
			    && !memcmp(path, simplify->path, len))
				return 1;
		}
	}
	return 0;
}

static int get_index_dtype(const char *path, int len)
{
	int pos;
	struct cache_entry *ce;

	ce = cache_name_exists(path, len, 0);
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
	pos = cache_name_pos(path, len);
	if (pos >= 0)
		return DT_UNKNOWN;
	pos = -pos-1;
	while (pos < active_nr) {
		ce = active_cache[pos++];
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

static int get_dtype(struct dirent *de, const char *path, int len)
{
	int dtype = de ? DTYPE(de) : DT_UNKNOWN;
	struct stat st;

	if (dtype != DT_UNKNOWN)
		return dtype;
	dtype = get_index_dtype(path, len);
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

enum path_treatment {
	path_ignored,
	path_handled,
	path_recurse
};

static enum path_treatment treat_one_path(struct dir_struct *dir,
					  struct strbuf *path,
					  const struct path_simplify *simplify,
					  int dtype, struct dirent *de)
{
	int exclude = excluded(dir, path->buf, &dtype);
	if (exclude && (dir->flags & DIR_COLLECT_IGNORED)
	    && exclude_matches_pathspec(path->buf, path->len, simplify))
		dir_add_ignored(dir, path->buf, path->len);

	/*
	 * Excluded? If we don't explicitly want to show
	 * ignored files, ignore it
	 */
	if (exclude && !(dir->flags & DIR_SHOW_IGNORED))
		return path_ignored;

	if (dtype == DT_UNKNOWN)
		dtype = get_dtype(de, path->buf, path->len);

	/*
	 * Do we want to see just the ignored files?
	 * We still need to recurse into directories,
	 * even if we don't ignore them, since the
	 * directory may contain files that we do..
	 */
	if (!exclude && (dir->flags & DIR_SHOW_IGNORED)) {
		if (dtype != DT_DIR)
			return path_ignored;
	}

	switch (dtype) {
	default:
		return path_ignored;
	case DT_DIR:
		strbuf_addch(path, '/');
		switch (treat_directory(dir, path->buf, path->len, simplify)) {
		case show_directory:
			if (exclude != !!(dir->flags
					  & DIR_SHOW_IGNORED))
				return path_ignored;
			break;
		case recurse_into_directory:
			return path_recurse;
		case ignore_directory:
			return path_ignored;
		}
		break;
	case DT_REG:
	case DT_LNK:
		break;
	}
	return path_handled;
}

static enum path_treatment treat_path(struct dir_struct *dir,
				      struct dirent *de,
				      struct strbuf *path,
				      int baselen,
				      const struct path_simplify *simplify)
{
	int dtype;

	if (is_dot_or_dotdot(de->d_name) || !strcmp(de->d_name, ".git"))
		return path_ignored;
	strbuf_setlen(path, baselen);
	strbuf_addstr(path, de->d_name);
	if (simplify_away(path->buf, path->len, simplify))
		return path_ignored;

	dtype = DTYPE(de);
	return treat_one_path(dir, path, simplify, dtype, de);
}

/*
 * Read a directory tree. We currently ignore anything but
 * directories, regular files and symlinks. That's because git
 * doesn't handle them at all yet. Maybe that will change some
 * day.
 *
 * Also, we ignore the name ".git" (even if it is not a directory).
 * That likely will not change.
 */
static int read_directory_recursive(struct dir_struct *dir,
				    const char *base, int baselen,
				    int check_only,
				    const struct path_simplify *simplify)
{
	DIR *fdir;
	int contents = 0;
	struct dirent *de;
	struct strbuf path = STRBUF_INIT;

	strbuf_add(&path, base, baselen);

	fdir = opendir(path.len ? path.buf : ".");
	if (!fdir)
		goto out;

	while ((de = readdir(fdir)) != NULL) {
		switch (treat_path(dir, de, &path, baselen, simplify)) {
		case path_recurse:
			contents += read_directory_recursive(dir, path.buf,
							     path.len, 0,
							     simplify);
			continue;
		case path_ignored:
			continue;
		case path_handled:
			break;
		}
		contents++;
		if (check_only)
			break;
		dir_add_name(dir, path.buf, path.len);
	}
	closedir(fdir);
 out:
	strbuf_release(&path);

	return contents;
}

static int cmp_name(const void *p1, const void *p2)
{
	const struct dir_entry *e1 = *(const struct dir_entry **)p1;
	const struct dir_entry *e2 = *(const struct dir_entry **)p2;

	return cache_name_compare(e1->name, e1->len,
				  e2->name, e2->len);
}

static struct path_simplify *create_simplify(const char **pathspec)
{
	int nr, alloc = 0;
	struct path_simplify *simplify = NULL;

	if (!pathspec)
		return NULL;

	for (nr = 0 ; ; nr++) {
		const char *match;
		if (nr >= alloc) {
			alloc = alloc_nr(alloc);
			simplify = xrealloc(simplify, alloc * sizeof(*simplify));
		}
		match = *pathspec++;
		if (!match)
			break;
		simplify[nr].path = match;
		simplify[nr].len = simple_length(match);
	}
	simplify[nr].path = NULL;
	simplify[nr].len = 0;
	return simplify;
}

static void free_simplify(struct path_simplify *simplify)
{
	free(simplify);
}

static int treat_leading_path(struct dir_struct *dir,
			      const char *path, int len,
			      const struct path_simplify *simplify)
{
	struct strbuf sb = STRBUF_INIT;
	int baselen, rc = 0;
	const char *cp;

	while (len && path[len - 1] == '/')
		len--;
	if (!len)
		return 1;
	baselen = 0;
	while (1) {
		cp = path + baselen + !!baselen;
		cp = memchr(cp, '/', path + len - cp);
		if (!cp)
			baselen = len;
		else
			baselen = cp - path;
		strbuf_setlen(&sb, 0);
		strbuf_add(&sb, path, baselen);
		if (!is_directory(sb.buf))
			break;
		if (simplify_away(sb.buf, sb.len, simplify))
			break;
		if (treat_one_path(dir, &sb, simplify,
				   DT_DIR, NULL) == path_ignored)
			break; /* do not recurse into it */
		if (len <= baselen) {
			rc = 1;
			break; /* finished checking */
		}
	}
	strbuf_release(&sb);
	return rc;
}

int read_directory(struct dir_struct *dir, const char *path, int len, const char **pathspec)
{
	struct path_simplify *simplify;

	if (has_symlink_leading_path(path, len))
		return dir->nr;

	simplify = create_simplify(pathspec);
	if (!len || treat_leading_path(dir, path, len, simplify))
		read_directory_recursive(dir, path, len, 0, simplify);
	free_simplify(simplify);
	qsort(dir->entries, dir->nr, sizeof(struct dir_entry *), cmp_name);
	qsort(dir->ignored, dir->ignored_nr, sizeof(struct dir_entry *), cmp_name);
	return dir->nr;
}

int file_exists(const char *f)
{
	struct stat sb;
	return lstat(f, &sb) == 0;
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

	while (*dir && *subdir && *dir == *subdir) {
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
	char cwd[PATH_MAX];
	if (!dir)
		return 0;
	if (!getcwd(cwd, sizeof(cwd)))
		die_errno("can't find the current directory");
	return dir_inside_of(cwd, dir) >= 0;
}

int is_empty_dir(const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *e;
	int ret = 1;

	if (!dir)
		return 0;

	while ((e = readdir(dir)) != NULL)
		if (!is_dot_or_dotdot(e->d_name)) {
			ret = 0;
			break;
		}

	closedir(dir);
	return ret;
}

static int remove_dir_recurse(struct strbuf *path, int flag, int *kept_up)
{
	DIR *dir;
	struct dirent *e;
	int ret = 0, original_len = path->len, len, kept_down = 0;
	int only_empty = (flag & REMOVE_DIR_EMPTY_ONLY);
	int keep_toplevel = (flag & REMOVE_DIR_KEEP_TOPLEVEL);
	unsigned char submodule_head[20];

	if ((flag & REMOVE_DIR_KEEP_NESTED_GIT) &&
	    !resolve_gitlink_ref(path->buf, "HEAD", submodule_head)) {
		/* Do not descend and nuke a nested git work tree. */
		if (kept_up)
			*kept_up = 1;
		return 0;
	}

	flag &= ~REMOVE_DIR_KEEP_TOPLEVEL;
	dir = opendir(path->buf);
	if (!dir) {
		/* an empty dir could be removed even if it is unreadble */
		if (!keep_toplevel)
			return rmdir(path->buf);
		else
			return -1;
	}
	if (path->buf[original_len - 1] != '/')
		strbuf_addch(path, '/');

	len = path->len;
	while ((e = readdir(dir)) != NULL) {
		struct stat st;
		if (is_dot_or_dotdot(e->d_name))
			continue;

		strbuf_setlen(path, len);
		strbuf_addstr(path, e->d_name);
		if (lstat(path->buf, &st))
			; /* fall thru */
		else if (S_ISDIR(st.st_mode)) {
			if (!remove_dir_recurse(path, flag, &kept_down))
				continue; /* happy */
		} else if (!only_empty && !unlink(path->buf))
			continue; /* happy, too */

		/* path too long, stat fails, or non-directory still exists */
		ret = -1;
		break;
	}
	closedir(dir);

	strbuf_setlen(path, original_len);
	if (!ret && !keep_toplevel && !kept_down)
		ret = rmdir(path->buf);
	else if (kept_up)
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

void setup_standard_excludes(struct dir_struct *dir)
{
	const char *path;
	char *xdg_path;

	dir->exclude_per_dir = ".gitignore";
	path = git_path("info/exclude");
	if (!excludes_file) {
		home_config_paths(NULL, &xdg_path, "ignore");
		excludes_file = xdg_path;
	}
	if (!access_or_warn(path, R_OK))
		add_excludes_from_file(dir, path);
	if (excludes_file && !access_or_warn(excludes_file, R_OK))
		add_excludes_from_file(dir, excludes_file);
}

int remove_path(const char *name)
{
	char *slash;

	if (unlink(name) && errno != ENOENT)
		return -1;

	slash = strrchr(name, '/');
	if (slash) {
		char *dirs = xstrdup(name);
		slash = dirs + (slash - name);
		do {
			*slash = '\0';
		} while (rmdir(dirs) == 0 && (slash = strrchr(dirs, '/')));
		free(dirs);
	}
	return 0;
}

static int pathspec_item_cmp(const void *a_, const void *b_)
{
	struct pathspec_item *a, *b;

	a = (struct pathspec_item *)a_;
	b = (struct pathspec_item *)b_;
	return strcmp(a->match, b->match);
}

int init_pathspec(struct pathspec *pathspec, const char **paths)
{
	const char **p = paths;
	int i;

	memset(pathspec, 0, sizeof(*pathspec));
	if (!p)
		return 0;
	while (*p)
		p++;
	pathspec->raw = paths;
	pathspec->nr = p - paths;
	if (!pathspec->nr)
		return 0;

	pathspec->items = xmalloc(sizeof(struct pathspec_item)*pathspec->nr);
	for (i = 0; i < pathspec->nr; i++) {
		struct pathspec_item *item = pathspec->items+i;
		const char *path = paths[i];

		item->match = path;
		item->len = strlen(path);
		item->use_wildcard = !no_wildcard(path);
		if (item->use_wildcard)
			pathspec->has_wildcard = 1;
	}

	qsort(pathspec->items, pathspec->nr,
	      sizeof(struct pathspec_item), pathspec_item_cmp);

	return 0;
}

void free_pathspec(struct pathspec *pathspec)
{
	free(pathspec->items);
	pathspec->items = NULL;
}
