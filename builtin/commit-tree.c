/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "config.h"
#include "object-store.h"
#include "repository.h"
#include "commit.h"
#include "tree.h"
#include "builtin.h"
#include "utf8.h"
#include "gpg-interface.h"
#include "parse-options.h"

static const char * const commit_tree_usage[] = {
	N_("git commit-tree <tree> [(-p <parent>)...]"),
	N_("git commit-tree [(-p <parent>)...] [-S[<keyid>]] [(-m <message>)...]\n"
	   "                [(-F <file>)...] <tree>"),
	NULL
};

static const char *sign_commit;

static void new_parent(struct commit *parent, struct commit_list **parents_p)
{
	struct object_id *oid = &parent->object.oid;
	struct commit_list *parents;
	for (parents = *parents_p; parents; parents = parents->next) {
		if (parents->item == parent) {
			error(_("duplicate parent %s ignored"), oid_to_hex(oid));
			return;
		}
		parents_p = &parents->next;
	}
	commit_list_insert(parent, parents_p);
}

static int commit_tree_config(const char *var, const char *value, void *cb)
{
	return git_default_config(var, value, cb);
}

static int parse_parent_arg_callback(const struct option *opt,
		const char *arg, int unset)
{
	struct object_id oid;
	struct commit_list **parents = opt->value;

	BUG_ON_OPT_NEG_NOARG(unset, arg);

	if (get_oid_commit(arg, &oid))
		die(_("not a valid object name %s"), arg);

	assert_oid_type(&oid, OBJ_COMMIT);
	new_parent(lookup_commit(the_repository, &oid), parents);
	return 0;
}

static int parse_message_arg_callback(const struct option *opt,
		const char *arg, int unset)
{
	struct strbuf *buf = opt->value;

	BUG_ON_OPT_NEG_NOARG(unset, arg);

	if (buf->len)
		strbuf_addch(buf, '\n');
	strbuf_addstr(buf, arg);
	strbuf_complete_line(buf);

	return 0;
}

static int parse_file_arg_callback(const struct option *opt,
		const char *arg, int unset)
{
	int fd;
	struct strbuf *buf = opt->value;

	BUG_ON_OPT_NEG_NOARG(unset, arg);

	if (buf->len)
		strbuf_addch(buf, '\n');
	if (!strcmp(arg, "-"))
		fd = 0;
	else {
		fd = xopen(arg, O_RDONLY);
	}
	if (strbuf_read(buf, fd, 0) < 0)
		die_errno(_("git commit-tree: failed to read '%s'"), arg);
	if (fd && close(fd))
		die_errno(_("git commit-tree: failed to close '%s'"), arg);

	return 0;
}

int cmd_commit_tree(int argc, const char **argv, const char *prefix)
{
	static struct strbuf buffer = STRBUF_INIT;
	struct commit_list *parents = NULL;
	struct object_id tree_oid;
	struct object_id commit_oid;

	struct option options[] = {
		OPT_CALLBACK_F('p', NULL, &parents, N_("parent"),
			N_("id of a parent commit object"), PARSE_OPT_NONEG,
			parse_parent_arg_callback),
		OPT_CALLBACK_F('m', NULL, &buffer, N_("message"),
			N_("commit message"), PARSE_OPT_NONEG,
			parse_message_arg_callback),
		OPT_CALLBACK_F('F', NULL, &buffer, N_("file"),
			N_("read commit log message from file"), PARSE_OPT_NONEG,
			parse_file_arg_callback),
		{ OPTION_STRING, 'S', "gpg-sign", &sign_commit, N_("key-id"),
			N_("GPG sign commit"), PARSE_OPT_OPTARG, NULL, (intptr_t) "" },
		OPT_END()
	};

	git_config(commit_tree_config, NULL);

	if (argc < 2 || !strcmp(argv[1], "-h"))
		usage_with_options(commit_tree_usage, options);

	argc = parse_options(argc, argv, prefix, options, commit_tree_usage, 0);

	if (argc != 1)
		die(_("must give exactly one tree"));

	if (get_oid_tree(argv[0], &tree_oid))
		die(_("not a valid object name %s"), argv[0]);

	if (!buffer.len) {
		if (strbuf_read(&buffer, 0, 0) < 0)
			die_errno(_("git commit-tree: failed to read"));
	}

	if (commit_tree(buffer.buf, buffer.len, &tree_oid, parents, &commit_oid,
			NULL, sign_commit)) {
		strbuf_release(&buffer);
		return 1;
	}

	printf("%s\n", oid_to_hex(&commit_oid));
	strbuf_release(&buffer);
	return 0;
}
