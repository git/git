#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "advice.h"
#include "cache-tree.h"
#include "commit.h"
#include "commit-reach.h"
#include "config.h"
#include "editor.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "merge-ort.h"
#include "oidmap.h"
#include "parse-options.h"
#include "path.h"
#include "read-cache.h"
#include "refs.h"
#include "replay.h"
#include "reset.h"
#include "revision.h"
#include "sequencer.h"
#include "strvec.h"
#include "tree.h"
#include "tree-walk.h"
#include "unpack-trees.h"
#include "wt-status.h"

#define GIT_HISTORY_DROP_USAGE \
	N_("git history drop <commit> [--dry-run] [--update-refs=(branches|head)] [--empty=(drop|keep|abort)]")
#define GIT_HISTORY_FIXUP_USAGE \
	N_("git history fixup <commit> [--dry-run] [--update-refs=(branches|head)] [--reedit-message] [--empty=(drop|keep|abort)]")
#define GIT_HISTORY_REWORD_USAGE \
	N_("git history reword <commit> [--dry-run] [--update-refs=(branches|head)]")
#define GIT_HISTORY_SPLIT_USAGE \
	N_("git history split <commit> [--dry-run] [--update-refs=(branches|head)] [--] [<pathspec>...]")
#define GIT_HISTORY_SQUASH_USAGE \
	N_("git history squash [--dry-run] [--update-refs=(branches|head)] [--reedit-message] <revision-range>")

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

	s.fp = fopen(path, "w");
	if (!s.fp)
		return error_errno(_("could not open '%s'"), path);

	strbuf_addstr(out, default_message);
	strbuf_addch(out, '\n');
	strbuf_commented_addf(out, comment_line_str, hint, action, comment_line_str);
	if (fwrite(out->buf, 1, out->len, s.fp) != out->len)
		die_errno(_("could not write to '%s'"), path);

	wt_status_collect_changes_trees(&s, old_tree, new_tree);
	wt_status_print(&s);
	wt_status_collect_free_buffers(&s);
	string_list_clear_func(&s.change, change_data_free);
	if (fclose(s.fp))
		die_errno(_("could not write to '%s'"), path);

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

enum commit_tree_flags {
	COMMIT_TREE_EDIT_MESSAGE = (1 << 0),
};

static int commit_tree_ext(struct repository *repo,
			   const char *action,
			   struct commit *commit_with_message,
			   const char *message_template,
			   const struct commit_list *parents,
			   const struct object_id *old_tree,
			   const struct object_id *new_tree,
			   struct commit **out,
			   enum commit_tree_flags flags)
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
	char *original_author = NULL;
	size_t len;
	int ret;

	/* We retain authorship of the original commit. */
	original_message = repo_logmsg_reencode(repo, commit_with_message, NULL, NULL);
	ptr = find_commit_header(original_message, "author", &len);
	if (ptr)
		original_author = xmemdupz(ptr, len);
	find_commit_subject(original_message, &original_body);

	if (!message_template)
		message_template = original_body;

	if (flags & COMMIT_TREE_EDIT_MESSAGE) {
		ret = fill_commit_message(repo, old_tree, new_tree,
					  message_template, action, &commit_message);
		if (ret < 0)
			goto out;
	} else {
		strbuf_addstr(&commit_message, message_template);
	}

	original_extra_headers = read_commit_extra_headers(commit_with_message,
							   exclude_gpgsig);

	ret = commit_tree_extended(commit_message.buf, commit_message.len, new_tree,
				   parents, &rewritten_commit_oid, original_author,
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

static int first_parent_tree_oid(struct repository *repo,
				 struct commit *commit,
				 struct object_id *out)
{
	struct commit *parent = commit->parents ? commit->parents->item : NULL;

	if (!parent) {
		oidcpy(out, repo->hash_algo->empty_tree);
		return 0;
	}

	if (repo_parse_commit(repo, parent))
		return error(_("unable to parse parent commit %s"),
			     oid_to_hex(&parent->object.oid));

	oidcpy(out, &repo_get_commit_tree(repo, parent)->object.oid);
	return 0;
}

static int commit_tree_with_edited_message(struct repository *repo,
					   const char *action,
					   struct commit *original,
					   struct commit **out)
{
	struct object_id parent_tree_oid;
	const struct object_id *tree_oid;

	tree_oid = &repo_get_commit_tree(repo, original)->object.oid;

	if (first_parent_tree_oid(repo, original, &parent_tree_oid) < 0)
		return -1;

	return commit_tree_ext(repo, action, original, NULL, original->parents,
			       &parent_tree_oid, tree_oid, out, COMMIT_TREE_EDIT_MESSAGE);
}

enum ref_action {
	REF_ACTION_DEFAULT,
	REF_ACTION_BRANCHES,
	REF_ACTION_HEAD,
};

static int parse_ref_action(const struct option *opt, const char *value, int unset)
{
	enum ref_action *action = opt->value;

	BUG_ON_OPT_NEG_NOARG(unset, value);
	if (!strcmp(value, "branches")) {
		*action = REF_ACTION_BRANCHES;
	} else if (!strcmp(value, "head")) {
		*action = REF_ACTION_HEAD;
	} else {
		return error(_("%s expects one of 'branches' or 'head'"),
			     opt->long_name);
	}

	return 0;
}

static int revwalk_contains_merges(struct repository *repo,
				   const struct strvec *revwalk_args)
{
	struct strvec args = STRVEC_INIT;
	struct rev_info revs;
	int ret;

	strvec_pushv(&args, revwalk_args->v);
	strvec_push(&args, "--min-parents=2");

	repo_init_revisions(repo, &revs, NULL);

	setup_revisions_from_strvec(&args, &revs, NULL);
	if (args.nr != 1)
		BUG("revisions were set up with invalid argument");

	if (prepare_revision_walk(&revs) < 0) {
		ret = error(_("error preparing revisions"));
		goto out;
	}

	if (get_revision(&revs)) {
		ret = error(_("replaying merge commits is not supported yet!"));
		goto out;
	}

	reset_revision_walk();
	ret = 0;

out:
	release_revisions(&revs);
	strvec_clear(&args);
	return ret;
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
		commit_list_free(from_list);

		if (ret < 0) {
			ret = error(_("cannot determine descendance"));
			goto out;
		} else if (!ret) {
			ret = error(_("rewritten commit must be an ancestor "
				      "of HEAD when using --update-refs=head"));
			goto out;
		}

		strvec_push(&args, "HEAD");
	} else {
		strvec_push(&args, "--branches");
		strvec_push(&args, "HEAD");
	}

	ret = revwalk_contains_merges(repo, &args);
	if (ret < 0)
		goto out;

	setup_revisions_from_strvec(&args, revs, NULL);
	if (args.nr != 1)
		BUG("revisions were set up with invalid argument");

	ret = 0;

out:
	strvec_clear(&args);
	return ret;
}

