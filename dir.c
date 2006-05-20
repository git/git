/*
 * This handles recursive filename detection with exclude
 * files, index knowledge etc..
 *
 * Copyright (C) Linus Torvalds, 2005-2006
 *		 Junio Hamano, 2005-2006
 */
#include <dirent.h>
#include <fnmatch.h>

#include "cache.h"
#include "dir.h"

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
		if (len >= prefix && !memcmp(path, next, len))
			continue;
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

static int match_one(const char *match, const char *name, int namelen)
{
	int matchlen;

	/* If the match was just the prefix, we matched */
	matchlen = strlen(match);
	if (!matchlen)
		return 1;

	/*
	 * If we don't match the matchstring exactly,
	 * we need to match by fnmatch
	 */
	if (strncmp(match, name, matchlen))
		return !fnmatch(match, name, 0);

	/*
	 * If we did match the string exactly, we still
	 * need to make sure that it happened on a path
	 * component boundary (ie either the last character
	 * of the match was '/', or the next character of
	 * the name was '/' or the terminating NUL.
	 */
	return	match[matchlen-1] == '/' ||
		name[matchlen] == '/' ||
		!name[matchlen];
}

int match_pathspec(const char **pathspec, const char *name, int namelen, int prefix, char *seen)
{
	int retval;
	const char *match;

	name += prefix;
	namelen -= prefix;

	for (retval = 0; (match = *pathspec++) != NULL; seen++) {
		if (retval & *seen)
			continue;
		match += prefix;
		if (match_one(match, name, namelen)) {
			retval = 1;
			*seen = 1;
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
		which->excludes = realloc(which->excludes,
					  which->alloc * sizeof(x));
	}
	which->excludes[which->nr++] = x;
}

static int add_excludes_from_file_1(const char *fname,
				    const char *base,
				    int baselen,
				    struct exclude_list *which)
{
	int fd, i;
	long size;
	char *buf, *entry;

	fd = open(fname, O_RDONLY);
	if (fd < 0)
		goto err;
	size = lseek(fd, 0, SEEK_END);
	if (size < 0)
		goto err;
	lseek(fd, 0, SEEK_SET);
	if (size == 0) {
		close(fd);
		return 0;
	}
	buf = xmalloc(size+1);
	if (read(fd, buf, size) != size)
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

static int push_exclude_per_directory(struct dir_struct *dir, const char *base, int baselen)
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

static void pop_exclude_per_directory(struct dir_struct *dir, int stk)
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

static void add_name(struct dir_struct *dir, const char *pathname, int len)
{
	struct dir_entry *ent;

	if (cache_name_pos(pathname, len) >= 0)
		return;

	if (dir->nr == dir->alloc) {
		int alloc = alloc_nr(dir->alloc);
		dir->alloc = alloc;
		dir->entries = xrealloc(dir->entries, alloc*sizeof(ent));
	}
	ent = xmalloc(sizeof(*ent) + len + 1);
	ent->len = len;
	memcpy(ent->name, pathname, len);
	ent->name[len] = 0;
	dir->entries[dir->nr++] = ent;
}

static int dir_exists(const char *dirname, int len)
{
	int pos = cache_name_pos(dirname, len);
	if (pos >= 0)
		return 1;
	pos = -pos-1;
	if (pos >= active_nr) /* can't */
		return 0;
	return !strncmp(active_cache[pos]->name, dirname, len);
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
static int read_directory_recursive(struct dir_struct *dir, const char *path, const char *base, int baselen)
{
	DIR *fdir = opendir(path);
	int contents = 0;

	if (fdir) {
		int exclude_stk;
		struct dirent *de;
		char fullname[MAXPATHLEN + 1];
		memcpy(fullname, base, baselen);

		exclude_stk = push_exclude_per_directory(dir, base, baselen);

		while ((de = readdir(fdir)) != NULL) {
			int len;

			if ((de->d_name[0] == '.') &&
			    (de->d_name[1] == 0 ||
			     !strcmp(de->d_name + 1, ".") ||
			     !strcmp(de->d_name + 1, "git")))
				continue;
			len = strlen(de->d_name);
			memcpy(fullname + baselen, de->d_name, len+1);
			if (excluded(dir, fullname) != dir->show_ignored) {
				if (!dir->show_ignored || DTYPE(de) != DT_DIR) {
					continue;
				}
			}

			switch (DTYPE(de)) {
			struct stat st;
			int subdir, rewind_base;
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
				rewind_base = dir->nr;
				subdir = read_directory_recursive(dir, fullname, fullname,
				                        baselen + len);
				if (dir->show_other_directories &&
				    (subdir || !dir->hide_empty_directories) &&
				    !dir_exists(fullname, baselen + len)) {
					// Rewind the read subdirectory
					while (dir->nr > rewind_base)
						free(dir->entries[--dir->nr]);
					break;
				}
				contents += subdir;
				continue;
			case DT_REG:
			case DT_LNK:
				break;
			}
			add_name(dir, fullname, baselen + len);
			contents++;
		}
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

int read_directory(struct dir_struct *dir, const char *path, const char *base, int baselen)
{
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

	read_directory_recursive(dir, path, base, baselen);
	qsort(dir->entries, dir->nr, sizeof(struct dir_entry *), cmp_name);
	return dir->nr;
}
