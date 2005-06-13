/*
 * rev-parse.c
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

int main(int argc, char **argv)
{
	int i, as_is = 0;
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
				as_is = 1;
			}
			if (!strcmp(arg, "--default")) {
				if (def)
					printf("%s\n", def);
				def = argv[i+1];
				i++;
				continue;
			}
			printf("%s\n", arg);
			continue;
		}
		def = NULL;
		if (!get_sha1(arg, sha1)) {
			printf("%s\n", sha1_to_hex(sha1));
			continue;
		}
		if (*arg == '^' && !get_sha1(arg+1, sha1)) {
			printf("^%s\n", sha1_to_hex(sha1));
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
					printf("%s\n", sha1_to_hex(end));
					printf("^%s\n", sha1_to_hex(sha1));
					continue;
				}
			}
			*dotdot = '.';
		}
		printf("%s\n", arg);
	}
	if (def)
		printf("%s\n", def);
	return 0;
}