static int handle_ref_update(struct ref_transaction *transaction,
			     const char *refname,
			     const struct object_id *new_oid,
			     const struct object_id *old_oid,
			     const char *reflog_msg,
			     struct strbuf *err)
{
	if (!transaction) {
		printf("update %s %s %s\n",
		       refname, oid_to_hex(new_oid), oid_to_hex(old_oid));
		return 0;
	}

	return ref_transaction_update(transaction, refname, new_oid, old_oid,
				      NULL, NULL, 0, reflog_msg, err);
}

static int compute_pending_ref_updates(struct rev_info *revs,
				       enum ref_action action,
				       struct commit *original,
				       struct commit *rewritten,
				       enum replay_empty_commit_action empty,
				       struct replay_result *result)
{
	const struct name_decoration *decoration;
	struct replay_revisions_options opts = {
		.empty = empty,
	};
	char hex[GIT_MAX_HEXSZ + 1];
	bool detached_head;
	int head_flags = 0;
	int ret;

	refs_read_ref_full(get_main_ref_store(revs->repo), "HEAD",
			   RESOLVE_REF_NO_RECURSE, NULL, &head_flags);
	detached_head = !(head_flags & REF_ISSYMREF);

	opts.onto = oid_to_hex_r(hex, &rewritten->object.oid);

	ret = replay_revisions(revs, &opts, result);
	if (ret)
		return ret;

	if (action != REF_ACTION_BRANCHES && action != REF_ACTION_HEAD)
		BUG("unsupported ref action %d", action);

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

		replay_result_queue_update(result, decoration->name,
					   &original->object.oid,
					   &rewritten->object.oid);
	}

	return 0;
}

static int apply_pending_ref_updates(struct repository *repo,
				     const struct replay_result *result,
				     const char *reflog_msg,
				     int dry_run)
{
	struct ref_transaction *transaction = NULL;
	struct strbuf err = STRBUF_INIT;
	int ret;

	if (!dry_run) {
		transaction = ref_store_transaction_begin(get_main_ref_store(repo),
							  0, &err);
		if (!transaction) {
			ret = error(_("failed to begin ref transaction: %s"), err.buf);
			goto out;
		}
	}

	for (size_t i = 0; i < result->updates_nr; i++) {
		ret = handle_ref_update(transaction,
					result->updates[i].refname,
					&result->updates[i].new_oid,
					&result->updates[i].old_oid,
					reflog_msg, &err);
		if (ret) {
			ret = error(_("failed to update ref '%s': %s"),
				    result->updates[i].refname, err.buf);
			goto out;
		}
	}

	if (transaction && ref_transaction_commit(transaction, &err)) {
		ret = error(_("failed to commit ref transaction: %s"), err.buf);
		goto out;
	}

	ret = 0;

out:
	ref_transaction_free(transaction);
	strbuf_release(&err);
	return ret;
}

static int handle_reference_updates(struct rev_info *revs,
				    enum ref_action action,
				    struct commit *original,
				    struct commit *rewritten,
				    const char *reflog_msg,
				    int dry_run,
				    enum replay_empty_commit_action empty)
{
	struct replay_result result = { 0 };
	int ret;

	ret = compute_pending_ref_updates(revs, action, original, rewritten,
					  empty, &result);
	if (ret)
		goto out;

	ret = apply_pending_ref_updates(revs->repo, &result, reflog_msg, dry_run);

out:
	replay_result_release(&result);
	return ret;
}

static int commit_became_empty(struct repository *repo,
			       struct commit *original,
			       struct tree *result)
{
	struct object_id parent_tree_oid;

	if (first_parent_tree_oid(repo, original, &parent_tree_oid) < 0)
		return -1;

	return oideq(&result->object.oid, &parent_tree_oid);
}

static int parse_opt_empty(const struct option *opt, const char *arg, int unset)
{
	enum replay_empty_commit_action *value = opt->value;

	BUG_ON_OPT_NEG(unset);

	if (!strcmp(arg, "drop"))
		*value = REPLAY_EMPTY_COMMIT_DROP;
	else if (!strcmp(arg, "keep"))
		*value = REPLAY_EMPTY_COMMIT_KEEP;
	else if (!strcmp(arg, "abort"))
		*value = REPLAY_EMPTY_COMMIT_ABORT;
	else
		die(_("unrecognized '--empty=' action '%s'; "
		      "valid values are \"drop\", \"keep\", and \"abort\"."), arg);

	return 0;
}

