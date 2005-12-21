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
	int opt;

	setup_git_directory();
	if (argc != 3 || get_sha1(argv[2], sha1))
		usage("git-cat-file [-t|-s|-e|<type>] <sha1>");

	opt = 0;
	if ( argv[1][0] == '-' ) {
		opt = argv[1][1];
		if ( !opt || argv[1][2] )
			opt = -1; /* Not a single character option */
	}

	buf = NULL;
	switch (opt) {
	case 't':
		if (!sha1_object_info(sha1, type, NULL)) {
			printf("%s\n", type);
			return 0;
		}
		break;

	case 's':
		if (!sha1_object_info(sha1, type, &size)) {
			printf("%lu\n", size);
			return 0;
		}
		break;

	case 'e':
		return !has_sha1_file(sha1);

	case 0:
		buf = read_object_with_reference(sha1, argv[1], &size, NULL);
		break;

	default:
		die("git-cat-file: unknown option: %s\n", argv[1]);
	}

	if (!buf)
		die("git-cat-file %s: bad file", argv[2]);

	while (size > 0) {
		long ret = xwrite(1, buf, size);
		if (ret < 0) {
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
