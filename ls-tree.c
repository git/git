/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

int line_termination = '\n';
int recursive = 0;

struct path_prefix {
	struct path_prefix *prev;
	const char *name;
};

static void print_path_prefix(struct path_prefix *prefix)
{
	if (prefix) {
		if (prefix->prev)
			print_path_prefix(prefix->prev);
		fputs(prefix->name, stdout);
		putchar('/');
	}
}

static void list_recursive(void *buffer,
			   const unsigned char *type,
			   unsigned long size,
			   struct path_prefix *prefix)
{
	struct path_prefix this_prefix;
	this_prefix.prev = prefix;

	if (strcmp(type, "tree"))
		die("expected a 'tree' node");

	while (size) {
		int namelen = strlen(buffer)+1;
		void *eltbuf;
		char elttype[20];
		unsigned long eltsize;
		unsigned char *sha1 = buffer + namelen;
		char *path = strchr(buffer, ' ') + 1;
		unsigned int mode;

		if (size < namelen + 20 || sscanf(buffer, "%o", &mode) != 1)
			die("corrupt 'tree' file");
		buffer = sha1 + 20;
		size -= namelen + 20;

		printf("%06o\t%s\t%s\t", mode,
		       S_ISDIR(mode) ? "tree" : "blob",
		       sha1_to_hex(sha1));
		print_path_prefix(prefix);
		fputs(path, stdout);
		putchar(line_termination);

		if (! recursive || ! S_ISDIR(mode))
			continue;

		if (! (eltbuf = read_sha1_file(sha1, elttype, &eltsize)) ) {
			error("cannot read %s", sha1_to_hex(sha1));
			continue;
		}
		this_prefix.name = path;
		list_recursive(eltbuf, elttype, eltsize, &this_prefix);
		free(eltbuf);
	}
}

static int list(unsigned char *sha1)
{
	void *buffer;
	unsigned long size;

	buffer = read_tree_with_tree_or_commit_sha1(sha1, &size, 0);
	if (!buffer)
		die("unable to read sha1 file");
	list_recursive(buffer, "tree", size, NULL);
	return 0;
}

static const char *ls_tree_usage = "ls-tree [-r] [-z] <key>";

int main(int argc, char **argv)
{
	unsigned char sha1[20];

	while (1 < argc && argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'z':
			line_termination = 0;
			break;
		case 'r':
			recursive = 1;
			break;
		default:
			usage(ls_tree_usage);
		}
		argc--; argv++;
	}

	if (argc != 2)
		usage(ls_tree_usage);
	if (get_sha1_hex(argv[1], sha1) < 0)
		usage(ls_tree_usage);
	sha1_file_directory = getenv(DB_ENVIRONMENT);
	if (!sha1_file_directory)
		sha1_file_directory = DEFAULT_DB_ENVIRONMENT;
	if (list(sha1) < 0)
		die("list failed");
	return 0;
}
