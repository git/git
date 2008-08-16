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

static int read_directory_recursive(struct dir_struct *dir,
	const char *path, const char *base, int baselen,
	int check_only, const struct path_simplify *simplify);
static int get_dtype(struct dirent *de, const char *path);

int common_prefix(const char **pathspec)
{
	const char *path, *slash, *next;
	int prefix;

	if (!pathspec)
		return 0;

	path = *pathspec;
	slash = strrchr(path, '/');
	if (!slash)
		return 0;

	prefix = slash - path + 1;
	while ((next = *++pathspec) != NULL) {
		int len = strlen(next);
		if (len >= prefix && !memcmp(path, next, prefix))
			continue;
		len = prefix - 1;
		for (;;) {
			if (!len)
				return 0;
			if (next[--len] != '/')
				continue;
			if (memcmp(path, next, len+1))
				continue;
			prefix = len + 1;
			break;
		}
	}
	return prefix;
}

static inline int special_char(unsigned char c1)
{
	return !c1 || c1 == '*' || c1 == '[' || c1 == '?' || c1 == '\\';
}

/*
 * Does 'match' matches the given name?
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

	for (;;) {
		unsigned char c1 = *match;
		unsigned char c2 = *name;
		if (special_char(c1))
			break;
		if (c1 != c2)
			return 0;
		match++;
		name++;
		namelen--;
	}


	/*
	 * If we don't match the matchstring exactly,
	 * we need to match by fnmatch
	 */
	matchlen = strlen(match);
	if (strncmp(match, name, matchlen))
		return !fnmatch(match, name, 0) ? MATCHED_FNMATCH : 0;

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
int match_pathspec(const char **pathspec, const char *name, int namelen, int prefix, char *seen)
{
	int retval;
	const char *match;

	name += prefix;
	namelen -= prefix;

	for (retval = 0; (match = *pathspec++) != NULL; seen++) {
		int how;
		if (retval && *seen == MATCHED_EXACTLY)
			continue;
		match += prefix;
		how = match_one(match, name, namelen);
		if (how) {
			if (retval < how)
				retval = how;
			if (*seen < how)
				*seen = how;
		}
	}
	return retval;
}

static int no_wildcard(const char *string)
{
	return string[strcspn(string, "*?[{")] == '\0';
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
		s = (char*)(x+1);
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
	if (no_wildcard(string))
		x->flags |= EXC_FLAG_NOWILDCARD;
	if (*string == '*' && no_wildcard(string+1))
		x->flags |= EXC_FLAG_ENDSWITH;
	ALLOC_GROW(which->excludes, which->nr + 1, which->alloc);
	which->excludes[which->nr++] = x;
}

static int add_excludes_from_file_1(const char *fname,
				    const char *base,
				    int baselen,
				    char **buf_p,
				    struct exclude_list *which)
{
	struct stat st;
	int fd, i;
	size_t size;
	char *buf, *entry;

	fd = open(fname, O_RDONLY);
	if (fd < 0 || fstat(fd, &st) < 0)
		goto err;
	size = xsize_t(st.st_size);
	if (size == 0) {
		close(fd);
		return 0;
	}
	buf = xmalloc(size+1);
	if (read_in_full(fd, buf, size) != size)
	{
		free(buf);
		goto err;
	}
	close(fd);

	if (buf_p)
		*buf_p = buf;
	buf[size++] = '\n';
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

 err:
	if (0 <= fd)
		close(fd);
	return -1;
}

void add_excludes_from_file(struct dir_struct *dir, const char *fname)
{
	if (add_excludes_from_file_1(fname, "", 0, NULL,
				     &dir->exclude_list[EXC_FILE]) < 0)
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
		add_excludes_from_file_1(dir->basebuf,
					 dir->basebuf, stk->baselen,
					 &stk->filebuf, el);
		dir->exclude_stack = stk;
		current = stk->baselen;
	}
	dir->basebuf[baselen] = '\0';
}

/* Scan the list and let the last match determines the fate.
 * Return 1 for exclude, 0 for include and -1 for undecided.
 */
static int excluded_1(const char *pathname,
		      int pathlen, const char *basename, int *dtype,
		      struct exclude_list *el)
{
	int i;

	if (el->nr) {
		for (i = el->nr - 1; 0 <= i; i--) {
			struct exclude *x = el->excludes[i];
			const char *exclude = x->pattern;
			int to_exclude = x->to_exclude;

			if (x->flags & EXC_FLAG_MUSTBEDIR) {
				if (*dtype == DT_UNKNOWN)
					*dtype = get_dtype(NULL, pathname);
				if (*dtype != DT_DIR)
					continue;
			}

			if (x->flags & EXC_FLAG_NODIR) {
				/* match basename */
				if (x->flags & EXC_FLAG_NOWILDCARD) {
					if (!strcmp(exclude, basename))
						return to_exclude;
				} else if (x->flags & EXC_FLAG_ENDSWITH) {
					if (x->patternlen - 1 <= pathlen &&
					    !strcmp(exclude + 1, pathname + pathlen - x->patternlen + 1))
						return to_exclude;
				} else {
					if (fnmatch(exclude, basename, 0) == 0)
						return to_exclude;
				}
			}
			else {
				/* match with FNM_PATHNAME:
				 * exclude has base (baselen long) implicitly
				 * in front of it.
				 */
				int baselen = x->baselen;
				if (*exclude == '/')
					exclude++;

				if (pathlen < baselen ||
				    (baselen && pathname[baselen-1] != '/') ||
				    strncmp(pathname, x->base, baselen))
				    continue;

				if (x->flags & EXC_FLAG_NOWILDCARD) {
					if (!strcmp(exclude, pathname + baselen))
						return to_exclude;
				} else {
					if (fnmatch(exclude, pathname+baselen,
						    FNM_PATHNAME) == 0)
					    return to_exclude;
				}
			}
		}
	}
	return -1; /* undecided */
}

