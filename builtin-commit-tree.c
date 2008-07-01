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

static const char commit_tree_usage[] = "git-commit-tree <sha1> [-p <sha1>]* < changelog";

static void new_parent(struct commit *parent, struct commit_list **parents_p)
{
	unsigned char *sha1 = parent->object.sha1;
	struct commit_list *parents;
	for (parents = *parents_p; parents; parents = parents->next) {
		if (parents->item == parent) {
			error("duplicate parent %s ignored", sha1_to_hex(sha1));
			return;
		}
		parents_p = &parents->next;
	}
	commit_list_insert(parent, parents_p);
}

static const char commit_utf8_warn[] =
"Warning: commit message does not conform to UTF-8.\n"
"You may want to amend it after fixing the message, or set the config\n"
"variable i18n.commitencoding to the encoding your project uses.\n";

int commit_tree(const char *msg, unsigned char *tree,
		struct commit_list *parents, unsigned char *ret)
{
	int encoding_is_utf8;
	struct strbuf buffer;

	check_valid(tree, OBJ_TREE);

	/* Not having i18n.commitencoding is the same as having utf-8 */
	encoding_is_utf8 = is_encoding_utf8(git_commit_encoding);

	strbuf_init(&buffer, 8192); /* should avoid reallocs for the headers */
	strbuf_addf(&buffer, "tree %s\n", sha1_to_hex(tree));

	/*
	 * NOTE! This ordering means that the same exact tree merged with a
	 * different order of parents will be a _different_ changeset even
	 * if everything else stays the same.
	 */
	while (parents) {
		struct commit_list *next = parents->next;
		strbuf_addf(&buffer, "parent %s\n",
			sha1_to_hex(parents->item->object.sha1));
		free(parents);
		parents = next;
	}

	/* Person/date information */
	strbuf_addf(&buffer, "author %s\n", git_author_info(IDENT_ERROR_ON_NO_NAME));
	strbuf_addf(&buffer, "committer %s\n", git_committer_info(IDENT_ERROR_ON_NO_NAME));
	if (!encoding_is_utf8)
		strbuf_addf(&buffer, "encoding %s\n", git_commit_encoding);
	strbuf_addch(&buffer, '\n');

	/* And add the comment */
	strbuf_addstr(&buffer, msg);

	/* And check the encoding */
	if (encoding_is_utf8 && !is_utf8(buffer.buf))
		fprintf(stderr, commit_utf8_warn);

	return write_sha1_file(buffer.buf, buffer.len, commit_type, ret);
}

int cmd_commit_tree(int argc, const char **argv, const char *prefix)
{
	int i;
	struct commit_list *parents = NULL;
	unsigned char tree_sha1[20];
	unsigned char commit_sha1[20];
	struct strbuf buffer = STRBUF_INIT;

	git_config(git_default_config, NULL);

	if (argc < 2)
		usage(commit_tree_usage);
	if (get_sha1(argv[1], tree_sha1))
		die("Not a valid object name %s", argv[1]);

	for (i = 2; i < argc; i += 2) {
		unsigned char sha1[20];
		const char *a, *b;
		a = argv[i]; b = argv[i+1];
		if (!b || strcmp(a, "-p"))
			usage(commit_tree_usage);

		if (get_sha1(b, sha1))
			die("Not a valid object name %s", b);
		check_valid(sha1, OBJ_COMMIT);
		new_parent(lookup_commit(sha1), &parents);
	}

	if (strbuf_read(&buffer, 0, 0) < 0)
		die("git-commit-tree: read returned %s", strerror(errno));

	if (!commit_tree(buffer.buf, tree_sha1, parents, commit_sha1)) {
		printf("%s\n", sha1_to_hex(commit_sha1));
		return 0;
	}
	else
		return 1;
}
