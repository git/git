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

static const char commit_tree_usage[] = "git commit-tree <sha1> [(-p <sha1>)...] < changelog";

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

int cmd_commit_tree(int argc, const char **argv, const char *prefix)
{
	int i, got_tree = 0;
	struct commit_list *parents = NULL;
	unsigned char tree_sha1[20];
	unsigned char commit_sha1[20];
	struct strbuf buffer = STRBUF_INIT;

	git_config(git_default_config, NULL);

	if (argc < 2 || !strcmp(argv[1], "-h"))
		usage(commit_tree_usage);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "-p")) {
			unsigned char sha1[20];
			if (argc <= ++i)
				usage(commit_tree_usage);
			if (get_sha1(argv[i], sha1))
				die("Not a valid object name %s", argv[i]);
			assert_sha1_type(sha1, OBJ_COMMIT);
			new_parent(lookup_commit(sha1), &parents);
			continue;
		}

		if (get_sha1(arg, tree_sha1))
			die("Not a valid object name %s", arg);
		if (got_tree)
			die("Cannot give more than one trees");
		got_tree = 1;
	}

	if (strbuf_read(&buffer, 0, 0) < 0)
		die_errno("git commit-tree: failed to read");

	if (commit_tree(buffer.buf, tree_sha1, parents, commit_sha1, NULL)) {
		strbuf_release(&buffer);
		return 1;
	}

	printf("%s\n", sha1_to_hex(commit_sha1));
	strbuf_release(&buffer);
	return 0;
}
