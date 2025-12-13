#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "cache-tree.h"
#include "commit-reach.h"
#include "commit.h"
#include "config.h"
#include "editor.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "oidmap.h"
#include "parse-options.h"
#include "path.h"
#include "read-cache.h"
#include "refs.h"
#include "replay.h"
#include "reset.h"
#include "revision.h"
#include "run-command.h"
#include "sequencer.h"
#include "strvec.h"
#include "tree.h"
#include "wt-status.h"

#define GIT_HISTORY_REWORD_USAGE N_("git history reword <commit>")
#define GIT_HISTORY_SPLIT_USAGE  N_("git history split <commit> [--] [<pathspec>...]")

static int collect_commits(struct repository *repo,
			   struct commit *old_commit,
			   struct commit *new_commit,
			   struct strvec *out)
{
	struct setup_revision_opt revision_opts = {
		.assume_dashdash = 1,
	};
	struct strvec revisions = STRVEC_INIT;
	struct commit *child;
	struct rev_info rev = { 0 };
	int ret;

	repo_init_revisions(repo, &rev, NULL);
	rev.reverse = 1;
	strvec_push(&revisions, "");
	strvec_push(&revisions, oid_to_hex(&new_commit->object.oid));
	if (old_commit) {
		strvec_pushf(&revisions, "^%s", oid_to_hex(&old_commit->object.oid));
		strvec_pushf(&revisions, "--ancestry-path=%s", oid_to_hex(&old_commit->object.oid));
	}

	setup_revisions_from_strvec(&revisions, &rev, &revision_opts);
	if (revisions.nr != 1 || prepare_revision_walk(&rev)) {
		ret = error(_("revision walk setup failed"));
		goto out;
	}

	while ((child = get_revision(&rev))) {
		if (old_commit && !child->parents)
			BUG("revision walk did not find child commit");
		if (child->parents && child->parents->next) {
			ret = error(_("cannot rearrange commit history with merges"));
			goto out;
		}

		strvec_push(out, oid_to_hex(&child->object.oid));
	}

	ret = 0;

out:
	strvec_clear(&revisions);
	release_revisions(&rev);
	reset_revision_walk();
	return ret;
}

static int gather_commits_between_head_and_revision(struct repository *repo,
						    const char *revision,
						    struct commit **original_commit,
						    struct commit **parent_commit,
						    struct commit **head,
						    struct strvec *commits)
{
	struct commit_list *from_list = NULL;
	int ret;

	*original_commit = lookup_commit_reference_by_name(revision);
	if (!*original_commit) {
		ret = error(_("commit cannot be found: %s"), revision);
		goto out;
	}

	*parent_commit = (*original_commit)->parents ? (*original_commit)->parents->item : NULL;
	if (*parent_commit && repo_parse_commit(repo, *parent_commit)) {
		ret = error(_("unable to parse commit %s"),
			    oid_to_hex(&(*parent_commit)->object.oid));
		goto out;
	}

	*head = lookup_commit_reference_by_name("HEAD");
	if (!(*head)) {
		ret = error(_("could not resolve HEAD to a commit"));
		goto out;
	}

	commit_list_append(*original_commit, &from_list);
	if (!repo_is_descendant_of(repo, *head, from_list)) {
		ret = error(_("commit must be reachable from current HEAD commit"));
		goto out;
	}

	/*
	 * Collect the list of commits that we'll have to reapply now already.
	 * This ensures that we'll abort early on in case the range of commits
	 * contains merges, which we do not yet handle.
	 */
	ret = collect_commits(repo, *parent_commit, *head, commits);
	if (ret < 0)
		goto out;

out:
	free_commit_list(from_list);
	return ret;
}

static void replace_commits(struct strvec *commits,
			    const struct object_id *commit_to_replace,
			    const struct object_id *replacements,
			    size_t replacements_nr)
{
	char commit_to_replace_oid[GIT_MAX_HEXSZ + 1];
	struct strvec replacement_oids = STRVEC_INIT;
	bool found = false;

	oid_to_hex_r(commit_to_replace_oid, commit_to_replace);
	for (size_t i = 0; i < replacements_nr; i++)
		strvec_push(&replacement_oids, oid_to_hex(&replacements[i]));

	for (size_t i = 0; i < commits->nr; i++) {
		if (strcmp(commits->v[i], commit_to_replace_oid))
			continue;
		strvec_splice(commits, i, 1, replacement_oids.v, replacement_oids.nr);
		found = true;
		break;
	}
	if (!found)
		BUG("could not find commit to replace");

	strvec_clear(&replacement_oids);
}

