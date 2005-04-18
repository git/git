/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static char *diff_cmd = "diff -L 'a/%s' -L 'b/%s' ";
static char *diff_opts = "-p -u";
static char *diff_arg_forward  = " - '%s'";
static char *diff_arg_reverse  = " '%s' -";

static void prepare_diff_cmd(void)
{
	/*
	 * Default values above are meant to match the
	 * Linux kernel development style.  Examples of
	 * alternative styles you can specify via environment
	 * variables are:
	 *
	 * GIT_DIFF_CMD="diff -L '%s' -L '%s'"
	 * GIT_DIFF_OPTS="-c";
	 */
	diff_cmd = getenv("GIT_DIFF_CMD") ? : diff_cmd;
	diff_opts = getenv("GIT_DIFF_OPTS") ? : diff_opts;
}

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
	int cnt, c;
	char *cp;

	/* count bytes needed to store the quoted string. */ 
	for (cnt = 1, cp = src; *cp; cnt++, cp++)
		if (*cp == '\'')
			cnt += 3;

	if (! (buf = malloc(cnt)))
	    return buf;
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

static void show_differences(char *name, char *label, void *old_contents,
			     unsigned long long old_size, int reverse)
{
	FILE *f;
	char *name_sq = sq_expand(name);
	char *label_sq = (name != label) ? sq_expand(label) : name_sq;
	char *diff_arg = reverse ? diff_arg_reverse : diff_arg_forward;
	int cmd_size = strlen(name_sq) + strlen(label_sq) * 2 +
		strlen(diff_cmd) + strlen(diff_opts) + strlen(diff_arg);
	char *cmd = malloc(cmd_size);
	int next_at;

	fflush(stdout);
	next_at = snprintf(cmd, cmd_size, diff_cmd, label_sq, label_sq);
	next_at += snprintf(cmd+next_at, cmd_size-next_at, "%s", diff_opts);
	next_at += snprintf(cmd+next_at, cmd_size-next_at, diff_arg, name_sq);
	f = popen(cmd, "w");
	if (old_size)
		fwrite(old_contents, old_size, 1, f);
	pclose(f);
	if (label_sq != name_sq)
		free(label_sq);
	free(name_sq);
	free(cmd);
}

static void show_diff_empty(struct cache_entry *ce, int reverse)
{
	char *old;
	unsigned long int size;
	unsigned char type[20];

	old = read_sha1_file(ce->sha1, type, &size);
	if (! old) {
		error("unable to read blob object for %s (%s)", ce->name,
		      sha1_to_hex(ce->sha1));
		return;
	}
	show_differences("/dev/null", ce->name, old, size, reverse);
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
	int reverse = 0;
	int entries = read_cache();
	int i;

	while (1 < argc && argv[1][0] == '-') {
		if  (!strcmp(argv[1], "-R"))
			reverse = 1;
		else if (!strcmp(argv[1], "-s"))
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
	prepare_diff_cmd();
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
					show_diff_empty(ce, reverse);
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
		if (silent)
			continue;

		old = read_sha1_file(ce->sha1, type, &size);
		if (! old)
			error("unable to read blob object for %s (%s)",
			      ce->name, sha1_to_hex(ce->sha1));
		else
			show_differences(ce->name, ce->name, old, size,
					 reverse);
		free(old);
	}
	return 0;
}
