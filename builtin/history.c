#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "commit.h"
#include "commit-reach.h"
#include "config.h"
#include "editor.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "parse-options.h"
#include "refs.h"
#include "replay.h"
#include "revision.h"
#include "sequencer.h"
#include "strvec.h"
#include "tree.h"
#include "wt-status.h"

#define GIT_HISTORY_REWORD_USAGE \
	N_("git history reword <commit> [--ref-action=(branches|head|print)]")

static void change_data_free(void *util, const char *str UNUSED)
{
	struct wt_status_change_data *d = util;
	free(d->rename_source);
	free(d);
}

static int fill_commit_message(struct repository *repo,
			       const struct object_id *old_tree,
			       const struct object_id *new_tree,
			       const char *default_message,
			       const char *action,
			       struct strbuf *out)
{
	const char *path = git_path_commit_editmsg();
	const char *hint =
		_("Please enter the commit message for the %s changes."
		  " Lines starting\nwith '%s' will be ignored, and an"
		  " empty message aborts the commit.\n");
	struct wt_status s;

	strbuf_addstr(out, default_message);
	strbuf_addch(out, '\n');
	strbuf_commented_addf(out, comment_line_str, hint, action, comment_line_str);
	write_file_buf(path, out->buf, out->len);

	wt_status_prepare(repo, &s);
	FREE_AND_NULL(s.branch);
	s.ahead_behind_flags = AHEAD_BEHIND_QUICK;
	s.commit_template = 1;
	s.colopts = 0;
	s.display_comment_prefix = 1;
	s.hints = 0;
	s.use_color = 0;
	s.whence = FROM_COMMIT;
	s.committable = 1;

	s.fp = fopen(git_path_commit_editmsg(), "a");
	if (!s.fp)
		return error_errno(_("could not open '%s'"), git_path_commit_editmsg());

	wt_status_collect_changes_trees(&s, old_tree, new_tree);
	wt_status_print(&s);
	wt_status_collect_free_buffers(&s);
	string_list_clear_func(&s.change, change_data_free);

	strbuf_reset(out);
	if (launch_editor(path, out, NULL)) {
		fprintf(stderr, _("Aborting commit as launching the editor failed.\n"));
		return -1;
	}
	strbuf_stripspace(out, comment_line_str);

	cleanup_message(out, COMMIT_MSG_CLEANUP_ALL, 0);

	if (!out->len) {
		fprintf(stderr, _("Aborting commit due to empty commit message.\n"));
		return -1;
	}

	return 0;
}

static int commit_tree_with_edited_message(struct repository *repo,
					   const char *action,
					   struct commit *original,
					   struct commit **out)
{
	const char *exclude_gpgsig[] = {
		/* We reencode the message, so the encoding needs to be stripped. */
		"encoding",
		/* We need to strip signatures as those will become invalid. */
		"gpgsig",
		"gpgsig-sha256",
		NULL,
	};
	const char *original_message, *original_body, *ptr;
	struct commit_extra_header *original_extra_headers = NULL;
	struct strbuf commit_message = STRBUF_INIT;
	struct object_id rewritten_commit_oid;
	struct object_id original_tree_oid;
	struct object_id parent_tree_oid;
	char *original_author = NULL;
	struct commit *parent;
	size_t len;
	int ret;

	original_tree_oid = repo_get_commit_tree(repo, original)->object.oid;

	parent = original->parents ? original->parents->item : NULL;
	if (parent) {
		if (repo_parse_commit(repo, parent)) {
			ret = error(_("unable to parse parent commit %s"),
				    oid_to_hex(&parent->object.oid));
			goto out;
		}

		parent_tree_oid = repo_get_commit_tree(repo, parent)->object.oid;
	} else {
		oidcpy(&parent_tree_oid, repo->hash_algo->empty_tree);
	}

	/* We retain authorship of the original commit. */
	original_message = repo_logmsg_reencode(repo, original, NULL, NULL);
	ptr = find_commit_header(original_message, "author", &len);
	if (ptr)
		original_author = xmemdupz(ptr, len);
	find_commit_subject(original_message, &original_body);

	ret = fill_commit_message(repo, &parent_tree_oid, &original_tree_oid,
				  original_body, action, &commit_message);
	if (ret < 0)
		goto out;

	original_extra_headers = read_commit_extra_headers(original, exclude_gpgsig);

