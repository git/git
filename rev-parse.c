/*
 * rev-parse.c
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "commit.h"
#include "refs.h"
#include "quote.h"

#define DO_REVS		1
#define DO_NOREV	2
#define DO_FLAGS	4
#define DO_NONFLAGS	8
static int filter = ~0;

static char *def = NULL;

#define NORMAL 0
#define REVERSED 1
static int show_type = NORMAL;
static int symbolic = 0;
static int output_sq = 0;

static int revs_count = 0;

/*
 * Some arguments are relevant "revision" arguments,
 * others are about output format or other details.
 * This sorts it all out.
 */
static int is_rev_argument(const char *arg)
{
	static const char *rev_args[] = {
		"--all",
		"--bisect",
		"--dense",
		"--header",
		"--max-age=",
		"--max-count=",
		"--merge-order",
		"--min-age=",
		"--no-merges",
		"--objects",
		"--parents",
		"--pretty",
		"--show-breaks",
		"--sparse",
		"--topo-order",
		"--unpacked",
		NULL
	};
	const char **p = rev_args;

	for (;;) {
		const char *str = *p++;
		int len;
		if (!str)
			return 0;
		len = strlen(str);
		if (!strcmp(arg, str) ||
		    (str[len-1] == '=' && !strncmp(arg, str, len)))
			return 1;
	}
}

/* Output argument as a string, either SQ or normal */
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

/* Output a revision, only if filter allows it */
static void show_rev(int type, const unsigned char *sha1, const char *name)
{
	if (!(filter & DO_REVS))
		return;
	def = NULL;
	revs_count++;

	if (type != show_type)
		putchar('^');
	if (symbolic && name)
		show(name);
	else
		show(sha1_to_hex(sha1));
}

/* Output a flag, only if filter allows it. */
static void show_flag(char *arg)
{
	if (!(filter & DO_FLAGS))
		return;
	if (filter & (is_rev_argument(arg) ? DO_REVS : DO_NOREV))
		show(arg);
}

static void show_default(void)
{
	char *s = def;

	if (s) {
		unsigned char sha1[20];

		def = NULL;
		if (!get_sha1(s, sha1)) {
			show_rev(NORMAL, sha1, s);
			return;
		}
	}
}

static int show_reference(const char *refname, const unsigned char *sha1)
{
	show_rev(NORMAL, sha1, refname);
	return 0;
}

static void show_datestring(const char *flag, const char *datestr)
{
	static char buffer[100];

	/* date handling requires both flags and revs */
	if ((filter & (DO_FLAGS | DO_REVS)) != (DO_FLAGS | DO_REVS))
		return;
	snprintf(buffer, sizeof(buffer), "%s%lu", flag, approxidate(datestr));
	show(buffer);
}

static void show_file(const char *arg)
{
	show_default();
	if ((filter & (DO_NONFLAGS|DO_NOREV)) == (DO_NONFLAGS|DO_NOREV))
		show(arg);
}

int main(int argc, char **argv)
{
	int i, as_is = 0, verify = 0;
	unsigned char sha1[20];
	const char *prefix = setup_git_directory();
	
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		char *dotdot;
	
		if (as_is) {
			show_file(arg);
			continue;
		}
		if (*arg == '-') {
			if (!strcmp(arg, "--")) {
				as_is = 1;
				/* Pass on the "--" if we show anything but files.. */
				if (filter & (DO_FLAGS | DO_REVS))
					show_file(arg);
				continue;
			}
			if (!strcmp(arg, "--default")) {
				def = argv[i+1];
				i++;
				continue;
			}
			if (!strcmp(arg, "--revs-only")) {
				filter &= ~DO_NOREV;
				continue;
			}
			if (!strcmp(arg, "--no-revs")) {
				filter &= ~DO_REVS;
				continue;
			}
			if (!strcmp(arg, "--flags")) {
				filter &= ~DO_NONFLAGS;
				continue;
			}
			if (!strcmp(arg, "--no-flags")) {
				filter &= ~DO_FLAGS;
				continue;
			}
			if (!strcmp(arg, "--verify")) {
				filter &= ~(DO_FLAGS|DO_NOREV);
				verify = 1;
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
			if (!strcmp(arg, "--symbolic")) {
				symbolic = 1;
				continue;
			}
			if (!strcmp(arg, "--all")) {
				for_each_ref(show_reference);
				continue;
			}
			if (!strcmp(arg, "--show-prefix")) {
				if (prefix)
					puts(prefix);
				continue;
			}
			if (!strcmp(arg, "--git-dir")) {
				const char *gitdir = getenv(GIT_DIR_ENVIRONMENT);
				static char cwd[PATH_MAX];
				if (gitdir) {
					puts(gitdir);
					continue;
				}
				if (!prefix) {
					puts(".git");
					continue;
				}
				if (!getcwd(cwd, PATH_MAX))
					die("unable to get current working directory");
				printf("%s/.git\n", cwd);
				continue;
			}
			if (!strncmp(arg, "--since=", 8)) {
				show_datestring("--max-age=", arg+8);
				continue;
			}
			if (!strncmp(arg, "--after=", 8)) {
				show_datestring("--max-age=", arg+8);
				continue;
			}
			if (!strncmp(arg, "--before=", 9)) {
				show_datestring("--min-age=", arg+9);
				continue;
			}
			if (!strncmp(arg, "--until=", 8)) {
				show_datestring("--min-age=", arg+8);
				continue;
			}
			if (verify)
				die("Needed a single revision");
			show_flag(arg);
			continue;
		}

		/* Not a flag argument */
		dotdot = strstr(arg, "..");
		if (dotdot) {
			unsigned char end[20];
			char *n = dotdot+2;
			*dotdot = 0;
			if (!get_sha1(arg, sha1)) {
				if (!*n)
					n = "HEAD";
				if (!get_sha1(n, end)) {
					show_rev(NORMAL, end, n);
					show_rev(REVERSED, sha1, arg);
					continue;
				}
			}
			*dotdot = '.';
		}
		if (!get_sha1(arg, sha1)) {
			show_rev(NORMAL, sha1, arg);
			continue;
		}
		if (*arg == '^' && !get_sha1(arg+1, sha1)) {
			show_rev(REVERSED, sha1, arg+1);
			continue;
		}
		if (verify)
			die("Needed a single revision");
		as_is = 1;
		show_file(arg);
	}
	show_default();
	if (verify && revs_count != 1)
		die("Needed a single revision");
	return 0;
}
