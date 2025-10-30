#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "commit-reach.h"
#include "commit.h"
#include "config.h"
#include "editor.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "parse-options.h"
#include "refs.h"
#include "replay.h"
#include "reset.h"
#include "revision.h"
#include "sequencer.h"
#include "strvec.h"
#include "tree.h"
#include "wt-status.h"

#define GIT_HISTORY_REWORD_USAGE N_("git history reword <commit>")

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
	strvec_push(&revisions, "");
	strvec_push(&revisions, oid_to_hex(&new_commit->object.oid));
	if (old_commit)
		strvec_pushf(&revisions, "^%s", oid_to_hex(&old_commit->object.oid));

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

		if (child->parents && old_commit &&
		    commit_list_contains(old_commit, child->parents))
			break;
	}

	/*
	 * Revisions are in newest-order-first. We have to reverse the
	 * array though so that we pick the oldest commits first.
	 */
	for (size_t i = 0, j = out->nr - 1; i < j; i++, j--)
		SWAP(out->v[i], out->v[j]);

	ret = 0;

out:
	strvec_clear(&revisions);
	release_revisions(&rev);
	reset_revision_walk();
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
			if (!onto)
				break;
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
		  " Lines starting\nwith '%s' will be ignored.\n");
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
		fprintf(stderr, _("Please supply the message using the -m option.\n"));
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
	struct strbuf final_message = STRBUF_INIT;
	struct commit *original_commit, *parent, *head;
	struct strvec commits = STRVEC_INIT;
	struct object_id parent_tree_oid, original_commit_tree_oid;
	struct object_id rewritten_commit;
	struct commit_list *from_list = NULL;
	const char *original_message, *original_body, *ptr;
	char *original_author = NULL;
	size_t len;
	int ret;

	argc = parse_options(argc, argv, prefix, options, usage, 0);
	if (argc != 1) {
		ret = error(_("command expects a single revision"));
		goto out;
	}
	repo_config(repo, git_default_config, NULL);

	original_commit = lookup_commit_reference_by_name(argv[0]);
	if (!original_commit) {
		ret = error(_("commit to be reworded cannot be found: %s"), argv[0]);
		goto out;
	}
	original_commit_tree_oid = repo_get_commit_tree(repo, original_commit)->object.oid;

	parent = original_commit->parents ? original_commit->parents->item : NULL;
	if (parent) {
		if (repo_parse_commit(repo, parent)) {
			ret = error(_("unable to parse commit %s"),
				    oid_to_hex(&parent->object.oid));
			goto out;
		}
		parent_tree_oid = repo_get_commit_tree(repo, parent)->object.oid;
	} else {
		oidcpy(&parent_tree_oid, repo->hash_algo->empty_tree);
	}

	head = lookup_commit_reference_by_name("HEAD");
	if (!head) {
		ret = error(_("could not resolve HEAD to a commit"));
		goto out;
	}

	commit_list_append(original_commit, &from_list);
	if (!repo_is_descendant_of(repo, head, from_list)) {
		ret = error (_("split commit must be reachable from current HEAD commit"));
		goto out;
	}

	/*
	 * Collect the list of commits that we'll have to reapply now already.
	 * This ensures that we'll abort early on in case the range of commits
	 * contains merges, which we do not yet handle.
	 */
	ret = collect_commits(repo, parent, head, &commits);
	if (ret < 0)
		goto out;

	/* We retain authorship of the original commit. */
	original_message = repo_logmsg_reencode(repo, original_commit, NULL, NULL);
	ptr = find_commit_header(original_message, "author", &len);
	if (ptr)
		original_author = xmemdupz(ptr, len);
	find_commit_subject(original_message, &original_body);

	ret = fill_commit_message(repo, &parent_tree_oid, &original_commit_tree_oid,
				  original_body, "reworded", &final_message);
	if (ret < 0)
		goto out;

	ret = commit_tree(final_message.buf, final_message.len, &original_commit_tree_oid,
			  original_commit->parents, &rewritten_commit, original_author, NULL);
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
	strbuf_release(&final_message);
	free_commit_list(from_list);
	strvec_clear(&commits);
	free(original_author);
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
