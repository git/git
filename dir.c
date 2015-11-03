/*
 * This handles recursive filename detection with exclude
 * files, index knowledge etc..
 *
 * See Documentation/technical/api-directory-listing.txt
 *
 * Copyright (C) Linus Torvalds, 2005-2006
 *		 Junio Hamano, 2005-2006
 */
#include "cache.h"
#include "dir.h"
#include "refs.h"
#include "wildmatch.h"
#include "pathspec.h"
#include "utf8.h"
#include "varint.h"
#include "ewah/ewok.h"

struct path_simplify {
	int len;
	const char *path;
};

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

	struct dirent *de;
	const char *file;
	struct untracked_cache_dir *ucd;
};

static enum path_treatment read_directory_recursive(struct dir_struct *dir,
	const char *path, int len, struct untracked_cache_dir *untracked,
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
	return wildmatch(pattern, string,
			 flags | (ignore_case ? WM_CASEFOLD : 0),
			 NULL);
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
				 (item->magic & PATHSPEC_ICASE ? WM_CASEFOLD : 0),
				 NULL);
	else
		/* wildmatch has not learned no FNM_PATHNAME mode yet */
		return wildmatch(pattern, string,
				 item->magic & PATHSPEC_ICASE ? WM_CASEFOLD : 0,
				 NULL);
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
	match_status = wildmatch(use_pat, use_str, flags, NULL);

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
	 * subdir xyz, the common prefix is still xyz, not xuz/abc as
	 * in non-:(icase).
	 */
	GUARD_PATHSPEC(pathspec,
		       PATHSPEC_FROMTOP |
		       PATHSPEC_MAXDEPTH |
		       PATHSPEC_LITERAL |
		       PATHSPEC_GLOB |
		       PATHSPEC_ICASE |
		       PATHSPEC_EXCLUDE);

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

int fill_directory(struct dir_struct *dir, const struct pathspec *pathspec)
{
	size_t len;

	/*
	 * Calculate common prefix for the pathspec, and
	 * use that to optimize the directory walk
	 */
	len = common_prefix_len(pathspec);

	/* Read the directory and prune it */
	read_directory(dir, pathspec->nr ? pathspec->_raw[0] : "", len, pathspec);
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

#define DO_MATCH_EXCLUDE   1
#define DO_MATCH_DIRECTORY 2

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

	return 0;
}

/*
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
static int do_match_pathspec(const struct pathspec *ps,
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
		       PATHSPEC_EXCLUDE);

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
		how = match_pathspec_item(ps->items+i, prefix, name,
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

int match_pathspec(const struct pathspec *ps,
		   const char *name, int namelen,
		   int prefix, char *seen, int is_dir)
{
	int positive, negative;
	unsigned flags = is_dir ? DO_MATCH_DIRECTORY : 0;
	positive = do_match_pathspec(ps, name, namelen,
				     prefix, seen, flags);
	if (!(ps->magic & PATHSPEC_EXCLUDE) || !positive)
		return positive;
	negative = do_match_pathspec(ps, name, namelen,
				     prefix, seen,
				     flags | DO_MATCH_EXCLUDE);
	return negative ? 0 : positive;
}

int report_path_error(const char *ps_matched,
		      const struct pathspec *pathspec,
		      const char *prefix)
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

		error("pathspec '%s' did not match any file(s) known to git.",
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

void parse_exclude_pattern(const char **pattern,
			   int *patternlen,
			   int *flags,
			   int *nowildcardlen)
{
	const char *p = *pattern;
	size_t i, len;

	*flags = 0;
	if (*p == '!') {
		*flags |= EXC_FLAG_NEGATIVE;
		p++;
	}
	len = strlen(p);
	if (len && p[len - 1] == '/') {
		len--;
		*flags |= EXC_FLAG_MUSTBEDIR;
	}
	for (i = 0; i < len; i++) {
		if (p[i] == '/')
			break;
	}
	if (i == len)
		*flags |= EXC_FLAG_NODIR;
	*nowildcardlen = simple_length(p);
	/*
	 * we should have excluded the trailing slash from 'p' too,
	 * but that's one more allocation. Instead just make sure
	 * nowildcardlen does not exceed real patternlen
	 */
	if (*nowildcardlen > len)
		*nowildcardlen = len;
	if (*p == '*' && no_wildcard(p + 1))
		*flags |= EXC_FLAG_ENDSWITH;
	*pattern = p;
	*patternlen = len;
}

void add_exclude(const char *string, const char *base,
		 int baselen, struct exclude_list *el, int srcpos)
{
	struct exclude *x;
	int patternlen;
	int flags;
	int nowildcardlen;

	parse_exclude_pattern(&string, &patternlen, &flags, &nowildcardlen);
	if (flags & EXC_FLAG_MUSTBEDIR) {
		char *s;
		x = xmalloc(sizeof(*x) + patternlen + 1);
		s = (char *)(x+1);
		memcpy(s, string, patternlen);
		s[patternlen] = '\0';
		x->pattern = s;
	} else {
		x = xmalloc(sizeof(*x));
		x->pattern = string;
	}
	x->patternlen = patternlen;
	x->nowildcardlen = nowildcardlen;
	x->base = base;
	x->baselen = baselen;
	x->flags = flags;
	x->srcpos = srcpos;
	ALLOC_GROW(el->excludes, el->nr + 1, el->alloc);
	el->excludes[el->nr++] = x;
	x->el = el;
}

static void *read_skip_worktree_file_from_index(const char *path, size_t *size,
						struct sha1_stat *sha1_stat)
{
	int pos, len;
	unsigned long sz;
	enum object_type type;
	void *data;

	len = strlen(path);
	pos = cache_name_pos(path, len);
	if (pos < 0)
		return NULL;
	if (!ce_skip_worktree(active_cache[pos]))
		return NULL;
	data = read_sha1_file(active_cache[pos]->sha1, &type, &sz);
	if (!data || type != OBJ_BLOB) {
		free(data);
		return NULL;
	}
	*size = xsize_t(sz);
	if (sha1_stat) {
		memset(&sha1_stat->stat, 0, sizeof(sha1_stat->stat));
		hashcpy(sha1_stat->sha1, active_cache[pos]->sha1);
	}
	return data;
}