int excluded(struct dir_struct *dir, const char *pathname, int *dtype_p)
{
	int pathlen = strlen(pathname);
	int st;
	const char *basename = strrchr(pathname, '/');
	basename = (basename) ? basename+1 : pathname;

	prep_exclude(dir, pathname, basename-pathname);
	for (st = EXC_CMDL; st <= EXC_FILE; st++) {
		switch (excluded_1(pathname, pathlen, basename,
				   dtype_p, &dir->exclude_list[st])) {
		case 0:
			return 0;
		case 1:
			return 1;
		}
	}
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

struct dir_entry *dir_add_name(struct dir_struct *dir, const char *pathname, int len)
{
	if (cache_name_exists(pathname, len, ignore_case))
		return NULL;

	ALLOC_GROW(dir->entries, dir->nr+1, dir->alloc);
	return dir->entries[dir->nr++] = dir_entry_new(pathname, len);
}

struct dir_entry *dir_add_ignored(struct dir_struct *dir, const char *pathname, int len)
{
	if (cache_name_pos(pathname, len) >= 0)
		return NULL;

	ALLOC_GROW(dir->ignored, dir->ignored_nr+1, dir->ignored_alloc);
	return dir->ignored[dir->ignored_nr++] = dir_entry_new(pathname, len);
}

enum exist_status {
	index_nonexistent = 0,
	index_directory,
	index_gitdir,
};

/*
 * The index sorts alphabetically by entry name, which
 * means that a gitlink sorts as '\0' at the end, while
 * a directory (which is defined not as an entry, but as
 * the files it contains) will sort with the '/' at the
 * end.
 */
static enum exist_status directory_exists_in_index(const char *dirname, int len)
{
	int pos = cache_name_pos(dirname, len);
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
	recurse_into_directory,
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
		if (dir->show_other_directories)
			return ignore_directory;
		return show_directory;

	case index_nonexistent:
		if (dir->show_other_directories)
			break;
		if (!dir->no_gitlinks) {
			unsigned char sha1[20];
			if (resolve_gitlink_ref(dirname, "HEAD", sha1) == 0)
				return show_directory;
		}
		return recurse_into_directory;
	}

	/* This is the "show_other_directories" case */
	if (!dir->hide_empty_directories)
		return show_directory;
	if (!read_directory_recursive(dir, dirname, dirname, len, 1, simplify))
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

static int in_pathspec(const char *path, int len, const struct path_simplify *simplify)
{
	if (simplify) {
		for (; simplify->path; simplify++) {
			if (len == simplify->len
			    && !memcmp(path, simplify->path, len))
				return 1;
		}
	}
	return 0;
}

static int get_dtype(struct dirent *de, const char *path)
{
	int dtype = de ? DTYPE(de) : DT_UNKNOWN;
	struct stat st;

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

/*
 * Read a directory tree. We currently ignore anything but
 * directories, regular files and symlinks. That's because git
 * doesn't handle them at all yet. Maybe that will change some
 * day.
 *
 * Also, we ignore the name ".git" (even if it is not a directory).
 * That likely will not change.
 */
static int read_directory_recursive(struct dir_struct *dir, const char *path, const char *base, int baselen, int check_only, const struct path_simplify *simplify)
{
	DIR *fdir = opendir(path);
	int contents = 0;

	if (fdir) {
		struct dirent *de;
		char fullname[PATH_MAX + 1];
		memcpy(fullname, base, baselen);

		while ((de = readdir(fdir)) != NULL) {
			int len, dtype;
			int exclude;

			if ((de->d_name[0] == '.') &&
			    (de->d_name[1] == 0 ||
			     !strcmp(de->d_name + 1, ".") ||
			     !strcmp(de->d_name + 1, "git")))
				continue;
			len = strlen(de->d_name);
			/* Ignore overly long pathnames! */
			if (len + baselen + 8 > sizeof(fullname))
				continue;
			memcpy(fullname + baselen, de->d_name, len+1);
			if (simplify_away(fullname, baselen + len, simplify))
				continue;

			dtype = DTYPE(de);
			exclude = excluded(dir, fullname, &dtype);
			if (exclude && dir->collect_ignored
			    && in_pathspec(fullname, baselen + len, simplify))
				dir_add_ignored(dir, fullname, baselen + len);

			/*
			 * Excluded? If we don't explicitly want to show
			 * ignored files, ignore it
			 */
			if (exclude && !dir->show_ignored)
				continue;

			if (dtype == DT_UNKNOWN)
				dtype = get_dtype(de, fullname);

			/*
			 * Do we want to see just the ignored files?
			 * We still need to recurse into directories,
			 * even if we don't ignore them, since the
			 * directory may contain files that we do..
			 */
			if (!exclude && dir->show_ignored) {
				if (dtype != DT_DIR)
					continue;
			}

			switch (dtype) {
			default:
				continue;
			case DT_DIR:
				memcpy(fullname + baselen + len, "/", 2);
				len++;
				switch (treat_directory(dir, fullname, baselen + len, simplify)) {
				case show_directory:
					if (exclude != dir->show_ignored)
						continue;
					break;
				case recurse_into_directory:
					contents += read_directory_recursive(dir,
						fullname, fullname, baselen + len, 0, simplify);
					continue;
				case ignore_directory:
					continue;
				}
				break;
			case DT_REG:
			case DT_LNK:
				break;
			}
			contents++;
			if (check_only)
				goto exit_early;
			else
				dir_add_name(dir, fullname, baselen + len);
		}
exit_early:
		closedir(fdir);
	}

	return contents;
}

static int cmp_name(const void *p1, const void *p2)
{
	const struct dir_entry *e1 = *(const struct dir_entry **)p1;
	const struct dir_entry *e2 = *(const struct dir_entry **)p2;

	return cache_name_compare(e1->name, e1->len,
				  e2->name, e2->len);
}

/*
 * Return the length of the "simple" part of a path match limiter.
 */
static int simple_length(const char *match)
{
	const char special[256] = {
		[0] = 1, ['?'] = 1,
		['\\'] = 1, ['*'] = 1,
		['['] = 1
	};
	int len = -1;

	for (;;) {
		unsigned char c = *match++;
		len++;
		if (special[c])
			return len;
	}
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

int read_directory(struct dir_struct *dir, const char *path, const char *base, int baselen, const char **pathspec)
{
	struct path_simplify *simplify = create_simplify(pathspec);

	read_directory_recursive(dir, path, base, baselen, 0, simplify);
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
 * get_relative_cwd() gets the prefix of the current working directory
 * relative to 'dir'.  If we are not inside 'dir', it returns NULL.
 *
 * As a convenience, it also returns NULL if 'dir' is already NULL.  The
 * reason for this behaviour is that it is natural for functions returning
 * directory names to return NULL to say "this directory does not exist"
 * or "this directory is invalid".  These cases are usually handled the
 * same as if the cwd is not inside 'dir' at all, so get_relative_cwd()
 * returns NULL for both of them.
 *
 * Most notably, get_relative_cwd(buffer, size, get_git_work_tree())
 * unifies the handling of "outside work tree" with "no work tree at all".
 */
char *get_relative_cwd(char *buffer, int size, const char *dir)
{
	char *cwd = buffer;

	if (!dir)
		return NULL;
	if (!getcwd(buffer, size))
		die("can't find the current directory: %s", strerror(errno));

	if (!is_absolute_path(dir))
		dir = make_absolute_path(dir);

	while (*dir && *dir == *cwd) {
		dir++;
		cwd++;
	}
	if (*dir)
		return NULL;
	if (*cwd == '/')
		return cwd + 1;
	return cwd;
}

int is_inside_dir(const char *dir)
{
	char buffer[PATH_MAX];
	return get_relative_cwd(buffer, sizeof(buffer), dir) != NULL;
}

int remove_dir_recursively(struct strbuf *path, int only_empty)
{
	DIR *dir = opendir(path->buf);
	struct dirent *e;
	int ret = 0, original_len = path->len, len;

	if (!dir)
		return -1;
	if (path->buf[original_len - 1] != '/')
		strbuf_addch(path, '/');

	len = path->len;
	while ((e = readdir(dir)) != NULL) {
		struct stat st;
		if ((e->d_name[0] == '.') &&
		    ((e->d_name[1] == 0) ||
		     ((e->d_name[1] == '.') && e->d_name[2] == 0)))
			continue; /* "." and ".." */

		strbuf_setlen(path, len);
		strbuf_addstr(path, e->d_name);
		if (lstat(path->buf, &st))
			; /* fall thru */
		else if (S_ISDIR(st.st_mode)) {
			if (!remove_dir_recursively(path, only_empty))
				continue; /* happy */
		} else if (!only_empty && !unlink(path->buf))
			continue; /* happy, too */

		/* path too long, stat fails, or non-directory still exists */
		ret = -1;
		break;
	}
	closedir(dir);

	strbuf_setlen(path, original_len);
	if (!ret)
		ret = rmdir(path->buf);
	return ret;
}

void setup_standard_excludes(struct dir_struct *dir)
{
	const char *path;

	dir->exclude_per_dir = ".gitignore";
	path = git_path("info/exclude");
	if (!access(path, R_OK))
		add_excludes_from_file(dir, path);
	if (excludes_file && !access(excludes_file, R_OK))
		add_excludes_from_file(dir, excludes_file);
}