static int cmd_history_fixup(int argc,
			     const char **argv,
			     const char *prefix,
			     struct repository *repo)
{
	const char * const usage[] = {
		GIT_HISTORY_FIXUP_USAGE,
		NULL,
	};
	enum replay_empty_commit_action empty = REPLAY_EMPTY_COMMIT_DROP;
	enum ref_action action = REF_ACTION_DEFAULT;
	enum commit_tree_flags flags = 0;
	int dry_run = 0;
	struct option options[] = {
		OPT_CALLBACK_F(0, "update-refs", &action, "(branches|head)",
			       N_("control which refs should be updated"),
			       PARSE_OPT_NONEG, parse_ref_action),
		OPT_BOOL('n', "dry-run", &dry_run,
			 N_("perform a dry-run without updating any refs")),
		OPT_BIT(0, "reedit-message", &flags,
			N_("open an editor to modify the commit message"),
			COMMIT_TREE_EDIT_MESSAGE),
		OPT_CALLBACK_F(0, "empty", &empty, "(drop|keep|abort)",
			       N_("how to handle commits that become empty"),
			       PARSE_OPT_NONEG, parse_opt_empty),
		OPT_END(),
	};
	struct merge_result merge_result = { 0 };
	struct merge_options merge_opts = { 0 };
	struct strbuf reflog_msg = STRBUF_INIT;
	struct commit *head_commit, *original, *rewritten;
	struct tree *head_tree, *original_tree, *index_tree;
	struct rev_info revs = { 0 };
	bool skip_commit = false;
	int ret;

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	if (argc != 1) {
		ret = error(_("command expects a single revision"));
		goto out;
	}
	repo_config(repo, git_default_config, NULL);

	if (action == REF_ACTION_DEFAULT)
		action = REF_ACTION_BRANCHES;

	if (is_bare_repository(repo)) {
		ret = error(_("cannot run fixup in a bare repository"));
		goto out;
	}

	/* Resolve the original commit, which is the one we want to fix up. */
	original = lookup_commit_reference_by_name(argv[0]);
	if (!original) {
		ret = error(_("commit cannot be found: %s"), argv[0]);
		goto out;
	}

	/*
	 * Resolve HEAD so we can use its tree as the merge base: the staged
	 * changes are expressed as a diff from HEAD's tree to the index tree.
	 */
	head_commit = lookup_commit_reference_by_name("HEAD");
	if (!head_commit) {
		ret = error(_("cannot look up HEAD"));
		goto out;
	}

	head_tree = repo_get_commit_tree(repo, head_commit);
	if (!head_tree) {
		ret = error(_("cannot get tree for HEAD"));
		goto out;
	}

	if (repo_read_index(repo) < 0) {
		ret = error(_("unable to read index"));
		goto out;
	}

	if (!repo_index_has_changes(repo, head_tree, NULL)) {
		ret = error(_("nothing to fixup: no staged changes"));
		goto out;
	}

	/*
	 * Write the index as a tree object. This is the "theirs" side of the
	 * three-way merge: it is HEAD's tree with the staged changes applied.
	 */
	index_tree = write_in_core_index_as_tree(repo, repo->index);
	if (!index_tree) {
		ret = error(_("unable to write index as a tree"));
		goto out;
	}

	original_tree = repo_get_commit_tree(repo, original);
	if (!original_tree) {
		ret = error(_("cannot get tree for commit %s"), argv[0]);
		goto out;
	}

	/*
	 * Perform the three-way merge to reapply changes in the index onto the
	 * target commit. This is using basically the same logic as a
	 * cherry-pick, where the base commit is our HEAD, ours is the original
	 * tree and theirs is the index tree.
	 */
	init_basic_merge_options(&merge_opts, repo);
	merge_opts.ancestor = "HEAD";
	merge_opts.branch1 = argv[0];
	merge_opts.branch2 = "staged";
	merge_incore_nonrecursive(&merge_opts, head_tree,
				  original_tree, index_tree, &merge_result);

	if (merge_result.clean < 0) {
		ret = error(_("merge failed while applying fixup"));
		goto out;
	}

	if (!merge_result.clean) {
		ret = error(_("fixup would produce conflicts; aborting"));
		goto out;
	}

	ret = commit_became_empty(repo, original, merge_result.tree);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		switch (empty) {
		case REPLAY_EMPTY_COMMIT_DROP:
			/*
			 * Drop the target commit by replaying its descendants
			 * directly onto its parent.
			 */
			rewritten = original->parents ? original->parents->item : NULL;

			/*
			 * TODO: we don't yet have the ability to drop root
			 * commits, but there's ultimately no good reason for
			 * this restriction to exist other than a technical
			 * limitation.
			 */
			if (!rewritten) {
				ret = error(_("cannot drop root commit %s: "
					      "it has no parent to replay onto"),
					    argv[0]);
				goto out;
			}

			skip_commit = true;
			break;
		case REPLAY_EMPTY_COMMIT_KEEP:
			/* Proceed and record the empty commit. */
			break;
		case REPLAY_EMPTY_COMMIT_ABORT:
			ret = error(_("fixup makes commit %s empty"), argv[0]);
			goto out;
		}
	}

	ret = setup_revwalk(repo, action, original, &revs);
	if (ret)
		goto out;

	if (!skip_commit) {
		ret = commit_tree_ext(repo, "fixup", original, NULL, original->parents,
				      &original_tree->object.oid, &merge_result.tree->object.oid,
				      &rewritten, flags);
		if (ret < 0) {
			ret = error(_("failed writing fixed-up commit"));
			goto out;
		}
	}

	strbuf_addf(&reflog_msg, "fixup: updating %s", argv[0]);

	ret = handle_reference_updates(&revs, action, original, rewritten,
				       reflog_msg.buf, dry_run, empty);
	if (ret < 0) {
		ret = error(_("failed replaying descendants"));
		goto out;
	}

	ret = 0;