/*
 * Frees memory within el which was allocated for exclude patterns and
 * the file buffer.  Does not free el itself.
 */
void clear_exclude_list(struct exclude_list *el)
{
	int i;

	for (i = 0; i < el->nr; i++)
		free(el->excludes[i]);
	free(el->excludes);
	free(el->filebuf);

	el->nr = 0;
	el->excludes = NULL;
	el->filebuf = NULL;
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
		int cmp, next = (last + first) >> 1;
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
	d = xmalloc(sizeof(*d) + len + 1);
	memset(d, 0, sizeof(*d));
	memcpy(d->name, name, len);
	d->name[len] = '\0';

	ALLOC_GROW(dir->dirs, dir->dirs_nr + 1, dir->dirs_alloc);
	memmove(dir->dirs + first + 1, dir->dirs + first,
		(dir->dirs_nr - first) * sizeof(*dir->dirs));
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
	uc->dir_invalidated++;
	dir->valid = 0;
	dir->untracked_nr = 0;
	for (i = 0; i < dir->dirs_nr; i++)
		dir->dirs[i]->recurse = 0;
}

/*
 * Given a file with name "fname", read it (either from disk, or from
 * the index if "check_index" is non-zero), parse it and store the
 * exclude rules in "el".
 *
 * If "ss" is not NULL, compute SHA-1 of the exclude file and fill
 * stat data from disk (only valid if add_excludes returns zero). If
 * ss_valid is non-zero, "ss" must contain good value as input.
 */
static int add_excludes(const char *fname, const char *base, int baselen,
			struct exclude_list *el, int check_index,
			struct sha1_stat *sha1_stat)
{
	struct stat st;
	int fd, i, lineno = 1;
	size_t size = 0;
	char *buf, *entry;

