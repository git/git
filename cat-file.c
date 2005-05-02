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
		usage("cat-file [-t | tagname] <sha1>");

	if (!strcmp("-t", argv[1])) {
		buf = read_sha1_file(sha1, type, &size);
		if (buf) {
			buf = type;
			size = strlen(type);
			type[size] = '\n';
			size++;
		}
	} else {
		buf = read_object_with_reference(sha1, argv[1], &size, NULL);
	}

	if (!buf)
		die("cat-file %s: bad file", argv[2]);

	while (size > 0) {
		long ret = write(1, buf, size);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			/* Ignore epipe */
			if (errno == EPIPE)
				break;
			die("cat-file: %s", strerror(errno));
		} else if (!ret) {
			die("cat-file: disk full?");
		}
		size -= ret;
		buf += ret;
	}
	return 0;
}
