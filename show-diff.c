/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static char *diff_cmd = "diff -L '%s' -u -N  - '%s'";

/* Help to copy the thing properly quoted for the shell safety.
 * any single quote is replaced with '\'', and the caller is
 * expected to enclose the result within a single quote pair.
 *
 * E.g.
 *  original     sq_expand     result
 *  name     ==> name      ==> 'name'
 *  a b      ==> a b       ==> 'a b'
 *  a'b      ==> a'\''b    ==> 'a'\''b'
 *
 * NOTE! The returned memory belongs to this function so
 * do not free it.
 */
static char *sq_expand(char *src)
{
	static char *buf = NULL;
	static int buf_size = -1;
	int cnt, c;
	char *cp;

	/* count single quote characters */ 
	for (cnt = 0, cp = src; *cp; cnt++, cp++)
		if (*cp == '\'')
			cnt += 3;

	if (buf_size < cnt) {
		free(buf);
		buf_size = cnt;
		buf = malloc(cnt);
	}

	cp = buf;
	while ((c = *src++)) {
		if (c != '\'')
			*cp++ = c;
		else {
			cp = strcpy(cp, "'\\''");
			cp += 4;
		}
	}
	*cp = 0;
	return buf;
}

static void show_differences(char *name, void *old_contents,
			     unsigned long long old_size)
{
	FILE *f;
	static char *cmd = NULL;
	static int cmd_size = -1;

	char *name_sq = sq_expand(name);
	int cmd_required_length = strlen(name_sq) * 2 + strlen(diff_cmd);

	if (cmd_size < cmd_required_length) {
		free(cmd);
		cmd_size = cmd_required_length;
		cmd = malloc(cmd_required_length);
	}
	snprintf(cmd, cmd_size, diff_cmd, name_sq, name_sq);
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

static const char *show_diff_usage = "show-diff [-q] [-s] [-z] [paths...]";

static int matches_pathspec(struct cache_entry *ce, char **spec, int cnt)
{
	int i;
	int namelen = ce_namelen(ce);
	for (i = 0; i < cnt; i++) {
		int speclen = strlen(spec[i]);
		if (! strncmp(spec[i], ce->name, speclen) &&
		    speclen <= namelen &&
		    (ce->name[speclen] == 0 ||
		     ce->name[speclen] == '/'))
			return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int silent = 0;
	int silent_on_nonexisting_files = 0;
	int machine_readable = 0;
	int entries = read_cache();
	int i;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-s"))
			silent_on_nonexisting_files = silent = 1;
		else if (!strcmp(argv[1], "-q"))
			silent_on_nonexisting_files = 1;
		else if (!strcmp(argv[1], "-z"))
			machine_readable = 1;
		else
			usage(show_diff_usage);
		argv++; argc--;
	}

	/* At this point, if argc == 1, then we are doing everything.
	 * Otherwise argv[1] .. argv[argc-1] have the explicit paths.
	 */
	if (entries < 0) {
		perror("read_cache");
		exit(1);
	}
	for (i = 0; i < entries; i++) {
		struct stat st;
		struct cache_entry *ce = active_cache[i];
		int changed;
		unsigned long size;
		char type[20];
		void *old;

		if (1 < argc &&
		    ! matches_pathspec(ce, argv+1, argc-1))
			continue;

		if (ce_stage(ce)) {
			if (machine_readable)
				printf("U %s%c", ce->name, 0);
			else
				printf("%s: Unmerged\n",
				       ce->name);
			while (i < entries &&
			       !strcmp(ce->name, active_cache[i]->name))
				i++;
			i--; /* compensate for loop control increments */
			continue;
		}
 
		if (stat(ce->name, &st) < 0) {
			if (errno == ENOENT && silent_on_nonexisting_files)
				continue;
			if (machine_readable)
				printf("X %s%c", ce->name, 0);
			else {
				printf("%s: %s\n", ce->name, strerror(errno));
				if (errno == ENOENT)
					show_diff_empty(ce);
			}
			continue;
		}
		changed = cache_match_stat(ce, &st);
		if (!changed)
			continue;
		if (!machine_readable)
			printf("%s: %s\n", ce->name, sha1_to_hex(ce->sha1));
		else {
			printf("%s %s%c", sha1_to_hex(ce->sha1), ce->name, 0);
			continue;
		}
		fflush(stdout);
		if (silent)
			continue;

		old = read_sha1_file(ce->sha1, type, &size);
		show_differences(ce->name, old, size);
		free(old);
	}
	return 0;
}
