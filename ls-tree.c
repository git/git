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
			  unsigned char *type,
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

		/* XXX: We do some ugly mode heuristics here.
		 * It seems not worth it to read each file just to get this
		 * and the file size. -- pasky@ucw.cz
		 * ... that is, when we are not recursive -- junkio@cox.net
		 */
		eltbuf = (recursive ? read_sha1_file(sha1, elttype, &eltsize) :
			  NULL);
		if (! eltbuf) {
			if (recursive)
				error("cannot read %s", sha1_to_hex(sha1));
			type = S_ISDIR(mode) ? "tree" : "blob";
		}
		else
			type = elttype;

		printf("%03o\t%s\t%s\t", mode, type, sha1_to_hex(sha1));
		print_path_prefix(prefix);
		fputs(path, stdout);
		putchar(line_termination);

		if (eltbuf && !strcmp(type, "tree")) {
			this_prefix.name = path;
			list_recursive(eltbuf, elttype, eltsize, &this_prefix);
		}
		free(eltbuf);
	}
}

static int list(unsigned char *sha1)
{
	void *buffer;
	unsigned long size;
	char type[20];

	buffer = read_sha1_file(sha1, type, &size);
	if (!buffer)
		die("unable to read sha1 file");
	list_recursive(buffer, type, size, NULL);
	return 0;
}

static void _usage(void)
{
	usage("ls-tree [-r] [-z] <key>");
}

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
			_usage();
		}
		argc--; argv++;
	}

	if (argc != 2)
		_usage();
	if (get_sha1_hex(argv[1], sha1) < 0)
		_usage();
	sha1_file_directory = getenv(DB_ENVIRONMENT);
	if (!sha1_file_directory)
		sha1_file_directory = DEFAULT_DB_ENVIRONMENT;
	if (list(sha1) < 0)
		die("list failed");
	return 0;
}
