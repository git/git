/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static void show_differences(struct cache_entry *ce, struct stat *cur,
	void *old_contents, unsigned long long old_size)
{
	static char cmd[1000];
	FILE *f;

	snprintf(cmd, sizeof(cmd), "diff -u - %s", ce->name);
	f = popen(cmd, "w");
	fwrite(old_contents, old_size, 1, f);
	pclose(f);
}

int main(int argc, char **argv)
{
	int entries = read_cache();
	int i;

	if (entries < 0) {
		perror("read_cache");
		exit(1);
	}
	for (i = 0; i < entries; i++) {
		struct stat st;
		struct cache_entry *ce = active_cache[i];
		int n, changed;
		unsigned long size;
		char type[20];
		void *new;

		if (stat(ce->name, &st) < 0) {
			printf("%s: %s\n", ce->name, strerror(errno));
			continue;
		}
		changed = cache_match_stat(ce, &st);
		if (!changed)
			continue;
		printf("%.*s:  ", ce->namelen, ce->name);
		for (n = 0; n < 20; n++)
			printf("%02x", ce->sha1[n]);
		printf("\n");
		new = read_sha1_file(ce->sha1, type, &size);
		show_differences(ce, &st, new, size);
		free(new);
	}
	return 0;
}
