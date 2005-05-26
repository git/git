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

static int nr_excludes;
static const char **excludes;
static int excludes_alloc;

static void add_exclude(const char *string)
{
	if (nr_excludes == excludes_alloc) {
		excludes_alloc = alloc_nr(excludes_alloc);
		excludes = realloc(excludes, excludes_alloc*sizeof(char *));
	}
	excludes[nr_excludes++] = string;
}

static void add_excludes_from_file(const char *fname)
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
		return;
	}
	buf = xmalloc(size);
	if (read(fd, buf, size) != size)
		goto err;
	close(fd);

	entry = buf;
	for (i = 0; i < size; i++) {
		if (buf[i] == '\n') {
			if (entry != buf + i) {
				buf[i] = 0;
				add_exclude(entry);
			}
			entry = buf + i + 1;
		}
	}
	return;

err:	perror(fname);
	exit(1);
}

static int excluded(const char *pathname)
{
	int i;
	if (nr_excludes) {
		const char *basename = strrchr(pathname, '/');
		basename = (basename) ? basename+1 : pathname;
		for (i = 0; i < nr_excludes; i++)
			if (fnmatch(excludes[i], basename, 0) == 0)
				return 1;
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
 * Also, we currently ignore all names starting with a dot.
 * That likely will not change.
 */
static void read_directory(const char *path, const char *base, int baselen)
{
	DIR *dir = opendir(path);

	if (dir) {
		struct dirent *de;
		char fullname[MAXPATHLEN + 1];
		memcpy(fullname, base, baselen);

		while ((de = readdir(dir)) != NULL) {
			int len;

			if ((de->d_name[0] == '.') &&
			    (de->d_name[1] == 0 ||
			     !strcmp(de->d_name + 1, ".") ||
			     !strcmp(de->d_name + 1, "git")))
				continue;
			if (excluded(de->d_name) != show_ignored)
				continue;
			len = strlen(de->d_name);
			memcpy(fullname + baselen, de->d_name, len+1);

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

static const char *ls_files_usage =
	"git-ls-files [-z] [-t] (--[cached|deleted|others|stage|unmerged|killed])* "
	"[ --ignored [--exclude=<pattern>] [--exclude-from=<file>) ]";

int main(int argc, char **argv)
{
	int i;

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
			add_exclude(argv[++i]);
		} else if (!strncmp(arg, "--exclude=", 10)) {
			add_exclude(arg+10);
		} else if (!strcmp(arg, "-X") && i+1 < argc) {
			add_excludes_from_file(argv[++i]);
		} else if (!strncmp(arg, "--exclude-from=", 15)) {
			add_excludes_from_file(arg+15);
		} else
			usage(ls_files_usage);
	}

	if (show_ignored && !nr_excludes) {
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