out:
	merge_finalize(&merge_opts, &merge_result);
	strbuf_release(&reflog_msg);
	release_revisions(&revs);
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
	int dry_run = 0;
	struct option options[] = {
		OPT_CALLBACK_F(0, "update-refs", &action, "(branches|head)",
			       N_("control which refs should be updated"),
			       PARSE_OPT_NONEG, parse_ref_action),
		OPT_BOOL('n', "dry-run", &dry_run,
			 N_("perform a dry-run without updating any refs")),
		OPT_END(),
	};
	struct strbuf reflog_msg = STRBUF_INIT;
	struct commit *original, *rewritten;
	struct rev_info revs = { 0 };
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
				       reflog_msg.buf, dry_run, REPLAY_EMPTY_COMMIT_ABORT);
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

static int write_ondisk_index(struct repository *repo,
			      struct object_id *oid,
			      const char *path)
{
	struct unpack_trees_options opts = { 0 };
	struct lock_file lock = LOCK_INIT;
	struct tree_desc tree_desc;
	struct index_state index;
	struct tree *tree;
	int ret;

	index_state_init(&index, repo);

	opts.head_idx = -1;
	opts.src_index = &index;
	opts.dst_index = &index;

	tree = repo_parse_tree_indirect(repo, oid);
	init_tree_desc(&tree_desc, &tree->object.oid, tree->buffer, tree->size);

	if (unpack_trees(1, &tree_desc, &opts)) {
		ret = error(_("unable to populate index with tree"));
		goto out;
	}

	prime_cache_tree(repo, &index, tree);

	if (repo_hold_lock_file_for_update(repo, &lock, path, 0) < 0) {
		ret = error_errno(_("unable to acquire index lock"));
		goto out;
	}

	if (write_locked_index(&index, &lock, COMMIT_LOCK)) {
		ret = error(_("unable to write new index file"));
		goto out;
	}

	ret = 0;

out:
	rollback_lock_file(&lock);
	release_index(&index);
	return ret;
}

static int split_commit(struct repository *repo,
			struct commit *original,
			struct pathspec *pathspec,
			struct commit **out)
{
	struct interactive_options interactive_opts = INTERACTIVE_OPTIONS_INIT;
	struct strbuf index_file = STRBUF_INIT;
	struct index_state index = INDEX_STATE_INIT(repo);
	const struct object_id *original_commit_tree_oid;
	const struct object_id *old_tree_oid, *new_tree_oid;
	struct object_id parent_tree_oid;
	char original_commit_oid[GIT_MAX_HEXSZ + 1];
	struct commit *first_commit, *second_commit;
	struct commit_list *parents = NULL;
	struct tree *split_tree;
	int ret;

	if (first_parent_tree_oid(repo, original, &parent_tree_oid) < 0) {
		ret = -1;
		goto out;
	}
	original_commit_tree_oid = get_commit_tree_oid(original);

	/*
	 * Construct the first commit. This is done by taking the original
	 * commit parent's tree and selectively patching changes from the diff
	 * between that parent and its child.
	 */
	repo_git_path_replace(repo, &index_file, "%s", "history-split.index");

	ret = write_ondisk_index(repo, &parent_tree_oid, index_file.buf);
	if (ret < 0)
		goto out;

	ret = read_index_from(&index, index_file.buf, repo->gitdir);
	if (ret < 0) {
		ret = error(_("failed reading temporary index"));
		goto out;
	}

	oid_to_hex_r(original_commit_oid, &original->object.oid);
	ret = run_add_p_index(repo, &index, index_file.buf, &interactive_opts,
			      original_commit_oid, pathspec, ADD_P_DISALLOW_EDIT);
	if (ret < 0)
		goto out;

	split_tree = write_in_core_index_as_tree(repo, &index);
	if (!split_tree) {
		ret = error(_("failed split tree"));
		goto out;
	}

	unlink(index_file.buf);
	strbuf_release(&index_file);

	/*
	 * We disallow the cases where either the split-out commit or the
	 * original commit would become empty. Consequently, if we see that the
	 * new tree ID matches either of those trees we abort.
	 */
	if (oideq(&split_tree->object.oid, &parent_tree_oid)) {
		ret = error(_("split commit is empty"));
		goto out;
	} else if (oideq(&split_tree->object.oid, original_commit_tree_oid)) {
		ret = error(_("split commit tree matches original commit"));
		goto out;
	}

	/*
	 * The first commit is constructed from the split-out tree. The base
	 * that shall be diffed against is the parent of the original commit.
	 */
	ret = commit_tree_ext(repo, "split-out", original, NULL, original->parents, &parent_tree_oid,
			      &split_tree->object.oid, &first_commit, COMMIT_TREE_EDIT_MESSAGE);
	if (ret < 0) {
		ret = error(_("failed writing first commit"));
		goto out;
	}

	/*
	 * The second commit is constructed from the original tree. The base to
	 * diff against and the parent in this case is the first split-out
	 * commit.
	 */
	commit_list_append(first_commit, &parents);

	old_tree_oid = &repo_get_commit_tree(repo, first_commit)->object.oid;
	new_tree_oid = &repo_get_commit_tree(repo, original)->object.oid;

	ret = commit_tree_ext(repo, "split-out", original, NULL, parents, old_tree_oid,
			      new_tree_oid, &second_commit, COMMIT_TREE_EDIT_MESSAGE);
	if (ret < 0) {
		ret = error(_("failed writing second commit"));
		goto out;
	}

	*out = second_commit;
	ret = 0;

out:
	if (index_file.len)
		unlink(index_file.buf);
	strbuf_release(&index_file);
	commit_list_free(parents);
	release_index(&index);
	return ret;
}

