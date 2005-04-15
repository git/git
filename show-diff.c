/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static void show_differences(char *name,
	void *old_contents, unsigned long long old_size)
{
	static char cmd[1000];
	FILE *f;

	snprintf(cmd, sizeof(cmd), "diff -L %s -u -N  - %s", name, name);
	f = popen(cmd, "w");
	if (old_size)
		fwrite(old_contents, old_size, 1, f);
	pclose(f);
}

static void show_diff_empty(struct cache_entry *ce)
{
	char *old;
	unsigned long int size;
	int lines=0;
	unsigned char type[20], *p, *end;

	old = read_sha1_file(ce->sha1, type, &size);
	if (size > 0) {
		int startline = 1;
		int c = 0;

		printf("--- %s\n", ce->name);
		printf("+++ /dev/null\n");
		p = old;
		end = old + size;
		while (p < end)
			if (*p++ == '\n')
				lines ++;
		printf("@@ -1,%d +0,0 @@\n", lines);
		p = old;
		while (p < end) {
			c = *p++;
			if (startline) {
				putchar('-');
				startline = 0;
			}
			putchar(c);
			if (c == '\n')
				startline = 1;
		}
		if (c!='\n')
			printf("\n");
		fflush(stdout);
	}
}

int main(int argc, char **argv)
{
	int silent = 0;
	int silent_on_nonexisting_files = 0;
	int entries = read_cache();
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-s")) {
			silent_on_nonexisting_files = silent = 1;
			continue;
		}
		if (!strcmp(argv[i], "-q")) {
			silent_on_nonexisting_files = 1;
			continue;
		}
		usage("show-diff [-s] [-q]");
	}

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
			if (errno == ENOENT && silent_on_nonexisting_files)
				continue;
			printf("%s: %s\n", ce->name, strerror(errno));
			if (errno == ENOENT)
				show_diff_empty(ce);
			continue;
		}
		changed = cache_match_stat(ce, &st);
		if (!changed)
			continue;
		printf("%.*s:  ", ce_namelen(ce), ce->name);
		for (n = 0; n < 20; n++)
			printf("%02x", ce->sha1[n]);
		printf("\n");
		fflush(stdout);
		if (silent)
			continue;

		new = read_sha1_file(ce->sha1, type, &size);
		show_differences(ce->name, new, size);
		free(new);
	}
	return 0;
}
