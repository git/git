/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 * Copyright (C) Junio C Hamano, 2005 
 */
#include "cache.h"

static void hash_object(const char *path, const char *type, int write_object)
{
	int fd;
	struct stat st;
	unsigned char sha1[20];
	fd = open(path, O_RDONLY);
	if (fd < 0 ||
	    fstat(fd, &st) < 0 ||
	    index_fd(sha1, fd, &st, write_object, type))
		die(write_object
		    ? "Unable to add %s to database"
		    : "Unable to hash %s", path);
	printf("%s\n", sha1_to_hex(sha1));
}

static const char hash_object_usage[] =
"git-hash-object [-t <type>] [-w] <file>...";

int main(int argc, char **argv)
{
	int i;
	const char *type = "blob";
	int write_object = 0;

	for (i = 1 ; i < argc; i++) {
		if (!strcmp(argv[i], "-t")) {
			if (argc <= ++i)
				die(hash_object_usage);
			type = argv[i];
		}
		else if (!strcmp(argv[i], "-w"))
			write_object = 1;
		else
			hash_object(argv[i], type, write_object);
	}
	return 0;
}
