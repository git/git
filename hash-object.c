/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 * Copyright (C) Junio C Hamano, 2005
 */
#include "cache.h"
#include "blob.h"

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

static void hash_stdin(const char *type, int write_object)
{
	unsigned char sha1[20];
	if (index_pipe(sha1, 0, type, write_object))
		die("Unable to add stdin to database");
	printf("%s\n", sha1_to_hex(sha1));
}

static const char hash_object_usage[] =
"git-hash-object [-t <type>] [-w] [--stdin] <file>...";

int main(int argc, char **argv)
{
	int i;
	const char *type = blob_type;
	int write_object = 0;
	const char *prefix = NULL;
	int prefix_length = -1;
	int no_more_flags = 0;

	for (i = 1 ; i < argc; i++) {
		if (!no_more_flags && argv[i][0] == '-') {
			if (!strcmp(argv[i], "-t")) {
				if (argc <= ++i)
					usage(hash_object_usage);
				type = argv[i];
			}
			else if (!strcmp(argv[i], "-w")) {
				if (prefix_length < 0) {
					prefix = setup_git_directory();
					prefix_length =
						prefix ? strlen(prefix) : 0;
				}
				write_object = 1;
			}
			else if (!strcmp(argv[i], "--")) {
				no_more_flags = 1;
			}
			else if (!strcmp(argv[i], "--help"))
				usage(hash_object_usage);
			else if (!strcmp(argv[i], "--stdin")) {
				hash_stdin(type, write_object);
			}
			else
				usage(hash_object_usage);
		}
		else {
			const char *arg = argv[i];
			if (0 <= prefix_length)
				arg = prefix_filename(prefix, prefix_length,
						      arg);
			hash_object(arg, type, write_object);
			no_more_flags = 1;
		}
	}
	return 0;
}
