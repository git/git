/*
 * This merges the file listing in the directory cache index
 * with the actual working directory list, and shows different
 * combinations of the two.
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include <dirent.h>

#include "cache.h"

static int show_deleted = 0;
static int show_cached = 0;
static int show_others = 0;
static int show_ignored = 0;
static int show_stage = 0;
static int show_unmerged = 0;
static int line_terminator = '\n';

static const char **dir;
static int nr_dir;
static int dir_alloc;

static void add_name(const char *pathname, int len)
{
	char *name;

	if (cache_name_pos(pathname, len) >= 0)
		return;

	if (nr_dir == dir_alloc) {
		dir_alloc = alloc_nr(dir_alloc);
		dir = xrealloc(dir, dir_alloc*sizeof(char *));
	}
	name = xmalloc(len + 1);
	memcpy(name, pathname, len + 1);
	dir[nr_dir++] = name;
}

/*
 * Read a directory tree. We currently ignore anything but
 * directories and regular files. That's because git doesn't
 * handle them at all yet. Maybe that will change some day.
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

			if (de->d_name[0] == '.')
				continue;
			len = strlen(de->d_name);
			memcpy(fullname + baselen, de->d_name, len+1);

			switch (de->d_type) {
			struct stat st;
			default:
				continue;
			case DT_UNKNOWN:
				if (lstat(fullname, &st))
					continue;
				if (S_ISREG(st.st_mode))
					break;
				if (!S_ISDIR(st.st_mode))
					continue;
				/* fallthrough */
			case DT_DIR:
				memcpy(fullname + baselen + len, "/", 2);
				read_directory(fullname, fullname, baselen + len + 1);
				continue;
			case DT_REG:
				break;
			}
			add_name(fullname, baselen + len);
		}
		closedir(dir);
	}
}

static int cmp_name(const void *p1, const void *p2)
{
	const char *n1 = *(const char **)p1;
	const char *n2 = *(const char **)p2;
	int l1 = strlen(n1), l2 = strlen(n2);

	return cache_name_compare(n1, l1, n2, l2);
}

static void show_files(void)
{
	int i;

	/* For cached/deleted files we don't need to even do the readdir */
	if (show_others | show_ignored) {
		read_directory(".", "", 0);
		qsort(dir, nr_dir, sizeof(char *), cmp_name);
	}
	if (show_others) {
		for (i = 0; i < nr_dir; i++)
			printf("%s%c", dir[i], line_terminator);
	}
	if (show_cached | show_stage) {
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			if (show_unmerged && !ce_stage(ce))
				continue;
			if (!show_stage)
				printf("%s%c", ce->name, line_terminator);
			else
				printf(/* "%06o %s %d %10d %s%c", */
				       "%06o %s %d %s%c",
				       ntohl(ce->ce_mode),
				       sha1_to_hex(ce->sha1),
				       ce_stage(ce),
				       /* ntohl(ce->ce_size), */
				       ce->name, line_terminator); 
		}
	}
	if (show_deleted) {
		for (i = 0; i < active_nr; i++) {
			struct cache_entry *ce = active_cache[i];
			struct stat st;
			if (!stat(ce->name, &st))
				continue;
			printf("%s%c", ce->name, line_terminator);
		}
	}
	if (show_ignored) {
		/* We don't have any "ignore" list yet */
	}
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (!strcmp(arg, "-z")) {
			line_terminator = 0;
			continue;
		}

		if (!strcmp(arg, "--cached")) {
			show_cached = 1;
			continue;
		}
		if (!strcmp(arg, "--deleted")) {
			show_deleted = 1;
			continue;
		}
		if (!strcmp(arg, "--others")) {
			show_others = 1;
			continue;
		}
		if (!strcmp(arg, "--ignored")) {
			show_ignored = 1;
			continue;
		}
		if (!strcmp(arg, "--stage")) {
			show_stage = 1;
			continue;
		}
		if (!strcmp(arg, "--unmerged")) {
			// There's no point in showing unmerged unless you also show the stage information
			show_stage = 1;
			show_unmerged = 1;
			continue;
		}

		usage("show-files [-z] (--[cached|deleted|others|ignored|stage])*");
	}

	/* With no flags, we default to showing the cached files */
	if (!(show_stage | show_deleted | show_others | show_ignored | show_unmerged))
		show_cached = 1;

	read_cache();
	show_files();
	return 0;
}
