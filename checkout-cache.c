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

static int force = 0, quiet = 0, not_new = 0;

static void create_directories(const char *path)
{
	int len = strlen(path);
	char *buf = xmalloc(len + 1);
	const char *slash = path;

	while ((slash = strchr(slash+1, '/')) != NULL) {
		len = slash - path;
		memcpy(buf, path, len);
		buf[len] = 0;
		mkdir(buf, 0755);
	}
	free(buf);
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

static int write_entry(struct cache_entry *ce, const char *path)
{
	int fd;
	void *new;
	unsigned long size;
	long wrote;
	char type[20];
	char target[1024];

	new = read_sha1_file(ce->sha1, type, &size);
	if (!new || strcmp(type, "blob")) {
		return error("checkout-cache: unable to read sha1 file of %s (%s)",
			path, sha1_to_hex(ce->sha1));
	}
	switch (ntohl(ce->ce_mode) & S_IFMT) {
	case S_IFREG:
		fd = create_file(path, ntohl(ce->ce_mode));
		if (fd < 0) {
			free(new);
			return error("checkout-cache: unable to create file %s (%s)",
				path, strerror(errno));
		}
		wrote = write(fd, new, size);
		close(fd);
		free(new);
		if (wrote != size)
			return error("checkout-cache: unable to write file %s", path);
		break;
	case S_IFLNK:
		memcpy(target, new, size);
		target[size] = '\0';
		create_directories(path);
		if (symlink(target, path)) {
			free(new);
			return error("checkout-cache: unable to create symlink %s (%s)",
				path, strerror(errno));
		}
		free(new);
		break;
	default:
		free(new);
		return error("checkout-cache: unknown file mode for %s", path);
	}
	return 0;
}

static int checkout_entry(struct cache_entry *ce, const char *base_dir)
{
	struct stat st;
	static char path[MAXPATHLEN+1];
	int len = strlen(base_dir);

	memcpy(path, base_dir, len);
	strcpy(path + len, ce->name);

	if (!lstat(path, &st)) {
		unsigned changed = cache_match_stat(ce, &st);
		if (!changed)
			return 0;
		if (!force) {
			if (!quiet)
				fprintf(stderr, "checkout-cache: %s already exists\n", path);
			return 0;
		}

		/*
		 * We unlink the old file, to get the new one with the
		 * right permissions (including umask, which is nasty
		 * to emulate by hand - much easier to let the system
		 * just do the right thing)
		 */
		unlink(path);
	} else if (not_new) 
		return 0;
	return write_entry(ce, path);
}

static int checkout_file(const char *name, const char *base_dir)
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
	return checkout_entry(active_cache[pos], base_dir);
}

static int checkout_all(const char *base_dir)
{
	int i;

	for (i = 0; i < active_nr ; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce))
			continue;
		if (checkout_entry(ce, base_dir) < 0)
			return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int i, force_filename = 0;
	const char *base_dir = "";

	if (read_cache() < 0) {
		die("invalid cache");
	}

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!force_filename) {
			if (!strcmp(arg, "-a")) {
				checkout_all(base_dir);
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
			if (!strcmp(arg, "-n")) {
				not_new = 1;
				continue;
			}
			if (!memcmp(arg, "--prefix=", 9)) {
				base_dir = arg+9;
				continue;
			}
		}
		checkout_file(arg, base_dir);
	}
	return 0;
}