static int apply_commits(struct repository *repo,
			 const struct strvec *commits,
			 struct commit *onto,
			 struct commit *orig_head,
			 const char *action)
{
	struct reset_head_opts reset_opts = { 0 };
	struct strbuf buf = STRBUF_INIT;
	int ret;

	for (size_t i = 0; i < commits->nr; i++) {
		struct object_id commit_id;
		struct commit *commit;
		const char *end;

		if (parse_oid_hex_algop(commits->v[i], &commit_id, &end,
					repo->hash_algo)) {
			ret = error(_("invalid object ID: %s"), commits->v[i]);
			goto out;
		}

		commit = lookup_commit(repo, &commit_id);
		if (!commit || repo_parse_commit(repo, commit)) {
			ret = error(_("failed to look up commit: %s"), oid_to_hex(&commit_id));
			goto out;
		}

		if (!onto) {
			onto = commit;
		} else {
			struct tree *tree = repo_get_commit_tree(repo, commit);
			onto = replay_create_commit(repo, tree, commit, onto);
			if (!onto) {
				ret = -1;
				goto out;
			}
		}
	}

	reset_opts.oid = &onto->object.oid;
	strbuf_addf(&buf, "%s: switch to rewritten %s", action, oid_to_hex(reset_opts.oid));
	reset_opts.flags = RESET_HEAD_REFS_ONLY | RESET_ORIG_HEAD;
	reset_opts.orig_head = &orig_head->object.oid;
	reset_opts.default_reflog_action = action;
	if (reset_head(repo, &reset_opts) < 0) {
		ret = error(_("could not switch to %s"), oid_to_hex(reset_opts.oid));
		goto out;
	}

	ret = 0;

out:
	strbuf_release(&buf);
	return ret;
}

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
					   struct commit *original_commit,
					   const struct object_id *new_tree_oid,
					   const struct commit_list *parents,
					   const struct object_id *parent_tree_oid,
					   struct object_id *out)
{
	const char *exclude_gpgsig[] = { "gpgsig", "gpgsig-sha256", NULL };
	const char *original_message, *original_body, *ptr;
	struct commit_extra_header *original_extra_headers = NULL;
	struct strbuf commit_message = STRBUF_INIT;
	char *original_author = NULL;
	size_t len;
	int ret;

	/* We retain authorship of the original commit. */
	original_message = repo_logmsg_reencode(repo, original_commit, NULL, NULL);
	ptr = find_commit_header(original_message, "author", &len);
	if (ptr)
		original_author = xmemdupz(ptr, len);
	find_commit_subject(original_message, &original_body);

	ret = fill_commit_message(repo, parent_tree_oid, new_tree_oid,
				  original_body, action, &commit_message);
	if (ret < 0)
		goto out;

	original_extra_headers = read_commit_extra_headers(original_commit, exclude_gpgsig);

	ret = commit_tree_extended(commit_message.buf, commit_message.len, new_tree_oid,
				   parents, out, original_author, NULL, NULL,
				   original_extra_headers);
	if (ret < 0)
		goto out;

out:
	free_commit_extra_headers(original_extra_headers);
	strbuf_release(&commit_message);
	free(original_author);
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
	struct option options[] = {
		OPT_END(),
	};
	struct commit *original_commit, *parent, *head;
	struct strvec commits = STRVEC_INIT;
	struct object_id parent_tree_oid, original_commit_tree_oid;
	struct object_id rewritten_commit;
	int ret;

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	if (argc != 1) {
		ret = error(_("command expects a single revision"));
		goto out;
	}
	repo_config(repo, git_default_config, NULL);

	ret = gather_commits_between_head_and_revision(repo, argv[0], &original_commit,
						       &parent, &head, &commits);
	if (ret < 0)
		goto out;

	original_commit_tree_oid = repo_get_commit_tree(repo, original_commit)->object.oid;
	if (parent)
		parent_tree_oid = repo_get_commit_tree(repo, parent)->object.oid;
	else
		oidcpy(&parent_tree_oid, repo->hash_algo->empty_tree);

	/* We retain authorship of the original commit. */
	ret = commit_tree_with_edited_message(repo, "reworded", original_commit,
					      &original_commit_tree_oid,
					      original_commit->parents, &parent_tree_oid,
					      &rewritten_commit);
	if (ret < 0) {
		ret = error(_("failed writing reworded commit"));
		goto out;
	}

	replace_commits(&commits, &original_commit->object.oid, &rewritten_commit, 1);

	ret = apply_commits(repo, &commits, parent, head, "reword");
	if (ret < 0)
		goto out;

	ret = 0;

out:
	strvec_clear(&commits);
	return ret;
}