static int cmd_history_split(int argc,
			     const char **argv,
			     const char *prefix,
			     struct repository *repo)
{
	const char * const usage[] = {
		GIT_HISTORY_SPLIT_USAGE,
		NULL,
	};
	enum ref_action action = REF_ACTION_DEFAULT;
	int dry_run = 0;
	struct option options[] = {
		OPT_CALLBACK_F(0, "update-refs", &action, "(branches|head)",
			       N_("control ref update behavior"),
			       PARSE_OPT_NONEG, parse_ref_action),
		OPT_BOOL('n', "dry-run", &dry_run,
			 N_("perform a dry-run without updating any refs")),
		OPT_END(),
	};
	struct commit *original, *rewritten = NULL;
	struct strbuf reflog_msg = STRBUF_INIT;
	struct pathspec pathspec = { 0 };
	struct rev_info revs = { 0 };
	int ret;

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	if (argc < 1) {
		ret = error(_("command expects a committish"));
		goto out;
	}
	repo_config(repo, git_default_config, NULL);

	if (action == REF_ACTION_DEFAULT)
		action = REF_ACTION_BRANCHES;

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_FULL |
		       PATHSPEC_SYMLINK_LEADING_PATH |
		       PATHSPEC_PREFIX_ORIGIN,
		       prefix, argv + 1);

	original = lookup_commit_reference_by_name(argv[0]);
	if (!original) {
		ret = error(_("commit cannot be found: %s"), argv[0]);
		goto out;
	}

	ret = setup_revwalk(repo, action, original, &revs);
	if (ret < 0)
		goto out;

	if (original->parents && original->parents->next) {
		ret = error(_("cannot split up merge commit"));
		goto out;
	}

	ret = split_commit(repo, original, &pathspec, &rewritten);
	if (ret < 0)
		goto out;

	strbuf_addf(&reflog_msg, "split: updating %s", argv[0]);

	ret = handle_reference_updates(&revs, action, original, rewritten,
				       reflog_msg.buf, dry_run, REPLAY_EMPTY_COMMIT_ABORT);
	if (ret < 0) {
		ret = error(_("failed replaying descendants"));
		goto out;
	}

	ret = 0;

out:
	strbuf_release(&reflog_msg);
	clear_pathspec(&pathspec);
	release_revisions(&revs);
	return ret;
}

/*
 * Resolve a "<base>..<tip>" revision range into the base commit just outside
 * the range (which becomes the parent of the squashed commit), the oldest
 * commit contained in the range (whose message the squash reuses), and the
 * range tip (whose tree becomes the result). A merge inside the range is fine,
 * but the range must have a single base and must not reach a root commit.
 */
static int resolve_squash_range(struct repository *repo,
				const char **argv,
				struct commit **base_out,
				struct commit **oldest_out,
				struct commit **tip_out,
				struct oidset *interior_out)
{
	struct rev_info revs;
	struct commit *commit, *base = NULL, *oldest = NULL, *tip = NULL;
	struct commit_list *boundaries = NULL, *b;
	struct strvec args = STRVEC_INIT;
	size_t i;
	int ret;

	repo_init_revisions(repo, &revs, NULL);
	revs.reverse = 1;
	revs.topo_order = 1;
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.simplify_history = 0;
	revs.boundary = 1;

	strvec_push(&args, "ignored");
	strvec_push(&args, "--ancestry-path");
	strvec_pushv(&args, argv);
	setup_revisions_from_strvec(&args, &revs, NULL);
	if (args.nr != 1) {
		ret = error(_("unrecognized argument: %s"), args.v[1]);
		goto out;
	}

	if (revs.reverse != 1 || revs.topo_order != 1 ||
	    revs.sort_order != REV_SORT_IN_GRAPH_ORDER ||
	    revs.simplify_history != 0 || revs.boundary != 1) {
		warning(_("ignoring rev-list options that would change how the "
			  "range is walked"));
		revs.reverse = 1;
		revs.topo_order = 1;
		revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
		revs.simplify_history = 0;
		revs.boundary = 1;
	}

	/*
	 * A squash needs a base to reparent onto, so the range has to exclude
	 * something, as in "<base>..<tip>". A revision range with no such
	 * bottom commit cannot be squashed.
	 */
	for (i = 0; i < revs.cmdline.nr; i++)
		if (revs.cmdline.rev[i].flags & UNINTERESTING)
			break;
	if (i == revs.cmdline.nr) {
		ret = error(_("not a '<base>..<tip>' revision range"));
		goto out;
	}

	if (prepare_revision_walk(&revs) < 0) {
		ret = error(_("error preparing revisions"));
		goto out;
	}

	/*
	 * Set boundary commits aside for the base check below, and put every
	 * in-range commit but the tip into the interior set. A ref pointing
	 * at an interior commit would dangle once the range is folded away.
	 */
	while ((commit = get_revision(&revs))) {
		if (commit->object.flags & BOUNDARY) {
			commit_list_insert(commit, &boundaries);
			continue;
		}
		if (!oldest)
			oldest = commit;
		if (tip)
			oidset_insert(interior_out, &tip->object.oid);
		tip = commit;
	}

	if (!oldest) {
		ret = error(_("the revision range is empty"));
		goto out;
	} else if (oldest == tip) {
		ret = error(_("the revision range holds a single commit; "
			      "nothing to squash"));
		goto out;
	} else if (!oldest->parents) {
		BUG("an in-range commit must have a parent");
	}
	base = oldest->parents->item;

	/*
	 * A boundary other than the base is an in-range commit reaching a
	 * commit outside the range, so the range has more than one base.
	 */
	for (b = boundaries; b; b = b->next) {
		if (b->item != base) {
			ret = error(_("the revision range has more than one base; "
				      "cannot squash"));
			goto out;
		}
	}

	*base_out = base;
	*oldest_out = oldest;
	*tip_out = tip;
	ret = 0;

out:
	commit_list_free(boundaries);
	reset_revision_walk();
	release_revisions(&revs);
	strvec_clear(&args);
	return ret;
}

