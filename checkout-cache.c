/*
 * Check-out files from the "current cache directory"
 *
 * Copyright (C) 2005 Linus Torvalds
 *
 * Careful: order of argument flags does matter. For example,
 *
 *	checkout-cache -a -f file.c
 *
 * Will first check out all files listed in the cache (but not
 * overwrite any old ones), and then force-checkout "file.c" a
 * second time (ie that one _will_ overwrite any old contents
 * with the same filename).
 *
 * Also, just doing "checkout-cache" does nothing. You probably
 * meant "checkout-cache -a". And if you want to force it, you
 * want "checkout-cache -f -a".
 *
 * Intuitiveness is not the goal here. Repeatability is. The
 * reason for the "no arguments means no work" thing is that
 * from scripts you are supposed to be able to do things like
 *
 *	find . -name '*.h' -print0 | xargs -0 checkout-cache -f --
 *
 * which will force all existing *.h files to be replaced with
 * their cached copies. If an empty command line implied "all",
 * then this would force-refresh everything in the cache, which
 * was not the point.
 *
 * Oh, and the "--" is just a good idea when you know the rest
 * will be filenames. Just so that you wouldn't have a filename
 * of "-a" causing problems (not possible in the above example,
 * but get used to it in scripting!).
 */
#include "cache.h"

static int force = 0, quiet = 0;

static void create_directories(const char *path)
{
	int len = strlen(path);
	char *buf = malloc(len + 1);
	const char *slash = path;

	while ((slash = strchr(slash+1, '/')) != NULL) {
		len = slash - path;
		memcpy(buf, path, len);
		buf[len] = 0;
		mkdir(buf, 0755);
	}
}

static int create_file(const char *path, unsigned int mode)
{
	int fd;

	mode = (mode & 0100) ? 0777 : 0666;
	fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, mode);
	if (fd < 0) {
		if (errno == ENOENT) {
			create_directories(path);
			fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, mode);
		}
	}
	return fd;
}

static int write_entry(struct cache_entry *ce)
{
	int fd;
	void *new;
	unsigned long size;
	long wrote;
	char type[20];

	new = read_sha1_file(ce->sha1, type, &size);
	if (!new || strcmp(type, "blob")) {
		return error("checkout-cache: unable to read sha1 file of %s (%s)",
			ce->name, sha1_to_hex(ce->sha1));
	}
	fd = create_file(ce->name, ntohl(ce->ce_mode));
	if (fd < 0) {
		free(new);
		return error("checkout-cache: unable to create %s (%s)",
			ce->name, strerror(errno));
	}
	wrote = write(fd, new, size);
	close(fd);
	free(new);
	if (wrote != size)
		return error("checkout-cache: unable to write %s", ce->name);
	return 0;
}

static int checkout_entry(struct cache_entry *ce)
{
	struct stat st;

	if (!stat(ce->name, &st)) {
		unsigned changed = cache_match_stat(ce, &st);
		if (!changed)
			return 0;
		if (!force) {
			if (!quiet)
				fprintf(stderr, "checkout-cache: %s already exists\n", ce->name);
			return 0;
		}

		/*
		 * We unlink the old file, to get the new one with the
		 * right permissions (including umask, which is nasty
		 * to emulate by hand - much easier to let the system
		 * just do the right thing)
		 */
		unlink(ce->name);
	}
	return write_entry(ce);
}

static int checkout_file(const char *name)
{
	int pos = cache_name_pos(name, strlen(name));
	if (pos < 0) {
		if (!quiet) {
			pos = -pos - 1;
			fprintf(stderr,
				"checkout-cache: %s is %s.\n",
				name,
				(pos < active_nr &&
				 !strcmp(active_cache[pos]->name, name)) ?
				"unmerged" : "not in the cache");
		}
		return -1;
	}
	return checkout_entry(active_cache[pos]);
}

static int checkout_all(void)
{
	int i;

	for (i = 0; i < active_nr ; i++) {
		struct cache_entry *ce = active_cache[i];
		if (checkout_entry(ce) < 0)
			return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int i, force_filename = 0;

	if (read_cache() < 0) {
		die("invalid cache");
	}

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!force_filename) {
			if (!strcmp(arg, "-a")) {
				checkout_all();
				continue;
			}
			if (!strcmp(arg, "--")) {
				force_filename = 1;
				continue;
			}
			if (!strcmp(arg, "-f")) {
				force = 1;
				continue;
			}
			if (!strcmp(arg, "-q")) {
				quiet = 1;
				continue;
			}
		}
		checkout_file(arg);
	}
	return 0;
}
