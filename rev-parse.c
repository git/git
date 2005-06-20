/*
 * rev-parse.c
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

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

int main(int argc, char **argv)
{
	int i, as_is = 0, revs_only = 0, no_revs = 0;
	char *def = NULL;
	unsigned char sha1[20];

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		char *dotdot;
	
		if (as_is) {
			printf("%s\n", arg);
			continue;
		}
		if (*arg == '-') {
			if (!strcmp(arg, "--")) {
				if (def) {
					printf("%s\n", def);
					def = NULL;
				}
				if (revs_only)
					break;
				as_is = 1;
			}
			if (!strcmp(arg, "--default")) {
				if (def)
					printf("%s\n", def);
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
			if (revs_only | no_revs) {
				if (is_rev_argument(arg) != revs_only)
					continue;
			}
			printf("%s\n", arg);
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
					printf("%s\n", sha1_to_hex(end));
					printf("^%s\n", sha1_to_hex(sha1));
					continue;
				}
			}
			*dotdot = '.';
		}
		if (!get_sha1(arg, sha1)) {
			if (no_revs)
				continue;
			def = NULL;
			printf("%s\n", sha1_to_hex(sha1));
			continue;
		}
		if (*arg == '^' && !get_sha1(arg+1, sha1)) {
			if (no_revs)
				continue;
			def = NULL;
			printf("^%s\n", sha1_to_hex(sha1));
			continue;
		}
		if (def) {
			printf("%s\n", def);
			def = NULL;
		}
		if (revs_only)
			continue;
		printf("%s\n", arg);
	}
	if (def)
		printf("%s\n", def);
	return 0;
}