static const char *autosquash_target(const char *subject)
{
	const char *rest;

	while (skip_prefix(subject, "fixup! ", &rest) ||
	       skip_prefix(subject, "squash! ", &rest) ||
	       skip_prefix(subject, "amend! ", &rest))
		subject = rest;
	return subject;
}

static int reject_dangling_fixups(struct repository *repo,
				  struct commit *base,
				  struct commit *tip,
				  struct commit *oldest,
				  struct commit **msg_source,
				  struct commit **amend_source)
{
	struct todo_list todo = TODO_LIST_INIT;
	struct replay_opts opts = REPLAY_OPTS_INIT;
	struct rev_info revs;
	struct commit *commit, *last_amend = NULL;
	struct strvec args = STRVEC_INIT;
	char *dangling_subject = NULL, *dangling_target = NULL;
	bool mixed_target = false, all_fixups_one_target;
	bool past_oldest_group = false;
	int i, ret, nr_dangling = 0;

	*msg_source = oldest;
	*amend_source = NULL;

	repo_init_revisions(repo, &revs, NULL);
	strvec_push(&args, "ignored");
	strvec_push(&args, "--reverse");
	strvec_push(&args, "--topo-order");
	strvec_pushf(&args, "%s..%s", oid_to_hex(&base->object.oid),
		     oid_to_hex(&tip->object.oid));
	setup_revisions_from_strvec(&args, &revs, NULL);

	if (prepare_revision_walk(&revs) < 0) {
		ret = error(_("error preparing revisions"));
		goto out;
	}
	while ((commit = get_revision(&revs)))
		strbuf_addf(&todo.buf, "pick %s\n",
			    oid_to_hex(&commit->object.oid));

	if (todo_list_parse_insn_buffer(repo, &opts, todo.buf.buf, &todo) < 0 ||
	    todo_list_rearrange_squash(&todo) < 0) {
		ret = error(_("could not check the range for fixups"));
		goto out;
	}

	for (i = 0; i < todo.nr; i++) {
		const char *message, *subject_start, *target;
		char *subject;
		size_t sublen;

		message = repo_logmsg_reencode(repo, todo.items[i].commit,
					       NULL, NULL);
		sublen = find_commit_subject(message, &subject_start);

		if (todo.items[i].command != TODO_PICK) {
			if (!past_oldest_group &&
			    starts_with(subject_start, "amend! "))
				*amend_source = todo.items[i].commit;
			repo_unuse_commit_buffer(repo, todo.items[i].commit, message);
			continue;
		}
		if (i)
			past_oldest_group = true;

		subject = xmemdupz(subject_start, sublen);
		target = autosquash_target(subject);
		if (target != subject) {
			nr_dangling++;
			if (!dangling_target) {
				dangling_target = xstrdup(target);
				dangling_subject = xstrdup(subject);
			} else if (strcmp(dangling_target, target)) {
				mixed_target = true;
			}
			if (starts_with(subject, "amend! "))
				last_amend = todo.items[i].commit;
		}
		free(subject);
		repo_unuse_commit_buffer(repo, todo.items[i].commit, message);
	}

	all_fixups_one_target = nr_dangling == todo.nr && !mixed_target;
	if (nr_dangling && !all_fixups_one_target) {
		ret = error(_("cannot squash '%s': its target is not in the "
			      "range"), dangling_subject);
	} else {
		if (last_amend)
			*msg_source = last_amend;
		ret = 0;
	}

out:
	free(dangling_subject);
	free(dangling_target);
	todo_list_release(&todo);
	replay_opts_release(&opts);
	reset_revision_walk();
	release_revisions(&revs);
	strvec_clear(&args);
	return ret;
}

struct interior_ref_cb {
	const struct oidset *interior;
	const char *name;
};

static int find_interior_ref(const struct reference *ref, void *cb_data)
{
	struct interior_ref_cb *data = cb_data;

	if (oidset_contains(data->interior, ref->oid)) {
		data->name = xstrdup(ref->name);
		return 1;
	}

	return 0;
}

static bool amend_replaces_target(struct todo_list *todo, int target)
{
	int i;

	for (i = target + 1; i < todo->nr &&
			     todo->items[i].command != TODO_PICK; i++) {
		if (todo->items[i].command == TODO_SQUASH)
			return false;
		if (todo->items[i].flags & TODO_REPLACE_FIXUP_MSG)
			return true;
	}
	return false;
}

static int build_squash_message(struct repository *repo,
				struct commit *base,
				struct commit *tip,
				struct strbuf *out)
{
	struct rev_info revs;
	struct commit *commit;
	struct strvec args = STRVEC_INIT;
	struct todo_list todo = TODO_LIST_INIT;
	struct replay_opts opts = REPLAY_OPTS_INIT;
	int i, nr_commits, ret;

	repo_init_revisions(repo, &revs, NULL);
	strvec_push(&args, "ignored");
	strvec_push(&args, "--reverse");
	strvec_push(&args, "--topo-order");
	strvec_pushf(&args, "%s..%s", oid_to_hex(&base->object.oid),
		     oid_to_hex(&tip->object.oid));
	setup_revisions_from_strvec(&args, &revs, NULL);

	if (prepare_revision_walk(&revs) < 0) {
		ret = error(_("error preparing revisions"));
		goto out;
	}

	while ((commit = get_revision(&revs)))
		strbuf_addf(&todo.buf, "pick %s\n",
			    oid_to_hex(&commit->object.oid));

	if (todo_list_parse_insn_buffer(repo, &opts, todo.buf.buf, &todo) < 0 ||
	    todo_list_rearrange_squash(&todo) < 0) {
		ret = error(_("could not prepare the squash message"));
		goto out;
	}

