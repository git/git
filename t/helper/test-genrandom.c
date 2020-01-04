/*
 * Simple random data generator used to create reproducible test files.
 * This is inspired from POSIX.1-2001 implementation example for rand().
 * Copyright (C) 2007 by Nicolas Pitre, licensed under the GPL version 2.
 */

#include "test-tool.h"
#include "git-compat-util.h"

int cmd__genrandom(int argc, const char **argv)
{
	unsigned long count, next = 0;
	unsigned char *c;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s <seed_string> [<size>]\n", argv[0]);
		return 1;
	}

	c = (unsigned char *) argv[1];
	do {
		next = next * 11 + *c;
	} while (*c++);

	count = (argc == 3) ? strtoul(argv[2], NULL, 0) : -1L;

	while (count--) {
		next = next * 1103515245 + 12345;
		if (putchar((next >> 16) & 0xff) == EOF)
			return -1;
	}

	return 0;
}
