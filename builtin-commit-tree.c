/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "commit.h"
#include "tree.h"
#include "builtin.h"
#include "utf8.h"

#define BLOCKING (1ul << 14)

/*
 * FIXME! Share the code with "write-tree.c"
 */
static void check_valid(unsigned char *sha1, enum object_type expect)
{
	enum object_type type = sha1_object_info(sha1, NULL);
	if (type < 0)
		die("%s is not a valid object", sha1_to_hex(sha1));
	if (type != expect)
		die("%s is not a valid '%s' object", sha1_to_hex(sha1),
		    typename(expect));
}

/*
 * Having more than two parents is not strange at all, and this is
 * how multi-way merges are represented.
 */
#define MAXPARENT (16)
static unsigned char parent_sha1[MAXPARENT][20];

static const char commit_tree_usage[] = "git-commit-tree <sha1> [-p <sha1>]* < changelog";

static int new_parent(int idx)
{
	int i;
	unsigned char *sha1 = parent_sha1[idx];
	for (i = 0; i < idx; i++) {
		if (!hashcmp(parent_sha1[i], sha1)) {
			error("duplicate parent %s ignored", sha1_to_hex(sha1));
			return 0;
		}
	}
	return 1;
}

static const char commit_utf8_warn[] =
"Warning: commit message does not conform to UTF-8.\n"
"You may want to amend it after fixing the message, or set the config\n"
"variable i18n.commitencoding to the encoding your project uses.\n";

int cmd_commit_tree(int argc, const char **argv, const char *prefix)
{
	int i;
	int parents = 0;
	unsigned char tree_sha1[20];
	unsigned char commit_sha1[20];
	struct strbuf buffer;
	int encoding_is_utf8;

	git_config(git_default_config, NULL);

	if (argc < 2)
		usage(commit_tree_usage);
	if (get_sha1(argv[1], tree_sha1))
		die("Not a valid object name %s", argv[1]);

	check_valid(tree_sha1, OBJ_TREE);
	for (i = 2; i < argc; i += 2) {
		const char *a, *b;
		a = argv[i]; b = argv[i+1];
		if (!b || strcmp(a, "-p"))
			usage(commit_tree_usage);

		if (parents >= MAXPARENT)
			die("Too many parents (%d max)", MAXPARENT);
		if (get_sha1(b, parent_sha1[parents]))
			die("Not a valid object name %s", b);
		check_valid(parent_sha1[parents], OBJ_COMMIT);
		if (new_parent(parents))
			parents++;
	}

	/* Not having i18n.commitencoding is the same as having utf-8 */
	encoding_is_utf8 = is_encoding_utf8(git_commit_encoding);

	strbuf_init(&buffer, 8192); /* should avoid reallocs for the headers */
	strbuf_addf(&buffer, "tree %s\n", sha1_to_hex(tree_sha1));

	/*
	 * NOTE! This ordering means that the same exact tree merged with a
	 * different order of parents will be a _different_ changeset even
	 * if everything else stays the same.
	 */
	for (i = 0; i < parents; i++)
		strbuf_addf(&buffer, "parent %s\n", sha1_to_hex(parent_sha1[i]));

	/* Person/date information */
	strbuf_addf(&buffer, "author %s\n", git_author_info(IDENT_ERROR_ON_NO_NAME));
	strbuf_addf(&buffer, "committer %s\n", git_committer_info(IDENT_ERROR_ON_NO_NAME));
	if (!encoding_is_utf8)
		strbuf_addf(&buffer, "encoding %s\n", git_commit_encoding);
	strbuf_addch(&buffer, '\n');

	/* And add the comment */
	if (strbuf_read(&buffer, 0, 0) < 0)
		die("git-commit-tree: read returned %s", strerror(errno));

	/* And check the encoding */
	if (encoding_is_utf8 && !is_utf8(buffer.buf))
		fprintf(stderr, commit_utf8_warn);

	if (!write_sha1_file(buffer.buf, buffer.len, commit_type, commit_sha1)) {
		printf("%s\n", sha1_to_hex(commit_sha1));
		return 0;
	}
	else
		return 1;
}