	nr_commits = todo.nr;
	for (i = 0; i < nr_commits; i++) {
		struct todo_item *item = &todo.items[i];
		const char *message, *body;
		size_t commented_len;
		bool skip, squashing;

		squashing = item->command == TODO_SQUASH ||
			    (item->flags & TODO_REPLACE_FIXUP_MSG);
		if (item->command == TODO_PICK)
			skip = amend_replaces_target(&todo, i);
		else
			skip = !squashing;

		message = repo_logmsg_reencode(repo, item->commit, NULL, NULL);
		find_commit_subject(message, &body);

		if (skip)
			commented_len = strlen(body);
		else if (squashing)
			commented_len = squash_subject_comment_len(body, 1);
		else
			commented_len = 0;

		if (!i)
			add_squash_combination_header(out, nr_commits);
		strbuf_addch(out, '\n');
		add_squash_message_header(out, i + 1, skip);
		strbuf_addstr(out, "\n\n");
		strbuf_add_commented_lines(out, body, commented_len, comment_line_str);
		strbuf_addstr(out, body + commented_len);
		strbuf_complete_line(out);

		repo_unuse_commit_buffer(repo, item->commit, message);
	}

	ret = 0;

out:
	todo_list_release(&todo);
	replay_opts_release(&opts);
	reset_revision_walk();
	release_revisions(&revs);
	strvec_clear(&args);
	return ret;
}

static int cmd_history_squash(int argc,
			      const char **argv,
			      const char *prefix,
			      struct repository *repo)
{
	const char * const usage[] = {
		GIT_HISTORY_SQUASH_USAGE,
		NULL,
	};
	enum ref_action action = REF_ACTION_DEFAULT;
	enum commit_tree_flags flags = 0;
	int dry_run = 0;
	struct option options[] = {
		OPT_CALLBACK_F(0, "update-refs", &action, "(branches|head)",
			       N_("control which refs should be updated"),
			       PARSE_OPT_NONEG, parse_ref_action),
		OPT_BOOL('n', "dry-run", &dry_run,
			 N_("perform a dry-run without updating any refs")),
		OPT_BIT(0, "reedit-message", &flags,
			N_("open an editor to modify the commit message"),
			COMMIT_TREE_EDIT_MESSAGE),
		OPT_END(),
	};
	struct strbuf reflog_msg = STRBUF_INIT;
	struct strbuf message = STRBUF_INIT;
	struct oidset interior = OIDSET_INIT;
	struct commit *base, *oldest, *tip, *rewritten, *msg_source,
		*amend_source;
	const struct object_id *base_tree_oid, *tip_tree_oid;
	const char *message_template = NULL;
	struct commit_list *parents = NULL;
	struct rev_info revs = { 0 };
	int ret;

	argc = parse_options(argc, argv, prefix, options, usage,
			     PARSE_OPT_KEEP_UNKNOWN_OPT);
	if (!argc) {
		ret = error(_("command expects a revision range"));
		goto out;
	}
	repo_config(repo, git_default_config, NULL);

	if (action == REF_ACTION_DEFAULT)
		action = REF_ACTION_BRANCHES;

	ret = resolve_squash_range(repo, argv, &base, &oldest, &tip,
				   &interior);
	if (ret < 0)
		goto out;

	ret = reject_dangling_fixups(repo, base, tip, oldest, &msg_source,
				     &amend_source);
	if (ret < 0)
		goto out;
	if (amend_source) {
		const char *amend_message, *body;

		amend_message = repo_logmsg_reencode(repo, amend_source,
						     NULL, NULL);
		find_commit_subject(amend_message, &body);
		body = skip_blank_lines(body + commit_subject_length(body));
		strbuf_addstr(&message, body);
		message_template = message.buf;
		repo_unuse_commit_buffer(repo, amend_source, amend_message);
	}

	if (action == REF_ACTION_BRANCHES) {
		struct interior_ref_cb cb = { .interior = &interior };

		refs_for_each_ref(get_main_ref_store(repo),
				  find_interior_ref, &cb);
		if (cb.name) {
			ret = error(_("'%s' points into the squashed range"),
				    cb.name);
			advise_if_enabled(ADVICE_HISTORY_UPDATE_REFS,
					  _("Use --update-refs=head to rewrite only "
					    "the current branch and leave such refs "
					    "untouched."));
			free((char *)cb.name);
			goto out;
		}
	}

	if (flags & COMMIT_TREE_EDIT_MESSAGE) {
		strbuf_reset(&message);
		ret = build_squash_message(repo, base, tip, &message);
		if (ret < 0)
			goto out;
		message_template = message.buf;
	}

	ret = setup_revwalk(repo, action, tip, &revs);
	if (ret < 0)
		goto out;

	base_tree_oid = &repo_get_commit_tree(repo, base)->object.oid;
	tip_tree_oid = &repo_get_commit_tree(repo, tip)->object.oid;
	commit_list_append(base, &parents);

	ret = commit_tree_ext(repo, "squash", msg_source, message_template,
			      parents,
			      base_tree_oid, tip_tree_oid, &rewritten, flags);
	if (ret < 0) {
		ret = error(_("failed writing squashed commit"));
		goto out;
	}

	strbuf_addstr(&reflog_msg, "squash: updating ");
	strbuf_join_argv(&reflog_msg, argc, argv, ' ');

	ret = handle_reference_updates(&revs, action, tip, rewritten,
				       reflog_msg.buf, dry_run,
				       REPLAY_EMPTY_COMMIT_ABORT);
	if (ret < 0) {
		ret = error(_("failed replaying descendants"));
		goto out;
	}

	ret = 0;

out:
	strbuf_release(&reflog_msg);
	strbuf_release(&message);
	oidset_clear(&interior);
	commit_list_free(parents);
	release_revisions(&revs);
	return ret;
}

