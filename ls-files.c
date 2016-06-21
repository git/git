/*
 * This merges the file listing in the directory cache index
 * with the actual working directory list, and shows different
 * combinations of the two.
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include <dirent.h>
#include <fnmatch.h>

#include "cache.h"

static int show_deleted = 0;
static int show_cached = 0;
static int show_others = 0;
static int show_ignored = 0;
static int show_stage = 0;
static int show_unmerged = 0;
static int show_killed = 0;
static int line_terminator = '\n';

static const char *tag_cached = "";
static const char *tag_unmerged = "";
static const char *tag_removed = "";
static const char *tag_other = "";
static const char *tag_killed = "";

static char *exclude_per_dir = NULL;

/* We maintain three exclude pattern lists:
 * EXC_CMDL lists patterns explicitly given on the command line.
 * EXC_DIRS lists patterns obtained from per-directory ignore files.
 * EXC_FILE lists patterns from fallback ignore files.
 */
#define EXC_CMDL 0
#define EXC_DIRS 1
#define EXC_FILE 2
static struct exclude_list {
	int nr;
	int alloc;
	struct exclude {
		const char *pattern;
		const char *base;
		int baselen;
	} **excludes;
} exclude_list[3];

static void add_exclude(const char *string, const char *base,
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
	buf = xmalloc(size);
	if (read(fd, buf, size) != size)
		goto err;
	close(fd);

	entry = buf;
	for (i = 0; i < size; i++) {
		if (buf[i] == '\n') {
			if (entry != buf + i && entry[0] != '#') {
				buf[i] = 0;
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

static void add_excludes_from_file(const char *fname)
{
	if (add_excludes_from_file_1(fname, "", 0,
				     &exclude_list[EXC_FILE]) < 0)
		die("cannot use %s as an exclude file", fname);
}

static int push_exclude_per_directory(const char *base, int baselen)
{
	char exclude_file[PATH_MAX];
	struct exclude_list *el = &exclude_list[EXC_DIRS];
	int current_nr = el->nr;

	if (exclude_per_dir) {
		memcpy(exclude_file, base, baselen);
		strcpy(exclude_file + baselen, exclude_per_dir);
		add_excludes_from_file_1(exclude_file, base, baselen, el);
	}
	return current_nr;
}

static void pop_exclude_per_directory(int stk)
{
	struct exclude_list *el = &exclude_list[EXC_DIRS];

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
				 * exclude has base (baselen long) inplicitly
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

static int excluded(const char *pathname)
{
	int pathlen = strlen(pathname);
	int st;

	for (st = EXC_CMDL; st <= EXC_FILE; st++) {
		switch (excluded_1(pathname, pathlen, &exclude_list[st])) {
		case 0:
			return 0;
		case 1:
			return 1;
		}
	}
	return 0;
}

struct nond_on_fs {
	int len;
	char name[0];
};

static struct nond_on_fs **dir;
static int nr_dir;
static int dir_alloc;

static void add_name(const char *pathname, int len)
{
	struct nond_on_fs *ent;

	if (cache_name_pos(pathname, len) >= 0)
		return;

	if (nr_dir == dir_alloc) {
		dir_alloc = alloc_nr(dir_alloc);
		dir = xrealloc(dir, dir_alloc*sizeof(ent));
	}
	ent = xmalloc(sizeof(*ent) + len + 1);
	ent->len = len;
	memcpy(ent->name, pathname, len);
	dir[nr_dir++] = ent;
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
static void read_directory(const char *path, const char *base, int baselen)
{
	DIR *dir = opendir(path);

	if (dir) {
		int exclude_stk;
		struct dirent *de;
		char fullname[MAXPATHLEN + 1];
		memcpy(fullname, base, baselen);

		exclude_stk = push_exclude_per_directory(base, baselen);

		while ((de = readdir(dir)) != NULL) {
			int len;

			if ((de->d_name[0] == '.') &&
			    (de->d_name[1] == 0 ||
			     !strcmp(de->d_name + 1, ".") ||
			     !strcmp(de->d_name + 1, "git")))
				continue;
			len = strlen(de->d_name);
			memcpy(fullname + baselen, de->d_name, len+1);
			if (excluded(fullname) != show_ignored)
				continue;

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
				read_directory(fullname, fullname,
					       baselen + len + 1);
				continue;
			case DT_REG:
			case DT_LNK:
				break;
			}
			add_name(fullname, baselen + len);
		}
		closedir(dir);

		pop_exclude_per_directory(exclude_stk);
	}
}

static int cmp_name(const void *p1, const void *p2)
{
	const struct nond_on_fs *e1 = *(const struct nond_on_fs **)p1;
	const struct nond_on_fs *e2 = *(const struct nond_on_fs **)p2;

	return cache_name_compare(e1->name, e1->len,
				  e2->name, e2->len);
}

static void show_killed_files(void)
{
	int i;
	for (i = 0; i < nr_dir; i++) {
		struct nond_on_fs *ent = dir[i];
		char *cp, *sp;
		int pos, len, killed = 0;

		for (cp = ent->name; cp - ent->name < ent->len; cp = sp + 1) {
			sp = strchr(cp, '/');
			if (!sp) {
				/* If ent->name is prefix of an entry in the
				 * cache, it will be killed.
				 */
				pos = cache_name_pos(ent->name, ent->len);
				if (0 <= pos)
					die("bug in show-killed-files");
				pos = -pos - 1;
				while (pos < active_nr &&
				       ce_stage(active_cache[pos]))
					pos++; /* skip unmerged */
				if (active_nr <= pos)
					break;
				/* pos points at a name immediately after
				 * ent->name in the cache.  Does it expect
				 * ent->name to be a directory?
				 */
				len = ce_namelen(active_cache[pos]);
				if ((ent->len < len) &&
				    !strncmp(active_cache[pos]->name,
					     ent->name, ent->len) &&
				    active_cache[pos]->name[ent->len] == '/')
					killed = 1;
				break;
			}
			if (0 <= cache_name_pos(ent->name, sp - ent->name)) {
				/* If any of the leading directories in
				 * ent->name is registered in the cache,
				 * ent->name will be killed.
				 */
				killed = 1;
				break;
			}
		}
		if (killed)
			printf("%s%.*s%c", tag_killed,
			       dir[i]->len, dir[i]->name,
			       line_terminator);
	}
}

static void show_files(void)
{
	int i;

	/* For cached/deleted files we don't need to even do the readdir */
	if (show_others || show_killed) {
		read_directory(".", "", 0);
		qsort(dir, nr_dir, sizeof(struct nond_on_fs *), cmp_name);
		if (show_others)
			for (i = 0; i < nr_dir; i++)
				printf("%s%.*s%c", tag_other,
				       dir[i]->len, dir[i]->name,
				       line_terminator);
		if (show_killed)
			show_killed_files();
	}
	if (show_cached | show_stage) {
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			if (excluded(ce->name) != show_ignored)
				continue;
			if (show_unmerged && !ce_stage(ce))
				continue;
			if (!show_stage)
				printf("%s%s%c",
				       ce_stage(ce) ? tag_unmerged :
				       tag_cached,
				       ce->name, line_terminator);
			else
				printf("%s%06o %s %d\t%s%c",
				       ce_stage(ce) ? tag_unmerged :
				       tag_cached,
				       ntohl(ce->ce_mode),
				       sha1_to_hex(ce->sha1),
				       ce_stage(ce),
				       ce->name, line_terminator); 
		}
	}
	if (show_deleted) {
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			struct stat st;
			if (excluded(ce->name) != show_ignored)
				continue;
			if (!lstat(ce->name, &st))
				continue;
			printf("%s%s%c", tag_removed, ce->name,
			       line_terminator);
		}
	}
}

static const char ls_files_usage[] =
	"git-ls-files [-z] [-t] (--[cached|deleted|others|stage|unmerged|killed])* "
	"[ --ignored ] [--exclude=<pattern>] [--exclude-from=<file>] "
	"[ --exclude-per-directory=<filename> ]";

int main(int argc, char **argv)
{
	int i;
	int exc_given = 0;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (!strcmp(arg, "-z")) {
			line_terminator = 0;
		} else if (!strcmp(arg, "-t")) {
			tag_cached = "H ";
			tag_unmerged = "M ";
			tag_removed = "R ";
			tag_other = "? ";
			tag_killed = "K ";
		} else if (!strcmp(arg, "-c") || !strcmp(arg, "--cached")) {
			show_cached = 1;
		} else if (!strcmp(arg, "-d") || !strcmp(arg, "--deleted")) {
			show_deleted = 1;
		} else if (!strcmp(arg, "-o") || !strcmp(arg, "--others")) {
			show_others = 1;
		} else if (!strcmp(arg, "-i") || !strcmp(arg, "--ignored")) {
			show_ignored = 1;
		} else if (!strcmp(arg, "-s") || !strcmp(arg, "--stage")) {
			show_stage = 1;
		} else if (!strcmp(arg, "-k") || !strcmp(arg, "--killed")) {
			show_killed = 1;
		} else if (!strcmp(arg, "-u") || !strcmp(arg, "--unmerged")) {
			/* There's no point in showing unmerged unless
			 * you also show the stage information.
			 */
			show_stage = 1;
			show_unmerged = 1;
		} else if (!strcmp(arg, "-x") && i+1 < argc) {
			exc_given = 1;
			add_exclude(argv[++i], "", 0, &exclude_list[EXC_CMDL]);
		} else if (!strncmp(arg, "--exclude=", 10)) {
			exc_given = 1;
			add_exclude(arg+10, "", 0, &exclude_list[EXC_CMDL]);
		} else if (!strcmp(arg, "-X") && i+1 < argc) {
			exc_given = 1;
			add_excludes_from_file(argv[++i]);
		} else if (!strncmp(arg, "--exclude-from=", 15)) {
			exc_given = 1;
			add_excludes_from_file(arg+15);
		} else if (!strncmp(arg, "--exclude-per-directory=", 24)) {
			exc_given = 1;
			exclude_per_dir = arg + 24;
		} else
			usage(ls_files_usage);
	}

	if (show_ignored && !exc_given) {
		fprintf(stderr, "%s: --ignored needs some exclude pattern\n",
			argv[0]);
		exit(1);
	}

	/* With no flags, we default to showing the cached files */
	if (!(show_stage | show_deleted | show_others | show_unmerged | show_killed))
		show_cached = 1;

	read_cache();
	show_files();
	return 0;
}
