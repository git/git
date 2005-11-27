/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "blob.h"
#include "tree.h"
#include "quote.h"

static int line_termination = '\n';
#define LS_RECURSIVE 1
#define LS_TREE_ONLY 2
static int ls_options = LS_RECURSIVE;

static const char ls_tree_usage[] =
	"git-ls-tree [-d] [-r] [-z] <tree-ish> [path...]";

static int show_tree(unsigned char *sha1, const char *base, int baselen, const char *pathname, unsigned mode, int stage)
{
	const char *type = "blob";

	if (S_ISDIR(mode)) {
		if (ls_options & LS_RECURSIVE)
			return READ_TREE_RECURSIVE;
		type = "tree";
	}

	printf("%06o %s %s\t%.*s%s%c", mode, type, sha1_to_hex(sha1), baselen, base, pathname, line_termination);
	return 0;
}

int main(int argc, const char **argv)
{
	const char **path, *prefix;
	unsigned char sha1[20];
	char *buf;
	unsigned long size;

	prefix = setup_git_directory();
	while (1 < argc && argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'z':
			line_termination = 0;
			break;
		case 'r':
			ls_options |= LS_RECURSIVE;
			break;
		case 'd':
			ls_options |= LS_TREE_ONLY;
			break;
		default:
			usage(ls_tree_usage);
		}
		argc--; argv++;
	}

	if (argc < 2)
		usage(ls_tree_usage);
	if (get_sha1(argv[1], sha1) < 0)
		usage(ls_tree_usage);

	path = get_pathspec(prefix, argv + 2);
	buf = read_object_with_reference(sha1, "tree", &size, NULL);
	if (!buf)
		die("not a tree object");
	read_tree_recursive(buf, size, "", 0, 0, path, show_tree);

	return 0;
}
