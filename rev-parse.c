/*
 * rev-parse.c
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "commit.h"
#include "refs.h"

static char *def = NULL;
static int no_revs = 0;
static int single_rev = 0;
static int revs_only = 0;
static int do_rev_argument = 1;
static int output_revs = 0;
static int flags_only = 0;
static int no_flags = 0;
static int output_sq = 0;

#define NORMAL 0
#define REVERSED 1
static int show_type = NORMAL;

/*
 * Some arguments are relevant "revision" arguments,
 * others are about output format or other details.
 * This sorts it all out.
 */
static int is_rev_argument(const char *arg)
{
	static const char *rev_args[] = {
		"--max-count=",
		"--max-age=",
		"--min-age=",
		"--merge-order",
		NULL
	};
	const char **p = rev_args;

	for (;;) {
		const char *str = *p++;
		int len;
		if (!str)
			return 0;
		len = strlen(str);
		if (!strncmp(arg, str, len))
			return 1;
	}
}

static void show(const char *arg)
{
	if (output_sq) {
		int sq = '\'', ch;

		putchar(sq);
		while ((ch = *arg++)) {
			if (ch == sq)
				fputs("'\\'", stdout);
			putchar(ch);
		}
		putchar(sq);
		putchar(' ');
	}
	else
		puts(arg);
}

static void show_rev(int type, const unsigned char *sha1)
{
	if (no_revs)
		return;
	output_revs++;

	/* Hexadecimal string plus possibly a carret;
	 * this does not have to be quoted even under output_sq.
	 */
	printf("%s%s%c", type == show_type ? "" : "^", sha1_to_hex(sha1),
	       output_sq ? ' ' : '\n');
}

static void show_rev_arg(char *rev)
{
	if (no_revs)
		return;
	show(rev);
}

static void show_norev(char *norev)
{
	if (flags_only)
		return;
	if (revs_only)
		return;
	show(norev);
}

static void show_arg(char *arg)
{
	if (no_flags)
		return;
	if (do_rev_argument && is_rev_argument(arg))
		show_rev_arg(arg);
	else
		show_norev(arg);
}

static void show_default(void)
{
	char *s = def;

	if (s) {
		unsigned char sha1[20];

		def = NULL;
		if (!get_sha1(s, sha1)) {
			show_rev(NORMAL, sha1);
			return;
		}
		show_arg(s);
	}
}

static int show_reference(const char *refname, const unsigned char *sha1)
{
	show_rev(NORMAL, sha1);
	return 0;
}

int main(int argc, char **argv)
{
	int i, as_is = 0;
	unsigned char sha1[20];

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		char *dotdot;
	
		if (as_is) {
			show_norev(arg);
			continue;
		}
		if (*arg == '-') {
			if (!strcmp(arg, "--")) {
				show_default();
				if (revs_only)
					break;
				as_is = 1;
			}
			if (!strcmp(arg, "--default")) {
				def = argv[i+1];
				i++;
				continue;
			}
			if (!strcmp(arg, "--revs-only")) {
				revs_only = 1;
				continue;
			}
			if (!strcmp(arg, "--no-revs")) {
				no_revs = 1;
				continue;
			}
			if (!strcmp(arg, "--flags")) {
				flags_only = 1;
				continue;
			}
			if (!strcmp(arg, "--no-flags")) {
				no_flags = 1;
				continue;
			}
			if (!strcmp(arg, "--verify")) {
				revs_only = 1;
				do_rev_argument = 0;
				single_rev = 1;
				continue;
			}
			if (!strcmp(arg, "--sq")) {
				output_sq = 1;
				continue;
			}
			if (!strcmp(arg, "--not")) {
				show_type ^= REVERSED;
				continue;
			}
			if (!strcmp(arg, "--all")) {
				for_each_ref(show_reference);
				continue;
			}
			show_arg(arg);
			continue;
		}
		dotdot = strstr(arg, "..");
		if (dotdot) {
			unsigned char end[20];
			char *n = dotdot+2;
			*dotdot = 0;
			if (!get_sha1(arg, sha1)) {
				if (!*n)
					n = "HEAD";
				if (!get_sha1(n, end)) {
					if (no_revs)
						continue;
					def = NULL;
					show_rev(NORMAL, end);
					show_rev(REVERSED, sha1);
					continue;
				}
			}
			*dotdot = '.';
		}
		if (!get_sha1(arg, sha1)) {
			if (no_revs)
				continue;
			def = NULL;
			show_rev(NORMAL, sha1);
			continue;
		}
		if (*arg == '^' && !get_sha1(arg+1, sha1)) {
			if (no_revs)
				continue;
			def = NULL;
			show_rev(REVERSED, sha1);
			continue;
		}
		show_default();
		show_norev(arg);
	}
	show_default();
	if (single_rev && output_revs != 1) {
		fprintf(stderr, "Needed a single revision\n");
		exit(1);
	}
	return 0;
}
