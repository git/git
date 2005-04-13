/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static int list(unsigned char *sha1)
{
	void *buffer;
	unsigned long size;
	char type[20];

	buffer = read_sha1_file(sha1, type, &size);
	if (!buffer)
		die("unable to read sha1 file");
	if (strcmp(type, "tree"))
		die("expected a 'tree' node");
	while (size) {
		int len = strlen(buffer)+1;
		unsigned char *sha1 = buffer + len;
		char *path = strchr(buffer, ' ')+1;
		unsigned int mode;
		unsigned char *type;

		if (size < len + 20 || sscanf(buffer, "%o", &mode) != 1)
			die("corrupt 'tree' file");
		buffer = sha1 + 20;
		size -= len + 20;
		/* XXX: We do some ugly mode heuristics here.
		 * It seems not worth it to read each file just to get this
		 * and the file size. -- pasky@ucw.cz */
		type = S_ISDIR(mode) ? "tree" : "blob";
		printf("%03o\t%s\t%s\t%s\n", mode, type, sha1_to_hex(sha1), path);
	}
	return 0;
}

int main(int argc, char **argv)
{
	unsigned char sha1[20];

	if (argc != 2)
		usage("ls-tree <key>");
	if (get_sha1_hex(argv[1], sha1) < 0)
		usage("ls-tree <key>");
	sha1_file_directory = getenv(DB_ENVIRONMENT);
	if (!sha1_file_directory)
		sha1_file_directory = DEFAULT_DB_ENVIRONMENT;
	if (list(sha1) < 0)
		die("list failed");
	return 0;
}
