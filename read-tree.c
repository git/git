/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static int read_one_entry(unsigned char *sha1, const char *base, int baselen, const char *pathname, unsigned mode)
{
	int len = strlen(pathname);
	unsigned int size = cache_entry_size(baselen + len);
	struct cache_entry *ce = malloc(size);

	memset(ce, 0, size);

	ce->st_mode = mode;
	ce->namelen = baselen + len;
	memcpy(ce->name, base, baselen);
	memcpy(ce->name + baselen, pathname, len+1);
	memcpy(ce->sha1, sha1, 20);
	return add_cache_entry(ce, 1);
}

static int read_tree(unsigned char *sha1, const char *base, int baselen)
{
	void *buffer;
	unsigned long size;
	char type[20];

	buffer = read_sha1_file(sha1, type, &size);
	if (!buffer)
		return -1;
	if (strcmp(type, "tree"))
		return -1;
	while (size) {
		int len = strlen(buffer)+1;
		unsigned char *sha1 = buffer + len;
		char *path = strchr(buffer, ' ')+1;
		unsigned int mode;

		if (size < len + 20 || sscanf(buffer, "%o", &mode) != 1)
			return -1;

		buffer = sha1 + 20;
		size -= len + 20;

		if (S_ISDIR(mode)) {
			int retval;
			int pathlen = strlen(path);
			char *newbase = malloc(baselen + 1 + pathlen);
			memcpy(newbase, base, baselen);
			memcpy(newbase + baselen, path, pathlen);
			newbase[baselen + pathlen] = '/';
			retval = read_tree(sha1, newbase, baselen + pathlen + 1);
			free(newbase);
			if (retval)
				return -1;
			continue;
		}
		if (read_one_entry(sha1, base, baselen, path, mode) < 0)
			return -1;
	}
	return 0;
}

static int remove_lock = 0;

static void remove_lock_file(void)
{
	if (remove_lock)
		unlink(".git/index.lock");
}

int main(int argc, char **argv)
{
	int i, newfd;
	unsigned char sha1[20];

	newfd = open(".git/index.lock", O_RDWR | O_CREAT | O_EXCL, 0600);
	if (newfd < 0)
		die("unable to create new cachefile");
	atexit(remove_lock_file);
	remove_lock = 1;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		/* "-m" stands for "merge" current directory cache */
		if (!strcmp(arg, "-m")) {
			if (active_cache)
				die("read-tree: cannot merge old cache on top of new");
			if (read_cache() < 0)
				die("read-tree: corrupt directory cache");
			continue;
		}
		if (get_sha1_hex(arg, sha1) < 0)
			usage("read-tree [-m] <sha1>");
		if (read_tree(sha1, "", 0) < 0)
			die("failed to unpack tree object %s", arg);
	}
	if (write_cache(newfd, active_cache, active_nr) ||
	    rename(".git/index.lock", ".git/index"))
		die("unable to write new index file");
	remove_lock = 0;
	return 0;
}
