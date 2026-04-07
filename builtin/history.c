#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "cache-tree.h"
#include "commit.h"
#include "commit-reach.h"
#include "config.h"
#include "editor.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "oidmap.h"
#include "parse-options.h"
#include "path.h"
#include "read-cache.h"
#include "refs.h"
#include "replay.h"
#include "revision.h"
#include "sequencer.h"
#include "strvec.h"
#include "tree.h"
#include "unpack-trees.h"
#include "wt-status.h"

#define GIT_HISTORY_REWORD_USAGE \
	N_("git history reword <commit> [--dry-run] [--update-refs=(branches|head)]")
#define GIT_HISTORY_SPLIT_USAGE \
	N_("git history split <commit> [--dry-run] [--update-refs=(branches|head)] [--] [<pathspec>...]")

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

static int commit_tree_with_edited_message_ext(struct repository *repo,
					       const char *action,
					       struct commit *commit_with_message,
					       const struct commit_list *parents,
					       const struct object_id *old_tree,
					       const struct object_id *new_tree,
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
	char *original_author = NULL;
	size_t len;
	int ret;

	/* We retain authorship of the original commit. */
	original_message = repo_logmsg_reencode(repo, commit_with_message, NULL, NULL);
	ptr = find_commit_header(original_message, "author", &len);
	if (ptr)
		original_author = xmemdupz(ptr, len);
	find_commit_subject(original_message, &original_body);

	ret = fill_commit_message(repo, old_tree, new_tree,
				  original_body, action, &commit_message);
	if (ret < 0)
		goto out;

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

static int commit_tree_with_edited_message(struct repository *repo,
					   const char *action,
					   struct commit *original,
					   struct commit **out)
{
	struct object_id parent_tree_oid;
	const struct object_id *tree_oid;
	struct commit *parent;

	tree_oid = &repo_get_commit_tree(repo, original)->object.oid;

	parent = original->parents ? original->parents->item : NULL;
	if (parent) {
		if (repo_parse_commit(repo, parent)) {
			return error(_("unable to parse parent commit %s"),
				     oid_to_hex(&parent->object.oid));
		}

		parent_tree_oid = repo_get_commit_tree(repo, parent)->object.oid;
	} else {
		oidcpy(&parent_tree_oid, repo->hash_algo->empty_tree);
	}

	return commit_tree_with_edited_message_ext(repo, action, original, original->parents,
						   &parent_tree_oid, tree_oid, out);
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
		free_commit_list(from_list);

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

static int handle_reference_updates(struct rev_info *revs,
				    enum ref_action action,
				    struct commit *original,
				    struct commit *rewritten,
				    const char *reflog_msg,
				    int dry_run)
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

	if (action != REF_ACTION_BRANCHES && action != REF_ACTION_HEAD)
		BUG("unsupported ref action %d", action);

	if (!dry_run) {
		transaction = ref_store_transaction_begin(get_main_ref_store(revs->repo), 0, &err);
		if (!transaction) {
			ret = error(_("failed to begin ref transaction: %s"), err.buf);
			goto out;
		}
	}

	for (size_t i = 0; i < result.updates_nr; i++) {
		ret = handle_ref_update(transaction,
					result.updates[i].refname,
					&result.updates[i].new_oid,
					&result.updates[i].old_oid,
					reflog_msg, &err);
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

		ret = handle_ref_update(transaction,
					decoration->name,
					&rewritten->object.oid,
					&original->object.oid,
					reflog_msg, &err);
		if (ret) {
			ret = error(_("failed to update ref '%s': %s"),
				    decoration->name, err.buf);
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
				       reflog_msg.buf, dry_run);
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

	if (hold_lock_file_for_update(&lock, path, 0) < 0) {
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

	if (original->parents) {
		if (repo_parse_commit(repo, original->parents->item)) {
			ret = error(_("unable to parse parent commit %s"),
				    oid_to_hex(&original->parents->item->object.oid));
			goto out;
		}

		parent_tree_oid = *get_commit_tree_oid(original->parents->item);
	} else {
		oidcpy(&parent_tree_oid, repo->hash_algo->empty_tree);
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
	ret = commit_tree_with_edited_message_ext(repo, "split-out", original,
						  original->parents, &parent_tree_oid,
						  &split_tree->object.oid, &first_commit);
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

	ret = commit_tree_with_edited_message_ext(repo, "split-out", original,
						  parents, old_tree_oid,
						  new_tree_oid, &second_commit);
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
	free_commit_list(parents);
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
				       reflog_msg.buf, dry_run);
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

int cmd_history(int argc,
		const char **argv,
		const char *prefix,
		struct repository *repo)
{
	const char * const usage[] = {
		GIT_HISTORY_REWORD_USAGE,
		GIT_HISTORY_SPLIT_USAGE,
		NULL,
	};
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("reword", &fn, cmd_history_reword),
		OPT_SUBCOMMAND("split", &fn, cmd_history_split),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	return fn(argc, argv, prefix, repo);
}
