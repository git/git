/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

int main(int argc, char **argv)
{
	unsigned char sha1[20];
	char type[20];
	void *buf;
	unsigned long size;

	if (argc != 3 || get_sha1(argv[2], sha1))
		usage("git-cat-file [-t | -s | tagname] <sha1>");

	if (!strcmp("-t", argv[1]) || !strcmp("-s", argv[1])) {
		if (!sha1_object_info(sha1, type,
				      argv[1][1] == 's' ? &size : NULL)) {
			switch (argv[1][1]) {
			case 't':
				printf("%s\n", type);
				break;
			case 's':
				printf("%lu\n", size);
				break;
			}
			return 0;
		}
		buf = NULL;
	} else {
		buf = read_object_with_reference(sha1, argv[1], &size, NULL);
	}

	if (!buf)
		die("git-cat-file %s: bad file", argv[2]);

	while (size > 0) {
		long ret = write(1, buf, size);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			/* Ignore epipe */
			if (errno == EPIPE)
				break;
			die("git-cat-file: %s", strerror(errno));
		} else if (!ret) {
			die("git-cat-file: disk full?");
		}
		size -= ret;
		buf += ret;
	}
	return 0;
}
