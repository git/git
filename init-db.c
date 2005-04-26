/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

void safe_create_dir(char *dir)
{
	if (mkdir(dir, 0755) < 0) {
		if (errno != EEXIST) {
			perror(dir);
			exit(1);
		}
	}
}

/*
 * If you want to, you can share the DB area with any number of branches.
 * That has advantages: you can save space by sharing all the SHA1 objects.
 * On the other hand, it might just make lookup slower and messier. You
 * be the judge.  The default case is to have one DB per managed directory.
 */
int main(int argc, char **argv)
{
	char *sha1_dir, *path;
	int len, i;

	safe_create_dir(".git");

	sha1_dir = getenv(DB_ENVIRONMENT);
	if (!sha1_dir) {
		sha1_dir = DEFAULT_DB_ENVIRONMENT;
		fprintf(stderr, "defaulting to local storage area\n");
	}
	len = strlen(sha1_dir);
	path = xmalloc(len + 40);
	memcpy(path, sha1_dir, len);

	safe_create_dir(sha1_dir);
	for (i = 0; i < 256; i++) {
		sprintf(path+len, "/%02x", i);
		safe_create_dir(path);
	}
	return 0;
}