	fd = open(fname, O_RDONLY);
	if (fd < 0 || fstat(fd, &st) < 0) {
		if (errno != ENOENT)
			warn_on_inaccessible(fname);
		if (0 <= fd)
			close(fd);
		if (!check_index ||
		    (buf = read_skip_worktree_file_from_index(fname, &size, sha1_stat)) == NULL)
			return -1;
		if (size == 0) {
			free(buf);
			return 0;
		}
		if (buf[size-1] != '\n') {
			buf = xrealloc(buf, size+1);
			buf[size++] = '\n';
		}
	} else {
		size = xsize_t(st.st_size);
		if (size == 0) {
			if (sha1_stat) {
				fill_stat_data(&sha1_stat->stat, &st);
				hashcpy(sha1_stat->sha1, EMPTY_BLOB_SHA1_BIN);
				sha1_stat->valid = 1;
			}
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
		if (sha1_stat) {
			int pos;
			if (sha1_stat->valid &&
			    !match_stat_data_racy(&the_index, &sha1_stat->stat, &st))
				; /* no content change, ss->sha1 still good */
			else if (check_index &&
				 (pos = cache_name_pos(fname, strlen(fname))) >= 0 &&
				 !ce_stage(active_cache[pos]) &&
				 ce_uptodate(active_cache[pos]) &&
				 !would_convert_to_git(fname))
				hashcpy(sha1_stat->sha1, active_cache[pos]->sha1);
			else
				hash_sha1_file(buf, size, "blob", sha1_stat->sha1);
			fill_stat_data(&sha1_stat->stat, &st);
			sha1_stat->valid = 1;
		}
	}

	el->filebuf = buf;

	if (skip_utf8_bom(&buf, size))
		size -= buf - el->filebuf;

	entry = buf;

	for (i = 0; i < size; i++) {
		if (buf[i] == '\n') {
			if (entry != buf + i && entry[0] != '#') {
				buf[i - (i && buf[i-1] == '\r')] = 0;
				trim_trailing_spaces(entry);
				add_exclude(entry, base, baselen, el, lineno);
			}
			lineno++;
			entry = buf + i + 1;
		}
	}
	return 0;
}

int add_excludes_from_file_to_list(const char *fname, const char *base,
				   int baselen, struct exclude_list *el,
				   int check_index)
{
	return add_excludes(fname, base, baselen, el, check_index, NULL);
}

struct exclude_list *add_exclude_list(struct dir_struct *dir,
				      int group_type, const char *src)
{
	struct exclude_list *el;
	struct exclude_list_group *group;

	group = &dir->exclude_list_group[group_type];
	ALLOC_GROW(group->el, group->nr + 1, group->alloc);
	el = &group->el[group->nr++];
	memset(el, 0, sizeof(*el));
	el->src = src;
	return el;
}

/*
 * Used to set up core.excludesfile and .git/info/exclude lists.
 */
static void add_excludes_from_file_1(struct dir_struct *dir, const char *fname,
				     struct sha1_stat *sha1_stat)
{
	struct exclude_list *el;
	/*
	 * catch setup_standard_excludes() that's called before
	 * dir->untracked is assigned. That function behaves
	 * differently when dir->untracked is non-NULL.
	 */
	if (!dir->untracked)
		dir->unmanaged_exclude_files++;
	el = add_exclude_list(dir, EXC_FILE, fname);
	if (add_excludes(fname, "", 0, el, 0, sha1_stat) < 0)
		die("cannot use %s as an exclude file", fname);
}

void add_excludes_from_file(struct dir_struct *dir, const char *fname)
{
	dir->unmanaged_exclude_files++; /* see validate_untracked_cache() */
	add_excludes_from_file_1(dir, fname, NULL);
}

int match_basename(const char *basename, int basenamelen,
		   const char *pattern, int prefix, int patternlen,
		   int flags)
{
	if (prefix == patternlen) {
		if (patternlen == basenamelen &&
		    !strncmp_icase(pattern, basename, basenamelen))
			return 1;
	} else if (flags & EXC_FLAG_ENDSWITH) {
		/* "*literal" matching against "fooliteral" */
		if (patternlen - 1 <= basenamelen &&
		    !strncmp_icase(pattern + 1,
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
		   const char *pattern, int prefix, int patternlen,
		   int flags)
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
	    strncmp_icase(pathname, base, baselen))
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

		if (strncmp_icase(pattern, name, prefix))
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
static struct exclude *last_exclude_matching_from_list(const char *pathname,
						       int pathlen,
						       const char *basename,
						       int *dtype,
						       struct exclude_list *el)
{
	int i;

	if (!el->nr)
		return NULL;	/* undefined */

	for (i = el->nr - 1; 0 <= i; i--) {
		struct exclude *x = el->excludes[i];
		const char *exclude = x->pattern;
		int prefix = x->nowildcardlen;

		if (x->flags & EXC_FLAG_MUSTBEDIR) {
			if (*dtype == DT_UNKNOWN)
				*dtype = get_dtype(NULL, pathname, pathlen);
			if (*dtype != DT_DIR)
				continue;
		}

		if (x->flags & EXC_FLAG_NODIR) {
			if (match_basename(basename,
					   pathlen - (basename - pathname),
					   exclude, prefix, x->patternlen,
					   x->flags))
				return x;
			continue;
		}

		assert(x->baselen == 0 || x->base[x->baselen - 1] == '/');
		if (match_pathname(pathname, pathlen,
				   x->base, x->baselen ? x->baselen - 1 : 0,
				   exclude, prefix, x->patternlen, x->flags))
			return x;
	}
	return NULL; /* undecided */
}

/*
 * Scan the list and let the last match determine the fate.
 * Return 1 for exclude, 0 for include and -1 for undecided.
 */
int is_excluded_from_list(const char *pathname,
			  int pathlen, const char *basename, int *dtype,
			  struct exclude_list *el)
{
	struct exclude *exclude;
	exclude = last_exclude_matching_from_list(pathname, pathlen, basename, dtype, el);
	if (exclude)
		return exclude->flags & EXC_FLAG_NEGATIVE ? 0 : 1;
	return -1; /* undecided */
}

static struct exclude *last_exclude_matching_from_lists(struct dir_struct *dir,
		const char *pathname, int pathlen, const char *basename,
		int *dtype_p)
{
	int i, j;
	struct exclude_list_group *group;
	struct exclude *exclude;
	for (i = EXC_CMDL; i <= EXC_FILE; i++) {
		group = &dir->exclude_list_group[i];
		for (j = group->nr - 1; j >= 0; j--) {
			exclude = last_exclude_matching_from_list(
				pathname, pathlen, basename, dtype_p,
				&group->el[j]);
			if (exclude)
				return exclude;
		}
	}
	return NULL;
}

/*
 * Loads the per-directory exclude list for the substring of base
 * which has a char length of baselen.
 */
static void prep_exclude(struct dir_struct *dir, const char *base, int baselen)
{
	struct exclude_list_group *group;
	struct exclude_list *el;
	struct exclude_stack *stk = NULL;
	struct untracked_cache_dir *untracked;
	int current;

	group = &dir->exclude_list_group[EXC_DIRS];

	/*
	 * Pop the exclude lists from the EXCL_DIRS exclude_list_group
	 * which originate from directories not in the prefix of the
	 * path being checked.
	 */
	while ((stk = dir->exclude_stack) != NULL) {
		if (stk->baselen <= baselen &&
		    !strncmp(dir->basebuf.buf, base, stk->baselen))
			break;
		el = &group->el[dir->exclude_stack->exclude_ix];
		dir->exclude_stack = stk->prev;
		dir->exclude = NULL;
		free((char *)el->src); /* see strbuf_detach() below */
		clear_exclude_list(el);
		free(stk);
		group->nr--;
	}

	/* Skip traversing into sub directories if the parent is excluded */
	if (dir->exclude)
		return;

	/*
	 * Lazy initialization. All call sites currently just
	 * memset(dir, 0, sizeof(*dir)) before use. Changing all of
	 * them seems lots of work for little benefit.
	 */
	if (!dir->basebuf.buf)
		strbuf_init(&dir->basebuf, PATH_MAX);

	/* Read from the parent directories and push them down. */
	current = stk ? stk->baselen : -1;
	strbuf_setlen(&dir->basebuf, current < 0 ? 0 : current);
	if (dir->untracked)
		untracked = stk ? stk->ucd : dir->untracked->root;
	else
		untracked = NULL;

	while (current < baselen) {
		const char *cp;
		struct sha1_stat sha1_stat;

		stk = xcalloc(1, sizeof(*stk));
		if (current < 0) {
			cp = base;
			current = 0;
		} else {
			cp = strchr(base + current + 1, '/');
			if (!cp)
				die("oops in prep_exclude");
			cp++;
			untracked =
				lookup_untracked(dir->untracked, untracked,
						 base + current,
						 cp - base - current);
		}
		stk->prev = dir->exclude_stack;
		stk->baselen = cp - base;
		stk->exclude_ix = group->nr;
		stk->ucd = untracked;
		el = add_exclude_list(dir, EXC_DIRS, NULL);
		strbuf_add(&dir->basebuf, base + current, stk->baselen - current);
		assert(stk->baselen == dir->basebuf.len);

		/* Abort if the directory is excluded */
		if (stk->baselen) {
			int dt = DT_DIR;
			dir->basebuf.buf[stk->baselen - 1] = 0;
			dir->exclude = last_exclude_matching_from_lists(dir,
				dir->basebuf.buf, stk->baselen - 1,
				dir->basebuf.buf + current, &dt);
			dir->basebuf.buf[stk->baselen - 1] = '/';
			if (dir->exclude &&
			    dir->exclude->flags & EXC_FLAG_NEGATIVE)
				dir->exclude = NULL;
			if (dir->exclude) {
				dir->exclude_stack = stk;
				return;
			}
		}

		/* Try to read per-directory file */
		hashclr(sha1_stat.sha1);
		sha1_stat.valid = 0;
		if (dir->exclude_per_dir &&
		    /*
		     * If we know that no files have been added in
		     * this directory (i.e. valid_cached_dir() has
		     * been executed and set untracked->valid) ..
		     */
		    (!untracked || !untracked->valid ||
		     /*
		      * .. and .gitignore does not exist before
		      * (i.e. null exclude_sha1). Then we can skip
		      * loading .gitignore, which would result in
		      * ENOENT anyway.
		      */
		     !is_null_sha1(untracked->exclude_sha1))) {
			/*
			 * dir->basebuf gets reused by the traversal, but we
			 * need fname to remain unchanged to ensure the src
			 * member of each struct exclude correctly
			 * back-references its source file.  Other invocations
			 * of add_exclude_list provide stable strings, so we
			 * strbuf_detach() and free() here in the caller.
			 */
			struct strbuf sb = STRBUF_INIT;
			strbuf_addbuf(&sb, &dir->basebuf);
			strbuf_addstr(&sb, dir->exclude_per_dir);
			el->src = strbuf_detach(&sb, NULL);
			add_excludes(el->src, el->src, stk->baselen, el, 1,
				     untracked ? &sha1_stat : NULL);
		}
		/*
		 * NEEDSWORK: when untracked cache is enabled, prep_exclude()
		 * will first be called in valid_cached_dir() then maybe many
		 * times more in last_exclude_matching(). When the cache is
		 * used, last_exclude_matching() will not be called and
		 * reading .gitignore content will be a waste.
		 *
		 * So when it's called by valid_cached_dir() and we can get
		 * .gitignore SHA-1 from the index (i.e. .gitignore is not
		 * modified on work tree), we could delay reading the
		 * .gitignore content until we absolutely need it in
		 * last_exclude_matching(). Be careful about ignore rule
		 * order, though, if you do that.
		 */
		if (untracked &&
		    hashcmp(sha1_stat.sha1, untracked->exclude_sha1)) {
			invalidate_gitignore(dir->untracked, untracked);
			hashcpy(untracked->exclude_sha1, sha1_stat.sha1);
		}
		dir->exclude_stack = stk;
		current = stk->baselen;
	}
	strbuf_setlen(&dir->basebuf, baselen);
}

/*
 * Loads the exclude lists for the directory containing pathname, then
 * scans all exclude lists to determine whether pathname is excluded.
 * Returns the exclude_list element which matched, or NULL for
 * undecided.
 */
struct exclude *last_exclude_matching(struct dir_struct *dir,
					     const char *pathname,
					     int *dtype_p)
{
	int pathlen = strlen(pathname);
	const char *basename = strrchr(pathname, '/');
	basename = (basename) ? basename+1 : pathname;

	prep_exclude(dir, pathname, basename-pathname);

	if (dir->exclude)
		return dir->exclude;

	return last_exclude_matching_from_lists(dir, pathname, pathlen,
			basename, dtype_p);
}

/*
 * Loads the exclude lists for the directory containing pathname, then
 * scans all exclude lists to determine whether pathname is excluded.
 * Returns 1 if true, otherwise 0.
 */
int is_excluded(struct dir_struct *dir, const char *pathname, int *dtype_p)
{
	struct exclude *exclude =
		last_exclude_matching(dir, pathname, dtype_p);
	if (exclude)
		return exclude->flags & EXC_FLAG_NEGATIVE ? 0 : 1;
	return 0;
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
	if (cache_file_exists(pathname, len, ignore_case))
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
 * Do not use the alphabetically sorted index to look up
 * the directory name; instead, use the case insensitive
 * directory hash.
 */
static enum exist_status directory_exists_in_index_icase(const char *dirname, int len)
{
	struct cache_entry *ce;

	if (cache_dir_exists(dirname, len))
		return index_directory;

	ce = cache_file_exists(dirname, len, ignore_case);
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
static enum exist_status directory_exists_in_index(const char *dirname, int len)
{
	int pos;

	if (ignore_case)
		return directory_exists_in_index_icase(dirname, len);

	pos = cache_name_pos(dirname, len);
	if (pos < 0)
		pos = -pos-1;
	while (pos < active_nr) {
		const struct cache_entry *ce = active_cache[pos++];
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
 *      also true, in which case we need to check if it contains any
 *      untracked and / or ignored files.
 *  (b) if it looks like a git directory, and we don't have
 *      'no_gitlinks' set we treat it as a gitlink, and show it
 *      as a directory.
 *  (c) otherwise, we recurse into it.
 */
static enum path_treatment treat_directory(struct dir_struct *dir,
	struct untracked_cache_dir *untracked,
	const char *dirname, int len, int baselen, int exclude,
	const struct path_simplify *simplify)
{
	/* The "len-1" is to strip the final '/' */
	switch (directory_exists_in_index(dirname, len-1)) {
	case index_directory:
		return path_recurse;

	case index_gitdir:
		return path_none;

	case index_nonexistent:
		if (dir->flags & DIR_SHOW_OTHER_DIRECTORIES)
			break;
		if (!(dir->flags & DIR_NO_GITLINKS)) {
			unsigned char sha1[20];
			if (resolve_gitlink_ref(dirname, "HEAD", sha1) == 0)
				return path_untracked;
		}
		return path_recurse;
	}

	/* This is the "show_other_directories" case */

	if (!(dir->flags & DIR_HIDE_EMPTY_DIRECTORIES))
		return exclude ? path_excluded : path_untracked;

	untracked = lookup_untracked(dir->untracked, untracked,
				     dirname + baselen, len - baselen);
	return read_directory_recursive(dir, dirname, len,
					untracked, 1, simplify);
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
	const struct cache_entry *ce;

	ce = cache_file_exists(path, len, 0);
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

static enum path_treatment treat_one_path(struct dir_struct *dir,
					  struct untracked_cache_dir *untracked,
					  struct strbuf *path,
					  int baselen,
					  const struct path_simplify *simplify,
					  int dtype, struct dirent *de)
{
	int exclude;
	int has_path_in_index = !!cache_file_exists(path->buf, path->len, ignore_case);

	if (dtype == DT_UNKNOWN)
		dtype = get_dtype(de, path->buf, path->len);

	/* Always exclude indexed files */
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
	    (directory_exists_in_index(path->buf, path->len) == index_nonexistent))
		return path_none;

	exclude = is_excluded(dir, path->buf, &dtype);

	/*
	 * Excluded? If we don't explicitly want to show
	 * ignored files, ignore it
	 */
	if (exclude && !(dir->flags & (DIR_SHOW_IGNORED|DIR_SHOW_IGNORED_TOO)))
		return path_excluded;

	switch (dtype) {
	default:
		return path_none;
	case DT_DIR:
		strbuf_addch(path, '/');
		return treat_directory(dir, untracked, path->buf, path->len,
				       baselen, exclude, simplify);
	case DT_REG:
	case DT_LNK:
		return exclude ? path_excluded : path_untracked;
	}
}

static enum path_treatment treat_path_fast(struct dir_struct *dir,
					   struct untracked_cache_dir *untracked,
					   struct cached_dir *cdir,
					   struct strbuf *path,
					   int baselen,
					   const struct path_simplify *simplify)
{
	strbuf_setlen(path, baselen);
	if (!cdir->ucd) {
		strbuf_addstr(path, cdir->file);
		return path_untracked;
	}
	strbuf_addstr(path, cdir->ucd->name);
	/* treat_one_path() does this before it calls treat_directory() */
	if (path->buf[path->len - 1] != '/')
		strbuf_addch(path, '/');
	if (cdir->ucd->check_only)
		/*
		 * check_only is set as a result of treat_directory() getting
		 * to its bottom. Verify again the same set of directories
		 * with check_only set.
		 */
		return read_directory_recursive(dir, path->buf, path->len,
						cdir->ucd, 1, simplify);
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
				      struct strbuf *path,
				      int baselen,
				      const struct path_simplify *simplify)
{
	int dtype;
	struct dirent *de = cdir->de;

	if (!de)
		return treat_path_fast(dir, untracked, cdir, path,
				       baselen, simplify);
	if (is_dot_or_dotdot(de->d_name) || !strcmp(de->d_name, ".git"))
		return path_none;
	strbuf_setlen(path, baselen);
	strbuf_addstr(path, de->d_name);
	if (simplify_away(path->buf, path->len, simplify))
		return path_none;

	dtype = DTYPE(de);
	return treat_one_path(dir, untracked, path, baselen, simplify, dtype, de);
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
			    struct strbuf *path,
			    int check_only)
{
	struct stat st;

	if (!untracked)
		return 0;

	if (stat(path->len ? path->buf : ".", &st)) {
		invalidate_directory(dir->untracked, untracked);
		memset(&untracked->stat_data, 0, sizeof(untracked->stat_data));
		return 0;
	}
	if (!untracked->valid ||
	    match_stat_data_racy(&the_index, &untracked->stat_data, &st)) {
		if (untracked->valid)
			invalidate_directory(dir->untracked, untracked);
		fill_stat_data(&untracked->stat_data, &st);
		return 0;
	}

	if (untracked->check_only != !!check_only) {
		invalidate_directory(dir->untracked, untracked);
		return 0;
	}

	/*
	 * prep_exclude will be called eventually on this directory,
	 * but it's called much later in last_exclude_matching(). We
	 * need it now to determine the validity of the cache for this
	 * path. The next calls will be nearly no-op, the way
	 * prep_exclude() is designed.
	 */
	if (path->len && path->buf[path->len - 1] != '/') {
		strbuf_addch(path, '/');
		prep_exclude(dir, path->buf, path->len);
		strbuf_setlen(path, path->len - 1);
	} else
		prep_exclude(dir, path->buf, path->len);

	/* hopefully prep_exclude() haven't invalidated this entry... */
	return untracked->valid;
}

static int open_cached_dir(struct cached_dir *cdir,
			   struct dir_struct *dir,
			   struct untracked_cache_dir *untracked,
			   struct strbuf *path,
			   int check_only)
{
	memset(cdir, 0, sizeof(*cdir));
	cdir->untracked = untracked;
	if (valid_cached_dir(dir, untracked, path, check_only))
		return 0;
	cdir->fdir = opendir(path->len ? path->buf : ".");
	if (dir->untracked)
		dir->untracked->dir_opened++;
	if (!cdir->fdir)
		return -1;
	return 0;
}

static int read_cached_dir(struct cached_dir *cdir)
{
	if (cdir->fdir) {
		cdir->de = readdir(cdir->fdir);
		if (!cdir->de)
			return -1;
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

/*
 * Read a directory tree. We currently ignore anything but
 * directories, regular files and symlinks. That's because git
 * doesn't handle them at all yet. Maybe that will change some
 * day.
 *
 * Also, we ignore the name ".git" (even if it is not a directory).
 * That likely will not change.
 *
 * Returns the most significant path_treatment value encountered in the scan.
 */
static enum path_treatment read_directory_recursive(struct dir_struct *dir,
				    const char *base, int baselen,
				    struct untracked_cache_dir *untracked, int check_only,
				    const struct path_simplify *simplify)
{
	struct cached_dir cdir;
	enum path_treatment state, subdir_state, dir_state = path_none;
	struct strbuf path = STRBUF_INIT;

	strbuf_add(&path, base, baselen);

	if (open_cached_dir(&cdir, dir, untracked, &path, check_only))
		goto out;

	if (untracked)
		untracked->check_only = !!check_only;

	while (!read_cached_dir(&cdir)) {
		/* check how the file or directory should be treated */
		state = treat_path(dir, untracked, &cdir, &path, baselen, simplify);

		if (state > dir_state)
			dir_state = state;

		/* recurse into subdir if instructed by treat_path */
		if (state == path_recurse) {
			struct untracked_cache_dir *ud;
			ud = lookup_untracked(dir->untracked, untracked,
					      path.buf + baselen,
					      path.len - baselen);
			subdir_state =
				read_directory_recursive(dir, path.buf, path.len,
							 ud, check_only, simplify);
			if (subdir_state > dir_state)
				dir_state = subdir_state;
		}

		if (check_only) {
			/* abort early if maximum state has been reached */
			if (dir_state == path_untracked) {
				if (cdir.fdir)
					add_untracked(untracked, path.buf + baselen);
				break;
			}
			/* skip the dir_add_* part */
			continue;
		}

		/* add the path to the appropriate result list */
		switch (state) {
		case path_excluded:
			if (dir->flags & DIR_SHOW_IGNORED)
				dir_add_name(dir, path.buf, path.len);
			else if ((dir->flags & DIR_SHOW_IGNORED_TOO) ||
				((dir->flags & DIR_COLLECT_IGNORED) &&
				exclude_matches_pathspec(path.buf, path.len,
					simplify)))
				dir_add_ignored(dir, path.buf, path.len);
			break;

		case path_untracked:
			if (dir->flags & DIR_SHOW_IGNORED)
				break;
			dir_add_name(dir, path.buf, path.len);
			if (cdir.fdir)
				add_untracked(untracked, path.buf + baselen);
			break;

		default:
			break;
		}
	}
	close_cached_dir(&cdir);
 out:
	strbuf_release(&path);

	return dir_state;
}

static int cmp_name(const void *p1, const void *p2)
{
	const struct dir_entry *e1 = *(const struct dir_entry **)p1;
	const struct dir_entry *e2 = *(const struct dir_entry **)p2;

	return name_compare(e1->name, e1->len, e2->name, e2->len);
}

static struct path_simplify *create_simplify(const char **pathspec)
{
	int nr, alloc = 0;
	struct path_simplify *simplify = NULL;

	if (!pathspec)
		return NULL;

	for (nr = 0 ; ; nr++) {
		const char *match;
		ALLOC_GROW(simplify, nr + 1, alloc);
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
	int old_flags = dir->flags;

	while (len && path[len - 1] == '/')
		len--;
	if (!len)
		return 1;
	baselen = 0;
	dir->flags &= ~DIR_SHOW_OTHER_DIRECTORIES;
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
		if (treat_one_path(dir, NULL, &sb, baselen, simplify,
				   DT_DIR, NULL) == path_none)
			break; /* do not recurse into it */
		if (len <= baselen) {
			rc = 1;
			break; /* finished checking */
		}
	}
	strbuf_release(&sb);
	dir->flags = old_flags;
	return rc;
}

static const char *get_ident_string(void)
{
	static struct strbuf sb = STRBUF_INIT;
	struct utsname uts;

	if (sb.len)
		return sb.buf;
	if (uname(&uts) < 0)
		die_errno(_("failed to get kernel name and information"));
	strbuf_addf(&sb, "Location %s, system %s %s %s", get_git_work_tree(),
		    uts.sysname, uts.release, uts.version);
	return sb.buf;
}

static int ident_in_untracked(const struct untracked_cache *uc)
{
	const char *end = uc->ident.buf + uc->ident.len;
	const char *p   = uc->ident.buf;

	for (p = uc->ident.buf; p < end; p += strlen(p) + 1)
		if (!strcmp(p, get_ident_string()))
			return 1;
	return 0;
}

void add_untracked_ident(struct untracked_cache *uc)
{
	if (ident_in_untracked(uc))
		return;
	strbuf_addstr(&uc->ident, get_ident_string());
	/* this strbuf contains a list of strings, save NUL too */
	strbuf_addch(&uc->ident, 0);
}

static struct untracked_cache_dir *validate_untracked_cache(struct dir_struct *dir,
						      int base_len,
						      const struct pathspec *pathspec)
{
	struct untracked_cache_dir *root;

	if (!dir->untracked || getenv("GIT_DISABLE_UNTRACKED_CACHE"))
		return NULL;

	/*
	 * We only support $GIT_DIR/info/exclude and core.excludesfile
	 * as the global ignore rule files. Any other additions
	 * (e.g. from command line) invalidate the cache. This
	 * condition also catches running setup_standard_excludes()
	 * before setting dir->untracked!
	 */
	if (dir->unmanaged_exclude_files)
		return NULL;

	/*
	 * Optimize for the main use case only: whole-tree git
	 * status. More work involved in treat_leading_path() if we
	 * use cache on just a subset of the worktree. pathspec
	 * support could make the matter even worse.
	 */
	if (base_len || (pathspec && pathspec->nr))
		return NULL;

	/* Different set of flags may produce different results */
	if (dir->flags != dir->untracked->dir_flags ||
	    /*
	     * See treat_directory(), case index_nonexistent. Without
	     * this flag, we may need to also cache .git file content
	     * for the resolve_gitlink_ref() call, which we don't.
	     */
	    !(dir->flags & DIR_SHOW_OTHER_DIRECTORIES) ||
	    /* We don't support collecting ignore files */
	    (dir->flags & (DIR_SHOW_IGNORED | DIR_SHOW_IGNORED_TOO |
			   DIR_COLLECT_IGNORED)))
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
	if (dir->exclude_list_group[EXC_CMDL].nr)
		return NULL;

	if (!ident_in_untracked(dir->untracked)) {
		warning(_("Untracked cache is disabled on this system."));
		return NULL;
	}

	if (!dir->untracked->root) {
		const int len = sizeof(*dir->untracked->root);
		dir->untracked->root = xmalloc(len);
		memset(dir->untracked->root, 0, len);
	}

	/* Validate $GIT_DIR/info/exclude and core.excludesfile */
	root = dir->untracked->root;
	if (hashcmp(dir->ss_info_exclude.sha1,
		    dir->untracked->ss_info_exclude.sha1)) {
		invalidate_gitignore(dir->untracked, root);
		dir->untracked->ss_info_exclude = dir->ss_info_exclude;
	}
	if (hashcmp(dir->ss_excludes_file.sha1,
		    dir->untracked->ss_excludes_file.sha1)) {
		invalidate_gitignore(dir->untracked, root);
		dir->untracked->ss_excludes_file = dir->ss_excludes_file;
	}

	/* Make sure this directory is not dropped out at saving phase */
	root->recurse = 1;
	return root;
}

int read_directory(struct dir_struct *dir, const char *path, int len, const struct pathspec *pathspec)
{
	struct path_simplify *simplify;
	struct untracked_cache_dir *untracked;

	/*
	 * Check out create_simplify()
	 */
	if (pathspec)
		GUARD_PATHSPEC(pathspec,
			       PATHSPEC_FROMTOP |
			       PATHSPEC_MAXDEPTH |
			       PATHSPEC_LITERAL |
			       PATHSPEC_GLOB |
			       PATHSPEC_ICASE |
			       PATHSPEC_EXCLUDE);

	if (has_symlink_leading_path(path, len))
		return dir->nr;

	/*
	 * exclude patterns are treated like positive ones in
	 * create_simplify. Usually exclude patterns should be a
	 * subset of positive ones, which has no impacts on
	 * create_simplify().
	 */
	simplify = create_simplify(pathspec ? pathspec->_raw : NULL);
	untracked = validate_untracked_cache(dir, len, pathspec);
	if (!untracked)
		/*
		 * make sure untracked cache code path is disabled,
		 * e.g. prep_exclude()
		 */
		dir->untracked = NULL;
	if (!len || treat_leading_path(dir, path, len, simplify))
		read_directory_recursive(dir, path, len, untracked, 0, simplify);
	free_simplify(simplify);
	qsort(dir->entries, dir->nr, sizeof(struct dir_entry *), cmp_name);
	qsort(dir->ignored, dir->ignored_nr, sizeof(struct dir_entry *), cmp_name);
	if (dir->untracked) {
		static struct trace_key trace_untracked_stats = TRACE_KEY_INIT(UNTRACKED_STATS);
		trace_printf_key(&trace_untracked_stats,
				 "node creation: %u\n"
				 "gitignore invalidation: %u\n"
				 "directory invalidation: %u\n"
				 "opendir: %u\n",
				 dir->untracked->dir_created,
				 dir->untracked->gitignore_invalidated,
				 dir->untracked->dir_invalidated,
				 dir->untracked->dir_opened);
		if (dir->untracked == the_index.untracked &&
		    (dir->untracked->dir_opened ||
		     dir->untracked->gitignore_invalidated ||
		     dir->untracked->dir_invalidated))
			the_index.cache_changed |= UNTRACKED_CHANGED;
		if (dir->untracked != the_index.untracked) {
			free(dir->untracked);
			dir->untracked = NULL;
		}
	}
	return dir->nr;
}

int file_exists(const char *f)
{
	struct stat sb;
	return lstat(f, &sb) == 0;
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
	if (path->buf[original_len - 1] != '/')
		strbuf_addch(path, '/');

	len = path->len;
	while ((e = readdir(dir)) != NULL) {
		struct stat st;
		if (is_dot_or_dotdot(e->d_name))
			continue;

		strbuf_setlen(path, len);
		strbuf_addstr(path, e->d_name);
		if (lstat(path->buf, &st)) {
			if (errno == ENOENT)
				/*
				 * file disappeared, which is what we
				 * wanted anyway
				 */
				continue;
			/* fall thru */
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
	if (!ret && !keep_toplevel && !kept_down)
		ret = (!rmdir(path->buf) || errno == ENOENT) ? 0 : -1;
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

static GIT_PATH_FUNC(git_path_info_exclude, "info/exclude")

void setup_standard_excludes(struct dir_struct *dir)
{
	const char *path;

	dir->exclude_per_dir = ".gitignore";

	/* core.excludefile defaulting to $XDG_HOME/git/ignore */
	if (!excludes_file)
		excludes_file = xdg_config_home("ignore");
	if (excludes_file && !access_or_warn(excludes_file, R_OK, 0))
		add_excludes_from_file_1(dir, excludes_file,
					 dir->untracked ? &dir->ss_excludes_file : NULL);

	/* per repository user preference */
	path = git_path_info_exclude();
	if (!access_or_warn(path, R_OK, 0))
		add_excludes_from_file_1(dir, path,
					 dir->untracked ? &dir->ss_info_exclude : NULL);
}

int remove_path(const char *name)
{
	char *slash;

	if (unlink(name) && errno != ENOENT && errno != ENOTDIR)
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

/*
 * Frees memory within dir which was allocated for exclude lists and
 * the exclude_stack.  Does not free dir itself.
 */
void clear_directory(struct dir_struct *dir)
{
	int i, j;
	struct exclude_list_group *group;
	struct exclude_list *el;
	struct exclude_stack *stk;

	for (i = EXC_CMDL; i <= EXC_FILE; i++) {
		group = &dir->exclude_list_group[i];
		for (j = 0; j < group->nr; j++) {
			el = &group->el[j];
			if (i == EXC_DIRS)
				free((char *)el->src);
			clear_exclude_list(el);
		}
		free(group->el);
	}

	stk = dir->exclude_stack;
	while (stk) {
		struct exclude_stack *prev = stk->prev;
		free(stk);
		stk = prev;
	}
	strbuf_release(&dir->basebuf);
}

struct ondisk_untracked_cache {
	struct stat_data info_exclude_stat;
	struct stat_data excludes_file_stat;
	uint32_t dir_flags;
	unsigned char info_exclude_sha1[20];
	unsigned char excludes_file_sha1[20];
	char exclude_per_dir[FLEX_ARRAY];
};

#define ouc_size(len) (offsetof(struct ondisk_untracked_cache, exclude_per_dir) + len + 1)

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
	if (!is_null_sha1(untracked->exclude_sha1)) {
		ewah_set(wd->sha1_valid, i);
		strbuf_add(&wd->sb_sha1, untracked->exclude_sha1, 20);
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
	int len = 0, varint_len;
	if (untracked->exclude_per_dir)
		len = strlen(untracked->exclude_per_dir);
	ouc = xmalloc(sizeof(*ouc) + len + 1);
	stat_data_to_disk(&ouc->info_exclude_stat, &untracked->ss_info_exclude.stat);
	stat_data_to_disk(&ouc->excludes_file_stat, &untracked->ss_excludes_file.stat);
	hashcpy(ouc->info_exclude_sha1, untracked->ss_info_exclude.sha1);
	hashcpy(ouc->excludes_file_sha1, untracked->ss_excludes_file.sha1);
	ouc->dir_flags = htonl(untracked->dir_flags);
	memcpy(ouc->exclude_per_dir, untracked->exclude_per_dir, len + 1);

	varint_len = encode_varint(untracked->ident.len, varbuf);
	strbuf_add(out, varbuf, varint_len);
	strbuf_add(out, untracked->ident.buf, untracked->ident.len);

	strbuf_add(out, ouc, ouc_size(len));
	free(ouc);
	ouc = NULL;

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
	if (uc)
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

static void stat_data_from_disk(struct stat_data *to, const struct stat_data *from)
{
	to->sd_ctime.sec  = get_be32(&from->sd_ctime.sec);
	to->sd_ctime.nsec = get_be32(&from->sd_ctime.nsec);
	to->sd_mtime.sec  = get_be32(&from->sd_mtime.sec);
	to->sd_mtime.nsec = get_be32(&from->sd_mtime.nsec);
	to->sd_dev	  = get_be32(&from->sd_dev);
	to->sd_ino	  = get_be32(&from->sd_ino);
	to->sd_uid	  = get_be32(&from->sd_uid);
	to->sd_gid	  = get_be32(&from->sd_gid);
	to->sd_size	  = get_be32(&from->sd_size);
}

static int read_one_dir(struct untracked_cache_dir **untracked_,
			struct read_data *rd)
{
	struct untracked_cache_dir ud, *untracked;
	const unsigned char *next, *data = rd->data, *end = rd->end;
	unsigned int value;
	int i, len;

	memset(&ud, 0, sizeof(ud));

	next = data;
	value = decode_varint(&next);
	if (next > end)
		return -1;
	ud.recurse	   = 1;
	ud.untracked_alloc = value;
	ud.untracked_nr	   = value;
	if (ud.untracked_nr)
		ud.untracked = xmalloc(sizeof(*ud.untracked) * ud.untracked_nr);
	data = next;

	next = data;
	ud.dirs_alloc = ud.dirs_nr = decode_varint(&next);
	if (next > end)
		return -1;
	ud.dirs = xmalloc(sizeof(*ud.dirs) * ud.dirs_nr);
	data = next;

	len = strlen((const char *)data);
	next = data + len + 1;
	if (next > rd->end)
		return -1;
	*untracked_ = untracked = xmalloc(sizeof(*untracked) + len);
	memcpy(untracked, &ud, sizeof(ud));
	memcpy(untracked->name, data, len + 1);
	data = next;

	for (i = 0; i < untracked->untracked_nr; i++) {
		len = strlen((const char *)data);
		next = data + len + 1;
		if (next > rd->end)
			return -1;
		untracked->untracked[i] = xstrdup((const char*)data);
		data = next;
	}

	rd->ucd[rd->index++] = untracked;
	rd->data = data;

	for (i = 0; i < untracked->dirs_nr; i++) {
		len = read_one_dir(untracked->dirs + i, rd);
		if (len < 0)
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
	stat_data_from_disk(&ud->stat_data, (struct stat_data *)rd->data);
	rd->data += sizeof(struct stat_data);
	ud->valid = 1;
}

static void read_sha1(size_t pos, void *cb)
{
	struct read_data *rd = cb;
	struct untracked_cache_dir *ud = rd->ucd[pos];
	if (rd->data + 20 > rd->end) {
		rd->data = rd->end + 1;
		return;
	}
	hashcpy(ud->exclude_sha1, rd->data);
	rd->data += 20;
}

static void load_sha1_stat(struct sha1_stat *sha1_stat,
			   const struct stat_data *stat,
			   const unsigned char *sha1)
{
	stat_data_from_disk(&sha1_stat->stat, stat);
	hashcpy(sha1_stat->sha1, sha1);
	sha1_stat->valid = 1;
}

struct untracked_cache *read_untracked_extension(const void *data, unsigned long sz)
{
	const struct ondisk_untracked_cache *ouc;
	struct untracked_cache *uc;
	struct read_data rd;
	const unsigned char *next = data, *end = (const unsigned char *)data + sz;
	const char *ident;
	int ident_len, len;

	if (sz <= 1 || end[-1] != '\0')
		return NULL;
	end--;

	ident_len = decode_varint(&next);
	if (next + ident_len > end)
		return NULL;
	ident = (const char *)next;
	next += ident_len;

	ouc = (const struct ondisk_untracked_cache *)next;
	if (next + ouc_size(0) > end)
		return NULL;

	uc = xcalloc(1, sizeof(*uc));
	strbuf_init(&uc->ident, ident_len);
	strbuf_add(&uc->ident, ident, ident_len);
	load_sha1_stat(&uc->ss_info_exclude, &ouc->info_exclude_stat,
		       ouc->info_exclude_sha1);
	load_sha1_stat(&uc->ss_excludes_file, &ouc->excludes_file_stat,
		       ouc->excludes_file_sha1);
	uc->dir_flags = get_be32(&ouc->dir_flags);
	uc->exclude_per_dir = xstrdup(ouc->exclude_per_dir);
	/* NUL after exclude_per_dir is covered by sizeof(*ouc) */
	next += ouc_size(strlen(ouc->exclude_per_dir));
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
	rd.ucd        = xmalloc(sizeof(*rd.ucd) * len);

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
	ewah_each_bit(rd.sha1_valid, read_sha1, &rd);
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
				     const char *path)
{
	if (!istate->untracked || !istate->untracked->root)
		return;
	invalidate_one_component(istate->untracked, istate->untracked->root,
				 path, strlen(path));
}

void untracked_cache_remove_from_index(struct index_state *istate,
				       const char *path)
{
	untracked_cache_invalidate_path(istate, path);
}

void untracked_cache_add_to_index(struct index_state *istate,
				  const char *path)
{
	untracked_cache_invalidate_path(istate, path);
}
