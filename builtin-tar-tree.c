/*
 * Copyright (c) 2005, 2006 Rene Scharfe
 */
#include "cache.h"
#include "commit.h"
#include "tar.h"
#include "builtin.h"
#include "quote.h"

static const char tar_tree_usage[] =
"git tar-tree [--remote=<repo>] <tree-ish> [basedir]\n"
"*** Note that this command is now deprecated; use \"git archive\" instead.";

static const char builtin_get_tar_commit_id_usage[] =
"git get-tar-commit-id < <tarfile>";

int cmd_tar_tree(int argc, const char **argv, const char *prefix)
{
	/*
	 * "git tar-tree" is now a wrapper around "git archive --format=tar"
	 *
	 * $0 --remote=<repo> arg... ==>
	 *	git archive --format=tar --remote=<repo> arg...
	 * $0 tree-ish ==>
	 *	git archive --format=tar tree-ish
	 * $0 tree-ish basedir ==>
	 * 	git archive --format-tar --prefix=basedir tree-ish
	 */
	int i;
	const char **nargv = xcalloc(sizeof(*nargv), argc + 3);
	char *basedir_arg;
	int nargc = 0;

	nargv[nargc++] = "archive";
	nargv[nargc++] = "--format=tar";

	if (2 <= argc && !prefixcmp(argv[1], "--remote=")) {
		nargv[nargc++] = argv[1];
		argv++;
		argc--;
	}

	/*
	 * Because it's just a compatibility wrapper, tar-tree supports only
	 * the old behaviour of reading attributes from the work tree.
	 */
	nargv[nargc++] = "--worktree-attributes";

	switch (argc) {
	default:
		usage(tar_tree_usage);
		break;
	case 3:
		/* base-path */
		basedir_arg = xmalloc(strlen(argv[2]) + 11);
		sprintf(basedir_arg, "--prefix=%s/", argv[2]);
		nargv[nargc++] = basedir_arg;
		/* fallthru */
	case 2:
		/* tree-ish */
		nargv[nargc++] = argv[1];
	}
	nargv[nargc] = NULL;

	fprintf(stderr,
		"*** \"git tar-tree\" is now deprecated.\n"
		"*** Running \"git archive\" instead.\n***");
	for (i = 0; i < nargc; i++) {
		fputc(' ', stderr);
		sq_quote_print(stderr, nargv[i]);
	}
	fputc('\n', stderr);
	return cmd_archive(nargc, nargv, prefix);
}

/* ustar header + extended global header content */
#define RECORDSIZE	(512)
#define HEADERSIZE (2 * RECORDSIZE)

int cmd_get_tar_commit_id(int argc, const char **argv, const char *prefix)
{
	char buffer[HEADERSIZE];
	struct ustar_header *header = (struct ustar_header *)buffer;
	char *content = buffer + RECORDSIZE;
	ssize_t n;

	if (argc != 1)
		usage(builtin_get_tar_commit_id_usage);

	n = read_in_full(0, buffer, HEADERSIZE);
	if (n < HEADERSIZE)
		die("git get-tar-commit-id: read error");
	if (header->typeflag[0] != 'g')
		return 1;
	if (memcmp(content, "52 comment=", 11))
		return 1;

	n = write_in_full(1, content + 11, 41);
	if (n < 41)
		die_errno("git get-tar-commit-id: write error");

	return 0;
}
