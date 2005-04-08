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

	if (argc != 3 || get_sha1_hex(argv[2], sha1))
		usage("cat-file: cat-file [-t | tagname] <sha1>");
	buf = read_sha1_file(sha1, type, &size);
	if (!buf) {
		fprintf(stderr, "cat-file %s: bad file\n", argv[2]);
		exit(1);
	}
	if (!strcmp("-t", argv[1])) {
		buf = type;
		size = strlen(type);
		type[size] = '\n';
		size++;
	} else if (strcmp(type, argv[1])) {
		fprintf(stderr, "cat-file %s: bad tag\n", argv[2]);
		exit(1);	/* bad tag */
	}

	while (size > 0) {
		long ret = write(1, buf, size);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			/* Ignore epipe */
			if (errno == EPIPE)
				break;
			fprintf(stderr, "cat-file: %s\n", strerror(errno));
			exit(1);
		}
		if (!ret) {
			fprintf(stderr, "cat-file: disk full?");
			exit(1);
		}
		size -= ret;
		buf += ret;
	}
	return 0;
}