	ret = commit_tree_extended(commit_message.buf, commit_message.len, &original_tree_oid,
				   original->parents, &rewritten_commit_oid, original_author,
				   NULL, NULL, original_extra_headers);
	if (ret < 0)
		goto out;

	*out = lookup_commit_or_die(&rewritten_commit_oid, "rewritten commit");

out:
	free_commit_extra_headers(original_extra_headers);
	strbuf_release(&commit_message);
	free(original_author);
	return ret;
}

enum ref_action {
	REF_ACTION_DEFAULT,
	REF_ACTION_BRANCHES,
	REF_ACTION_HEAD,
	REF_ACTION_PRINT,
};

static int parse_ref_action(const struct option *opt, const char *value, int unset)
{
	enum ref_action *action = opt->value;

	BUG_ON_OPT_NEG_NOARG(unset, value);
	if (!strcmp(value, "branches")) {
		*action = REF_ACTION_BRANCHES;
	} else if (!strcmp(value, "head")) {
		*action = REF_ACTION_HEAD;
	} else if (!strcmp(value, "print")) {
		*action = REF_ACTION_PRINT;
	} else {
		return error(_("%s expects one of 'branches', 'head' or 'print'"),
			     opt->long_name);
	}

	return 0;
}

static int setup_revwalk(struct repository *repo,
			 enum ref_action action,
			 struct commit *original,
			 struct rev_info *revs)
{
	struct strvec args = STRVEC_INIT;
	int ret;

	repo_init_revisions(repo, revs, NULL);
	strvec_push(&args, "ignored");
	strvec_push(&args, "--reverse");
	strvec_push(&args, "--topo-order");
	strvec_push(&args, "--full-history");

	/* We only want to see commits that are descendants of the old commit. */
	strvec_pushf(&args, "--ancestry-path=%s",
		     oid_to_hex(&original->object.oid));

	/*
	 * Ancestry path may also show ancestors of the old commit, but we
	 * don't want to see those, either.
	 */
	strvec_pushf(&args, "^%s", oid_to_hex(&original->object.oid));

	/*
	 * When we're asked to update HEAD we need to verify that the commit
	 * that we want to rewrite is actually an ancestor of it and, if so,
	 * update it. Otherwise we'll update (or print) all descendant
	 * branches.
	 */
	if (action == REF_ACTION_HEAD) {
		struct commit_list *from_list = NULL;
		struct commit *head;

		head = lookup_commit_reference_by_name("HEAD");
		if (!head) {
			ret = error(_("cannot look up HEAD"));
			goto out;
		}

		commit_list_insert(original, &from_list);
		ret = repo_is_descendant_of(repo, head, from_list);
		free_commit_list(from_list);

		if (ret < 0) {
			ret = error(_("cannot determine descendance"));
			goto out;
		} else if (!ret) {
			ret = error(_("rewritten commit must be an ancestor "
				      "of HEAD when using --ref-action=head"));
			goto out;
		}

		strvec_push(&args, "HEAD");
	} else {
		strvec_push(&args, "--branches");
		strvec_push(&args, "HEAD");
	}

	setup_revisions_from_strvec(&args, revs, NULL);
	if (args.nr != 1)
		BUG("revisions were set up with invalid argument");

	ret = 0;

out:
	strvec_clear(&args);
	return ret;
}