static int update_worktree(struct repository *repo,
			   const struct commit *old_head,
			   const struct commit *new_head,
			   bool dry_run)
{
	struct reset_working_tree_options opts = {
		.oid_from = &old_head->object.oid,
		.oid = &new_head->object.oid,
	};
	if (dry_run)
		opts.flags |= RESET_WORKING_TREE_DRY_RUN;
	return reset_working_tree(repo, &opts);
}

static int find_head_tree_change(struct repository *repo,
				 const struct replay_result *result,
				 struct commit **old_head,
				 struct commit **new_head,
				 bool *changed)
{
	const struct replay_ref_update *head_update = NULL;
	struct commit *old_head_commit, *new_head_commit;
	struct tree *old_head_tree, *new_head_tree;
	const char *head_target;
	int head_flags;

	*changed = false;

	head_target = refs_resolve_ref_unsafe(get_main_ref_store(repo), "HEAD",
					      RESOLVE_REF_NO_RECURSE | RESOLVE_REF_READING,
					      NULL, &head_flags);
	if (!head_target)
		return error(_("cannot look up HEAD"));

	for (size_t i = 0; i < result->updates_nr; i++) {
		if (!strcmp(result->updates[i].refname, head_target)) {
			head_update = &result->updates[i];
			break;
		}
	}

	if (!head_update)
		return 0;

	old_head_commit = lookup_commit_reference(repo, &head_update->old_oid);
	new_head_commit = lookup_commit_reference(repo, &head_update->new_oid);
	if (!old_head_commit || !new_head_commit)
		return error(_("cannot resolve HEAD commit"));

	old_head_tree = repo_get_commit_tree(repo, old_head_commit);
	new_head_tree = repo_get_commit_tree(repo, new_head_commit);
	if (!old_head_tree || !new_head_tree)
		return error(_("cannot resolve tree for HEAD"));

	if (oideq(&old_head_tree->object.oid, &new_head_tree->object.oid))
		return 0;

	*old_head = old_head_commit;
	*new_head = new_head_commit;
	*changed = true;

	return 0;
}

static int cmd_history_drop(int argc,
			    const char **argv,
			    const char *prefix,
			    struct repository *repo)
{
	const char * const usage[] = {
		GIT_HISTORY_DROP_USAGE,
		NULL,
	};
	enum replay_empty_commit_action empty = REPLAY_EMPTY_COMMIT_DROP;
	enum ref_action action = REF_ACTION_DEFAULT;
	int dry_run = 0;
	struct option options[] = {
		OPT_CALLBACK_F(0, "update-refs", &action, "(branches|head)",
			       N_("control which refs should be updated"),
			       PARSE_OPT_NONEG, parse_ref_action),
		OPT_BOOL('n', "dry-run", &dry_run,
			 N_("perform a dry-run without updating any refs")),
		OPT_CALLBACK_F(0, "empty", &empty, "(drop|keep|abort)",
			       N_("how to handle descendants that become empty"),
			       PARSE_OPT_NONEG, parse_opt_empty),
		OPT_END(),
	};
	struct strbuf reflog_msg = STRBUF_INIT;
	struct commit *original, *rewritten;
	struct rev_info revs = { 0 };
	struct replay_result result = { 0 };
	struct commit *old_head, *new_head;
	bool head_moves = false;
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

	if (!original->parents) {
		ret = error(_("cannot drop root commit %s: "
			      "it has no parent to replay onto"),
			    argv[0]);
		goto out;
	} else if (original->parents->next) {
		ret = error(_("cannot drop merge commit: %s"), argv[0]);
		goto out;
	}

	ret = setup_revwalk(repo, action, original, &revs);
	if (ret)
		goto out;

	rewritten = original->parents->item;

	ret = compute_pending_ref_updates(&revs, action, original, rewritten,
					  empty, &result);
	if (ret) {
		ret = error(_("failed replaying descendants"));
		goto out;
	}

	/*
	 * If HEAD will move as a result of the rewrite then we'll have to
	 * merge in the changes into the worktree and index. This merge can of
	 * course conflict, which will cause the whole operation to abort.
	 *
	 * If we had already updated the refs at that point then we'd have an
	 * inconsistent repository state. So we first perform a dry-run merge
	 * here before updating refs.
	 */
	if (!is_bare_repository(repo)) {
		ret = find_head_tree_change(repo, &result, &old_head,
					    &new_head, &head_moves);
		if (ret < 0)
			goto out;

		if (head_moves && update_worktree(repo, old_head, new_head, true) < 0) {
			ret = error(_("dropping this commit would "
				      "overwrite local changes; aborting"));
			goto out;
		}
	}

	strbuf_addf(&reflog_msg, "drop: dropping %s", argv[0]);
	ret = apply_pending_ref_updates(repo, &result, reflog_msg.buf, dry_run);
	if (ret < 0) {
		ret = error(_("failed to update references"));
		goto out;
	}

	if (!dry_run && head_moves && update_worktree(repo, old_head, new_head, false) < 0) {
		ret = error(_("could not update working tree to new commit %s"),
			    oid_to_hex(&new_head->object.oid));
		goto out;
	}

	ret = 0;

out:
	replay_result_release(&result);
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
		GIT_HISTORY_DROP_USAGE,
		GIT_HISTORY_FIXUP_USAGE,
		GIT_HISTORY_REWORD_USAGE,
		GIT_HISTORY_SPLIT_USAGE,
		GIT_HISTORY_SQUASH_USAGE,
		NULL,
	};
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("drop", &fn, cmd_history_drop),
		OPT_SUBCOMMAND("fixup", &fn, cmd_history_fixup),
		OPT_SUBCOMMAND("reword", &fn, cmd_history_reword),
		OPT_SUBCOMMAND("split", &fn, cmd_history_split),
		OPT_SUBCOMMAND("squash", &fn, cmd_history_squash),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	return fn(argc, argv, prefix, repo);
}
