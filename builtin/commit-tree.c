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
#include "gpg-interface.h"

static const char commit_tree_usage[] = "git commit-tree [(-p <sha1>)...] [-S[<keyid>]] [-m <message>] [-F <file>] <sha1>";

static const char *sign_commit;

static void new_parent(struct commit *parent, struct commit_list **parents_p)
{
	struct object_id *oid = &parent->object.oid;
	struct commit_list *parents;
	for (parents = *parents_p; parents; parents = parents->next) {
		if (parents->item == parent) {
			error("duplicate parent %s ignored", oid_to_hex(oid));
			return;
		}
		parents_p = &parents->next;
	}
	commit_list_insert(parent, parents_p);
}

static int commit_tree_config(const char *var, const char *value, void *cb)
{
	int status = git_gpg_config(var, value, NULL);
	if (status)
		return status;
	return git_default_config(var, value, cb);
}

int cmd_commit_tree(int argc, const char **argv, const char *prefix)
{
	int i, got_tree = 0;
	struct commit_list *parents = NULL;
	struct object_id tree_oid;
	struct object_id commit_oid;
	struct strbuf buffer = STRBUF_INIT;

	git_config(commit_tree_config, NULL);

	if (argc < 2 || !strcmp(argv[1], "-h"))
		usage(commit_tree_usage);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "-p")) {
			struct object_id oid;
			if (argc <= ++i)
				usage(commit_tree_usage);
			if (get_sha1_commit(argv[i], oid.hash))
				die("Not a valid object name %s", argv[i]);
			assert_sha1_type(oid.hash, OBJ_COMMIT);
			new_parent(lookup_commit(oid.hash), &parents);
			continue;
		}

		if (skip_prefix(arg, "-S", &sign_commit))
			continue;

		if (!strcmp(arg, "--no-gpg-sign")) {
			sign_commit = NULL;
			continue;
		}

		if (!strcmp(arg, "-m")) {
			if (argc <= ++i)
				usage(commit_tree_usage);
			if (buffer.len)
				strbuf_addch(&buffer, '\n');
			strbuf_addstr(&buffer, argv[i]);
			strbuf_complete_line(&buffer);
			continue;
		}

		if (!strcmp(arg, "-F")) {
			int fd;

			if (argc <= ++i)
				usage(commit_tree_usage);
			if (buffer.len)
				strbuf_addch(&buffer, '\n');
			if (!strcmp(argv[i], "-"))
				fd = 0;
			else {
				fd = open(argv[i], O_RDONLY);
				if (fd < 0)
					die_errno("git commit-tree: failed to open '%s'",
						  argv[i]);
			}
			if (strbuf_read(&buffer, fd, 0) < 0)
				die_errno("git commit-tree: failed to read '%s'",
					  argv[i]);
			if (fd && close(fd))
				die_errno("git commit-tree: failed to close '%s'",
					  argv[i]);
			strbuf_complete_line(&buffer);
			continue;
		}

		if (get_sha1_tree(arg, tree_oid.hash))
			die("Not a valid object name %s", arg);
		if (got_tree)
			die("Cannot give more than one trees");
		got_tree = 1;
	}

	if (!buffer.len) {
		if (strbuf_read(&buffer, 0, 0) < 0)
			die_errno("git commit-tree: failed to read");
	}

	if (commit_tree(buffer.buf, buffer.len, tree_oid.hash, parents,
			commit_oid.hash, NULL, sign_commit)) {
		strbuf_release(&buffer);
		return 1;
	}

	printf("%s\n", oid_to_hex(&commit_oid));
	strbuf_release(&buffer);
	return 0;
}