static int handle_reference_updates(struct rev_info *revs,
				    enum ref_action action,
				    struct commit *original,
				    struct commit *rewritten,
				    const char *reflog_msg)
{
	const struct name_decoration *decoration;
	struct replay_revisions_options opts = { 0 };
	struct replay_result result = { 0 };
	struct ref_transaction *transaction = NULL;
	struct strbuf err = STRBUF_INIT;
	char hex[GIT_MAX_HEXSZ + 1];
	bool detached_head;
	int head_flags = 0;
	int ret;

	refs_read_ref_full(get_main_ref_store(revs->repo), "HEAD",
			   RESOLVE_REF_NO_RECURSE, NULL, &head_flags);
	detached_head = !(head_flags & REF_ISSYMREF);

	opts.onto = oid_to_hex_r(hex, &rewritten->object.oid);

	ret = replay_revisions(revs, &opts, &result);
	if (ret)
		goto out;

	switch (action) {
	case REF_ACTION_BRANCHES:
	case REF_ACTION_HEAD:
		transaction = ref_store_transaction_begin(get_main_ref_store(revs->repo), 0, &err);
		if (!transaction) {
			ret = error(_("failed to begin ref transaction: %s"), err.buf);
			goto out;
		}

		for (size_t i = 0; i < result.updates_nr; i++) {
			ret = ref_transaction_update(transaction,
						     result.updates[i].refname,
						     &result.updates[i].new_oid,
						     &result.updates[i].old_oid,
						     NULL, NULL, 0, reflog_msg, &err);
			if (ret) {
				ret = error(_("failed to update ref '%s': %s"),
					    result.updates[i].refname, err.buf);
				goto out;
			}
		}

		/*
		 * `replay_revisions()` only updates references that are
		 * ancestors of `rewritten`, so we need to manually
		 * handle updating references that point to `original`.
		 */
		for (decoration = get_name_decoration(&original->object);
		     decoration;
		     decoration = decoration->next)
		{
			if (decoration->type != DECORATION_REF_LOCAL &&
			    decoration->type != DECORATION_REF_HEAD)
				continue;

			if (action == REF_ACTION_HEAD &&
			    decoration->type != DECORATION_REF_HEAD)
				continue;

			/*
			 * We only need to update HEAD separately in case it's
			 * detached. If it's not we'd already update the branch
			 * it is pointing to.
			 */
			if (action == REF_ACTION_BRANCHES &&
			    decoration->type == DECORATION_REF_HEAD &&
			    !detached_head)
				continue;

			ret = ref_transaction_update(transaction,
						     decoration->name,
						     &rewritten->object.oid,
						     &original->object.oid,
						     NULL, NULL, 0, reflog_msg, &err);
			if (ret) {
				ret = error(_("failed to update ref '%s': %s"),
					    decoration->name, err.buf);
				goto out;
			}
		}

		if (ref_transaction_commit(transaction, &err)) {
			ret = error(_("failed to commit ref transaction: %s"), err.buf);
			goto out;
		}

		break;
	case REF_ACTION_PRINT:
		for (size_t i = 0; i < result.updates_nr; i++)
			printf("update %s %s %s\n",
			       result.updates[i].refname,
			       oid_to_hex(&result.updates[i].new_oid),
			       oid_to_hex(&result.updates[i].old_oid));
		break;
	default:
		BUG("unsupported ref action %d", action);
	}

	ret = 0;

out:
	ref_transaction_free(transaction);
	replay_result_release(&result);
	strbuf_release(&err);
	return ret;
}

static int cmd_history_reword(int argc,
			      const char **argv,
			      const char *prefix,
			      struct repository *repo)
{
	const char * const usage[] = {
		GIT_HISTORY_REWORD_USAGE,
		NULL,
	};
	enum ref_action action = REF_ACTION_DEFAULT;
	struct option options[] = {
		OPT_CALLBACK_F(0, "ref-action", &action, N_("<action>"),
			       N_("control ref update behavior (branches|head|print)"),
			       PARSE_OPT_NONEG, parse_ref_action),
		OPT_END(),
	};
	struct strbuf reflog_msg = STRBUF_INIT;
	struct commit *original, *rewritten;
	struct rev_info revs;
	int ret;

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	if (argc != 1) {
		ret = error(_("command expects a single revision"));
		goto out;
	}
	repo_config(repo, git_default_config, NULL);

	if (action == REF_ACTION_DEFAULT)
		action = REF_ACTION_BRANCHES;

	original = lookup_commit_reference_by_name(argv[0]);
	if (!original) {
		ret = error(_("commit cannot be found: %s"), argv[0]);
		goto out;
	}

	ret = setup_revwalk(repo, action, original, &revs);
	if (ret)
		goto out;

	ret = commit_tree_with_edited_message(repo, "reworded", original, &rewritten);
	if (ret < 0) {
		ret = error(_("failed writing reworded commit"));
		goto out;
	}

	strbuf_addf(&reflog_msg, "reword: updating %s", argv[0]);

	ret = handle_reference_updates(&revs, action, original, rewritten,
				       reflog_msg.buf);
	if (ret < 0) {
		ret = error(_("failed replaying descendants"));
		goto out;
	}

	ret = 0;

out:
	strbuf_release(&reflog_msg);
	release_revisions(&revs);
	return ret;
}

int cmd_history(int argc,
		const char **argv,
		const char *prefix,
		struct repository *repo)
{
	const char * const usage[] = {
		GIT_HISTORY_REWORD_USAGE,
		NULL,
	};
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("reword", &fn, cmd_history_reword),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	return fn(argc, argv, prefix, repo);
}
