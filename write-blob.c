/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

int main(int argc, char **argv)
{
	int i;

	for (i = 1 ; i < argc; i++) {
		char *path = argv[i];
		int fd;
		struct stat st;
		unsigned char sha1[20];
		fd = open(path, O_RDONLY);
		if (fd < 0 ||
		    fstat(fd, &st) < 0 ||
		    index_fd(sha1, fd, &st) < 0)
			die("Unable to add blob %s to database", path);
		printf("%s\n", sha1_to_hex(sha1));
	}
	return 0;
}
