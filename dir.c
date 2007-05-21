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
	matchlen = strlen(match);
	if (!matchlen)
		return MATCHED_RECURSIVELY;

	/*
	 * If we don't match the matchstring exactly,
	 * we need to match by fnmatch
	 */
	if (strncmp(match, name, matchlen))
		return !fnmatch(match, name, 0) ? MATCHED_FNMATCH : 0;

	if (!name[matchlen])
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

void add_exclude(const char *string, const char *base,
		 int baselen, struct exclude_list *which)
{
	struct exclude *x = xmalloc(sizeof (*x));

	x->pattern = string;
	x->base = base;
	x->baselen = baselen;
	if (which->nr == which->alloc) {
		which->alloc = alloc_nr(which->alloc);
		which->excludes = xrealloc(which->excludes,
					   which->alloc * sizeof(x));
	}
	which->excludes[which->nr++] = x;
}

static int add_excludes_from_file_1(const char *fname,
				    const char *base,
				    int baselen,
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
		goto err;
	close(fd);

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
	if (add_excludes_from_file_1(fname, "", 0,
				     &dir->exclude_list[EXC_FILE]) < 0)
		die("cannot use %s as an exclude file", fname);
}

int push_exclude_per_directory(struct dir_struct *dir, const char *base, int baselen)
{
	char exclude_file[PATH_MAX];
	struct exclude_list *el = &dir->exclude_list[EXC_DIRS];
	int current_nr = el->nr;

	if (dir->exclude_per_dir) {
		memcpy(exclude_file, base, baselen);
		strcpy(exclude_file + baselen, dir->exclude_per_dir);
		add_excludes_from_file_1(exclude_file, base, baselen, el);
	}
	return current_nr;
}

void pop_exclude_per_directory(struct dir_struct *dir, int stk)
{
	struct exclude_list *el = &dir->exclude_list[EXC_DIRS];

	while (stk < el->nr)
		free(el->excludes[--el->nr]);
}

/* Scan the list and let the last match determines the fate.
 * Return 1 for exclude, 0 for include and -1 for undecided.
 */
static int excluded_1(const char *pathname,
		      int pathlen,
		      struct exclude_list *el)
{
	int i;

	if (el->nr) {
		for (i = el->nr - 1; 0 <= i; i--) {
			struct exclude *x = el->excludes[i];
			const char *exclude = x->pattern;
			int to_exclude = 1;

			if (*exclude == '!') {
				to_exclude = 0;
				exclude++;
			}

			if (!strchr(exclude, '/')) {
				/* match basename */
				const char *basename = strrchr(pathname, '/');
				basename = (basename) ? basename+1 : pathname;
				if (fnmatch(exclude, basename, 0) == 0)
					return to_exclude;
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

				if (fnmatch(exclude, pathname+baselen,
					    FNM_PATHNAME) == 0)
					return to_exclude;
			}
		}
	}
	return -1; /* undecided */
}

int excluded(struct dir_struct *dir, const char *pathname)
{
	int pathlen = strlen(pathname);
	int st;

	for (st = EXC_CMDL; st <= EXC_FILE; st++) {
		switch (excluded_1(pathname, pathlen, &dir->exclude_list[st])) {
		case 0:
			return 0;
		case 1:
			return 1;
		}
	}
	return 0;
}

struct dir_entry *dir_add_name(struct dir_struct *dir, const char *pathname, int len)
{
	struct dir_entry *ent;

	if (cache_name_pos(pathname, len) >= 0)
		return NULL;

	if (dir->nr == dir->alloc) {
		int alloc = alloc_nr(dir->alloc);
		dir->alloc = alloc;
		dir->entries = xrealloc(dir->entries, alloc*sizeof(ent));
	}
	ent = xmalloc(sizeof(*ent) + len + 1);
	ent->ignored = ent->ignored_dir = 0;
	ent->len = len;
	memcpy(ent->name, pathname, len);
	ent->name[len] = 0;
	dir->entries[dir->nr++] = ent;
	return ent;
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
		if (!endchar && S_ISGITLINK(ntohl(ce->ce_mode)))
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
		int exclude_stk;
		struct dirent *de;
		char fullname[PATH_MAX + 1];
		memcpy(fullname, base, baselen);

		exclude_stk = push_exclude_per_directory(dir, base, baselen);

		while ((de = readdir(fdir)) != NULL) {
			int len;
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

			exclude = excluded(dir, fullname);
			if (exclude != dir->show_ignored) {
				if (!dir->show_ignored || DTYPE(de) != DT_DIR) {
					continue;
				}
			}

			switch (DTYPE(de)) {
			struct stat st;
			default:
				continue;
			case DT_UNKNOWN:
				if (lstat(fullname, &st))
					continue;
				if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
					break;
				if (!S_ISDIR(st.st_mode))
					continue;
				/* fallthrough */
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

		pop_exclude_per_directory(dir, exclude_stk);
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
	if (simplify)
		free(simplify);
}

int read_directory(struct dir_struct *dir, const char *path, const char *base, int baselen, const char **pathspec)
{
	struct path_simplify *simplify = create_simplify(pathspec);

	/*
	 * Make sure to do the per-directory exclude for all the
	 * directories leading up to our base.
	 */
	if (baselen) {
		if (dir->exclude_per_dir) {
			char *p, *pp = xmalloc(baselen+1);
			memcpy(pp, base, baselen+1);
			p = pp;
			while (1) {
				char save = *p;
				*p = 0;
				push_exclude_per_directory(dir, pp, p-pp);
				*p++ = save;
				if (!save)
					break;
				p = strchr(p, '/');
				if (p)
					p++;
				else
					p = pp + baselen;
			}
			free(pp);
		}
	}

	read_directory_recursive(dir, path, base, baselen, 0, simplify);
	free_simplify(simplify);
	qsort(dir->entries, dir->nr, sizeof(struct dir_entry *), cmp_name);
	return dir->nr;
}

int
file_exists(const char *f)
{
  struct stat sb;
  return stat(f, &sb) == 0;
}