static int split_commit(struct repository *repo,
			struct commit *original_commit,
			struct pathspec *pathspec,
			struct object_id *out)
{
	struct interactive_options interactive_opts = INTERACTIVE_OPTIONS_INIT;
	struct strbuf index_file = STRBUF_INIT;
	struct child_process read_tree_cmd = CHILD_PROCESS_INIT;
	struct index_state index = INDEX_STATE_INIT(repo);
	struct object_id original_commit_tree_oid, parent_tree_oid;
	char original_commit_oid[GIT_MAX_HEXSZ + 1];
	struct commit_list *parents = NULL;
	struct commit *first_commit;
	struct tree *split_tree;
	int ret;

	if (original_commit->parents)
		parent_tree_oid = *get_commit_tree_oid(original_commit->parents->item);
	else
		oidcpy(&parent_tree_oid, repo->hash_algo->empty_tree);
	original_commit_tree_oid = *get_commit_tree_oid(original_commit);

	/*
	* Construct the first commit. This is done by taking the original
	* commit parent's tree and selectively patching changes from the diff
	* between that parent and its child.
	*/
	repo_git_path_replace(repo, &index_file, "%s", "history-split.index");

	read_tree_cmd.git_cmd = 1;
	strvec_pushf(&read_tree_cmd.env, "GIT_INDEX_FILE=%s", index_file.buf);
	strvec_push(&read_tree_cmd.args, "read-tree");
	strvec_push(&read_tree_cmd.args, oid_to_hex(&parent_tree_oid));
	ret = run_command(&read_tree_cmd);
	if (ret < 0)
		goto out;

	ret = read_index_from(&index, index_file.buf, repo->gitdir);
	if (ret < 0) {
		ret = error(_("failed reading temporary index"));
		goto out;
	}

	oid_to_hex_r(original_commit_oid, &original_commit->object.oid);
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

	/*
	* We disallow the cases where either the split-out commit or the
	* original commit would become empty. Consequently, if we see that the
	* new tree ID matches either of those trees we abort.
	*/
	if (oideq(&split_tree->object.oid, &parent_tree_oid)) {
		ret = error(_("split commit is empty"));
		goto out;
	} else if (oideq(&split_tree->object.oid, &original_commit_tree_oid)) {
		ret = error(_("split commit tree matches original commit"));
		goto out;
	}

	/*
	 * The first commit is constructed from the split-out tree. The base
	 * that shall be diffed against is the parent of the original commit.
	 */
	ret = commit_tree_with_edited_message(repo, "split-out", original_commit,
					      &split_tree->object.oid,
					      original_commit->parents, &parent_tree_oid, &out[0]);
	if (ret < 0) {
		ret = error(_("failed writing split-out commit"));
		goto out;
	}

	/*
	* The second commit is constructed from the original tree. The base to
	* diff against and the parent in this case is the first split-out
	* commit.
	*/
	first_commit = lookup_commit_reference(repo, &out[0]);
	commit_list_append(first_commit, &parents);

	ret = commit_tree_with_edited_message(repo, "split-out", original_commit,
					      &original_commit_tree_oid,
					      parents, get_commit_tree_oid(first_commit), &out[1]);
	if (ret < 0) {
		ret = error(_("failed writing split-out commit"));
		goto out;
	}

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
	struct option options[] = {
		OPT_END(),
	};
	struct commit *original_commit, *parent, *head;
	struct strvec commits = STRVEC_INIT;
	struct object_id split_commits[2];
	struct pathspec pathspec = { 0 };
	int ret;

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	if (argc < 1) {
		ret = error(_("command expects a revision"));
		goto out;
	}
	repo_config(repo, git_default_config, NULL);

	parse_pathspec(&pathspec, 0,
		PATHSPEC_PREFER_FULL | PATHSPEC_SYMLINK_LEADING_PATH | PATHSPEC_PREFIX_ORIGIN,
		prefix, argv + 1);

	ret = gather_commits_between_head_and_revision(repo, argv[0], &original_commit,
						       &parent, &head, &commits);
	if (ret < 0)
		goto out;

	/*
	 * Then we split up the commit and replace the original commit with the
	 * new ones.
	 */
	ret = split_commit(repo, original_commit, &pathspec, split_commits);
	if (ret < 0)
		goto out;

	replace_commits(&commits, &original_commit->object.oid,
			split_commits, ARRAY_SIZE(split_commits));

	ret = apply_commits(repo, &commits, parent, head, "split");
	if (ret < 0)
		goto out;

	ret = 0;

out:
	clear_pathspec(&pathspec);
	strvec_clear(&commits);
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
