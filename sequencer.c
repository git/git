#include "cache.h"
#include "alloc.h"
#include "config.h"
#include "hex.h"
#include "lockfile.h"
#include "dir.h"
#include "object-store.h"
#include "object.h"
#include "commit.h"
#include "sequencer.h"
#include "tag.h"
#include "run-command.h"
#include "hook.h"
#include "exec-cmd.h"
#include "utf8.h"
#include "cache-tree.h"
#include "diff.h"
#include "revision.h"
#include "rerere.h"
#include "merge-ort.h"
#include "merge-ort-wrappers.h"
#include "refs.h"
#include "strvec.h"
#include "quote.h"
#include "trailer.h"
#include "log-tree.h"
#include "wt-status.h"
#include "hashmap.h"
#include "notes-utils.h"
#include "sigchain.h"
#include "unpack-trees.h"
#include "worktree.h"
#include "oidmap.h"
#include "oidset.h"
#include "commit-slab.h"
#include "alias.h"
#include "commit-reach.h"
#include "rebase-interactive.h"
#include "reset.h"
#include "branch.h"

#define GIT_REFLOG_ACTION "GIT_REFLOG_ACTION"

static const char sign_off_header[] = "Signed-off-by: ";
static const char cherry_picked_prefix[] = "(cherry picked from commit ";

GIT_PATH_FUNC(git_path_commit_editmsg, "COMMIT_EDITMSG")

static GIT_PATH_FUNC(git_path_seq_dir, "sequencer")

static GIT_PATH_FUNC(git_path_todo_file, "sequencer/todo")
static GIT_PATH_FUNC(git_path_opts_file, "sequencer/opts")
static GIT_PATH_FUNC(git_path_head_file, "sequencer/head")
static GIT_PATH_FUNC(git_path_abort_safety_file, "sequencer/abort-safety")

static GIT_PATH_FUNC(rebase_path, "rebase-merge")
/*
 * The file containing rebase commands, comments, and empty lines.
 * This file is created by "git rebase -i" then edited by the user. As
 * the lines are processed, they are removed from the front of this
 * file and written to the tail of 'done'.
 */
GIT_PATH_FUNC(rebase_path_todo, "rebase-merge/git-rebase-todo")
GIT_PATH_FUNC(rebase_path_todo_backup, "rebase-merge/git-rebase-todo.backup")

GIT_PATH_FUNC(rebase_path_dropped, "rebase-merge/dropped")

/*
 * The rebase command lines that have already been processed. A line
 * is moved here when it is first handled, before any associated user
 * actions.
 */
static GIT_PATH_FUNC(rebase_path_done, "rebase-merge/done")
/*
 * The file to keep track of how many commands were already processed (e.g.
 * for the prompt).
 */
static GIT_PATH_FUNC(rebase_path_msgnum, "rebase-merge/msgnum")
/*
 * The file to keep track of how many commands are to be processed in total
 * (e.g. for the prompt).
 */
static GIT_PATH_FUNC(rebase_path_msgtotal, "rebase-merge/end")
/*
 * The commit message that is planned to be used for any changes that
 * need to be committed following a user interaction.
 */
static GIT_PATH_FUNC(rebase_path_message, "rebase-merge/message")
/*
 * The file into which is accumulated the suggested commit message for
 * squash/fixup commands. When the first of a series of squash/fixups
 * is seen, the file is created and the commit message from the
 * previous commit and from the first squash/fixup commit are written
 * to it. The commit message for each subsequent squash/fixup commit
 * is appended to the file as it is processed.
 */
static GIT_PATH_FUNC(rebase_path_squash_msg, "rebase-merge/message-squash")
/*
 * If the current series of squash/fixups has not yet included a squash
 * command, then this file exists and holds the commit message of the
 * original "pick" commit.  (If the series ends without a "squash"
 * command, then this can be used as the commit message of the combined
 * commit without opening the editor.)
 */
static GIT_PATH_FUNC(rebase_path_fixup_msg, "rebase-merge/message-fixup")
/*
 * This file contains the list fixup/squash commands that have been
 * accumulated into message-fixup or message-squash so far.
 */
static GIT_PATH_FUNC(rebase_path_current_fixups, "rebase-merge/current-fixups")
/*
 * A script to set the GIT_AUTHOR_NAME, GIT_AUTHOR_EMAIL, and
 * GIT_AUTHOR_DATE that will be used for the commit that is currently
 * being rebased.
 */
static GIT_PATH_FUNC(rebase_path_author_script, "rebase-merge/author-script")
/*
 * When an "edit" rebase command is being processed, the SHA1 of the
 * commit to be edited is recorded in this file.  When "git rebase
 * --continue" is executed, if there are any staged changes then they
 * will be amended to the HEAD commit, but only provided the HEAD
 * commit is still the commit to be edited.  When any other rebase
 * command is processed, this file is deleted.
 */
static GIT_PATH_FUNC(rebase_path_amend, "rebase-merge/amend")
/*
 * When we stop at a given patch via the "edit" command, this file contains
 * the commit object name of the corresponding patch.
 */
static GIT_PATH_FUNC(rebase_path_stopped_sha, "rebase-merge/stopped-sha")
/*
 * For the post-rewrite hook, we make a list of rewritten commits and
 * their new sha1s.  The rewritten-pending list keeps the sha1s of
 * commits that have been processed, but not committed yet,
 * e.g. because they are waiting for a 'squash' command.
 */
static GIT_PATH_FUNC(rebase_path_rewritten_list, "rebase-merge/rewritten-list")
static GIT_PATH_FUNC(rebase_path_rewritten_pending,
	"rebase-merge/rewritten-pending")

/*
 * The path of the file containing the OID of the "squash onto" commit, i.e.
 * the dummy commit used for `reset [new root]`.
 */
static GIT_PATH_FUNC(rebase_path_squash_onto, "rebase-merge/squash-onto")

/*
 * The path of the file listing refs that need to be deleted after the rebase
 * finishes. This is used by the `label` command to record the need for cleanup.
 */
static GIT_PATH_FUNC(rebase_path_refs_to_delete, "rebase-merge/refs-to-delete")

/*
 * The update-refs file stores a list of refs that will be updated at the end
 * of the rebase sequence. The 'update-ref <ref>' commands in the todo file
 * update the OIDs for the refs in this file, but the refs are not updated
 * until the end of the rebase sequence.
 *
 * rebase_path_update_refs() returns the path to this file for a given
 * worktree directory. For the current worktree, pass the_repository->gitdir.
 */
static char *rebase_path_update_refs(const char *wt_git_dir)
{
	return xstrfmt("%s/rebase-merge/update-refs", wt_git_dir);
}

/*
 * The following files are written by git-rebase just after parsing the
 * command-line.
 */
static GIT_PATH_FUNC(rebase_path_gpg_sign_opt, "rebase-merge/gpg_sign_opt")
static GIT_PATH_FUNC(rebase_path_cdate_is_adate, "rebase-merge/cdate_is_adate")
static GIT_PATH_FUNC(rebase_path_ignore_date, "rebase-merge/ignore_date")
static GIT_PATH_FUNC(rebase_path_orig_head, "rebase-merge/orig-head")
static GIT_PATH_FUNC(rebase_path_verbose, "rebase-merge/verbose")
static GIT_PATH_FUNC(rebase_path_quiet, "rebase-merge/quiet")
static GIT_PATH_FUNC(rebase_path_signoff, "rebase-merge/signoff")
static GIT_PATH_FUNC(rebase_path_head_name, "rebase-merge/head-name")
static GIT_PATH_FUNC(rebase_path_onto, "rebase-merge/onto")
static GIT_PATH_FUNC(rebase_path_autostash, "rebase-merge/autostash")
static GIT_PATH_FUNC(rebase_path_strategy, "rebase-merge/strategy")
static GIT_PATH_FUNC(rebase_path_strategy_opts, "rebase-merge/strategy_opts")
static GIT_PATH_FUNC(rebase_path_allow_rerere_autoupdate, "rebase-merge/allow_rerere_autoupdate")
static GIT_PATH_FUNC(rebase_path_reschedule_failed_exec, "rebase-merge/reschedule-failed-exec")
static GIT_PATH_FUNC(rebase_path_no_reschedule_failed_exec, "rebase-merge/no-reschedule-failed-exec")
static GIT_PATH_FUNC(rebase_path_drop_redundant_commits, "rebase-merge/drop_redundant_commits")
static GIT_PATH_FUNC(rebase_path_keep_redundant_commits, "rebase-merge/keep_redundant_commits")

/**
 * A 'struct update_refs_record' represents a value in the update-refs
 * list. We use a string_list to map refs to these (before, after) pairs.
 */
struct update_ref_record {
	struct object_id before;
	struct object_id after;
};

static struct update_ref_record *init_update_ref_record(const char *ref)
{
	struct update_ref_record *rec;

	CALLOC_ARRAY(rec, 1);

	oidcpy(&rec->before, null_oid());
	oidcpy(&rec->after, null_oid());

	/* This may fail, but that's fine, we will keep the null OID. */
	read_ref(ref, &rec->before);

	return rec;
}

static int git_sequencer_config(const char *k, const char *v, void *cb)
{
	struct replay_opts *opts = cb;
	int status;

	if (!strcmp(k, "commit.cleanup")) {
		const char *s;

		status = git_config_string(&s, k, v);
		if (status)
			return status;

		if (!strcmp(s, "verbatim")) {
			opts->default_msg_cleanup = COMMIT_MSG_CLEANUP_NONE;
			opts->explicit_cleanup = 1;
		} else if (!strcmp(s, "whitespace")) {
			opts->default_msg_cleanup = COMMIT_MSG_CLEANUP_SPACE;
			opts->explicit_cleanup = 1;
		} else if (!strcmp(s, "strip")) {
			opts->default_msg_cleanup = COMMIT_MSG_CLEANUP_ALL;
			opts->explicit_cleanup = 1;
		} else if (!strcmp(s, "scissors")) {
			opts->default_msg_cleanup = COMMIT_MSG_CLEANUP_SCISSORS;
			opts->explicit_cleanup = 1;
		} else {
			warning(_("invalid commit message cleanup mode '%s'"),
				  s);
		}

		free((char *)s);
		return status;
	}

	if (!strcmp(k, "commit.gpgsign")) {
		opts->gpg_sign = git_config_bool(k, v) ? xstrdup("") : NULL;
		return 0;
	}

	if (!opts->default_strategy && !strcmp(k, "pull.twohead")) {
		int ret = git_config_string((const char**)&opts->default_strategy, k, v);
		if (ret == 0) {
			/*
			 * pull.twohead is allowed to be multi-valued; we only
			 * care about the first value.
			 */
			char *tmp = strchr(opts->default_strategy, ' ');
			if (tmp)
				*tmp = '\0';
		}
		return ret;
	}

	if (opts->action == REPLAY_REVERT && !strcmp(k, "revert.reference"))
		opts->commit_use_reference = git_config_bool(k, v);

	return git_diff_basic_config(k, v, NULL);
}

void sequencer_init_config(struct replay_opts *opts)
{
	opts->default_msg_cleanup = COMMIT_MSG_CLEANUP_NONE;
	git_config(git_sequencer_config, opts);
}

static inline int is_rebase_i(const struct replay_opts *opts)
{
	return opts->action == REPLAY_INTERACTIVE_REBASE;
}

static const char *get_dir(const struct replay_opts *opts)
{
	if (is_rebase_i(opts))
		return rebase_path();
	return git_path_seq_dir();
}

static const char *get_todo_path(const struct replay_opts *opts)
{
	if (is_rebase_i(opts))
		return rebase_path_todo();
	return git_path_todo_file();
}

/*
 * Returns 0 for non-conforming footer
 * Returns 1 for conforming footer
 * Returns 2 when sob exists within conforming footer
 * Returns 3 when sob exists within conforming footer as last entry
 */
static int has_conforming_footer(struct strbuf *sb, struct strbuf *sob,
	size_t ignore_footer)
{
	struct process_trailer_options opts = PROCESS_TRAILER_OPTIONS_INIT;
	struct trailer_info info;
	size_t i;
	int found_sob = 0, found_sob_last = 0;
	char saved_char;

	opts.no_divider = 1;

	if (ignore_footer) {
		saved_char = sb->buf[sb->len - ignore_footer];
		sb->buf[sb->len - ignore_footer] = '\0';
	}

	trailer_info_get(&info, sb->buf, &opts);

	if (ignore_footer)
		sb->buf[sb->len - ignore_footer] = saved_char;

	if (info.trailer_start == info.trailer_end)
		return 0;

	for (i = 0; i < info.trailer_nr; i++)
		if (sob && !strncmp(info.trailers[i], sob->buf, sob->len)) {
			found_sob = 1;
			if (i == info.trailer_nr - 1)
				found_sob_last = 1;
		}

	trailer_info_release(&info);

	if (found_sob_last)
		return 3;
	if (found_sob)
		return 2;
	return 1;
}

static const char *gpg_sign_opt_quoted(struct replay_opts *opts)
{
	static struct strbuf buf = STRBUF_INIT;

	strbuf_reset(&buf);
	if (opts->gpg_sign)
		sq_quotef(&buf, "-S%s", opts->gpg_sign);
	return buf.buf;
}

void replay_opts_release(struct replay_opts *opts)
{
	free(opts->gpg_sign);
	free(opts->reflog_action);
	free(opts->default_strategy);
	free(opts->strategy);
	for (size_t i = 0; i < opts->xopts_nr; i++)
		free(opts->xopts[i]);
	free(opts->xopts);
	strbuf_release(&opts->current_fixups);
	if (opts->revs)
		release_revisions(opts->revs);
	free(opts->revs);
}

int sequencer_remove_state(struct replay_opts *opts)
{
	struct strbuf buf = STRBUF_INIT;
	int ret = 0;

	if (is_rebase_i(opts) &&
	    strbuf_read_file(&buf, rebase_path_refs_to_delete(), 0) > 0) {
		char *p = buf.buf;
		while (*p) {
			char *eol = strchr(p, '\n');
			if (eol)
				*eol = '\0';
			if (delete_ref("(rebase) cleanup", p, NULL, 0) < 0) {
				warning(_("could not delete '%s'"), p);
				ret = -1;
			}
			if (!eol)
				break;
			p = eol + 1;
		}
	}

	strbuf_reset(&buf);
	strbuf_addstr(&buf, get_dir(opts));
	if (remove_dir_recursively(&buf, 0))
		ret = error(_("could not remove '%s'"), buf.buf);
	strbuf_release(&buf);

	return ret;
}

static const char *action_name(const struct replay_opts *opts)
{
	switch (opts->action) {
	case REPLAY_REVERT:
		return N_("revert");
	case REPLAY_PICK:
		return N_("cherry-pick");
	case REPLAY_INTERACTIVE_REBASE:
		return N_("rebase");
	}
	die(_("unknown action: %d"), opts->action);
}

struct commit_message {
	char *parent_label;
	char *label;
	char *subject;
	const char *message;
};

static const char *short_commit_name(struct commit *commit)
{
	return find_unique_abbrev(&commit->object.oid, DEFAULT_ABBREV);
}

static int get_message(struct commit *commit, struct commit_message *out)
{
	const char *abbrev, *subject;
	int subject_len;

	out->message = logmsg_reencode(commit, NULL, get_commit_output_encoding());
	abbrev = short_commit_name(commit);

	subject_len = find_commit_subject(out->message, &subject);

	out->subject = xmemdupz(subject, subject_len);
	out->label = xstrfmt("%s (%s)", abbrev, out->subject);
	out->parent_label = xstrfmt("parent of %s", out->label);

	return 0;
}

static void free_message(struct commit *commit, struct commit_message *msg)
{
	free(msg->parent_label);
	free(msg->label);
	free(msg->subject);
	unuse_commit_buffer(commit, msg->message);
}

static void print_advice(struct repository *r, int show_hint,
			 struct replay_opts *opts)
{
	char *msg = getenv("GIT_CHERRY_PICK_HELP");

	if (msg) {
		advise("%s\n", msg);
		/*
		 * A conflict has occurred but the porcelain
		 * (typically rebase --interactive) wants to take care
		 * of the commit itself so remove CHERRY_PICK_HEAD
		 */
		refs_delete_ref(get_main_ref_store(r), "", "CHERRY_PICK_HEAD",
				NULL, 0);
		return;
	}

	if (show_hint) {
		if (opts->no_commit)
			advise(_("after resolving the conflicts, mark the corrected paths\n"
				 "with 'git add <paths>' or 'git rm <paths>'"));
		else if (opts->action == REPLAY_PICK)
			advise(_("After resolving the conflicts, mark them with\n"
				 "\"git add/rm <pathspec>\", then run\n"
				 "\"git cherry-pick --continue\".\n"
				 "You can instead skip this commit with \"git cherry-pick --skip\".\n"
				 "To abort and get back to the state before \"git cherry-pick\",\n"
				 "run \"git cherry-pick --abort\"."));
		else if (opts->action == REPLAY_REVERT)
			advise(_("After resolving the conflicts, mark them with\n"
				 "\"git add/rm <pathspec>\", then run\n"
				 "\"git revert --continue\".\n"
				 "You can instead skip this commit with \"git revert --skip\".\n"
				 "To abort and get back to the state before \"git revert\",\n"
				 "run \"git revert --abort\"."));
		else
			BUG("unexpected pick action in print_advice()");
	}
}

static int write_message(const void *buf, size_t len, const char *filename,
			 int append_eol)
{
	struct lock_file msg_file = LOCK_INIT;

	int msg_fd = hold_lock_file_for_update(&msg_file, filename, 0);
	if (msg_fd < 0)
		return error_errno(_("could not lock '%s'"), filename);
	if (write_in_full(msg_fd, buf, len) < 0) {
		error_errno(_("could not write to '%s'"), filename);
		rollback_lock_file(&msg_file);
		return -1;
	}
	if (append_eol && write(msg_fd, "\n", 1) < 0) {
		error_errno(_("could not write eol to '%s'"), filename);
		rollback_lock_file(&msg_file);
		return -1;
	}
	if (commit_lock_file(&msg_file) < 0)
		return error(_("failed to finalize '%s'"), filename);

	return 0;
}

int read_oneliner(struct strbuf *buf,
	const char *path, unsigned flags)
{
	int orig_len = buf->len;

	if (strbuf_read_file(buf, path, 0) < 0) {
		if ((flags & READ_ONELINER_WARN_MISSING) ||
		    (errno != ENOENT && errno != ENOTDIR))
			warning_errno(_("could not read '%s'"), path);
		return 0;
	}

	if (buf->len > orig_len && buf->buf[buf->len - 1] == '\n') {
		if (--buf->len > orig_len && buf->buf[buf->len - 1] == '\r')
			--buf->len;
		buf->buf[buf->len] = '\0';
	}

	if ((flags & READ_ONELINER_SKIP_IF_EMPTY) && buf->len == orig_len)
		return 0;

	return 1;
}

static struct tree *empty_tree(struct repository *r)
{
	return lookup_tree(r, the_hash_algo->empty_tree);
}

static int error_dirty_index(struct repository *repo, struct replay_opts *opts)
{
	if (repo_read_index_unmerged(repo))
		return error_resolve_conflict(action_name(opts));

	error(_("your local changes would be overwritten by %s."),
		_(action_name(opts)));

	if (advice_enabled(ADVICE_COMMIT_BEFORE_MERGE))
		advise(_("commit your changes or stash them to proceed."));
	return -1;
}

static void update_abort_safety_file(void)
{
	struct object_id head;

	/* Do nothing on a single-pick */
	if (!file_exists(git_path_seq_dir()))
		return;

	if (!get_oid("HEAD", &head))
		write_file(git_path_abort_safety_file(), "%s", oid_to_hex(&head));
	else
		write_file(git_path_abort_safety_file(), "%s", "");
}

static int fast_forward_to(struct repository *r,
			   const struct object_id *to,
			   const struct object_id *from,
			   int unborn,
			   struct replay_opts *opts)
{
	struct ref_transaction *transaction;
	struct strbuf sb = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;

	repo_read_index(r);
	if (checkout_fast_forward(r, from, to, 1))
		return -1; /* the callee should have complained already */

	strbuf_addf(&sb, "%s: fast-forward", action_name(opts));

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, "HEAD",
				   to, unborn && !is_rebase_i(opts) ?
				   null_oid() : from,
				   0, sb.buf, &err) ||
	    ref_transaction_commit(transaction, &err)) {
		ref_transaction_free(transaction);
		error("%s", err.buf);
		strbuf_release(&sb);
		strbuf_release(&err);
		return -1;
	}

	strbuf_release(&sb);
	strbuf_release(&err);
	ref_transaction_free(transaction);
	update_abort_safety_file();
	return 0;
}

enum commit_msg_cleanup_mode get_cleanup_mode(const char *cleanup_arg,
	int use_editor)
{
	if (!cleanup_arg || !strcmp(cleanup_arg, "default"))
		return use_editor ? COMMIT_MSG_CLEANUP_ALL :
				    COMMIT_MSG_CLEANUP_SPACE;
	else if (!strcmp(cleanup_arg, "verbatim"))
		return COMMIT_MSG_CLEANUP_NONE;
	else if (!strcmp(cleanup_arg, "whitespace"))
		return COMMIT_MSG_CLEANUP_SPACE;
	else if (!strcmp(cleanup_arg, "strip"))
		return COMMIT_MSG_CLEANUP_ALL;
	else if (!strcmp(cleanup_arg, "scissors"))
		return use_editor ? COMMIT_MSG_CLEANUP_SCISSORS :
				    COMMIT_MSG_CLEANUP_SPACE;
	else
		die(_("Invalid cleanup mode %s"), cleanup_arg);
}

/*
 * NB using int rather than enum cleanup_mode to stop clang's
 * -Wtautological-constant-out-of-range-compare complaining that the comparison
 * is always true.
 */
static const char *describe_cleanup_mode(int cleanup_mode)
{
	static const char *modes[] = { "whitespace",
				       "verbatim",
				       "scissors",
				       "strip" };

	if (cleanup_mode < ARRAY_SIZE(modes))
		return modes[cleanup_mode];

	BUG("invalid cleanup_mode provided (%d)", cleanup_mode);
}

void append_conflicts_hint(struct index_state *istate,
	struct strbuf *msgbuf, enum commit_msg_cleanup_mode cleanup_mode)
{
	int i;

	if (cleanup_mode == COMMIT_MSG_CLEANUP_SCISSORS) {
		strbuf_addch(msgbuf, '\n');
		wt_status_append_cut_line(msgbuf);
		strbuf_addch(msgbuf, comment_line_char);
	}

	strbuf_addch(msgbuf, '\n');
	strbuf_commented_addf(msgbuf, "Conflicts:\n");
	for (i = 0; i < istate->cache_nr;) {
		const struct cache_entry *ce = istate->cache[i++];
		if (ce_stage(ce)) {
			strbuf_commented_addf(msgbuf, "\t%s\n", ce->name);
			while (i < istate->cache_nr &&
			       !strcmp(ce->name, istate->cache[i]->name))
				i++;
		}
	}
}

static int do_recursive_merge(struct repository *r,
			      struct commit *base, struct commit *next,
			      const char *base_label, const char *next_label,
			      struct object_id *head, struct strbuf *msgbuf,
			      struct replay_opts *opts)
{
	struct merge_options o;
	struct merge_result result;
	struct tree *next_tree, *base_tree, *head_tree;
	int clean, show_output;
	int i;
	struct lock_file index_lock = LOCK_INIT;

	if (repo_hold_locked_index(r, &index_lock, LOCK_REPORT_ON_ERROR) < 0)
		return -1;

	repo_read_index(r);

	init_merge_options(&o, r);
	o.ancestor = base ? base_label : "(empty tree)";
	o.branch1 = "HEAD";
	o.branch2 = next ? next_label : "(empty tree)";
	if (is_rebase_i(opts))
		o.buffer_output = 2;
	o.show_rename_progress = 1;

	head_tree = parse_tree_indirect(head);
	next_tree = next ? get_commit_tree(next) : empty_tree(r);
	base_tree = base ? get_commit_tree(base) : empty_tree(r);

	for (i = 0; i < opts->xopts_nr; i++)
		parse_merge_opt(&o, opts->xopts[i]);

	if (!opts->strategy || !strcmp(opts->strategy, "ort")) {
		memset(&result, 0, sizeof(result));
		merge_incore_nonrecursive(&o, base_tree, head_tree, next_tree,
					    &result);
		show_output = !is_rebase_i(opts) || !result.clean;
		/*
		 * TODO: merge_switch_to_result will update index/working tree;
		 * we only really want to do that if !result.clean || this is
		 * the final patch to be picked.  But determining this is the
		 * final patch would take some work, and "head_tree" would need
		 * to be replace with the tree the index matched before we
		 * started doing any picks.
		 */
		merge_switch_to_result(&o, head_tree, &result, 1, show_output);
		clean = result.clean;
	} else {
		ensure_full_index(r->index);
		clean = merge_trees(&o, head_tree, next_tree, base_tree);
		if (is_rebase_i(opts) && clean <= 0)
			fputs(o.obuf.buf, stdout);
		strbuf_release(&o.obuf);
	}
	if (clean < 0) {
		rollback_lock_file(&index_lock);
		return clean;
	}

	if (write_locked_index(r->index, &index_lock,
			       COMMIT_LOCK | SKIP_IF_UNCHANGED))
		/*
		 * TRANSLATORS: %s will be "revert", "cherry-pick" or
		 * "rebase".
		 */
		return error(_("%s: Unable to write new index file"),
			_(action_name(opts)));

	if (!clean)
		append_conflicts_hint(r->index, msgbuf,
				      opts->default_msg_cleanup);

	return !clean;
}

static struct object_id *get_cache_tree_oid(struct index_state *istate)
{
	if (!cache_tree_fully_valid(istate->cache_tree))
		if (cache_tree_update(istate, 0)) {
			error(_("unable to update cache tree"));
			return NULL;
		}

	return &istate->cache_tree->oid;
}

static int is_index_unchanged(struct repository *r)
{
	struct object_id head_oid, *cache_tree_oid;
	struct commit *head_commit;
	struct index_state *istate = r->index;

	if (!resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, &head_oid, NULL))
		return error(_("could not resolve HEAD commit"));

	head_commit = lookup_commit(r, &head_oid);

	/*
	 * If head_commit is NULL, check_commit, called from
	 * lookup_commit, would have indicated that head_commit is not
	 * a commit object already.  parse_commit() will return failure
	 * without further complaints in such a case.  Otherwise, if
	 * the commit is invalid, parse_commit() will complain.  So
	 * there is nothing for us to say here.  Just return failure.
	 */
	if (parse_commit(head_commit))
		return -1;

	if (!(cache_tree_oid = get_cache_tree_oid(istate)))
		return -1;

	return oideq(cache_tree_oid, get_commit_tree_oid(head_commit));
}

static int write_author_script(const char *message)
{
	struct strbuf buf = STRBUF_INIT;
	const char *eol;
	int res;

	for (;;)
		if (!*message || starts_with(message, "\n")) {
missing_author:
			/* Missing 'author' line? */
			unlink(rebase_path_author_script());
			return 0;
		} else if (skip_prefix(message, "author ", &message))
			break;
		else if ((eol = strchr(message, '\n')))
			message = eol + 1;
		else
			goto missing_author;

	strbuf_addstr(&buf, "GIT_AUTHOR_NAME='");
	while (*message && *message != '\n' && *message != '\r')
		if (skip_prefix(message, " <", &message))
			break;
		else if (*message != '\'')
			strbuf_addch(&buf, *(message++));
		else
			strbuf_addf(&buf, "'\\%c'", *(message++));
	strbuf_addstr(&buf, "'\nGIT_AUTHOR_EMAIL='");
	while (*message && *message != '\n' && *message != '\r')
		if (skip_prefix(message, "> ", &message))
			break;
		else if (*message != '\'')
			strbuf_addch(&buf, *(message++));
		else
			strbuf_addf(&buf, "'\\%c'", *(message++));
	strbuf_addstr(&buf, "'\nGIT_AUTHOR_DATE='@");
	while (*message && *message != '\n' && *message != '\r')
		if (*message != '\'')
			strbuf_addch(&buf, *(message++));
		else
			strbuf_addf(&buf, "'\\%c'", *(message++));
	strbuf_addch(&buf, '\'');
	res = write_message(buf.buf, buf.len, rebase_path_author_script(), 1);
	strbuf_release(&buf);
	return res;
}

/**
 * Take a series of KEY='VALUE' lines where VALUE part is
 * sq-quoted, and append <KEY, VALUE> at the end of the string list
 */
static int parse_key_value_squoted(char *buf, struct string_list *list)
{
	while (*buf) {
		struct string_list_item *item;
		char *np;
		char *cp = strchr(buf, '=');
		if (!cp) {
			np = strchrnul(buf, '\n');
			return error(_("no key present in '%.*s'"),
				     (int) (np - buf), buf);
		}
		np = strchrnul(cp, '\n');
		*cp++ = '\0';
		item = string_list_append(list, buf);

		buf = np + (*np == '\n');
		*np = '\0';
		cp = sq_dequote(cp);
		if (!cp)
			return error(_("unable to dequote value of '%s'"),
				     item->string);
		item->util = xstrdup(cp);
	}
	return 0;
}

/**
 * Reads and parses the state directory's "author-script" file, and sets name,
 * email and date accordingly.
 * Returns 0 on success, -1 if the file could not be parsed.
 *
 * The author script is of the format:
 *
 *	GIT_AUTHOR_NAME='$author_name'
 *	GIT_AUTHOR_EMAIL='$author_email'
 *	GIT_AUTHOR_DATE='$author_date'
 *
 * where $author_name, $author_email and $author_date are quoted. We are strict
 * with our parsing, as the file was meant to be eval'd in the now-removed
 * git-am.sh/git-rebase--interactive.sh scripts, and thus if the file differs
 * from what this function expects, it is better to bail out than to do
 * something that the user does not expect.
 */
int read_author_script(const char *path, char **name, char **email, char **date,
		       int allow_missing)
{
	struct strbuf buf = STRBUF_INIT;
	struct string_list kv = STRING_LIST_INIT_DUP;
	int retval = -1; /* assume failure */
	int i, name_i = -2, email_i = -2, date_i = -2, err = 0;

	if (strbuf_read_file(&buf, path, 256) <= 0) {
		strbuf_release(&buf);
		if (errno == ENOENT && allow_missing)
			return 0;
		else
			return error_errno(_("could not open '%s' for reading"),
					   path);
	}

	if (parse_key_value_squoted(buf.buf, &kv))
		goto finish;

	for (i = 0; i < kv.nr; i++) {
		if (!strcmp(kv.items[i].string, "GIT_AUTHOR_NAME")) {
			if (name_i != -2)
				name_i = error(_("'GIT_AUTHOR_NAME' already given"));
			else
				name_i = i;
		} else if (!strcmp(kv.items[i].string, "GIT_AUTHOR_EMAIL")) {
			if (email_i != -2)
				email_i = error(_("'GIT_AUTHOR_EMAIL' already given"));
			else
				email_i = i;
		} else if (!strcmp(kv.items[i].string, "GIT_AUTHOR_DATE")) {
			if (date_i != -2)
				date_i = error(_("'GIT_AUTHOR_DATE' already given"));
			else
				date_i = i;
		} else {
			err = error(_("unknown variable '%s'"),
				    kv.items[i].string);
		}
	}
	if (name_i == -2)
		error(_("missing 'GIT_AUTHOR_NAME'"));
	if (email_i == -2)
		error(_("missing 'GIT_AUTHOR_EMAIL'"));
	if (date_i == -2)
		error(_("missing 'GIT_AUTHOR_DATE'"));
	if (name_i < 0 || email_i < 0 || date_i < 0 || err)
		goto finish;
	*name = kv.items[name_i].util;
	*email = kv.items[email_i].util;
	*date = kv.items[date_i].util;
	retval = 0;
finish:
	string_list_clear(&kv, !!retval);
	strbuf_release(&buf);
	return retval;
}

/*
 * Read a GIT_AUTHOR_NAME, GIT_AUTHOR_EMAIL AND GIT_AUTHOR_DATE from a
 * file with shell quoting into struct strvec. Returns -1 on
 * error, 0 otherwise.
 */
static int read_env_script(struct strvec *env)
{
	char *name, *email, *date;

	if (read_author_script(rebase_path_author_script(),
			       &name, &email, &date, 0))
		return -1;

	strvec_pushf(env, "GIT_AUTHOR_NAME=%s", name);
	strvec_pushf(env, "GIT_AUTHOR_EMAIL=%s", email);
	strvec_pushf(env, "GIT_AUTHOR_DATE=%s", date);
	free(name);
	free(email);
	free(date);

	return 0;
}

static char *get_author(const char *message)
{
	size_t len;
	const char *a;

	a = find_commit_header(message, "author", &len);
	if (a)
		return xmemdupz(a, len);

	return NULL;
}

static const char *author_date_from_env(const struct strvec *env)
{
	int i;
	const char *date;

	for (i = 0; i < env->nr; i++)
		if (skip_prefix(env->v[i],
				"GIT_AUTHOR_DATE=", &date))
			return date;
	/*
	 * If GIT_AUTHOR_DATE is missing we should have already errored out when
	 * reading the script
	 */
	BUG("GIT_AUTHOR_DATE missing from author script");
}

static const char staged_changes_advice[] =
N_("you have staged changes in your working tree\n"
"If these changes are meant to be squashed into the previous commit, run:\n"
"\n"
"  git commit --amend %s\n"
"\n"
"If they are meant to go into a new commit, run:\n"
"\n"
"  git commit %s\n"
"\n"
"In both cases, once you're done, continue with:\n"
"\n"
"  git rebase --continue\n");

#define ALLOW_EMPTY (1<<0)
#define EDIT_MSG    (1<<1)
#define AMEND_MSG   (1<<2)
#define CLEANUP_MSG (1<<3)
#define VERIFY_MSG  (1<<4)
#define CREATE_ROOT_COMMIT (1<<5)
#define VERBATIM_MSG (1<<6)

static int run_command_silent_on_success(struct child_process *cmd)
{
	struct strbuf buf = STRBUF_INIT;
	int rc;

	cmd->stdout_to_stderr = 1;
	rc = pipe_command(cmd,
			  NULL, 0,
			  NULL, 0,
			  &buf, 0);

	if (rc)
		fputs(buf.buf, stderr);
	strbuf_release(&buf);
	return rc;
}

/*
 * If we are cherry-pick, and if the merge did not result in
 * hand-editing, we will hit this commit and inherit the original
 * author date and name.
 *
 * If we are revert, or if our cherry-pick results in a hand merge,
 * we had better say that the current user is responsible for that.
 *
 * An exception is when run_git_commit() is called during an
 * interactive rebase: in that case, we will want to retain the
 * author metadata.
 */
static int run_git_commit(const char *defmsg,
			  struct replay_opts *opts,
			  unsigned int flags)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	if ((flags & CLEANUP_MSG) && (flags & VERBATIM_MSG))
		BUG("CLEANUP_MSG and VERBATIM_MSG are mutually exclusive");

	cmd.git_cmd = 1;

	if (is_rebase_i(opts) &&
	    ((opts->committer_date_is_author_date && !opts->ignore_date) ||
	     !(!defmsg && (flags & AMEND_MSG))) &&
	    read_env_script(&cmd.env)) {
		const char *gpg_opt = gpg_sign_opt_quoted(opts);

		return error(_(staged_changes_advice),
			     gpg_opt, gpg_opt);
	}

	strvec_pushf(&cmd.env, GIT_REFLOG_ACTION "=%s", opts->reflog_message);

	if (opts->committer_date_is_author_date)
		strvec_pushf(&cmd.env, "GIT_COMMITTER_DATE=%s",
			     opts->ignore_date ?
			     "" :
			     author_date_from_env(&cmd.env));
	if (opts->ignore_date)
		strvec_push(&cmd.env, "GIT_AUTHOR_DATE=");

	strvec_push(&cmd.args, "commit");

	if (!(flags & VERIFY_MSG))
		strvec_push(&cmd.args, "-n");
	if ((flags & AMEND_MSG))
		strvec_push(&cmd.args, "--amend");
	if (opts->gpg_sign)
		strvec_pushf(&cmd.args, "-S%s", opts->gpg_sign);
	else
		strvec_push(&cmd.args, "--no-gpg-sign");
	if (defmsg)
		strvec_pushl(&cmd.args, "-F", defmsg, NULL);
	else if (!(flags & EDIT_MSG))
		strvec_pushl(&cmd.args, "-C", "HEAD", NULL);
	if ((flags & CLEANUP_MSG))
		strvec_push(&cmd.args, "--cleanup=strip");
	if ((flags & VERBATIM_MSG))
		strvec_push(&cmd.args, "--cleanup=verbatim");
	if ((flags & EDIT_MSG))
		strvec_push(&cmd.args, "-e");
	else if (!(flags & CLEANUP_MSG) &&
		 !opts->signoff && !opts->record_origin &&
		 !opts->explicit_cleanup)
		strvec_push(&cmd.args, "--cleanup=verbatim");

	if ((flags & ALLOW_EMPTY))
		strvec_push(&cmd.args, "--allow-empty");

	if (!(flags & EDIT_MSG))
		strvec_push(&cmd.args, "--allow-empty-message");

	if (is_rebase_i(opts) && !(flags & EDIT_MSG))
		return run_command_silent_on_success(&cmd);
	else
		return run_command(&cmd);
}

static int rest_is_empty(const struct strbuf *sb, int start)
{
	int i, eol;
	const char *nl;

	/* Check if the rest is just whitespace and Signed-off-by's. */
	for (i = start; i < sb->len; i++) {
		nl = memchr(sb->buf + i, '\n', sb->len - i);
		if (nl)
			eol = nl - sb->buf;
		else
			eol = sb->len;

		if (strlen(sign_off_header) <= eol - i &&
		    starts_with(sb->buf + i, sign_off_header)) {
			i = eol;
			continue;
		}
		while (i < eol)
			if (!isspace(sb->buf[i++]))
				return 0;
	}

	return 1;
}

void cleanup_message(struct strbuf *msgbuf,
	enum commit_msg_cleanup_mode cleanup_mode, int verbose)
{
	if (verbose || /* Truncate the message just before the diff, if any. */
	    cleanup_mode == COMMIT_MSG_CLEANUP_SCISSORS)
		strbuf_setlen(msgbuf, wt_status_locate_end(msgbuf->buf, msgbuf->len));
	if (cleanup_mode != COMMIT_MSG_CLEANUP_NONE)
		strbuf_stripspace(msgbuf, cleanup_mode == COMMIT_MSG_CLEANUP_ALL);
}

/*
 * Find out if the message in the strbuf contains only whitespace and
 * Signed-off-by lines.
 */
int message_is_empty(const struct strbuf *sb,
		     enum commit_msg_cleanup_mode cleanup_mode)
{
	if (cleanup_mode == COMMIT_MSG_CLEANUP_NONE && sb->len)
		return 0;
	return rest_is_empty(sb, 0);
}

/*
 * See if the user edited the message in the editor or left what
 * was in the template intact
 */
int template_untouched(const struct strbuf *sb, const char *template_file,
		       enum commit_msg_cleanup_mode cleanup_mode)
{
	struct strbuf tmpl = STRBUF_INIT;
	const char *start;

	if (cleanup_mode == COMMIT_MSG_CLEANUP_NONE && sb->len)
		return 0;

	if (!template_file || strbuf_read_file(&tmpl, template_file, 0) <= 0)
		return 0;

	strbuf_stripspace(&tmpl, cleanup_mode == COMMIT_MSG_CLEANUP_ALL);
	if (!skip_prefix(sb->buf, tmpl.buf, &start))
		start = sb->buf;
	strbuf_release(&tmpl);
	return rest_is_empty(sb, start - sb->buf);
}

int update_head_with_reflog(const struct commit *old_head,
			    const struct object_id *new_head,
			    const char *action, const struct strbuf *msg,
			    struct strbuf *err)
{
	struct ref_transaction *transaction;
	struct strbuf sb = STRBUF_INIT;
	const char *nl;
	int ret = 0;

	if (action) {
		strbuf_addstr(&sb, action);
		strbuf_addstr(&sb, ": ");
	}

	nl = strchr(msg->buf, '\n');
	if (nl) {
		strbuf_add(&sb, msg->buf, nl + 1 - msg->buf);
	} else {
		strbuf_addbuf(&sb, msg);
		strbuf_addch(&sb, '\n');
	}

	transaction = ref_transaction_begin(err);
	if (!transaction ||
	    ref_transaction_update(transaction, "HEAD", new_head,
				   old_head ? &old_head->object.oid : null_oid(),
				   0, sb.buf, err) ||
	    ref_transaction_commit(transaction, err)) {
		ret = -1;
	}
	ref_transaction_free(transaction);
	strbuf_release(&sb);

	return ret;
}

static int run_rewrite_hook(const struct object_id *oldoid,
			    const struct object_id *newoid)
{
	struct child_process proc = CHILD_PROCESS_INIT;
	int code;
	struct strbuf sb = STRBUF_INIT;
	const char *hook_path = find_hook("post-rewrite");

	if (!hook_path)
		return 0;

	strvec_pushl(&proc.args, hook_path, "amend", NULL);
	proc.in = -1;
	proc.stdout_to_stderr = 1;
	proc.trace2_hook_name = "post-rewrite";

	code = start_command(&proc);
	if (code)
		return code;
	strbuf_addf(&sb, "%s %s\n", oid_to_hex(oldoid), oid_to_hex(newoid));
	sigchain_push(SIGPIPE, SIG_IGN);
	write_in_full(proc.in, sb.buf, sb.len);
	close(proc.in);
	strbuf_release(&sb);
	sigchain_pop(SIGPIPE);
	return finish_command(&proc);
}

void commit_post_rewrite(struct repository *r,
			 const struct commit *old_head,
			 const struct object_id *new_head)
{
	struct notes_rewrite_cfg *cfg;

	cfg = init_copy_notes_for_rewrite("amend");
	if (cfg) {
		/* we are amending, so old_head is not NULL */
		copy_note_for_rewrite(cfg, &old_head->object.oid, new_head);
		finish_copy_notes_for_rewrite(r, cfg, "Notes added by 'git commit --amend'");
	}
	run_rewrite_hook(&old_head->object.oid, new_head);
}

static int run_prepare_commit_msg_hook(struct repository *r,
				       struct strbuf *msg,
				       const char *commit)
{
	int ret = 0;
	const char *name, *arg1 = NULL, *arg2 = NULL;

	name = git_path_commit_editmsg();
	if (write_message(msg->buf, msg->len, name, 0))
		return -1;

	if (commit) {
		arg1 = "commit";
		arg2 = commit;
	} else {
		arg1 = "message";
	}
	if (run_commit_hook(0, r->index_file, NULL, "prepare-commit-msg", name,
			    arg1, arg2, NULL))
		ret = error(_("'prepare-commit-msg' hook failed"));

	return ret;
}

static const char implicit_ident_advice_noconfig[] =
N_("Your name and email address were configured automatically based\n"
"on your username and hostname. Please check that they are accurate.\n"
"You can suppress this message by setting them explicitly. Run the\n"
"following command and follow the instructions in your editor to edit\n"
"your configuration file:\n"
"\n"
"    git config --global --edit\n"
"\n"
"After doing this, you may fix the identity used for this commit with:\n"
"\n"
"    git commit --amend --reset-author\n");

static const char implicit_ident_advice_config[] =
N_("Your name and email address were configured automatically based\n"
"on your username and hostname. Please check that they are accurate.\n"
"You can suppress this message by setting them explicitly:\n"
"\n"
"    git config --global user.name \"Your Name\"\n"
"    git config --global user.email you@example.com\n"
"\n"
"After doing this, you may fix the identity used for this commit with:\n"
"\n"
"    git commit --amend --reset-author\n");

static const char *implicit_ident_advice(void)
{
	char *user_config = interpolate_path("~/.gitconfig", 0);
	char *xdg_config = xdg_config_home("config");
	int config_exists = file_exists(user_config) || file_exists(xdg_config);

	free(user_config);
	free(xdg_config);

	if (config_exists)
		return _(implicit_ident_advice_config);
	else
		return _(implicit_ident_advice_noconfig);

}

void print_commit_summary(struct repository *r,
			  const char *prefix,
			  const struct object_id *oid,
			  unsigned int flags)
{
	struct rev_info rev;
	struct commit *commit;
	struct strbuf format = STRBUF_INIT;
	const char *head;
	struct pretty_print_context pctx = {0};
	struct strbuf author_ident = STRBUF_INIT;
	struct strbuf committer_ident = STRBUF_INIT;
	struct ref_store *refs;

	commit = lookup_commit(r, oid);
	if (!commit)
		die(_("couldn't look up newly created commit"));
	if (parse_commit(commit))
		die(_("could not parse newly created commit"));

	strbuf_addstr(&format, "format:%h] %s");

	format_commit_message(commit, "%an <%ae>", &author_ident, &pctx);
	format_commit_message(commit, "%cn <%ce>", &committer_ident, &pctx);
	if (strbuf_cmp(&author_ident, &committer_ident)) {
		strbuf_addstr(&format, "\n Author: ");
		strbuf_addbuf_percentquote(&format, &author_ident);
	}
	if (flags & SUMMARY_SHOW_AUTHOR_DATE) {
		struct strbuf date = STRBUF_INIT;

		format_commit_message(commit, "%ad", &date, &pctx);
		strbuf_addstr(&format, "\n Date: ");
		strbuf_addbuf_percentquote(&format, &date);
		strbuf_release(&date);
	}
	if (!committer_ident_sufficiently_given()) {
		strbuf_addstr(&format, "\n Committer: ");
		strbuf_addbuf_percentquote(&format, &committer_ident);
		if (advice_enabled(ADVICE_IMPLICIT_IDENTITY)) {
			strbuf_addch(&format, '\n');
			strbuf_addstr(&format, implicit_ident_advice());
		}
	}
	strbuf_release(&author_ident);
	strbuf_release(&committer_ident);

	repo_init_revisions(r, &rev, prefix);
	setup_revisions(0, NULL, &rev, NULL);

	rev.diff = 1;
	rev.diffopt.output_format =
		DIFF_FORMAT_SHORTSTAT | DIFF_FORMAT_SUMMARY;

	rev.verbose_header = 1;
	rev.show_root_diff = 1;
	get_commit_format(format.buf, &rev);
	rev.always_show_header = 0;
	rev.diffopt.detect_rename = DIFF_DETECT_RENAME;
	diff_setup_done(&rev.diffopt);

	refs = get_main_ref_store(the_repository);
	head = refs_resolve_ref_unsafe(refs, "HEAD", 0, NULL, NULL);
	if (!head)
		die(_("unable to resolve HEAD after creating commit"));
	if (!strcmp(head, "HEAD"))
		head = _("detached HEAD");
	else
		skip_prefix(head, "refs/heads/", &head);
	printf("[%s%s ", head, (flags & SUMMARY_INITIAL_COMMIT) ?
						_(" (root-commit)") : "");

	if (!log_tree_commit(&rev, commit)) {
		rev.always_show_header = 1;
		rev.use_terminator = 1;
		log_tree_commit(&rev, commit);
	}

	release_revisions(&rev);
	strbuf_release(&format);
}

static int parse_head(struct repository *r, struct commit **head)
{
	struct commit *current_head;
	struct object_id oid;

	if (get_oid("HEAD", &oid)) {
		current_head = NULL;
	} else {
		current_head = lookup_commit_reference(r, &oid);
		if (!current_head)
			return error(_("could not parse HEAD"));
		if (!oideq(&oid, &current_head->object.oid)) {
			warning(_("HEAD %s is not a commit!"),
				oid_to_hex(&oid));
		}
		if (parse_commit(current_head))
			return error(_("could not parse HEAD commit"));
	}
	*head = current_head;

	return 0;
}

/*
 * Try to commit without forking 'git commit'. In some cases we need
 * to run 'git commit' to display an error message
 *
 * Returns:
 *  -1 - error unable to commit
 *   0 - success
 *   1 - run 'git commit'
 */
static int try_to_commit(struct repository *r,
			 struct strbuf *msg, const char *author,
			 struct replay_opts *opts, unsigned int flags,
			 struct object_id *oid)
{
	struct object_id tree;
	struct commit *current_head = NULL;
	struct commit_list *parents = NULL;
	struct commit_extra_header *extra = NULL;
	struct strbuf err = STRBUF_INIT;
	struct strbuf commit_msg = STRBUF_INIT;
	char *amend_author = NULL;
	const char *committer = NULL;
	const char *hook_commit = NULL;
	enum commit_msg_cleanup_mode cleanup;
	int res = 0;

	if ((flags & CLEANUP_MSG) && (flags & VERBATIM_MSG))
		BUG("CLEANUP_MSG and VERBATIM_MSG are mutually exclusive");

	if (parse_head(r, &current_head))
		return -1;

	if (flags & AMEND_MSG) {
		const char *exclude_gpgsig[] = { "gpgsig", "gpgsig-sha256", NULL };
		const char *out_enc = get_commit_output_encoding();
		const char *message = logmsg_reencode(current_head, NULL,
						      out_enc);

		if (!msg) {
			const char *orig_message = NULL;

			find_commit_subject(message, &orig_message);
			msg = &commit_msg;
			strbuf_addstr(msg, orig_message);
			hook_commit = "HEAD";
		}
		author = amend_author = get_author(message);
		unuse_commit_buffer(current_head, message);
		if (!author) {
			res = error(_("unable to parse commit author"));
			goto out;
		}
		parents = copy_commit_list(current_head->parents);
		extra = read_commit_extra_headers(current_head, exclude_gpgsig);
	} else if (current_head &&
		   (!(flags & CREATE_ROOT_COMMIT) || (flags & AMEND_MSG))) {
		commit_list_insert(current_head, &parents);
	}

	if (write_index_as_tree(&tree, r->index, r->index_file, 0, NULL)) {
		res = error(_("git write-tree failed to write a tree"));
		goto out;
	}

	if (!(flags & ALLOW_EMPTY)) {
		struct commit *first_parent = current_head;

		if (flags & AMEND_MSG) {
			if (current_head->parents) {
				first_parent = current_head->parents->item;
				if (repo_parse_commit(r, first_parent)) {
					res = error(_("could not parse HEAD commit"));
					goto out;
				}
			} else {
				first_parent = NULL;
			}
		}
		if (oideq(first_parent
			  ? get_commit_tree_oid(first_parent)
			  : the_hash_algo->empty_tree,
			  &tree)) {
			res = 1; /* run 'git commit' to display error message */
			goto out;
		}
	}

	if (hook_exists("prepare-commit-msg")) {
		res = run_prepare_commit_msg_hook(r, msg, hook_commit);
		if (res)
			goto out;
		if (strbuf_read_file(&commit_msg, git_path_commit_editmsg(),
				     2048) < 0) {
			res = error_errno(_("unable to read commit message "
					      "from '%s'"),
					    git_path_commit_editmsg());
			goto out;
		}
		msg = &commit_msg;
	}

	if (flags & CLEANUP_MSG)
		cleanup = COMMIT_MSG_CLEANUP_ALL;
	else if (flags & VERBATIM_MSG)
		cleanup = COMMIT_MSG_CLEANUP_NONE;
	else if ((opts->signoff || opts->record_origin) &&
		 !opts->explicit_cleanup)
		cleanup = COMMIT_MSG_CLEANUP_SPACE;
	else
		cleanup = opts->default_msg_cleanup;

	if (cleanup != COMMIT_MSG_CLEANUP_NONE)
		strbuf_stripspace(msg, cleanup == COMMIT_MSG_CLEANUP_ALL);
	if ((flags & EDIT_MSG) && message_is_empty(msg, cleanup)) {
		res = 1; /* run 'git commit' to display error message */
		goto out;
	}

	if (opts->committer_date_is_author_date) {
		struct ident_split id;
		struct strbuf date = STRBUF_INIT;

		if (!opts->ignore_date) {
			if (split_ident_line(&id, author, (int)strlen(author)) < 0) {
				res = error(_("invalid author identity '%s'"),
					    author);
				goto out;
			}
			if (!id.date_begin) {
				res = error(_(
					"corrupt author: missing date information"));
				goto out;
			}
			strbuf_addf(&date, "@%.*s %.*s",
				    (int)(id.date_end - id.date_begin),
				    id.date_begin,
				    (int)(id.tz_end - id.tz_begin),
				    id.tz_begin);
		} else {
			reset_ident_date();
		}
		committer = fmt_ident(getenv("GIT_COMMITTER_NAME"),
				      getenv("GIT_COMMITTER_EMAIL"),
				      WANT_COMMITTER_IDENT,
				      opts->ignore_date ? NULL : date.buf,
				      IDENT_STRICT);
		strbuf_release(&date);
	} else {
		reset_ident_date();
	}

	if (opts->ignore_date) {
		struct ident_split id;
		char *name, *email;

		if (split_ident_line(&id, author, strlen(author)) < 0) {
			error(_("invalid author identity '%s'"), author);
			goto out;
		}
		name = xmemdupz(id.name_begin, id.name_end - id.name_begin);
		email = xmemdupz(id.mail_begin, id.mail_end - id.mail_begin);
		author = fmt_ident(name, email, WANT_AUTHOR_IDENT, NULL,
				   IDENT_STRICT);
		free(name);
		free(email);
	}

	if (commit_tree_extended(msg->buf, msg->len, &tree, parents, oid,
				 author, committer, opts->gpg_sign, extra)) {
		res = error(_("failed to write commit object"));
		goto out;
	}

	if (update_head_with_reflog(current_head, oid, opts->reflog_message,
				    msg, &err)) {
		res = error("%s", err.buf);
		goto out;
	}

	run_commit_hook(0, r->index_file, NULL, "post-commit", NULL);
	if (flags & AMEND_MSG)
		commit_post_rewrite(r, current_head, oid);

out:
	free_commit_extra_headers(extra);
	strbuf_release(&err);
	strbuf_release(&commit_msg);
	free(amend_author);

	return res;
}

static int write_rebase_head(struct object_id *oid)
{
	if (update_ref("rebase", "REBASE_HEAD", oid,
		       NULL, REF_NO_DEREF, UPDATE_REFS_MSG_ON_ERR))
		return error(_("could not update %s"), "REBASE_HEAD");

	return 0;
}

static int do_commit(struct repository *r,
		     const char *msg_file, const char *author,
		     struct replay_opts *opts, unsigned int flags,
		     struct object_id *oid)
{
	int res = 1;

	if (!(flags & EDIT_MSG) && !(flags & VERIFY_MSG)) {
		struct object_id oid;
		struct strbuf sb = STRBUF_INIT;

		if (msg_file && strbuf_read_file(&sb, msg_file, 2048) < 0)
			return error_errno(_("unable to read commit message "
					     "from '%s'"),
					   msg_file);

		res = try_to_commit(r, msg_file ? &sb : NULL,
				    author, opts, flags, &oid);
		strbuf_release(&sb);
		if (!res) {
			refs_delete_ref(get_main_ref_store(r), "",
					"CHERRY_PICK_HEAD", NULL, 0);
			unlink(git_path_merge_msg(r));
			if (!is_rebase_i(opts))
				print_commit_summary(r, NULL, &oid,
						SUMMARY_SHOW_AUTHOR_DATE);
			return res;
		}
	}
	if (res == 1) {
		if (is_rebase_i(opts) && oid)
			if (write_rebase_head(oid))
			    return -1;
		return run_git_commit(msg_file, opts, flags);
	}

	return res;
}

static int is_original_commit_empty(struct commit *commit)
{
	const struct object_id *ptree_oid;

	if (parse_commit(commit))
		return error(_("could not parse commit %s"),
			     oid_to_hex(&commit->object.oid));
	if (commit->parents) {
		struct commit *parent = commit->parents->item;
		if (parse_commit(parent))
			return error(_("could not parse parent commit %s"),
				oid_to_hex(&parent->object.oid));
		ptree_oid = get_commit_tree_oid(parent);
	} else {
		ptree_oid = the_hash_algo->empty_tree; /* commit is root */
	}

	return oideq(ptree_oid, get_commit_tree_oid(commit));
}

/*
 * Should empty commits be allowed?  Return status:
 *    <0: Error in is_index_unchanged(r) or is_original_commit_empty(commit)
 *     0: Halt on empty commit
 *     1: Allow empty commit
 *     2: Drop empty commit
 */
static int allow_empty(struct repository *r,
		       struct replay_opts *opts,
		       struct commit *commit)
{
	int index_unchanged, originally_empty;

	/*
	 * Four cases:
	 *
	 * (1) we do not allow empty at all and error out.
	 *
	 * (2) we allow ones that were initially empty, and
	 *     just drop the ones that become empty
	 *
	 * (3) we allow ones that were initially empty, but
	 *     halt for the ones that become empty;
	 *
	 * (4) we allow both.
	 */
	if (!opts->allow_empty)
		return 0; /* let "git commit" barf as necessary */

	index_unchanged = is_index_unchanged(r);
	if (index_unchanged < 0)
		return index_unchanged;
	if (!index_unchanged)
		return 0; /* we do not have to say --allow-empty */

	if (opts->keep_redundant_commits)
		return 1;

	originally_empty = is_original_commit_empty(commit);
	if (originally_empty < 0)
		return originally_empty;
	if (originally_empty)
		return 1;
	else if (opts->drop_redundant_commits)
		return 2;
	else
		return 0;
}

static struct {
	char c;
	const char *str;
} todo_command_info[] = {
	[TODO_PICK] = { 'p', "pick" },
	[TODO_REVERT] = { 0,   "revert" },
	[TODO_EDIT] = { 'e', "edit" },
	[TODO_REWORD] = { 'r', "reword" },
	[TODO_FIXUP] = { 'f', "fixup" },
	[TODO_SQUASH] = { 's', "squash" },
	[TODO_EXEC] = { 'x', "exec" },
	[TODO_BREAK] = { 'b', "break" },
	[TODO_LABEL] = { 'l', "label" },
	[TODO_RESET] = { 't', "reset" },
	[TODO_MERGE] = { 'm', "merge" },
	[TODO_UPDATE_REF] = { 'u', "update-ref" },
	[TODO_NOOP] = { 0,   "noop" },
	[TODO_DROP] = { 'd', "drop" },
	[TODO_COMMENT] = { 0,   NULL },
};

static const char *command_to_string(const enum todo_command command)
{
	if (command < TODO_COMMENT)
		return todo_command_info[command].str;
	die(_("unknown command: %d"), command);
}

static char command_to_char(const enum todo_command command)
{
	if (command < TODO_COMMENT)
		return todo_command_info[command].c;
	return comment_line_char;
}

static int is_noop(const enum todo_command command)
{
	return TODO_NOOP <= command;
}

static int is_fixup(enum todo_command command)
{
	return command == TODO_FIXUP || command == TODO_SQUASH;
}

/* Does this command create a (non-merge) commit? */
static int is_pick_or_similar(enum todo_command command)
{
	switch (command) {
	case TODO_PICK:
	case TODO_REVERT:
	case TODO_EDIT:
	case TODO_REWORD:
	case TODO_FIXUP:
	case TODO_SQUASH:
		return 1;
	default:
		return 0;
	}
}

enum todo_item_flags {
	TODO_EDIT_MERGE_MSG    = (1 << 0),
	TODO_REPLACE_FIXUP_MSG = (1 << 1),
	TODO_EDIT_FIXUP_MSG    = (1 << 2),
};

static const char first_commit_msg_str[] = N_("This is the 1st commit message:");
static const char nth_commit_msg_fmt[] = N_("This is the commit message #%d:");
static const char skip_first_commit_msg_str[] = N_("The 1st commit message will be skipped:");
static const char skip_nth_commit_msg_fmt[] = N_("The commit message #%d will be skipped:");
static const char combined_commit_msg_fmt[] = N_("This is a combination of %d commits.");

static int is_fixup_flag(enum todo_command command, unsigned flag)
{
	return command == TODO_FIXUP && ((flag & TODO_REPLACE_FIXUP_MSG) ||
					 (flag & TODO_EDIT_FIXUP_MSG));
}

/*
 * Wrapper around strbuf_add_commented_lines() which avoids double
 * commenting commit subjects.
 */
static void add_commented_lines(struct strbuf *buf, const void *str, size_t len)
{
	const char *s = str;
	while (len > 0 && s[0] == comment_line_char) {
		size_t count;
		const char *n = memchr(s, '\n', len);
		if (!n)
			count = len;
		else
			count = n - s + 1;
		strbuf_add(buf, s, count);
		s += count;
		len -= count;
	}
	strbuf_add_commented_lines(buf, s, len);
}

/* Does the current fixup chain contain a squash command? */
static int seen_squash(struct replay_opts *opts)
{
	return starts_with(opts->current_fixups.buf, "squash") ||
		strstr(opts->current_fixups.buf, "\nsquash");
}

static void update_comment_bufs(struct strbuf *buf1, struct strbuf *buf2, int n)
{
	strbuf_setlen(buf1, 2);
	strbuf_addf(buf1, _(nth_commit_msg_fmt), n);
	strbuf_addch(buf1, '\n');
	strbuf_setlen(buf2, 2);
	strbuf_addf(buf2, _(skip_nth_commit_msg_fmt), n);
	strbuf_addch(buf2, '\n');
}

/*
 * Comment out any un-commented commit messages, updating the message comments
 * to say they will be skipped but do not comment out the empty lines that
 * surround commit messages and their comments.
 */
static void update_squash_message_for_fixup(struct strbuf *msg)
{
	void (*copy_lines)(struct strbuf *, const void *, size_t) = strbuf_add;
	struct strbuf buf1 = STRBUF_INIT, buf2 = STRBUF_INIT;
	const char *s, *start;
	char *orig_msg;
	size_t orig_msg_len;
	int i = 1;

	strbuf_addf(&buf1, "# %s\n", _(first_commit_msg_str));
	strbuf_addf(&buf2, "# %s\n", _(skip_first_commit_msg_str));
	s = start = orig_msg = strbuf_detach(msg, &orig_msg_len);
	while (s) {
		const char *next;
		size_t off;
		if (skip_prefix(s, buf1.buf, &next)) {
			/*
			 * Copy the last message, preserving the blank line
			 * preceding the current line
			 */
			off = (s > start + 1 && s[-2] == '\n') ? 1 : 0;
			copy_lines(msg, start, s - start - off);
			if (off)
				strbuf_addch(msg, '\n');
			/*
			 * The next message needs to be commented out but the
			 * message header is already commented out so just copy
			 * it and the blank line that follows it.
			 */
			strbuf_addbuf(msg, &buf2);
			if (*next == '\n')
				strbuf_addch(msg, *next++);
			start = s = next;
			copy_lines = add_commented_lines;
			update_comment_bufs(&buf1, &buf2, ++i);
		} else if (skip_prefix(s, buf2.buf, &next)) {
			off = (s > start + 1 && s[-2] == '\n') ? 1 : 0;
			copy_lines(msg, start, s - start - off);
			start = s - off;
			s = next;
			copy_lines = strbuf_add;
			update_comment_bufs(&buf1, &buf2, ++i);
		} else {
			s = strchr(s, '\n');
			if (s)
				s++;
		}
	}
	copy_lines(msg, start, orig_msg_len - (start - orig_msg));
	free(orig_msg);
	strbuf_release(&buf1);
	strbuf_release(&buf2);
}

static int append_squash_message(struct strbuf *buf, const char *body,
			 enum todo_command command, struct replay_opts *opts,
			 unsigned flag)
{
	const char *fixup_msg;
	size_t commented_len = 0, fixup_off;
	/*
	 * amend is non-interactive and not normally used with fixup!
	 * or squash! commits, so only comment out those subjects when
	 * squashing commit messages.
	 */
	if (starts_with(body, "amend!") ||
	    ((command == TODO_SQUASH || seen_squash(opts)) &&
	     (starts_with(body, "squash!") || starts_with(body, "fixup!"))))
		commented_len = commit_subject_length(body);

	strbuf_addf(buf, "\n%c ", comment_line_char);
	strbuf_addf(buf, _(nth_commit_msg_fmt),
		    ++opts->current_fixup_count + 1);
	strbuf_addstr(buf, "\n\n");
	strbuf_add_commented_lines(buf, body, commented_len);
	/* buf->buf may be reallocated so store an offset into the buffer */
	fixup_off = buf->len;
	strbuf_addstr(buf, body + commented_len);

	/* fixup -C after squash behaves like squash */
	if (is_fixup_flag(command, flag) && !seen_squash(opts)) {
		/*
		 * We're replacing the commit message so we need to
		 * append the Signed-off-by: trailer if the user
		 * requested '--signoff'.
		 */
		if (opts->signoff)
			append_signoff(buf, 0, 0);

		if ((command == TODO_FIXUP) &&
		    (flag & TODO_REPLACE_FIXUP_MSG) &&
		    (file_exists(rebase_path_fixup_msg()) ||
		     !file_exists(rebase_path_squash_msg()))) {
			fixup_msg = skip_blank_lines(buf->buf + fixup_off);
			if (write_message(fixup_msg, strlen(fixup_msg),
					rebase_path_fixup_msg(), 0) < 0)
				return error(_("cannot write '%s'"),
					rebase_path_fixup_msg());
		} else {
			unlink(rebase_path_fixup_msg());
		}
	} else  {
		unlink(rebase_path_fixup_msg());
	}

	return 0;
}

static int update_squash_messages(struct repository *r,
				  enum todo_command command,
				  struct commit *commit,
				  struct replay_opts *opts,
				  unsigned flag)
{
	struct strbuf buf = STRBUF_INIT;
	int res = 0;
	const char *message, *body;
	const char *encoding = get_commit_output_encoding();

	if (opts->current_fixup_count > 0) {
		struct strbuf header = STRBUF_INIT;
		char *eol;

		if (strbuf_read_file(&buf, rebase_path_squash_msg(), 9) <= 0)
			return error(_("could not read '%s'"),
				rebase_path_squash_msg());

		eol = buf.buf[0] != comment_line_char ?
			buf.buf : strchrnul(buf.buf, '\n');

		strbuf_addf(&header, "%c ", comment_line_char);
		strbuf_addf(&header, _(combined_commit_msg_fmt),
			    opts->current_fixup_count + 2);
		strbuf_splice(&buf, 0, eol - buf.buf, header.buf, header.len);
		strbuf_release(&header);
		if (is_fixup_flag(command, flag) && !seen_squash(opts))
			update_squash_message_for_fixup(&buf);
	} else {
		struct object_id head;
		struct commit *head_commit;
		const char *head_message, *body;

		if (get_oid("HEAD", &head))
			return error(_("need a HEAD to fixup"));
		if (!(head_commit = lookup_commit_reference(r, &head)))
			return error(_("could not read HEAD"));
		if (!(head_message = logmsg_reencode(head_commit, NULL, encoding)))
			return error(_("could not read HEAD's commit message"));

		find_commit_subject(head_message, &body);
		if (command == TODO_FIXUP && !flag && write_message(body, strlen(body),
							rebase_path_fixup_msg(), 0) < 0) {
			unuse_commit_buffer(head_commit, head_message);
			return error(_("cannot write '%s'"), rebase_path_fixup_msg());
		}
		strbuf_addf(&buf, "%c ", comment_line_char);
		strbuf_addf(&buf, _(combined_commit_msg_fmt), 2);
		strbuf_addf(&buf, "\n%c ", comment_line_char);
		strbuf_addstr(&buf, is_fixup_flag(command, flag) ?
			      _(skip_first_commit_msg_str) :
			      _(first_commit_msg_str));
		strbuf_addstr(&buf, "\n\n");
		if (is_fixup_flag(command, flag))
			strbuf_add_commented_lines(&buf, body, strlen(body));
		else
			strbuf_addstr(&buf, body);

		unuse_commit_buffer(head_commit, head_message);
	}

	if (!(message = logmsg_reencode(commit, NULL, encoding)))
		return error(_("could not read commit message of %s"),
			     oid_to_hex(&commit->object.oid));
	find_commit_subject(message, &body);

	if (command == TODO_SQUASH || is_fixup_flag(command, flag)) {
		res = append_squash_message(&buf, body, command, opts, flag);
	} else if (command == TODO_FIXUP) {
		strbuf_addf(&buf, "\n%c ", comment_line_char);
		strbuf_addf(&buf, _(skip_nth_commit_msg_fmt),
			    ++opts->current_fixup_count + 1);
		strbuf_addstr(&buf, "\n\n");
		strbuf_add_commented_lines(&buf, body, strlen(body));
	} else
		return error(_("unknown command: %d"), command);
	unuse_commit_buffer(commit, message);

	if (!res)
		res = write_message(buf.buf, buf.len, rebase_path_squash_msg(),
				    0);
	strbuf_release(&buf);

	if (!res) {
		strbuf_addf(&opts->current_fixups, "%s%s %s",
			    opts->current_fixups.len ? "\n" : "",
			    command_to_string(command),
			    oid_to_hex(&commit->object.oid));
		res = write_message(opts->current_fixups.buf,
				    opts->current_fixups.len,
				    rebase_path_current_fixups(), 0);
	}

	return res;
}

static void flush_rewritten_pending(void)
{
	struct strbuf buf = STRBUF_INIT;
	struct object_id newoid;
	FILE *out;

	if (strbuf_read_file(&buf, rebase_path_rewritten_pending(), (GIT_MAX_HEXSZ + 1) * 2) > 0 &&
	    !get_oid("HEAD", &newoid) &&
	    (out = fopen_or_warn(rebase_path_rewritten_list(), "a"))) {
		char *bol = buf.buf, *eol;

		while (*bol) {
			eol = strchrnul(bol, '\n');
			fprintf(out, "%.*s %s\n", (int)(eol - bol),
					bol, oid_to_hex(&newoid));
			if (!*eol)
				break;
			bol = eol + 1;
		}
		fclose(out);
		unlink(rebase_path_rewritten_pending());
	}
	strbuf_release(&buf);
}

static void record_in_rewritten(struct object_id *oid,
		enum todo_command next_command)
{
	FILE *out = fopen_or_warn(rebase_path_rewritten_pending(), "a");

	if (!out)
		return;

	fprintf(out, "%s\n", oid_to_hex(oid));
	fclose(out);

	if (!is_fixup(next_command))
		flush_rewritten_pending();
}

static int should_edit(struct replay_opts *opts) {
	if (opts->edit < 0)
		/*
		 * Note that we only handle the case of non-conflicted
		 * commits; continue_single_pick() handles the conflicted
		 * commits itself instead of calling this function.
		 */
		return (opts->action == REPLAY_REVERT && isatty(0)) ? 1 : 0;
	return opts->edit;
}

static void refer_to_commit(struct replay_opts *opts,
			    struct strbuf *msgbuf, struct commit *commit)
{
	if (opts->commit_use_reference) {
		struct pretty_print_context ctx = {
			.abbrev = DEFAULT_ABBREV,
			.date_mode.type = DATE_SHORT,
		};
		format_commit_message(commit, "%h (%s, %ad)", msgbuf, &ctx);
	} else {
		strbuf_addstr(msgbuf, oid_to_hex(&commit->object.oid));
	}
}

static int do_pick_commit(struct repository *r,
			  struct todo_item *item,
			  struct replay_opts *opts,
			  int final_fixup, int *check_todo)
{
	unsigned int flags = should_edit(opts) ? EDIT_MSG : 0;
	const char *msg_file = should_edit(opts) ? NULL : git_path_merge_msg(r);
	struct object_id head;
	struct commit *base, *next, *parent;
	const char *base_label, *next_label;
	char *author = NULL;
	struct commit_message msg = { NULL, NULL, NULL, NULL };
	struct strbuf msgbuf = STRBUF_INIT;
	int res, unborn = 0, reword = 0, allow, drop_commit;
	enum todo_command command = item->command;
	struct commit *commit = item->commit;

	if (opts->no_commit) {
		/*
		 * We do not intend to commit immediately.  We just want to
		 * merge the differences in, so let's compute the tree
		 * that represents the "current" state for the merge machinery
		 * to work on.
		 */
		if (write_index_as_tree(&head, r->index, r->index_file, 0, NULL))
			return error(_("your index file is unmerged."));
	} else {
		unborn = get_oid("HEAD", &head);
		/* Do we want to generate a root commit? */
		if (is_pick_or_similar(command) && opts->have_squash_onto &&
		    oideq(&head, &opts->squash_onto)) {
			if (is_fixup(command))
				return error(_("cannot fixup root commit"));
			flags |= CREATE_ROOT_COMMIT;
			unborn = 1;
		} else if (unborn)
			oidcpy(&head, the_hash_algo->empty_tree);
		if (index_differs_from(r, unborn ? empty_tree_oid_hex() : "HEAD",
				       NULL, 0))
			return error_dirty_index(r, opts);
	}
	discard_index(r->index);

	if (!commit->parents)
		parent = NULL;
	else if (commit->parents->next) {
		/* Reverting or cherry-picking a merge commit */
		int cnt;
		struct commit_list *p;

		if (!opts->mainline)
			return error(_("commit %s is a merge but no -m option was given."),
				oid_to_hex(&commit->object.oid));

		for (cnt = 1, p = commit->parents;
		     cnt != opts->mainline && p;
		     cnt++)
			p = p->next;
		if (cnt != opts->mainline || !p)
			return error(_("commit %s does not have parent %d"),
				oid_to_hex(&commit->object.oid), opts->mainline);
		parent = p->item;
	} else if (1 < opts->mainline)
		/*
		 *  Non-first parent explicitly specified as mainline for
		 *  non-merge commit
		 */
		return error(_("commit %s does not have parent %d"),
			     oid_to_hex(&commit->object.oid), opts->mainline);
	else
		parent = commit->parents->item;

	if (get_message(commit, &msg) != 0)
		return error(_("cannot get commit message for %s"),
			oid_to_hex(&commit->object.oid));

	if (opts->allow_ff && !is_fixup(command) &&
	    ((parent && oideq(&parent->object.oid, &head)) ||
	     (!parent && unborn))) {
		if (is_rebase_i(opts))
			write_author_script(msg.message);
		res = fast_forward_to(r, &commit->object.oid, &head, unborn,
			opts);
		if (res || command != TODO_REWORD)
			goto leave;
		reword = 1;
		msg_file = NULL;
		goto fast_forward_edit;
	}
	if (parent && parse_commit(parent) < 0)
		/* TRANSLATORS: The first %s will be a "todo" command like
		   "revert" or "pick", the second %s a SHA1. */
		return error(_("%s: cannot parse parent commit %s"),
			command_to_string(command),
			oid_to_hex(&parent->object.oid));

	/*
	 * "commit" is an existing commit.  We would want to apply
	 * the difference it introduces since its first parent "prev"
	 * on top of the current HEAD if we are cherry-pick.  Or the
	 * reverse of it if we are revert.
	 */

	if (command == TODO_REVERT) {
		base = commit;
		base_label = msg.label;
		next = parent;
		next_label = msg.parent_label;
		if (opts->commit_use_reference) {
			strbuf_addstr(&msgbuf,
				"# *** SAY WHY WE ARE REVERTING ON THE TITLE LINE ***");
		} else {
			strbuf_addstr(&msgbuf, "Revert \"");
			strbuf_addstr(&msgbuf, msg.subject);
			strbuf_addstr(&msgbuf, "\"");
		}
		strbuf_addstr(&msgbuf, "\n\nThis reverts commit ");
		refer_to_commit(opts, &msgbuf, commit);

		if (commit->parents && commit->parents->next) {
			strbuf_addstr(&msgbuf, ", reversing\nchanges made to ");
			refer_to_commit(opts, &msgbuf, parent);
		}
		strbuf_addstr(&msgbuf, ".\n");
	} else {
		const char *p;

		base = parent;
		base_label = msg.parent_label;
		next = commit;
		next_label = msg.label;

		/* Append the commit log message to msgbuf. */
		if (find_commit_subject(msg.message, &p))
			strbuf_addstr(&msgbuf, p);

		if (opts->record_origin) {
			strbuf_complete_line(&msgbuf);
			if (!has_conforming_footer(&msgbuf, NULL, 0))
				strbuf_addch(&msgbuf, '\n');
			strbuf_addstr(&msgbuf, cherry_picked_prefix);
			strbuf_addstr(&msgbuf, oid_to_hex(&commit->object.oid));
			strbuf_addstr(&msgbuf, ")\n");
		}
		if (!is_fixup(command))
			author = get_author(msg.message);
	}

	if (command == TODO_REWORD)
		reword = 1;
	else if (is_fixup(command)) {
		if (update_squash_messages(r, command, commit,
					   opts, item->flags)) {
			res = -1;
			goto leave;
		}
		flags |= AMEND_MSG;
		if (!final_fixup)
			msg_file = rebase_path_squash_msg();
		else if (file_exists(rebase_path_fixup_msg())) {
			flags |= VERBATIM_MSG;
			msg_file = rebase_path_fixup_msg();
		} else {
			const char *dest = git_path_squash_msg(r);
			unlink(dest);
			if (copy_file(dest, rebase_path_squash_msg(), 0666)) {
				res = error(_("could not rename '%s' to '%s'"),
					    rebase_path_squash_msg(), dest);
				goto leave;
			}
			unlink(git_path_merge_msg(r));
			msg_file = dest;
			flags |= EDIT_MSG;
		}
	}

	if (opts->signoff && !is_fixup(command))
		append_signoff(&msgbuf, 0, 0);

	if (is_rebase_i(opts) && write_author_script(msg.message) < 0)
		res = -1;
	else if (!opts->strategy ||
		 !strcmp(opts->strategy, "recursive") ||
		 !strcmp(opts->strategy, "ort") ||
		 command == TODO_REVERT) {
		res = do_recursive_merge(r, base, next, base_label, next_label,
					 &head, &msgbuf, opts);
		if (res < 0)
			goto leave;

		res |= write_message(msgbuf.buf, msgbuf.len,
				     git_path_merge_msg(r), 0);
	} else {
		struct commit_list *common = NULL;
		struct commit_list *remotes = NULL;

		res = write_message(msgbuf.buf, msgbuf.len,
				    git_path_merge_msg(r), 0);

		commit_list_insert(base, &common);
		commit_list_insert(next, &remotes);
		res |= try_merge_command(r, opts->strategy,
					 opts->xopts_nr, (const char **)opts->xopts,
					common, oid_to_hex(&head), remotes);
		free_commit_list(common);
		free_commit_list(remotes);
	}

	/*
	 * If the merge was clean or if it failed due to conflict, we write
	 * CHERRY_PICK_HEAD for the subsequent invocation of commit to use.
	 * However, if the merge did not even start, then we don't want to
	 * write it at all.
	 */
	if ((command == TODO_PICK || command == TODO_REWORD ||
	     command == TODO_EDIT) && !opts->no_commit &&
	    (res == 0 || res == 1) &&
	    update_ref(NULL, "CHERRY_PICK_HEAD", &commit->object.oid, NULL,
		       REF_NO_DEREF, UPDATE_REFS_MSG_ON_ERR))
		res = -1;
	if (command == TODO_REVERT && ((opts->no_commit && res == 0) || res == 1) &&
	    update_ref(NULL, "REVERT_HEAD", &commit->object.oid, NULL,
		       REF_NO_DEREF, UPDATE_REFS_MSG_ON_ERR))
		res = -1;

	if (res) {
		error(command == TODO_REVERT
		      ? _("could not revert %s... %s")
		      : _("could not apply %s... %s"),
		      short_commit_name(commit), msg.subject);
		print_advice(r, res == 1, opts);
		repo_rerere(r, opts->allow_rerere_auto);
		goto leave;
	}

	drop_commit = 0;
	allow = allow_empty(r, opts, commit);
	if (allow < 0) {
		res = allow;
		goto leave;
	} else if (allow == 1) {
		flags |= ALLOW_EMPTY;
	} else if (allow == 2) {
		drop_commit = 1;
		refs_delete_ref(get_main_ref_store(r), "", "CHERRY_PICK_HEAD",
				NULL, 0);
		unlink(git_path_merge_msg(r));
		unlink(git_path_auto_merge(r));
		fprintf(stderr,
			_("dropping %s %s -- patch contents already upstream\n"),
			oid_to_hex(&commit->object.oid), msg.subject);
	} /* else allow == 0 and there's nothing special to do */
	if (!opts->no_commit && !drop_commit) {
		if (author || command == TODO_REVERT || (flags & AMEND_MSG))
			res = do_commit(r, msg_file, author, opts, flags,
					commit? &commit->object.oid : NULL);
		else
			res = error(_("unable to parse commit author"));
		*check_todo = !!(flags & EDIT_MSG);
		if (!res && reword) {
fast_forward_edit:
			res = run_git_commit(NULL, opts, EDIT_MSG |
					     VERIFY_MSG | AMEND_MSG |
					     (flags & ALLOW_EMPTY));
			*check_todo = 1;
		}
	}


	if (!res && final_fixup) {
		unlink(rebase_path_fixup_msg());
		unlink(rebase_path_squash_msg());
		unlink(rebase_path_current_fixups());
		strbuf_reset(&opts->current_fixups);
		opts->current_fixup_count = 0;
	}

leave:
	free_message(commit, &msg);
	free(author);
	strbuf_release(&msgbuf);
	update_abort_safety_file();

	return res;
}

static int prepare_revs(struct replay_opts *opts)
{
	/*
	 * picking (but not reverting) ranges (but not individual revisions)
	 * should be done in reverse
	 */
	if (opts->action == REPLAY_PICK && !opts->revs->no_walk)
		opts->revs->reverse ^= 1;

	if (prepare_revision_walk(opts->revs))
		return error(_("revision walk setup failed"));

	return 0;
}

static int read_and_refresh_cache(struct repository *r,
				  struct replay_opts *opts)
{
	struct lock_file index_lock = LOCK_INIT;
	int index_fd = repo_hold_locked_index(r, &index_lock, 0);
	if (repo_read_index(r) < 0) {
		rollback_lock_file(&index_lock);
		return error(_("git %s: failed to read the index"),
			action_name(opts));
	}
	refresh_index(r->index, REFRESH_QUIET|REFRESH_UNMERGED, NULL, NULL, NULL);

	if (index_fd >= 0) {
		if (write_locked_index(r->index, &index_lock,
				       COMMIT_LOCK | SKIP_IF_UNCHANGED)) {
			return error(_("git %s: failed to refresh the index"),
				action_name(opts));
		}
	}

	/*
	 * If we are resolving merges in any way other than "ort", then
	 * expand the sparse index.
	 */
	if (opts->strategy && strcmp(opts->strategy, "ort"))
		ensure_full_index(r->index);
	return 0;
}

void todo_list_release(struct todo_list *todo_list)
{
	strbuf_release(&todo_list->buf);
	FREE_AND_NULL(todo_list->items);
	todo_list->nr = todo_list->alloc = 0;
}

static struct todo_item *append_new_todo(struct todo_list *todo_list)
{
	ALLOC_GROW(todo_list->items, todo_list->nr + 1, todo_list->alloc);
	todo_list->total_nr++;
	return todo_list->items + todo_list->nr++;
}

const char *todo_item_get_arg(struct todo_list *todo_list,
			      struct todo_item *item)
{
	return todo_list->buf.buf + item->arg_offset;
}

static int is_command(enum todo_command command, const char **bol)
{
	const char *str = todo_command_info[command].str;
	const char nick = todo_command_info[command].c;
	const char *p = *bol;

	return (skip_prefix(p, str, &p) || (nick && *p++ == nick)) &&
		(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || !*p) &&
		(*bol = p);
}

static int check_label_or_ref_arg(enum todo_command command, const char *arg)
{
	switch (command) {
	case TODO_LABEL:
		/*
		 * '#' is not a valid label as the merge command uses it to
		 * separate merge parents from the commit subject.
		 */
		if (!strcmp(arg, "#") ||
		    check_refname_format(arg, REFNAME_ALLOW_ONELEVEL))
			return error(_("'%s' is not a valid label"), arg);
		break;

	case TODO_UPDATE_REF:
		if (check_refname_format(arg, REFNAME_ALLOW_ONELEVEL))
			return error(_("'%s' is not a valid refname"), arg);
		if (check_refname_format(arg, 0))
			return error(_("update-ref requires a fully qualified "
				       "refname e.g. refs/heads/%s"), arg);
		break;

	default:
		BUG("unexpected todo_command");
	}

	return 0;
}

static int parse_insn_line(struct repository *r, struct todo_item *item,
			   const char *buf, const char *bol, char *eol)
{
	struct object_id commit_oid;
	char *end_of_object_name;
	int i, saved, status, padding;

	item->flags = 0;

	/* left-trim */
	bol += strspn(bol, " \t");

	if (bol == eol || *bol == '\r' || *bol == comment_line_char) {
		item->command = TODO_COMMENT;
		item->commit = NULL;
		item->arg_offset = bol - buf;
		item->arg_len = eol - bol;
		return 0;
	}

	for (i = 0; i < TODO_COMMENT; i++)
		if (is_command(i, &bol)) {
			item->command = i;
			break;
		}
	if (i >= TODO_COMMENT)
		return error(_("invalid command '%.*s'"),
			     (int)strcspn(bol, " \t\r\n"), bol);

	/* Eat up extra spaces/ tabs before object name */
	padding = strspn(bol, " \t");
	bol += padding;

	if (item->command == TODO_NOOP || item->command == TODO_BREAK) {
		if (bol != eol)
			return error(_("%s does not accept arguments: '%s'"),
				     command_to_string(item->command), bol);
		item->commit = NULL;
		item->arg_offset = bol - buf;
		item->arg_len = eol - bol;
		return 0;
	}

	if (!padding)
		return error(_("missing arguments for %s"),
			     command_to_string(item->command));

	if (item->command == TODO_EXEC || item->command == TODO_LABEL ||
	    item->command == TODO_RESET || item->command == TODO_UPDATE_REF) {
		int ret = 0;

		item->commit = NULL;
		item->arg_offset = bol - buf;
		item->arg_len = (int)(eol - bol);
		if (item->command == TODO_LABEL ||
		    item->command == TODO_UPDATE_REF) {
			saved = *eol;
			*eol = '\0';
			ret = check_label_or_ref_arg(item->command, bol);
			*eol = saved;
		}
		return ret;
	}

	if (item->command == TODO_FIXUP) {
		if (skip_prefix(bol, "-C", &bol)) {
			bol += strspn(bol, " \t");
			item->flags |= TODO_REPLACE_FIXUP_MSG;
		} else if (skip_prefix(bol, "-c", &bol)) {
			bol += strspn(bol, " \t");
			item->flags |= TODO_EDIT_FIXUP_MSG;
		}
	}

	if (item->command == TODO_MERGE) {
		if (skip_prefix(bol, "-C", &bol))
			bol += strspn(bol, " \t");
		else if (skip_prefix(bol, "-c", &bol)) {
			bol += strspn(bol, " \t");
			item->flags |= TODO_EDIT_MERGE_MSG;
		} else {
			item->flags |= TODO_EDIT_MERGE_MSG;
			item->commit = NULL;
			item->arg_offset = bol - buf;
			item->arg_len = (int)(eol - bol);
			return 0;
		}
	}

	end_of_object_name = (char *) bol + strcspn(bol, " \t\n");
	saved = *end_of_object_name;
	*end_of_object_name = '\0';
	status = get_oid(bol, &commit_oid);
	if (status < 0)
		error(_("could not parse '%s'"), bol); /* return later */
	*end_of_object_name = saved;

	bol = end_of_object_name + strspn(end_of_object_name, " \t");
	item->arg_offset = bol - buf;
	item->arg_len = (int)(eol - bol);

	if (status < 0)
		return status;

	item->commit = lookup_commit_reference(r, &commit_oid);
	return item->commit ? 0 : -1;
}

int sequencer_get_last_command(struct repository *r, enum replay_action *action)
{
	const char *todo_file, *bol;
	struct strbuf buf = STRBUF_INIT;
	int ret = 0;

	todo_file = git_path_todo_file();
	if (strbuf_read_file(&buf, todo_file, 0) < 0) {
		if (errno == ENOENT || errno == ENOTDIR)
			return -1;
		else
			return error_errno("unable to open '%s'", todo_file);
	}
	bol = buf.buf + strspn(buf.buf, " \t\r\n");
	if (is_command(TODO_PICK, &bol) && (*bol == ' ' || *bol == '\t'))
		*action = REPLAY_PICK;
	else if (is_command(TODO_REVERT, &bol) &&
		 (*bol == ' ' || *bol == '\t'))
		*action = REPLAY_REVERT;
	else
		ret = -1;

	strbuf_release(&buf);

	return ret;
}

int todo_list_parse_insn_buffer(struct repository *r, char *buf,
				struct todo_list *todo_list)
{
	struct todo_item *item;
	char *p = buf, *next_p;
	int i, res = 0, fixup_okay = file_exists(rebase_path_done());

	todo_list->current = todo_list->nr = 0;

	for (i = 1; *p; i++, p = next_p) {
		char *eol = strchrnul(p, '\n');

		next_p = *eol ? eol + 1 /* skip LF */ : eol;

		if (p != eol && eol[-1] == '\r')
			eol--; /* strip Carriage Return */

		item = append_new_todo(todo_list);
		item->offset_in_buf = p - todo_list->buf.buf;
		if (parse_insn_line(r, item, buf, p, eol)) {
			res = error(_("invalid line %d: %.*s"),
				i, (int)(eol - p), p);
			item->command = TODO_COMMENT + 1;
			item->arg_offset = p - buf;
			item->arg_len = (int)(eol - p);
			item->commit = NULL;
		}

		if (fixup_okay)
			; /* do nothing */
		else if (is_fixup(item->command))
			return error(_("cannot '%s' without a previous commit"),
				command_to_string(item->command));
		else if (!is_noop(item->command))
			fixup_okay = 1;
	}

	return res;
}

static int count_commands(struct todo_list *todo_list)
{
	int count = 0, i;

	for (i = 0; i < todo_list->nr; i++)
		if (todo_list->items[i].command != TODO_COMMENT)
			count++;

	return count;
}

static int get_item_line_offset(struct todo_list *todo_list, int index)
{
	return index < todo_list->nr ?
		todo_list->items[index].offset_in_buf : todo_list->buf.len;
}

static const char *get_item_line(struct todo_list *todo_list, int index)
{
	return todo_list->buf.buf + get_item_line_offset(todo_list, index);
}

static int get_item_line_length(struct todo_list *todo_list, int index)
{
	return get_item_line_offset(todo_list, index + 1)
		-  get_item_line_offset(todo_list, index);
}

static ssize_t strbuf_read_file_or_whine(struct strbuf *sb, const char *path)
{
	int fd;
	ssize_t len;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return error_errno(_("could not open '%s'"), path);
	len = strbuf_read(sb, fd, 0);
	close(fd);
	if (len < 0)
		return error(_("could not read '%s'."), path);
	return len;
}

static int have_finished_the_last_pick(void)
{
	struct strbuf buf = STRBUF_INIT;
	const char *eol;
	const char *todo_path = git_path_todo_file();
	int ret = 0;

	if (strbuf_read_file(&buf, todo_path, 0) < 0) {
		if (errno == ENOENT) {
			return 0;
		} else {
			error_errno("unable to open '%s'", todo_path);
			return 0;
		}
	}
	/* If there is only one line then we are done */
	eol = strchr(buf.buf, '\n');
	if (!eol || !eol[1])
		ret = 1;

	strbuf_release(&buf);

	return ret;
}

void sequencer_post_commit_cleanup(struct repository *r, int verbose)
{
	struct replay_opts opts = REPLAY_OPTS_INIT;
	int need_cleanup = 0;

	if (refs_ref_exists(get_main_ref_store(r), "CHERRY_PICK_HEAD")) {
		if (!refs_delete_ref(get_main_ref_store(r), "",
				     "CHERRY_PICK_HEAD", NULL, 0) &&
		    verbose)
			warning(_("cancelling a cherry picking in progress"));
		opts.action = REPLAY_PICK;
		need_cleanup = 1;
	}

	if (refs_ref_exists(get_main_ref_store(r), "REVERT_HEAD")) {
		if (!refs_delete_ref(get_main_ref_store(r), "", "REVERT_HEAD",
				     NULL, 0) &&
		    verbose)
			warning(_("cancelling a revert in progress"));
		opts.action = REPLAY_REVERT;
		need_cleanup = 1;
	}

	unlink(git_path_auto_merge(r));

	if (!need_cleanup)
		return;

	if (!have_finished_the_last_pick())
		return;

	sequencer_remove_state(&opts);
}

static void todo_list_write_total_nr(struct todo_list *todo_list)
{
	FILE *f = fopen_or_warn(rebase_path_msgtotal(), "w");

	if (f) {
		fprintf(f, "%d\n", todo_list->total_nr);
		fclose(f);
	}
}

static int read_populate_todo(struct repository *r,
			      struct todo_list *todo_list,
			      struct replay_opts *opts)
{
	const char *todo_file = get_todo_path(opts);
	int res;

	strbuf_reset(&todo_list->buf);
	if (strbuf_read_file_or_whine(&todo_list->buf, todo_file) < 0)
		return -1;

	res = todo_list_parse_insn_buffer(r, todo_list->buf.buf, todo_list);
	if (res) {
		if (is_rebase_i(opts))
			return error(_("please fix this using "
				       "'git rebase --edit-todo'."));
		return error(_("unusable instruction sheet: '%s'"), todo_file);
	}

	if (!todo_list->nr &&
	    (!is_rebase_i(opts) || !file_exists(rebase_path_done())))
		return error(_("no commits parsed."));

	if (!is_rebase_i(opts)) {
		enum todo_command valid =
			opts->action == REPLAY_PICK ? TODO_PICK : TODO_REVERT;
		int i;

		for (i = 0; i < todo_list->nr; i++)
			if (valid == todo_list->items[i].command)
				continue;
			else if (valid == TODO_PICK)
				return error(_("cannot cherry-pick during a revert."));
			else
				return error(_("cannot revert during a cherry-pick."));
	}

	if (is_rebase_i(opts)) {
		struct todo_list done = TODO_LIST_INIT;

		if (strbuf_read_file(&done.buf, rebase_path_done(), 0) > 0 &&
		    !todo_list_parse_insn_buffer(r, done.buf.buf, &done))
			todo_list->done_nr = count_commands(&done);
		else
			todo_list->done_nr = 0;

		todo_list->total_nr = todo_list->done_nr
			+ count_commands(todo_list);
		todo_list_release(&done);

		todo_list_write_total_nr(todo_list);
	}

	return 0;
}

static int git_config_string_dup(char **dest,
				 const char *var, const char *value)
{
	if (!value)
		return config_error_nonbool(var);
	free(*dest);
	*dest = xstrdup(value);
	return 0;
}

static int populate_opts_cb(const char *key, const char *value, void *data)
{
	struct replay_opts *opts = data;
	int error_flag = 1;

	if (!value)
		error_flag = 0;
	else if (!strcmp(key, "options.no-commit"))
		opts->no_commit = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.edit"))
		opts->edit = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.allow-empty"))
		opts->allow_empty =
			git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.allow-empty-message"))
		opts->allow_empty_message =
			git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.keep-redundant-commits"))
		opts->keep_redundant_commits =
			git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.signoff"))
		opts->signoff = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.record-origin"))
		opts->record_origin = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.allow-ff"))
		opts->allow_ff = git_config_bool_or_int(key, value, &error_flag);
	else if (!strcmp(key, "options.mainline"))
		opts->mainline = git_config_int(key, value);
	else if (!strcmp(key, "options.strategy"))
		git_config_string_dup(&opts->strategy, key, value);
	else if (!strcmp(key, "options.gpg-sign"))
		git_config_string_dup(&opts->gpg_sign, key, value);
	else if (!strcmp(key, "options.strategy-option")) {
		ALLOC_GROW(opts->xopts, opts->xopts_nr + 1, opts->xopts_alloc);
		opts->xopts[opts->xopts_nr++] = xstrdup(value);
	} else if (!strcmp(key, "options.allow-rerere-auto"))
		opts->allow_rerere_auto =
			git_config_bool_or_int(key, value, &error_flag) ?
				RERERE_AUTOUPDATE : RERERE_NOAUTOUPDATE;
	else if (!strcmp(key, "options.default-msg-cleanup")) {
		opts->explicit_cleanup = 1;
		opts->default_msg_cleanup = get_cleanup_mode(value, 1);
	} else
		return error(_("invalid key: %s"), key);

	if (!error_flag)
		return error(_("invalid value for '%s': '%s'"), key, value);

	return 0;
}

void parse_strategy_opts(struct replay_opts *opts, char *raw_opts)
{
	int i;
	int count;
	char *strategy_opts_string = raw_opts;

	if (*strategy_opts_string == ' ')
		strategy_opts_string++;

	count = split_cmdline(strategy_opts_string,
			      (const char ***)&opts->xopts);
	if (count < 0)
		die(_("could not split '%s': %s"), strategy_opts_string,
			    split_cmdline_strerror(count));
	opts->xopts_nr = count;
	for (i = 0; i < opts->xopts_nr; i++) {
		const char *arg = opts->xopts[i];

		skip_prefix(arg, "--", &arg);
		opts->xopts[i] = xstrdup(arg);
	}
}

static void read_strategy_opts(struct replay_opts *opts, struct strbuf *buf)
{
	strbuf_reset(buf);
	if (!read_oneliner(buf, rebase_path_strategy(), 0))
		return;
	opts->strategy = strbuf_detach(buf, NULL);
	if (!read_oneliner(buf, rebase_path_strategy_opts(), 0))
		return;

	parse_strategy_opts(opts, buf->buf);
}

static int read_populate_opts(struct replay_opts *opts)
{
	if (is_rebase_i(opts)) {
		struct strbuf buf = STRBUF_INIT;
		int ret = 0;

		if (read_oneliner(&buf, rebase_path_gpg_sign_opt(),
				  READ_ONELINER_SKIP_IF_EMPTY)) {
			if (!starts_with(buf.buf, "-S"))
				strbuf_reset(&buf);
			else {
				free(opts->gpg_sign);
				opts->gpg_sign = xstrdup(buf.buf + 2);
			}
			strbuf_reset(&buf);
		}

		if (read_oneliner(&buf, rebase_path_allow_rerere_autoupdate(),
				  READ_ONELINER_SKIP_IF_EMPTY)) {
			if (!strcmp(buf.buf, "--rerere-autoupdate"))
				opts->allow_rerere_auto = RERERE_AUTOUPDATE;
			else if (!strcmp(buf.buf, "--no-rerere-autoupdate"))
				opts->allow_rerere_auto = RERERE_NOAUTOUPDATE;
			strbuf_reset(&buf);
		}

		if (file_exists(rebase_path_verbose()))
			opts->verbose = 1;

		if (file_exists(rebase_path_quiet()))
			opts->quiet = 1;

		if (file_exists(rebase_path_signoff())) {
			opts->allow_ff = 0;
			opts->signoff = 1;
		}

		if (file_exists(rebase_path_cdate_is_adate())) {
			opts->allow_ff = 0;
			opts->committer_date_is_author_date = 1;
		}

		if (file_exists(rebase_path_ignore_date())) {
			opts->allow_ff = 0;
			opts->ignore_date = 1;
		}

		if (file_exists(rebase_path_reschedule_failed_exec()))
			opts->reschedule_failed_exec = 1;
		else if (file_exists(rebase_path_no_reschedule_failed_exec()))
			opts->reschedule_failed_exec = 0;

		if (file_exists(rebase_path_drop_redundant_commits()))
			opts->drop_redundant_commits = 1;

		if (file_exists(rebase_path_keep_redundant_commits()))
			opts->keep_redundant_commits = 1;

		read_strategy_opts(opts, &buf);
		strbuf_reset(&buf);

		if (read_oneliner(&opts->current_fixups,
				  rebase_path_current_fixups(),
				  READ_ONELINER_SKIP_IF_EMPTY)) {
			const char *p = opts->current_fixups.buf;
			opts->current_fixup_count = 1;
			while ((p = strchr(p, '\n'))) {
				opts->current_fixup_count++;
				p++;
			}
		}

		if (read_oneliner(&buf, rebase_path_squash_onto(), 0)) {
			if (get_oid_committish(buf.buf, &opts->squash_onto) < 0) {
				ret = error(_("unusable squash-onto"));
				goto done_rebase_i;
			}
			opts->have_squash_onto = 1;
		}

done_rebase_i:
		strbuf_release(&buf);
		return ret;
	}

	if (!file_exists(git_path_opts_file()))
		return 0;
	/*
	 * The function git_parse_source(), called from git_config_from_file(),
	 * may die() in case of a syntactically incorrect file. We do not care
	 * about this case, though, because we wrote that file ourselves, so we
	 * are pretty certain that it is syntactically correct.
	 */
	if (git_config_from_file(populate_opts_cb, git_path_opts_file(), opts) < 0)
		return error(_("malformed options sheet: '%s'"),
			git_path_opts_file());
	return 0;
}

static void write_strategy_opts(struct replay_opts *opts)
{
	int i;
	struct strbuf buf = STRBUF_INIT;

	for (i = 0; i < opts->xopts_nr; ++i)
		strbuf_addf(&buf, " --%s", opts->xopts[i]);

	write_file(rebase_path_strategy_opts(), "%s\n", buf.buf);
	strbuf_release(&buf);
}

int write_basic_state(struct replay_opts *opts, const char *head_name,
		      struct commit *onto, const struct object_id *orig_head)
{
	if (head_name)
		write_file(rebase_path_head_name(), "%s\n", head_name);
	if (onto)
		write_file(rebase_path_onto(), "%s\n",
			   oid_to_hex(&onto->object.oid));
	if (orig_head)
		write_file(rebase_path_orig_head(), "%s\n",
			   oid_to_hex(orig_head));

	if (opts->quiet)
		write_file(rebase_path_quiet(), "%s", "");
	if (opts->verbose)
		write_file(rebase_path_verbose(), "%s", "");
	if (opts->strategy)
		write_file(rebase_path_strategy(), "%s\n", opts->strategy);
	if (opts->xopts_nr > 0)
		write_strategy_opts(opts);

	if (opts->allow_rerere_auto == RERERE_AUTOUPDATE)
		write_file(rebase_path_allow_rerere_autoupdate(), "--rerere-autoupdate\n");
	else if (opts->allow_rerere_auto == RERERE_NOAUTOUPDATE)
		write_file(rebase_path_allow_rerere_autoupdate(), "--no-rerere-autoupdate\n");

	if (opts->gpg_sign)
		write_file(rebase_path_gpg_sign_opt(), "-S%s\n", opts->gpg_sign);
	if (opts->signoff)
		write_file(rebase_path_signoff(), "--signoff\n");
	if (opts->drop_redundant_commits)
		write_file(rebase_path_drop_redundant_commits(), "%s", "");
	if (opts->keep_redundant_commits)
		write_file(rebase_path_keep_redundant_commits(), "%s", "");
	if (opts->committer_date_is_author_date)
		write_file(rebase_path_cdate_is_adate(), "%s", "");
	if (opts->ignore_date)
		write_file(rebase_path_ignore_date(), "%s", "");
	if (opts->reschedule_failed_exec)
		write_file(rebase_path_reschedule_failed_exec(), "%s", "");
	else
		write_file(rebase_path_no_reschedule_failed_exec(), "%s", "");

	return 0;
}

static int walk_revs_populate_todo(struct todo_list *todo_list,
				struct replay_opts *opts)
{
	enum todo_command command = opts->action == REPLAY_PICK ?
		TODO_PICK : TODO_REVERT;
	const char *command_string = todo_command_info[command].str;
	const char *encoding;
	struct commit *commit;

	if (prepare_revs(opts))
		return -1;

	encoding = get_log_output_encoding();

	while ((commit = get_revision(opts->revs))) {
		struct todo_item *item = append_new_todo(todo_list);
		const char *commit_buffer = logmsg_reencode(commit, NULL, encoding);
		const char *subject;
		int subject_len;

		item->command = command;
		item->commit = commit;
		item->arg_offset = 0;
		item->arg_len = 0;
		item->offset_in_buf = todo_list->buf.len;
		subject_len = find_commit_subject(commit_buffer, &subject);
		strbuf_addf(&todo_list->buf, "%s %s %.*s\n", command_string,
			short_commit_name(commit), subject_len, subject);
		unuse_commit_buffer(commit, commit_buffer);
	}

	if (!todo_list->nr)
		return error(_("empty commit set passed"));

	return 0;
}

static int create_seq_dir(struct repository *r)
{
	enum replay_action action;
	const char *in_progress_error = NULL;
	const char *in_progress_advice = NULL;
	unsigned int advise_skip =
		refs_ref_exists(get_main_ref_store(r), "REVERT_HEAD") ||
		refs_ref_exists(get_main_ref_store(r), "CHERRY_PICK_HEAD");

	if (!sequencer_get_last_command(r, &action)) {
		switch (action) {
		case REPLAY_REVERT:
			in_progress_error = _("revert is already in progress");
			in_progress_advice =
			_("try \"git revert (--continue | %s--abort | --quit)\"");
			break;
		case REPLAY_PICK:
			in_progress_error = _("cherry-pick is already in progress");
			in_progress_advice =
			_("try \"git cherry-pick (--continue | %s--abort | --quit)\"");
			break;
		default:
			BUG("unexpected action in create_seq_dir");
		}
	}
	if (in_progress_error) {
		error("%s", in_progress_error);
		if (advice_enabled(ADVICE_SEQUENCER_IN_USE))
			advise(in_progress_advice,
				advise_skip ? "--skip | " : "");
		return -1;
	}
	if (mkdir(git_path_seq_dir(), 0777) < 0)
		return error_errno(_("could not create sequencer directory '%s'"),
				   git_path_seq_dir());

	return 0;
}

static int save_head(const char *head)
{
	struct lock_file head_lock = LOCK_INIT;
	struct strbuf buf = STRBUF_INIT;
	int fd;
	ssize_t written;

	fd = hold_lock_file_for_update(&head_lock, git_path_head_file(), 0);
	if (fd < 0)
		return error_errno(_("could not lock HEAD"));
	strbuf_addf(&buf, "%s\n", head);
	written = write_in_full(fd, buf.buf, buf.len);
	strbuf_release(&buf);
	if (written < 0) {
		error_errno(_("could not write to '%s'"), git_path_head_file());
		rollback_lock_file(&head_lock);
		return -1;
	}
	if (commit_lock_file(&head_lock) < 0)
		return error(_("failed to finalize '%s'"), git_path_head_file());
	return 0;
}

static int rollback_is_safe(void)
{
	struct strbuf sb = STRBUF_INIT;
	struct object_id expected_head, actual_head;

	if (strbuf_read_file(&sb, git_path_abort_safety_file(), 0) >= 0) {
		strbuf_trim(&sb);
		if (get_oid_hex(sb.buf, &expected_head)) {
			strbuf_release(&sb);
			die(_("could not parse %s"), git_path_abort_safety_file());
		}
		strbuf_release(&sb);
	}
	else if (errno == ENOENT)
		oidclr(&expected_head);
	else
		die_errno(_("could not read '%s'"), git_path_abort_safety_file());

	if (get_oid("HEAD", &actual_head))
		oidclr(&actual_head);

	return oideq(&actual_head, &expected_head);
}

static int reset_merge(const struct object_id *oid)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	cmd.git_cmd = 1;
	strvec_pushl(&cmd.args, "reset", "--merge", NULL);

	if (!is_null_oid(oid))
		strvec_push(&cmd.args, oid_to_hex(oid));

	return run_command(&cmd);
}

static int rollback_single_pick(struct repository *r)
{
	struct object_id head_oid;

	if (!refs_ref_exists(get_main_ref_store(r), "CHERRY_PICK_HEAD") &&
	    !refs_ref_exists(get_main_ref_store(r), "REVERT_HEAD"))
		return error(_("no cherry-pick or revert in progress"));
	if (read_ref_full("HEAD", 0, &head_oid, NULL))
		return error(_("cannot resolve HEAD"));
	if (is_null_oid(&head_oid))
		return error(_("cannot abort from a branch yet to be born"));
	return reset_merge(&head_oid);
}

static int skip_single_pick(void)
{
	struct object_id head;

	if (read_ref_full("HEAD", 0, &head, NULL))
		return error(_("cannot resolve HEAD"));
	return reset_merge(&head);
}

int sequencer_rollback(struct repository *r, struct replay_opts *opts)
{
	FILE *f;
	struct object_id oid;
	struct strbuf buf = STRBUF_INIT;
	const char *p;

	f = fopen(git_path_head_file(), "r");
	if (!f && errno == ENOENT) {
		/*
		 * There is no multiple-cherry-pick in progress.
		 * If CHERRY_PICK_HEAD or REVERT_HEAD indicates
		 * a single-cherry-pick in progress, abort that.
		 */
		return rollback_single_pick(r);
	}
	if (!f)
		return error_errno(_("cannot open '%s'"), git_path_head_file());
	if (strbuf_getline_lf(&buf, f)) {
		error(_("cannot read '%s': %s"), git_path_head_file(),
		      ferror(f) ?  strerror(errno) : _("unexpected end of file"));
		fclose(f);
		goto fail;
	}
	fclose(f);
	if (parse_oid_hex(buf.buf, &oid, &p) || *p != '\0') {
		error(_("stored pre-cherry-pick HEAD file '%s' is corrupt"),
			git_path_head_file());
		goto fail;
	}
	if (is_null_oid(&oid)) {
		error(_("cannot abort from a branch yet to be born"));
		goto fail;
	}

	if (!rollback_is_safe()) {
		/* Do not error, just do not rollback */
		warning(_("You seem to have moved HEAD. "
			  "Not rewinding, check your HEAD!"));
	} else
	if (reset_merge(&oid))
		goto fail;
	strbuf_release(&buf);
	return sequencer_remove_state(opts);
fail:
	strbuf_release(&buf);
	return -1;
}

int sequencer_skip(struct repository *r, struct replay_opts *opts)
{
	enum replay_action action = -1;
	sequencer_get_last_command(r, &action);

	/*
	 * Check whether the subcommand requested to skip the commit is actually
	 * in progress and that it's safe to skip the commit.
	 *
	 * opts->action tells us which subcommand requested to skip the commit.
	 * If the corresponding .git/<ACTION>_HEAD exists, we know that the
	 * action is in progress and we can skip the commit.
	 *
	 * Otherwise we check that the last instruction was related to the
	 * particular subcommand we're trying to execute and barf if that's not
	 * the case.
	 *
	 * Finally we check that the rollback is "safe", i.e., has the HEAD
	 * moved? In this case, it doesn't make sense to "reset the merge" and
	 * "skip the commit" as the user already handled this by committing. But
	 * we'd not want to barf here, instead give advice on how to proceed. We
	 * only need to check that when .git/<ACTION>_HEAD doesn't exist because
	 * it gets removed when the user commits, so if it still exists we're
	 * sure the user can't have committed before.
	 */
	switch (opts->action) {
	case REPLAY_REVERT:
		if (!refs_ref_exists(get_main_ref_store(r), "REVERT_HEAD")) {
			if (action != REPLAY_REVERT)
				return error(_("no revert in progress"));
			if (!rollback_is_safe())
				goto give_advice;
		}
		break;
	case REPLAY_PICK:
		if (!refs_ref_exists(get_main_ref_store(r),
				     "CHERRY_PICK_HEAD")) {
			if (action != REPLAY_PICK)
				return error(_("no cherry-pick in progress"));
			if (!rollback_is_safe())
				goto give_advice;
		}
		break;
	default:
		BUG("unexpected action in sequencer_skip");
	}

	if (skip_single_pick())
		return error(_("failed to skip the commit"));
	if (!is_directory(git_path_seq_dir()))
		return 0;

	return sequencer_continue(r, opts);

give_advice:
	error(_("there is nothing to skip"));

	if (advice_enabled(ADVICE_RESOLVE_CONFLICT)) {
		advise(_("have you committed already?\n"
			 "try \"git %s --continue\""),
			 action == REPLAY_REVERT ? "revert" : "cherry-pick");
	}
	return -1;
}

static int save_todo(struct todo_list *todo_list, struct replay_opts *opts)
{
	struct lock_file todo_lock = LOCK_INIT;
	const char *todo_path = get_todo_path(opts);
	int next = todo_list->current, offset, fd;

	/*
	 * rebase -i writes "git-rebase-todo" without the currently executing
	 * command, appending it to "done" instead.
	 */
	if (is_rebase_i(opts))
		next++;

	fd = hold_lock_file_for_update(&todo_lock, todo_path, 0);
	if (fd < 0)
		return error_errno(_("could not lock '%s'"), todo_path);
	offset = get_item_line_offset(todo_list, next);
	if (write_in_full(fd, todo_list->buf.buf + offset,
			todo_list->buf.len - offset) < 0)
		return error_errno(_("could not write to '%s'"), todo_path);
	if (commit_lock_file(&todo_lock) < 0)
		return error(_("failed to finalize '%s'"), todo_path);

	if (is_rebase_i(opts) && next > 0) {
		const char *done = rebase_path_done();
		int fd = open(done, O_CREAT | O_WRONLY | O_APPEND, 0666);
		int ret = 0;

		if (fd < 0)
			return 0;
		if (write_in_full(fd, get_item_line(todo_list, next - 1),
				  get_item_line_length(todo_list, next - 1))
		    < 0)
			ret = error_errno(_("could not write to '%s'"), done);
		if (close(fd) < 0)
			ret = error_errno(_("failed to finalize '%s'"), done);
		return ret;
	}
	return 0;
}

static int save_opts(struct replay_opts *opts)
{
	const char *opts_file = git_path_opts_file();
	int res = 0;

	if (opts->no_commit)
		res |= git_config_set_in_file_gently(opts_file,
					"options.no-commit", "true");
	if (opts->edit >= 0)
		res |= git_config_set_in_file_gently(opts_file, "options.edit",
						     opts->edit ? "true" : "false");
	if (opts->allow_empty)
		res |= git_config_set_in_file_gently(opts_file,
					"options.allow-empty", "true");
	if (opts->allow_empty_message)
		res |= git_config_set_in_file_gently(opts_file,
				"options.allow-empty-message", "true");
	if (opts->keep_redundant_commits)
		res |= git_config_set_in_file_gently(opts_file,
				"options.keep-redundant-commits", "true");
	if (opts->signoff)
		res |= git_config_set_in_file_gently(opts_file,
					"options.signoff", "true");
	if (opts->record_origin)
		res |= git_config_set_in_file_gently(opts_file,
					"options.record-origin", "true");
	if (opts->allow_ff)
		res |= git_config_set_in_file_gently(opts_file,
					"options.allow-ff", "true");
	if (opts->mainline) {
		struct strbuf buf = STRBUF_INIT;
		strbuf_addf(&buf, "%d", opts->mainline);
		res |= git_config_set_in_file_gently(opts_file,
					"options.mainline", buf.buf);
		strbuf_release(&buf);
	}
	if (opts->strategy)
		res |= git_config_set_in_file_gently(opts_file,
					"options.strategy", opts->strategy);
	if (opts->gpg_sign)
		res |= git_config_set_in_file_gently(opts_file,
					"options.gpg-sign", opts->gpg_sign);
	if (opts->xopts) {
		int i;
		for (i = 0; i < opts->xopts_nr; i++)
			res |= git_config_set_multivar_in_file_gently(opts_file,
					"options.strategy-option",
					opts->xopts[i], "^$", 0);
	}
	if (opts->allow_rerere_auto)
		res |= git_config_set_in_file_gently(opts_file,
				"options.allow-rerere-auto",
				opts->allow_rerere_auto == RERERE_AUTOUPDATE ?
				"true" : "false");

	if (opts->explicit_cleanup)
		res |= git_config_set_in_file_gently(opts_file,
				"options.default-msg-cleanup",
				describe_cleanup_mode(opts->default_msg_cleanup));
	return res;
}

static int make_patch(struct repository *r,
		      struct commit *commit,
		      struct replay_opts *opts)
{
	struct strbuf buf = STRBUF_INIT;
	struct rev_info log_tree_opt;
	const char *subject;
	char hex[GIT_MAX_HEXSZ + 1];
	int res = 0;

	oid_to_hex_r(hex, &commit->object.oid);
	if (write_message(hex, strlen(hex), rebase_path_stopped_sha(), 1) < 0)
		return -1;
	res |= write_rebase_head(&commit->object.oid);

	strbuf_addf(&buf, "%s/patch", get_dir(opts));
	memset(&log_tree_opt, 0, sizeof(log_tree_opt));
	repo_init_revisions(r, &log_tree_opt, NULL);
	log_tree_opt.abbrev = 0;
	log_tree_opt.diff = 1;
	log_tree_opt.diffopt.output_format = DIFF_FORMAT_PATCH;
	log_tree_opt.disable_stdin = 1;
	log_tree_opt.no_commit_id = 1;
	log_tree_opt.diffopt.file = fopen(buf.buf, "w");
	log_tree_opt.diffopt.use_color = GIT_COLOR_NEVER;
	if (!log_tree_opt.diffopt.file)
		res |= error_errno(_("could not open '%s'"), buf.buf);
	else {
		res |= log_tree_commit(&log_tree_opt, commit);
		fclose(log_tree_opt.diffopt.file);
	}
	strbuf_reset(&buf);

	strbuf_addf(&buf, "%s/message", get_dir(opts));
	if (!file_exists(buf.buf)) {
		const char *encoding = get_commit_output_encoding();
		const char *commit_buffer = logmsg_reencode(commit, NULL, encoding);
		find_commit_subject(commit_buffer, &subject);
		res |= write_message(subject, strlen(subject), buf.buf, 1);
		unuse_commit_buffer(commit, commit_buffer);
	}
	strbuf_release(&buf);
	release_revisions(&log_tree_opt);

	return res;
}

static int intend_to_amend(void)
{
	struct object_id head;
	char *p;

	if (get_oid("HEAD", &head))
		return error(_("cannot read HEAD"));

	p = oid_to_hex(&head);
	return write_message(p, strlen(p), rebase_path_amend(), 1);
}

static int error_with_patch(struct repository *r,
			    struct commit *commit,
			    const char *subject, int subject_len,
			    struct replay_opts *opts,
			    int exit_code, int to_amend)
{
	if (commit) {
		if (make_patch(r, commit, opts))
			return -1;
	} else if (copy_file(rebase_path_message(),
			     git_path_merge_msg(r), 0666))
		return error(_("unable to copy '%s' to '%s'"),
			     git_path_merge_msg(r), rebase_path_message());

	if (to_amend) {
		if (intend_to_amend())
			return -1;

		fprintf(stderr,
			_("You can amend the commit now, with\n"
			  "\n"
			  "  git commit --amend %s\n"
			  "\n"
			  "Once you are satisfied with your changes, run\n"
			  "\n"
			  "  git rebase --continue\n"),
			gpg_sign_opt_quoted(opts));
	} else if (exit_code) {
		if (commit)
			fprintf_ln(stderr, _("Could not apply %s... %.*s"),
				   short_commit_name(commit), subject_len, subject);
		else
			/*
			 * We don't have the hash of the parent so
			 * just print the line from the todo file.
			 */
			fprintf_ln(stderr, _("Could not merge %.*s"),
				   subject_len, subject);
	}

	return exit_code;
}

static int error_failed_squash(struct repository *r,
			       struct commit *commit,
			       struct replay_opts *opts,
			       int subject_len,
			       const char *subject)
{
	if (copy_file(rebase_path_message(), rebase_path_squash_msg(), 0666))
		return error(_("could not copy '%s' to '%s'"),
			rebase_path_squash_msg(), rebase_path_message());
	unlink(git_path_merge_msg(r));
	if (copy_file(git_path_merge_msg(r), rebase_path_message(), 0666))
		return error(_("could not copy '%s' to '%s'"),
			     rebase_path_message(),
			     git_path_merge_msg(r));
	return error_with_patch(r, commit, subject, subject_len, opts, 1, 0);
}

static int do_exec(struct repository *r, const char *command_line)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	int dirty, status;

	fprintf(stderr, _("Executing: %s\n"), command_line);
	cmd.use_shell = 1;
	strvec_push(&cmd.args, command_line);
	status = run_command(&cmd);

	/* force re-reading of the cache */
	discard_index(r->index);
	if (repo_read_index(r) < 0)
		return error(_("could not read index"));

	dirty = require_clean_work_tree(r, "rebase", NULL, 1, 1);

	if (status) {
		warning(_("execution failed: %s\n%s"
			  "You can fix the problem, and then run\n"
			  "\n"
			  "  git rebase --continue\n"
			  "\n"),
			command_line,
			dirty ? N_("and made changes to the index and/or the "
				"working tree\n") : "");
		if (status == 127)
			/* command not found */
			status = 1;
	} else if (dirty) {
		warning(_("execution succeeded: %s\nbut "
			  "left changes to the index and/or the working tree\n"
			  "Commit or stash your changes, and then run\n"
			  "\n"
			  "  git rebase --continue\n"
			  "\n"), command_line);
		status = 1;
	}

	return status;
}

__attribute__((format (printf, 2, 3)))
static int safe_append(const char *filename, const char *fmt, ...)
{
	va_list ap;
	struct lock_file lock = LOCK_INIT;
	int fd = hold_lock_file_for_update(&lock, filename,
					   LOCK_REPORT_ON_ERROR);
	struct strbuf buf = STRBUF_INIT;

	if (fd < 0)
		return -1;

	if (strbuf_read_file(&buf, filename, 0) < 0 && errno != ENOENT) {
		error_errno(_("could not read '%s'"), filename);
		rollback_lock_file(&lock);
		return -1;
	}
	strbuf_complete(&buf, '\n');
	va_start(ap, fmt);
	strbuf_vaddf(&buf, fmt, ap);
	va_end(ap);

	if (write_in_full(fd, buf.buf, buf.len) < 0) {
		error_errno(_("could not write to '%s'"), filename);
		strbuf_release(&buf);
		rollback_lock_file(&lock);
		return -1;
	}
	if (commit_lock_file(&lock) < 0) {
		strbuf_release(&buf);
		rollback_lock_file(&lock);
		return error(_("failed to finalize '%s'"), filename);
	}

	strbuf_release(&buf);
	return 0;
}

static int do_label(struct repository *r, const char *name, int len)
{
	struct ref_store *refs = get_main_ref_store(r);
	struct ref_transaction *transaction;
	struct strbuf ref_name = STRBUF_INIT, err = STRBUF_INIT;
	struct strbuf msg = STRBUF_INIT;
	int ret = 0;
	struct object_id head_oid;

	if (len == 1 && *name == '#')
		return error(_("illegal label name: '%.*s'"), len, name);

	strbuf_addf(&ref_name, "refs/rewritten/%.*s", len, name);
	strbuf_addf(&msg, "rebase (label) '%.*s'", len, name);

	transaction = ref_store_transaction_begin(refs, &err);
	if (!transaction) {
		error("%s", err.buf);
		ret = -1;
	} else if (get_oid("HEAD", &head_oid)) {
		error(_("could not read HEAD"));
		ret = -1;
	} else if (ref_transaction_update(transaction, ref_name.buf, &head_oid,
					  NULL, 0, msg.buf, &err) < 0 ||
		   ref_transaction_commit(transaction, &err)) {
		error("%s", err.buf);
		ret = -1;
	}
	ref_transaction_free(transaction);
	strbuf_release(&err);
	strbuf_release(&msg);

	if (!ret)
		ret = safe_append(rebase_path_refs_to_delete(),
				  "%s\n", ref_name.buf);
	strbuf_release(&ref_name);

	return ret;
}

static const char *sequencer_reflog_action(struct replay_opts *opts)
{
	if (!opts->reflog_action) {
		opts->reflog_action = getenv(GIT_REFLOG_ACTION);
		opts->reflog_action =
			xstrdup(opts->reflog_action ? opts->reflog_action
						    : action_name(opts));
	}

	return opts->reflog_action;
}

__attribute__((format (printf, 3, 4)))
static const char *reflog_message(struct replay_opts *opts,
	const char *sub_action, const char *fmt, ...)
{
	va_list ap;
	static struct strbuf buf = STRBUF_INIT;

	va_start(ap, fmt);
	strbuf_reset(&buf);
	strbuf_addstr(&buf, sequencer_reflog_action(opts));
	if (sub_action)
		strbuf_addf(&buf, " (%s)", sub_action);
	if (fmt) {
		strbuf_addstr(&buf, ": ");
		strbuf_vaddf(&buf, fmt, ap);
	}
	va_end(ap);

	return buf.buf;
}

static struct commit *lookup_label(struct repository *r, const char *label,
				   int len, struct strbuf *buf)
{
	struct commit *commit;
	struct object_id oid;

	strbuf_reset(buf);
	strbuf_addf(buf, "refs/rewritten/%.*s", len, label);
	if (!read_ref(buf->buf, &oid)) {
		commit = lookup_commit_object(r, &oid);
	} else {
		/* fall back to non-rewritten ref or commit */
		strbuf_splice(buf, 0, strlen("refs/rewritten/"), "", 0);
		commit = lookup_commit_reference_by_name(buf->buf);
	}

	if (!commit)
		error(_("could not resolve '%s'"), buf->buf);

	return commit;
}

static int do_reset(struct repository *r,
		    const char *name, int len,
		    struct replay_opts *opts)
{
	struct strbuf ref_name = STRBUF_INIT;
	struct object_id oid;
	struct lock_file lock = LOCK_INIT;
	struct tree_desc desc = { 0 };
	struct tree *tree;
	struct unpack_trees_options unpack_tree_opts = { 0 };
	int ret = 0;

	if (repo_hold_locked_index(r, &lock, LOCK_REPORT_ON_ERROR) < 0)
		return -1;

	if (len == 10 && !strncmp("[new root]", name, len)) {
		if (!opts->have_squash_onto) {
			const char *hex;
			if (commit_tree("", 0, the_hash_algo->empty_tree,
					NULL, &opts->squash_onto,
					NULL, NULL))
				return error(_("writing fake root commit"));
			opts->have_squash_onto = 1;
			hex = oid_to_hex(&opts->squash_onto);
			if (write_message(hex, strlen(hex),
					  rebase_path_squash_onto(), 0))
				return error(_("writing squash-onto"));
		}
		oidcpy(&oid, &opts->squash_onto);
	} else {
		int i;
		struct commit *commit;

		/* Determine the length of the label */
		for (i = 0; i < len; i++)
			if (isspace(name[i]))
				break;
		len = i;

		commit = lookup_label(r, name, len, &ref_name);
		if (!commit) {
			ret = -1;
			goto cleanup;
		}
		oid = commit->object.oid;
	}

	setup_unpack_trees_porcelain(&unpack_tree_opts, "reset");
	unpack_tree_opts.head_idx = 1;
	unpack_tree_opts.src_index = r->index;
	unpack_tree_opts.dst_index = r->index;
	unpack_tree_opts.fn = oneway_merge;
	unpack_tree_opts.merge = 1;
	unpack_tree_opts.update = 1;
	unpack_tree_opts.preserve_ignored = 0; /* FIXME: !overwrite_ignore */
	unpack_tree_opts.skip_cache_tree_update = 1;
	init_checkout_metadata(&unpack_tree_opts.meta, name, &oid, NULL);

	if (repo_read_index_unmerged(r)) {
		ret = error_resolve_conflict(action_name(opts));
		goto cleanup;
	}

	if (!fill_tree_descriptor(r, &desc, &oid)) {
		ret = error(_("failed to find tree of %s"), oid_to_hex(&oid));
		goto cleanup;
	}

	if (unpack_trees(1, &desc, &unpack_tree_opts)) {
		ret = -1;
		goto cleanup;
	}

	tree = parse_tree_indirect(&oid);
	prime_cache_tree(r, r->index, tree);

	if (write_locked_index(r->index, &lock, COMMIT_LOCK) < 0)
		ret = error(_("could not write index"));

	if (!ret)
		ret = update_ref(reflog_message(opts, "reset", "'%.*s'",
						len, name), "HEAD", &oid,
				 NULL, 0, UPDATE_REFS_MSG_ON_ERR);
cleanup:
	free((void *)desc.buffer);
	if (ret < 0)
		rollback_lock_file(&lock);
	strbuf_release(&ref_name);
	clear_unpack_trees_porcelain(&unpack_tree_opts);
	return ret;
}

static int do_merge(struct repository *r,
		    struct commit *commit,
		    const char *arg, int arg_len,
		    int flags, int *check_todo, struct replay_opts *opts)
{
	int run_commit_flags = 0;
	struct strbuf ref_name = STRBUF_INIT;
	struct commit *head_commit, *merge_commit, *i;
	struct commit_list *bases, *j;
	struct commit_list *to_merge = NULL, **tail = &to_merge;
	const char *strategy = !opts->xopts_nr &&
		(!opts->strategy ||
		 !strcmp(opts->strategy, "recursive") ||
		 !strcmp(opts->strategy, "ort")) ?
		NULL : opts->strategy;
	struct merge_options o;
	int merge_arg_len, oneline_offset, can_fast_forward, ret, k;
	static struct lock_file lock;
	const char *p;

	if (repo_hold_locked_index(r, &lock, LOCK_REPORT_ON_ERROR) < 0) {
		ret = -1;
		goto leave_merge;
	}

	head_commit = lookup_commit_reference_by_name("HEAD");
	if (!head_commit) {
		ret = error(_("cannot merge without a current revision"));
		goto leave_merge;
	}

	/*
	 * For octopus merges, the arg starts with the list of revisions to be
	 * merged. The list is optionally followed by '#' and the oneline.
	 */
	merge_arg_len = oneline_offset = arg_len;
	for (p = arg; p - arg < arg_len; p += strspn(p, " \t\n")) {
		if (!*p)
			break;
		if (*p == '#' && (!p[1] || isspace(p[1]))) {
			p += 1 + strspn(p + 1, " \t\n");
			oneline_offset = p - arg;
			break;
		}
		k = strcspn(p, " \t\n");
		if (!k)
			continue;
		merge_commit = lookup_label(r, p, k, &ref_name);
		if (!merge_commit) {
			ret = error(_("unable to parse '%.*s'"), k, p);
			goto leave_merge;
		}
		tail = &commit_list_insert(merge_commit, tail)->next;
		p += k;
		merge_arg_len = p - arg;
	}

	if (!to_merge) {
		ret = error(_("nothing to merge: '%.*s'"), arg_len, arg);
		goto leave_merge;
	}

	if (opts->have_squash_onto &&
	    oideq(&head_commit->object.oid, &opts->squash_onto)) {
		/*
		 * When the user tells us to "merge" something into a
		 * "[new root]", let's simply fast-forward to the merge head.
		 */
		rollback_lock_file(&lock);
		if (to_merge->next)
			ret = error(_("octopus merge cannot be executed on "
				      "top of a [new root]"));
		else
			ret = fast_forward_to(r, &to_merge->item->object.oid,
					      &head_commit->object.oid, 0,
					      opts);
		goto leave_merge;
	}

	/*
	 * If HEAD is not identical to the first parent of the original merge
	 * commit, we cannot fast-forward.
	 */
	can_fast_forward = opts->allow_ff && commit && commit->parents &&
		oideq(&commit->parents->item->object.oid,
		      &head_commit->object.oid);

	/*
	 * If any merge head is different from the original one, we cannot
	 * fast-forward.
	 */
	if (can_fast_forward) {
		struct commit_list *p = commit->parents->next;

		for (j = to_merge; j && p; j = j->next, p = p->next)
			if (!oideq(&j->item->object.oid,
				   &p->item->object.oid)) {
				can_fast_forward = 0;
				break;
			}
		/*
		 * If the number of merge heads differs from the original merge
		 * commit, we cannot fast-forward.
		 */
		if (j || p)
			can_fast_forward = 0;
	}

	if (can_fast_forward) {
		rollback_lock_file(&lock);
		ret = fast_forward_to(r, &commit->object.oid,
				      &head_commit->object.oid, 0, opts);
		if (flags & TODO_EDIT_MERGE_MSG)
			goto fast_forward_edit;

		goto leave_merge;
	}

	if (commit) {
		const char *encoding = get_commit_output_encoding();
		const char *message = logmsg_reencode(commit, NULL, encoding);
		const char *body;
		int len;

		if (!message) {
			ret = error(_("could not get commit message of '%s'"),
				    oid_to_hex(&commit->object.oid));
			goto leave_merge;
		}
		write_author_script(message);
		find_commit_subject(message, &body);
		len = strlen(body);
		ret = write_message(body, len, git_path_merge_msg(r), 0);
		unuse_commit_buffer(commit, message);
		if (ret) {
			error_errno(_("could not write '%s'"),
				    git_path_merge_msg(r));
			goto leave_merge;
		}
	} else {
		struct strbuf buf = STRBUF_INIT;
		int len;

		strbuf_addf(&buf, "author %s", git_author_info(0));
		write_author_script(buf.buf);
		strbuf_reset(&buf);

		if (oneline_offset < arg_len) {
			p = arg + oneline_offset;
			len = arg_len - oneline_offset;
		} else {
			strbuf_addf(&buf, "Merge %s '%.*s'",
				    to_merge->next ? "branches" : "branch",
				    merge_arg_len, arg);
			p = buf.buf;
			len = buf.len;
		}

		ret = write_message(p, len, git_path_merge_msg(r), 0);
		strbuf_release(&buf);
		if (ret) {
			error_errno(_("could not write '%s'"),
				    git_path_merge_msg(r));
			goto leave_merge;
		}
	}

	if (strategy || to_merge->next) {
		/* Octopus merge */
		struct child_process cmd = CHILD_PROCESS_INIT;

		if (read_env_script(&cmd.env)) {
			const char *gpg_opt = gpg_sign_opt_quoted(opts);

			ret = error(_(staged_changes_advice), gpg_opt, gpg_opt);
			goto leave_merge;
		}

		if (opts->committer_date_is_author_date)
			strvec_pushf(&cmd.env, "GIT_COMMITTER_DATE=%s",
				     opts->ignore_date ?
				     "" :
				     author_date_from_env(&cmd.env));
		if (opts->ignore_date)
			strvec_push(&cmd.env, "GIT_AUTHOR_DATE=");

		cmd.git_cmd = 1;
		strvec_push(&cmd.args, "merge");
		strvec_push(&cmd.args, "-s");
		if (!strategy)
			strvec_push(&cmd.args, "octopus");
		else {
			strvec_push(&cmd.args, strategy);
			for (k = 0; k < opts->xopts_nr; k++)
				strvec_pushf(&cmd.args,
					     "-X%s", opts->xopts[k]);
		}
		if (!(flags & TODO_EDIT_MERGE_MSG))
			strvec_push(&cmd.args, "--no-edit");
		else
			strvec_push(&cmd.args, "--edit");
		strvec_push(&cmd.args, "--no-ff");
		strvec_push(&cmd.args, "--no-log");
		strvec_push(&cmd.args, "--no-stat");
		strvec_push(&cmd.args, "-F");
		strvec_push(&cmd.args, git_path_merge_msg(r));
		if (opts->gpg_sign)
			strvec_pushf(&cmd.args, "-S%s", opts->gpg_sign);
		else
			strvec_push(&cmd.args, "--no-gpg-sign");

		/* Add the tips to be merged */
		for (j = to_merge; j; j = j->next)
			strvec_push(&cmd.args,
				    oid_to_hex(&j->item->object.oid));

		strbuf_release(&ref_name);
		refs_delete_ref(get_main_ref_store(r), "", "CHERRY_PICK_HEAD",
				NULL, 0);
		rollback_lock_file(&lock);

		ret = run_command(&cmd);

		/* force re-reading of the cache */
		if (!ret) {
			discard_index(r->index);
			if (repo_read_index(r) < 0)
				ret = error(_("could not read index"));
		}
		goto leave_merge;
	}

	merge_commit = to_merge->item;
	bases = get_merge_bases(head_commit, merge_commit);
	if (bases && oideq(&merge_commit->object.oid,
			   &bases->item->object.oid)) {
		ret = 0;
		/* skip merging an ancestor of HEAD */
		goto leave_merge;
	}

	write_message(oid_to_hex(&merge_commit->object.oid), the_hash_algo->hexsz,
		      git_path_merge_head(r), 0);
	write_message("no-ff", 5, git_path_merge_mode(r), 0);

	bases = reverse_commit_list(bases);

	repo_read_index(r);
	init_merge_options(&o, r);
	o.branch1 = "HEAD";
	o.branch2 = ref_name.buf;
	o.buffer_output = 2;

	if (!opts->strategy || !strcmp(opts->strategy, "ort")) {
		/*
		 * TODO: Should use merge_incore_recursive() and
		 * merge_switch_to_result(), skipping the call to
		 * merge_switch_to_result() when we don't actually need to
		 * update the index and working copy immediately.
		 */
		ret = merge_ort_recursive(&o,
					  head_commit, merge_commit, bases,
					  &i);
	} else {
		ret = merge_recursive(&o, head_commit, merge_commit, bases,
				      &i);
	}
	if (ret <= 0)
		fputs(o.obuf.buf, stdout);
	strbuf_release(&o.obuf);
	if (ret < 0) {
		error(_("could not even attempt to merge '%.*s'"),
		      merge_arg_len, arg);
		goto leave_merge;
	}
	/*
	 * The return value of merge_recursive() is 1 on clean, and 0 on
	 * unclean merge.
	 *
	 * Let's reverse that, so that do_merge() returns 0 upon success and
	 * 1 upon failed merge (keeping the return value -1 for the cases where
	 * we will want to reschedule the `merge` command).
	 */
	ret = !ret;

	if (r->index->cache_changed &&
	    write_locked_index(r->index, &lock, COMMIT_LOCK)) {
		ret = error(_("merge: Unable to write new index file"));
		goto leave_merge;
	}

	rollback_lock_file(&lock);
	if (ret)
		repo_rerere(r, opts->allow_rerere_auto);
	else
		/*
		 * In case of problems, we now want to return a positive
		 * value (a negative one would indicate that the `merge`
		 * command needs to be rescheduled).
		 */
		ret = !!run_git_commit(git_path_merge_msg(r), opts,
				       run_commit_flags);

	if (!ret && flags & TODO_EDIT_MERGE_MSG) {
	fast_forward_edit:
		*check_todo = 1;
		run_commit_flags |= AMEND_MSG | EDIT_MSG | VERIFY_MSG;
		ret = !!run_git_commit(NULL, opts, run_commit_flags);
	}


leave_merge:
	strbuf_release(&ref_name);
	rollback_lock_file(&lock);
	free_commit_list(to_merge);
	return ret;
}

static int write_update_refs_state(struct string_list *refs_to_oids)
{
	int result = 0;
	struct lock_file lock = LOCK_INIT;
	FILE *fp = NULL;
	struct string_list_item *item;
	char *path;

	path = rebase_path_update_refs(the_repository->gitdir);

	if (!refs_to_oids->nr) {
		if (unlink(path) && errno != ENOENT)
			result = error_errno(_("could not unlink: %s"), path);
		goto cleanup;
	}

	if (safe_create_leading_directories(path)) {
		result = error(_("unable to create leading directories of %s"),
			       path);
		goto cleanup;
	}

	if (hold_lock_file_for_update(&lock, path, 0) < 0) {
		result = error(_("another 'rebase' process appears to be running; "
				 "'%s.lock' already exists"),
			       path);
		goto cleanup;
	}

	fp = fdopen_lock_file(&lock, "w");
	if (!fp) {
		result = error_errno(_("could not open '%s' for writing"), path);
		rollback_lock_file(&lock);
		goto cleanup;
	}

	for_each_string_list_item(item, refs_to_oids) {
		struct update_ref_record *rec = item->util;
		fprintf(fp, "%s\n%s\n%s\n", item->string,
			oid_to_hex(&rec->before), oid_to_hex(&rec->after));
	}

	result = commit_lock_file(&lock);

cleanup:
	free(path);
	return result;
}

/*
 * Parse the update-refs file for the current rebase, then remove the
 * refs that do not appear in the todo_list (and have not had updated
 * values stored) and add refs that are in the todo_list but not
 * represented in the update-refs file.
 *
 * If there are changes to the update-refs list, then write the new state
 * to disk.
 */
void todo_list_filter_update_refs(struct repository *r,
				  struct todo_list *todo_list)
{
	int i;
	int updated = 0;
	struct string_list update_refs = STRING_LIST_INIT_DUP;

	sequencer_get_update_refs_state(r->gitdir, &update_refs);

	/*
	 * For each item in the update_refs list, if it has no updated
	 * value and does not appear in the todo_list, then remove it
	 * from the update_refs list.
	 */
	for (i = 0; i < update_refs.nr; i++) {
		int j;
		int found = 0;
		const char *ref = update_refs.items[i].string;
		size_t reflen = strlen(ref);
		struct update_ref_record *rec = update_refs.items[i].util;

		/* OID already stored as updated. */
		if (!is_null_oid(&rec->after))
			continue;

		for (j = 0; !found && j < todo_list->total_nr; j++) {
			struct todo_item *item = &todo_list->items[j];
			const char *arg = todo_list->buf.buf + item->arg_offset;

			if (item->command != TODO_UPDATE_REF)
				continue;

			if (item->arg_len != reflen ||
			    strncmp(arg, ref, reflen))
				continue;

			found = 1;
		}

		if (!found) {
			free(update_refs.items[i].string);
			free(update_refs.items[i].util);

			update_refs.nr--;
			MOVE_ARRAY(update_refs.items + i, update_refs.items + i + 1, update_refs.nr - i);

			updated = 1;
			i--;
		}
	}

	/*
	 * For each todo_item, check if its ref is in the update_refs list.
	 * If not, then add it as an un-updated ref.
	 */
	for (i = 0; i < todo_list->total_nr; i++) {
		struct todo_item *item = &todo_list->items[i];
		const char *arg = todo_list->buf.buf + item->arg_offset;
		int j, found = 0;

		if (item->command != TODO_UPDATE_REF)
			continue;

		for (j = 0; !found && j < update_refs.nr; j++) {
			const char *ref = update_refs.items[j].string;

			found = strlen(ref) == item->arg_len &&
				!strncmp(ref, arg, item->arg_len);
		}

		if (!found) {
			struct string_list_item *inserted;
			struct strbuf argref = STRBUF_INIT;

			strbuf_add(&argref, arg, item->arg_len);
			inserted = string_list_insert(&update_refs, argref.buf);
			inserted->util = init_update_ref_record(argref.buf);
			strbuf_release(&argref);
			updated = 1;
		}
	}

	if (updated)
		write_update_refs_state(&update_refs);
	string_list_clear(&update_refs, 1);
}

static int do_update_ref(struct repository *r, const char *refname)
{
	struct string_list_item *item;
	struct string_list list = STRING_LIST_INIT_DUP;

	if (sequencer_get_update_refs_state(r->gitdir, &list))
		return -1;

	for_each_string_list_item(item, &list) {
		if (!strcmp(item->string, refname)) {
			struct update_ref_record *rec = item->util;
			if (read_ref("HEAD", &rec->after))
				return -1;
			break;
		}
	}

	write_update_refs_state(&list);
	string_list_clear(&list, 1);
	return 0;
}

static int do_update_refs(struct repository *r, int quiet)
{
	int res = 0;
	struct string_list_item *item;
	struct string_list refs_to_oids = STRING_LIST_INIT_DUP;
	struct ref_store *refs = get_main_ref_store(r);
	struct strbuf update_msg = STRBUF_INIT;
	struct strbuf error_msg = STRBUF_INIT;

	if ((res = sequencer_get_update_refs_state(r->gitdir, &refs_to_oids)))
		return res;

	for_each_string_list_item(item, &refs_to_oids) {
		struct update_ref_record *rec = item->util;
		int loop_res;

		loop_res = refs_update_ref(refs, "rewritten during rebase",
					   item->string,
					   &rec->after, &rec->before,
					   0, UPDATE_REFS_MSG_ON_ERR);
		res |= loop_res;

		if (quiet)
			continue;

		if (loop_res)
			strbuf_addf(&error_msg, "\t%s\n", item->string);
		else
			strbuf_addf(&update_msg, "\t%s\n", item->string);
	}

	if (!quiet &&
	    (update_msg.len || error_msg.len)) {
		fprintf(stderr,
			_("Updated the following refs with %s:\n%s"),
			"--update-refs",
			update_msg.buf);

		if (res)
			fprintf(stderr,
				_("Failed to update the following refs with %s:\n%s"),
				"--update-refs",
				error_msg.buf);
	}

	string_list_clear(&refs_to_oids, 1);
	strbuf_release(&update_msg);
	strbuf_release(&error_msg);
	return res;
}

static int is_final_fixup(struct todo_list *todo_list)
{
	int i = todo_list->current;

	if (!is_fixup(todo_list->items[i].command))
		return 0;

	while (++i < todo_list->nr)
		if (is_fixup(todo_list->items[i].command))
			return 0;
		else if (!is_noop(todo_list->items[i].command))
			break;
	return 1;
}

static enum todo_command peek_command(struct todo_list *todo_list, int offset)
{
	int i;

	for (i = todo_list->current + offset; i < todo_list->nr; i++)
		if (!is_noop(todo_list->items[i].command))
			return todo_list->items[i].command;

	return -1;
}

void create_autostash(struct repository *r, const char *path)
{
	struct strbuf buf = STRBUF_INIT;
	struct lock_file lock_file = LOCK_INIT;
	int fd;

	fd = repo_hold_locked_index(r, &lock_file, 0);
	refresh_index(r->index, REFRESH_QUIET, NULL, NULL, NULL);
	if (0 <= fd)
		repo_update_index_if_able(r, &lock_file);
	rollback_lock_file(&lock_file);

	if (has_unstaged_changes(r, 1) ||
	    has_uncommitted_changes(r, 1)) {
		struct child_process stash = CHILD_PROCESS_INIT;
		struct reset_head_opts ropts = { .flags = RESET_HEAD_HARD };
		struct object_id oid;

		strvec_pushl(&stash.args,
			     "stash", "create", "autostash", NULL);
		stash.git_cmd = 1;
		stash.no_stdin = 1;
		strbuf_reset(&buf);
		if (capture_command(&stash, &buf, GIT_MAX_HEXSZ))
			die(_("Cannot autostash"));
		strbuf_trim_trailing_newline(&buf);
		if (get_oid(buf.buf, &oid))
			die(_("Unexpected stash response: '%s'"),
			    buf.buf);
		strbuf_reset(&buf);
		strbuf_add_unique_abbrev(&buf, &oid, DEFAULT_ABBREV);

		if (safe_create_leading_directories_const(path))
			die(_("Could not create directory for '%s'"),
			    path);
		write_file(path, "%s", oid_to_hex(&oid));
		printf(_("Created autostash: %s\n"), buf.buf);
		if (reset_head(r, &ropts) < 0)
			die(_("could not reset --hard"));
		discard_index(r->index);
		if (repo_read_index(r) < 0)
			die(_("could not read index"));
	}
	strbuf_release(&buf);
}

static int apply_save_autostash_oid(const char *stash_oid, int attempt_apply)
{
	struct child_process child = CHILD_PROCESS_INIT;
	int ret = 0;

	if (attempt_apply) {
		child.git_cmd = 1;
		child.no_stdout = 1;
		child.no_stderr = 1;
		strvec_push(&child.args, "stash");
		strvec_push(&child.args, "apply");
		strvec_push(&child.args, stash_oid);
		ret = run_command(&child);
	}

	if (attempt_apply && !ret)
		fprintf(stderr, _("Applied autostash.\n"));
	else {
		struct child_process store = CHILD_PROCESS_INIT;

		store.git_cmd = 1;
		strvec_push(&store.args, "stash");
		strvec_push(&store.args, "store");
		strvec_push(&store.args, "-m");
		strvec_push(&store.args, "autostash");
		strvec_push(&store.args, "-q");
		strvec_push(&store.args, stash_oid);
		if (run_command(&store))
			ret = error(_("cannot store %s"), stash_oid);
		else
			fprintf(stderr,
				_("%s\n"
				  "Your changes are safe in the stash.\n"
				  "You can run \"git stash pop\" or"
				  " \"git stash drop\" at any time.\n"),
				attempt_apply ?
				_("Applying autostash resulted in conflicts.") :
				_("Autostash exists; creating a new stash entry."));
	}

	return ret;
}

static int apply_save_autostash(const char *path, int attempt_apply)
{
	struct strbuf stash_oid = STRBUF_INIT;
	int ret = 0;

	if (!read_oneliner(&stash_oid, path,
			   READ_ONELINER_SKIP_IF_EMPTY)) {
		strbuf_release(&stash_oid);
		return 0;
	}
	strbuf_trim(&stash_oid);

	ret = apply_save_autostash_oid(stash_oid.buf, attempt_apply);

	unlink(path);
	strbuf_release(&stash_oid);
	return ret;
}

int save_autostash(const char *path)
{
	return apply_save_autostash(path, 0);
}

int apply_autostash(const char *path)
{
	return apply_save_autostash(path, 1);
}

int apply_autostash_oid(const char *stash_oid)
{
	return apply_save_autostash_oid(stash_oid, 1);
}

static int checkout_onto(struct repository *r, struct replay_opts *opts,
			 const char *onto_name, const struct object_id *onto,
			 const struct object_id *orig_head)
{
	struct reset_head_opts ropts = {
		.oid = onto,
		.orig_head = orig_head,
		.flags = RESET_HEAD_DETACH | RESET_ORIG_HEAD |
				RESET_HEAD_RUN_POST_CHECKOUT_HOOK,
		.head_msg = reflog_message(opts, "start", "checkout %s",
					   onto_name),
		.default_reflog_action = sequencer_reflog_action(opts)
	};
	if (reset_head(r, &ropts)) {
		apply_autostash(rebase_path_autostash());
		sequencer_remove_state(opts);
		return error(_("could not detach HEAD"));
	}

	return 0;
}

static int stopped_at_head(struct repository *r)
{
	struct object_id head;
	struct commit *commit;
	struct commit_message message;

	if (get_oid("HEAD", &head) ||
	    !(commit = lookup_commit(r, &head)) ||
	    parse_commit(commit) || get_message(commit, &message))
		fprintf(stderr, _("Stopped at HEAD\n"));
	else {
		fprintf(stderr, _("Stopped at %s\n"), message.label);
		free_message(commit, &message);
	}
	return 0;

}

static int reread_todo_if_changed(struct repository *r,
				  struct todo_list *todo_list,
				  struct replay_opts *opts)
{
	int offset;
	struct strbuf buf = STRBUF_INIT;

	if (strbuf_read_file_or_whine(&buf, get_todo_path(opts)) < 0)
		return -1;
	offset = get_item_line_offset(todo_list, todo_list->current + 1);
	if (buf.len != todo_list->buf.len - offset ||
	    memcmp(buf.buf, todo_list->buf.buf + offset, buf.len)) {
		/* Reread the todo file if it has changed. */
		todo_list_release(todo_list);
		if (read_populate_todo(r, todo_list, opts))
			return -1; /* message was printed */
		/* `current` will be incremented on return */
		todo_list->current = -1;
	}
	strbuf_release(&buf);

	return 0;
}

static const char rescheduled_advice[] =
N_("Could not execute the todo command\n"
"\n"
"    %.*s"
"\n"
"It has been rescheduled; To edit the command before continuing, please\n"
"edit the todo list first:\n"
"\n"
"    git rebase --edit-todo\n"
"    git rebase --continue\n");

static int pick_commits(struct repository *r,
			struct todo_list *todo_list,
			struct replay_opts *opts)
{
	int res = 0, reschedule = 0;

	opts->reflog_message = sequencer_reflog_action(opts);
	if (opts->allow_ff)
		assert(!(opts->signoff || opts->no_commit ||
			 opts->record_origin || should_edit(opts) ||
			 opts->committer_date_is_author_date ||
			 opts->ignore_date));
	if (read_and_refresh_cache(r, opts))
		return -1;

	while (todo_list->current < todo_list->nr) {
		struct todo_item *item = todo_list->items + todo_list->current;
		const char *arg = todo_item_get_arg(todo_list, item);
		int check_todo = 0;

		if (save_todo(todo_list, opts))
			return -1;
		if (is_rebase_i(opts)) {
			if (item->command != TODO_COMMENT) {
				FILE *f = fopen(rebase_path_msgnum(), "w");

				todo_list->done_nr++;

				if (f) {
					fprintf(f, "%d\n", todo_list->done_nr);
					fclose(f);
				}
				if (!opts->quiet)
					fprintf(stderr, _("Rebasing (%d/%d)%s"),
						todo_list->done_nr,
						todo_list->total_nr,
						opts->verbose ? "\n" : "\r");
			}
			unlink(rebase_path_message());
			unlink(rebase_path_author_script());
			unlink(rebase_path_stopped_sha());
			unlink(rebase_path_amend());
			unlink(git_path_merge_head(r));
			unlink(git_path_auto_merge(r));
			delete_ref(NULL, "REBASE_HEAD", NULL, REF_NO_DEREF);

			if (item->command == TODO_BREAK) {
				if (!opts->verbose)
					term_clear_line();
				return stopped_at_head(r);
			}
		}
		if (item->command <= TODO_SQUASH) {
			if (is_rebase_i(opts))
				opts->reflog_message = reflog_message(opts,
				      command_to_string(item->command), NULL);

			res = do_pick_commit(r, item, opts,
					     is_final_fixup(todo_list),
					     &check_todo);
			if (is_rebase_i(opts) && res < 0) {
				/* Reschedule */
				advise(_(rescheduled_advice),
				       get_item_line_length(todo_list,
							    todo_list->current),
				       get_item_line(todo_list,
						     todo_list->current));
				todo_list->current--;
				if (save_todo(todo_list, opts))
					return -1;
			}
			if (item->command == TODO_EDIT) {
				struct commit *commit = item->commit;
				if (!res) {
					if (!opts->verbose)
						term_clear_line();
					fprintf(stderr,
						_("Stopped at %s...  %.*s\n"),
						short_commit_name(commit),
						item->arg_len, arg);
				}
				return error_with_patch(r, commit,
					arg, item->arg_len, opts, res, !res);
			}
			if (is_rebase_i(opts) && !res)
				record_in_rewritten(&item->commit->object.oid,
					peek_command(todo_list, 1));
			if (res && is_fixup(item->command)) {
				if (res == 1)
					intend_to_amend();
				return error_failed_squash(r, item->commit, opts,
					item->arg_len, arg);
			} else if (res && is_rebase_i(opts) && item->commit) {
				int to_amend = 0;
				struct object_id oid;

				/*
				 * If we are rewording and have either
				 * fast-forwarded already, or are about to
				 * create a new root commit, we want to amend,
				 * otherwise we do not.
				 */
				if (item->command == TODO_REWORD &&
				    !get_oid("HEAD", &oid) &&
				    (oideq(&item->commit->object.oid, &oid) ||
				     (opts->have_squash_onto &&
				      oideq(&opts->squash_onto, &oid))))
					to_amend = 1;

				return res | error_with_patch(r, item->commit,
						arg, item->arg_len, opts,
						res, to_amend);
			}
		} else if (item->command == TODO_EXEC) {
			char *end_of_arg = (char *)(arg + item->arg_len);
			int saved = *end_of_arg;

			if (!opts->verbose)
				term_clear_line();
			*end_of_arg = '\0';
			res = do_exec(r, arg);
			*end_of_arg = saved;

			if (res) {
				if (opts->reschedule_failed_exec)
					reschedule = 1;
			}
			check_todo = 1;
		} else if (item->command == TODO_LABEL) {
			if ((res = do_label(r, arg, item->arg_len)))
				reschedule = 1;
		} else if (item->command == TODO_RESET) {
			if ((res = do_reset(r, arg, item->arg_len, opts)))
				reschedule = 1;
		} else if (item->command == TODO_MERGE) {
			if ((res = do_merge(r, item->commit, arg, item->arg_len,
					    item->flags, &check_todo, opts)) < 0)
				reschedule = 1;
			else if (item->commit)
				record_in_rewritten(&item->commit->object.oid,
						    peek_command(todo_list, 1));
			if (res > 0)
				/* failed with merge conflicts */
				return error_with_patch(r, item->commit,
							arg, item->arg_len,
							opts, res, 0);
		} else if (item->command == TODO_UPDATE_REF) {
			struct strbuf ref = STRBUF_INIT;
			strbuf_add(&ref, arg, item->arg_len);
			if ((res = do_update_ref(r, ref.buf)))
				reschedule = 1;
			strbuf_release(&ref);
		} else if (!is_noop(item->command))
			return error(_("unknown command %d"), item->command);

		if (reschedule) {
			advise(_(rescheduled_advice),
			       get_item_line_length(todo_list,
						    todo_list->current),
			       get_item_line(todo_list, todo_list->current));
			todo_list->current--;
			if (save_todo(todo_list, opts))
				return -1;
			if (item->commit)
				return error_with_patch(r,
							item->commit,
							arg, item->arg_len,
							opts, res, 0);
		} else if (is_rebase_i(opts) && check_todo && !res &&
			   reread_todo_if_changed(r, todo_list, opts)) {
			return -1;
		}

		todo_list->current++;
		if (res)
			return res;
	}

	if (is_rebase_i(opts)) {
		struct strbuf head_ref = STRBUF_INIT, buf = STRBUF_INIT;
		struct stat st;

		/* Stopped in the middle, as planned? */
		if (todo_list->current < todo_list->nr)
			return 0;

		if (read_oneliner(&head_ref, rebase_path_head_name(), 0) &&
				starts_with(head_ref.buf, "refs/")) {
			const char *msg;
			struct object_id head, orig;
			int res;

			if (get_oid("HEAD", &head)) {
				res = error(_("cannot read HEAD"));
cleanup_head_ref:
				strbuf_release(&head_ref);
				strbuf_release(&buf);
				return res;
			}
			if (!read_oneliner(&buf, rebase_path_orig_head(), 0) ||
					get_oid_hex(buf.buf, &orig)) {
				res = error(_("could not read orig-head"));
				goto cleanup_head_ref;
			}
			strbuf_reset(&buf);
			if (!read_oneliner(&buf, rebase_path_onto(), 0)) {
				res = error(_("could not read 'onto'"));
				goto cleanup_head_ref;
			}
			msg = reflog_message(opts, "finish", "%s onto %s",
				head_ref.buf, buf.buf);
			if (update_ref(msg, head_ref.buf, &head, &orig,
				       REF_NO_DEREF, UPDATE_REFS_MSG_ON_ERR)) {
				res = error(_("could not update %s"),
					head_ref.buf);
				goto cleanup_head_ref;
			}
			msg = reflog_message(opts, "finish", "returning to %s",
				head_ref.buf);
			if (create_symref("HEAD", head_ref.buf, msg)) {
				res = error(_("could not update HEAD to %s"),
					head_ref.buf);
				goto cleanup_head_ref;
			}
			strbuf_reset(&buf);
		}

		if (opts->verbose) {
			struct rev_info log_tree_opt;
			struct object_id orig, head;

			memset(&log_tree_opt, 0, sizeof(log_tree_opt));
			repo_init_revisions(r, &log_tree_opt, NULL);
			log_tree_opt.diff = 1;
			log_tree_opt.diffopt.output_format =
				DIFF_FORMAT_DIFFSTAT;
			log_tree_opt.disable_stdin = 1;

			if (read_oneliner(&buf, rebase_path_orig_head(), 0) &&
			    !get_oid(buf.buf, &orig) &&
			    !get_oid("HEAD", &head)) {
				diff_tree_oid(&orig, &head, "",
					      &log_tree_opt.diffopt);
				log_tree_diff_flush(&log_tree_opt);
			}
			release_revisions(&log_tree_opt);
		}
		flush_rewritten_pending();
		if (!stat(rebase_path_rewritten_list(), &st) &&
				st.st_size > 0) {
			struct child_process child = CHILD_PROCESS_INIT;
			struct run_hooks_opt hook_opt = RUN_HOOKS_OPT_INIT;

			child.in = open(rebase_path_rewritten_list(), O_RDONLY);
			child.git_cmd = 1;
			strvec_push(&child.args, "notes");
			strvec_push(&child.args, "copy");
			strvec_push(&child.args, "--for-rewrite=rebase");
			/* we don't care if this copying failed */
			run_command(&child);

			hook_opt.path_to_stdin = rebase_path_rewritten_list();
			strvec_push(&hook_opt.args, "rebase");
			run_hooks_opt("post-rewrite", &hook_opt);
		}
		apply_autostash(rebase_path_autostash());

		if (!opts->quiet) {
			if (!opts->verbose)
				term_clear_line();
			fprintf(stderr,
				_("Successfully rebased and updated %s.\n"),
				head_ref.buf);
		}

		strbuf_release(&buf);
		strbuf_release(&head_ref);

		if (do_update_refs(r, opts->quiet))
			return -1;
	}

	/*
	 * Sequence of picks finished successfully; cleanup by
	 * removing the .git/sequencer directory
	 */
	return sequencer_remove_state(opts);
}

static int continue_single_pick(struct repository *r, struct replay_opts *opts)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	if (!refs_ref_exists(get_main_ref_store(r), "CHERRY_PICK_HEAD") &&
	    !refs_ref_exists(get_main_ref_store(r), "REVERT_HEAD"))
		return error(_("no cherry-pick or revert in progress"));

	cmd.git_cmd = 1;
	strvec_push(&cmd.args, "commit");

	/*
	 * continue_single_pick() handles the case of recovering from a
	 * conflict.  should_edit() doesn't handle that case; for a conflict,
	 * we want to edit if the user asked for it, or if they didn't specify
	 * and stdin is a tty.
	 */
	if (!opts->edit || (opts->edit < 0 && !isatty(0)))
		/*
		 * Include --cleanup=strip as well because we don't want the
		 * "# Conflicts:" messages.
		 */
		strvec_pushl(&cmd.args, "--no-edit", "--cleanup=strip", NULL);

	return run_command(&cmd);
}

static int commit_staged_changes(struct repository *r,
				 struct replay_opts *opts,
				 struct todo_list *todo_list)
{
	unsigned int flags = ALLOW_EMPTY | EDIT_MSG;
	unsigned int final_fixup = 0, is_clean;

	if (has_unstaged_changes(r, 1))
		return error(_("cannot rebase: You have unstaged changes."));

	is_clean = !has_uncommitted_changes(r, 0);

	if (file_exists(rebase_path_amend())) {
		struct strbuf rev = STRBUF_INIT;
		struct object_id head, to_amend;

		if (get_oid("HEAD", &head))
			return error(_("cannot amend non-existing commit"));
		if (!read_oneliner(&rev, rebase_path_amend(), 0))
			return error(_("invalid file: '%s'"), rebase_path_amend());
		if (get_oid_hex(rev.buf, &to_amend))
			return error(_("invalid contents: '%s'"),
				rebase_path_amend());
		if (!is_clean && !oideq(&head, &to_amend))
			return error(_("\nYou have uncommitted changes in your "
				       "working tree. Please, commit them\n"
				       "first and then run 'git rebase "
				       "--continue' again."));
		/*
		 * When skipping a failed fixup/squash, we need to edit the
		 * commit message, the current fixup list and count, and if it
		 * was the last fixup/squash in the chain, we need to clean up
		 * the commit message and if there was a squash, let the user
		 * edit it.
		 */
		if (!is_clean || !opts->current_fixup_count)
			; /* this is not the final fixup */
		else if (!oideq(&head, &to_amend) ||
			 !file_exists(rebase_path_stopped_sha())) {
			/* was a final fixup or squash done manually? */
			if (!is_fixup(peek_command(todo_list, 0))) {
				unlink(rebase_path_fixup_msg());
				unlink(rebase_path_squash_msg());
				unlink(rebase_path_current_fixups());
				strbuf_reset(&opts->current_fixups);
				opts->current_fixup_count = 0;
			}
		} else {
			/* we are in a fixup/squash chain */
			const char *p = opts->current_fixups.buf;
			int len = opts->current_fixups.len;

			opts->current_fixup_count--;
			if (!len)
				BUG("Incorrect current_fixups:\n%s", p);
			while (len && p[len - 1] != '\n')
				len--;
			strbuf_setlen(&opts->current_fixups, len);
			if (write_message(p, len, rebase_path_current_fixups(),
					  0) < 0)
				return error(_("could not write file: '%s'"),
					     rebase_path_current_fixups());

			/*
			 * If a fixup/squash in a fixup/squash chain failed, the
			 * commit message is already correct, no need to commit
			 * it again.
			 *
			 * Only if it is the final command in the fixup/squash
			 * chain, and only if the chain is longer than a single
			 * fixup/squash command (which was just skipped), do we
			 * actually need to re-commit with a cleaned up commit
			 * message.
			 */
			if (opts->current_fixup_count > 0 &&
			    !is_fixup(peek_command(todo_list, 0))) {
				final_fixup = 1;
				/*
				 * If there was not a single "squash" in the
				 * chain, we only need to clean up the commit
				 * message, no need to bother the user with
				 * opening the commit message in the editor.
				 */
				if (!starts_with(p, "squash ") &&
				    !strstr(p, "\nsquash "))
					flags = (flags & ~EDIT_MSG) | CLEANUP_MSG;
			} else if (is_fixup(peek_command(todo_list, 0))) {
				/*
				 * We need to update the squash message to skip
				 * the latest commit message.
				 */
				struct commit *commit;
				const char *path = rebase_path_squash_msg();
				const char *encoding = get_commit_output_encoding();

				if (parse_head(r, &commit) ||
				    !(p = logmsg_reencode(commit, NULL, encoding)) ||
				    write_message(p, strlen(p), path, 0)) {
					unuse_commit_buffer(commit, p);
					return error(_("could not write file: "
						       "'%s'"), path);
				}
				unuse_commit_buffer(commit, p);
			}
		}

		strbuf_release(&rev);
		flags |= AMEND_MSG;
	}

	if (is_clean) {
		if (refs_ref_exists(get_main_ref_store(r),
				    "CHERRY_PICK_HEAD") &&
		    refs_delete_ref(get_main_ref_store(r), "",
				    "CHERRY_PICK_HEAD", NULL, 0))
			return error(_("could not remove CHERRY_PICK_HEAD"));
		if (unlink(git_path_merge_msg(r)) && errno != ENOENT)
			return error_errno(_("could not remove '%s'"),
					   git_path_merge_msg(r));
		if (!final_fixup)
			return 0;
	}

	if (run_git_commit(final_fixup ? NULL : rebase_path_message(),
			   opts, flags))
		return error(_("could not commit staged changes."));
	unlink(rebase_path_amend());
	unlink(git_path_merge_head(r));
	unlink(git_path_auto_merge(r));
	if (final_fixup) {
		unlink(rebase_path_fixup_msg());
		unlink(rebase_path_squash_msg());
	}
	if (opts->current_fixup_count > 0) {
		/*
		 * Whether final fixup or not, we just cleaned up the commit
		 * message...
		 */
		unlink(rebase_path_current_fixups());
		strbuf_reset(&opts->current_fixups);
		opts->current_fixup_count = 0;
	}
	return 0;
}

int sequencer_continue(struct repository *r, struct replay_opts *opts)
{
	struct todo_list todo_list = TODO_LIST_INIT;
	int res;

	if (read_and_refresh_cache(r, opts))
		return -1;

	if (read_populate_opts(opts))
		return -1;
	if (is_rebase_i(opts)) {
		if ((res = read_populate_todo(r, &todo_list, opts)))
			goto release_todo_list;

		if (file_exists(rebase_path_dropped())) {
			if ((res = todo_list_check_against_backup(r, &todo_list)))
				goto release_todo_list;

			unlink(rebase_path_dropped());
		}

		opts->reflog_message = reflog_message(opts, "continue", NULL);
		if (commit_staged_changes(r, opts, &todo_list)) {
			res = -1;
			goto release_todo_list;
		}
	} else if (!file_exists(get_todo_path(opts)))
		return continue_single_pick(r, opts);
	else if ((res = read_populate_todo(r, &todo_list, opts)))
		goto release_todo_list;

	if (!is_rebase_i(opts)) {
		/* Verify that the conflict has been resolved */
		if (refs_ref_exists(get_main_ref_store(r),
				    "CHERRY_PICK_HEAD") ||
		    refs_ref_exists(get_main_ref_store(r), "REVERT_HEAD")) {
			res = continue_single_pick(r, opts);
			if (res)
				goto release_todo_list;
		}
		if (index_differs_from(r, "HEAD", NULL, 0)) {
			res = error_dirty_index(r, opts);
			goto release_todo_list;
		}
		todo_list.current++;
	} else if (file_exists(rebase_path_stopped_sha())) {
		struct strbuf buf = STRBUF_INIT;
		struct object_id oid;

		if (read_oneliner(&buf, rebase_path_stopped_sha(),
				  READ_ONELINER_SKIP_IF_EMPTY) &&
		    !get_oid_hex(buf.buf, &oid))
			record_in_rewritten(&oid, peek_command(&todo_list, 0));
		strbuf_release(&buf);
	}

	res = pick_commits(r, &todo_list, opts);
release_todo_list:
	todo_list_release(&todo_list);
	return res;
}

static int single_pick(struct repository *r,
		       struct commit *cmit,
		       struct replay_opts *opts)
{
	int check_todo;
	struct todo_item item;

	item.command = opts->action == REPLAY_PICK ?
			TODO_PICK : TODO_REVERT;
	item.commit = cmit;

	opts->reflog_message = sequencer_reflog_action(opts);
	return do_pick_commit(r, &item, opts, 0, &check_todo);
}

int sequencer_pick_revisions(struct repository *r,
			     struct replay_opts *opts)
{
	struct todo_list todo_list = TODO_LIST_INIT;
	struct object_id oid;
	int i, res;

	assert(opts->revs);
	if (read_and_refresh_cache(r, opts))
		return -1;

	for (i = 0; i < opts->revs->pending.nr; i++) {
		struct object_id oid;
		const char *name = opts->revs->pending.objects[i].name;

		/* This happens when using --stdin. */
		if (!strlen(name))
			continue;

		if (!get_oid(name, &oid)) {
			if (!lookup_commit_reference_gently(r, &oid, 1)) {
				enum object_type type = oid_object_info(r,
									&oid,
									NULL);
				return error(_("%s: can't cherry-pick a %s"),
					name, type_name(type));
			}
		} else
			return error(_("%s: bad revision"), name);
	}

	/*
	 * If we were called as "git cherry-pick <commit>", just
	 * cherry-pick/revert it, set CHERRY_PICK_HEAD /
	 * REVERT_HEAD, and don't touch the sequencer state.
	 * This means it is possible to cherry-pick in the middle
	 * of a cherry-pick sequence.
	 */
	if (opts->revs->cmdline.nr == 1 &&
	    opts->revs->cmdline.rev->whence == REV_CMD_REV &&
	    opts->revs->no_walk &&
	    !opts->revs->cmdline.rev->flags) {
		struct commit *cmit;
		if (prepare_revision_walk(opts->revs))
			return error(_("revision walk setup failed"));
		cmit = get_revision(opts->revs);
		if (!cmit)
			return error(_("empty commit set passed"));
		if (get_revision(opts->revs))
			BUG("unexpected extra commit from walk");
		return single_pick(r, cmit, opts);
	}

	/*
	 * Start a new cherry-pick/ revert sequence; but
	 * first, make sure that an existing one isn't in
	 * progress
	 */

	if (walk_revs_populate_todo(&todo_list, opts) ||
			create_seq_dir(r) < 0)
		return -1;
	if (get_oid("HEAD", &oid) && (opts->action == REPLAY_REVERT))
		return error(_("can't revert as initial commit"));
	if (save_head(oid_to_hex(&oid)))
		return -1;
	if (save_opts(opts))
		return -1;
	update_abort_safety_file();
	res = pick_commits(r, &todo_list, opts);
	todo_list_release(&todo_list);
	return res;
}

void append_signoff(struct strbuf *msgbuf, size_t ignore_footer, unsigned flag)
{
	unsigned no_dup_sob = flag & APPEND_SIGNOFF_DEDUP;
	struct strbuf sob = STRBUF_INIT;
	int has_footer;

	strbuf_addstr(&sob, sign_off_header);
	strbuf_addstr(&sob, fmt_name(WANT_COMMITTER_IDENT));
	strbuf_addch(&sob, '\n');

	if (!ignore_footer)
		strbuf_complete_line(msgbuf);

	/*
	 * If the whole message buffer is equal to the sob, pretend that we
	 * found a conforming footer with a matching sob
	 */
	if (msgbuf->len - ignore_footer == sob.len &&
	    !strncmp(msgbuf->buf, sob.buf, sob.len))
		has_footer = 3;
	else
		has_footer = has_conforming_footer(msgbuf, &sob, ignore_footer);

	if (!has_footer) {
		const char *append_newlines = NULL;
		size_t len = msgbuf->len - ignore_footer;

		if (!len) {
			/*
			 * The buffer is completely empty.  Leave foom for
			 * the title and body to be filled in by the user.
			 */
			append_newlines = "\n\n";
		} else if (len == 1) {
			/*
			 * Buffer contains a single newline.  Add another
			 * so that we leave room for the title and body.
			 */
			append_newlines = "\n";
		} else if (msgbuf->buf[len - 2] != '\n') {
			/*
			 * Buffer ends with a single newline.  Add another
			 * so that there is an empty line between the message
			 * body and the sob.
			 */
			append_newlines = "\n";
		} /* else, the buffer already ends with two newlines. */

		if (append_newlines)
			strbuf_splice(msgbuf, msgbuf->len - ignore_footer, 0,
				append_newlines, strlen(append_newlines));
	}

	if (has_footer != 3 && (!no_dup_sob || has_footer != 2))
		strbuf_splice(msgbuf, msgbuf->len - ignore_footer, 0,
				sob.buf, sob.len);

	strbuf_release(&sob);
}

struct labels_entry {
	struct hashmap_entry entry;
	char label[FLEX_ARRAY];
};

static int labels_cmp(const void *fndata UNUSED,
		      const struct hashmap_entry *eptr,
		      const struct hashmap_entry *entry_or_key, const void *key)
{
	const struct labels_entry *a, *b;

	a = container_of(eptr, const struct labels_entry, entry);
	b = container_of(entry_or_key, const struct labels_entry, entry);

	return key ? strcmp(a->label, key) : strcmp(a->label, b->label);
}

struct string_entry {
	struct oidmap_entry entry;
	char string[FLEX_ARRAY];
};

struct label_state {
	struct oidmap commit2label;
	struct hashmap labels;
	struct strbuf buf;
};

static const char *label_oid(struct object_id *oid, const char *label,
			     struct label_state *state)
{
	struct labels_entry *labels_entry;
	struct string_entry *string_entry;
	struct object_id dummy;
	int i;

	string_entry = oidmap_get(&state->commit2label, oid);
	if (string_entry)
		return string_entry->string;

	/*
	 * For "uninteresting" commits, i.e. commits that are not to be
	 * rebased, and which can therefore not be labeled, we use a unique
	 * abbreviation of the commit name. This is slightly more complicated
	 * than calling find_unique_abbrev() because we also need to make
	 * sure that the abbreviation does not conflict with any other
	 * label.
	 *
	 * We disallow "interesting" commits to be labeled by a string that
	 * is a valid full-length hash, to ensure that we always can find an
	 * abbreviation for any uninteresting commit's names that does not
	 * clash with any other label.
	 */
	strbuf_reset(&state->buf);
	if (!label) {
		char *p;

		strbuf_grow(&state->buf, GIT_MAX_HEXSZ);
		label = p = state->buf.buf;

		find_unique_abbrev_r(p, oid, default_abbrev);

		/*
		 * We may need to extend the abbreviated hash so that there is
		 * no conflicting label.
		 */
		if (hashmap_get_from_hash(&state->labels, strihash(p), p)) {
			size_t i = strlen(p) + 1;

			oid_to_hex_r(p, oid);
			for (; i < the_hash_algo->hexsz; i++) {
				char save = p[i];
				p[i] = '\0';
				if (!hashmap_get_from_hash(&state->labels,
							   strihash(p), p))
					break;
				p[i] = save;
			}
		}
	} else {
		struct strbuf *buf = &state->buf;

		/*
		 * Sanitize labels by replacing non-alpha-numeric characters
		 * (including white-space ones) by dashes, as they might be
		 * illegal in file names (and hence in ref names).
		 *
		 * Note that we retain non-ASCII UTF-8 characters (identified
		 * via the most significant bit). They should be all acceptable
		 * in file names. We do not validate the UTF-8 here, that's not
		 * the job of this function.
		 */
		for (; *label; label++)
			if ((*label & 0x80) || isalnum(*label))
				strbuf_addch(buf, *label);
			/* avoid leading dash and double-dashes */
			else if (buf->len && buf->buf[buf->len - 1] != '-')
				strbuf_addch(buf, '-');
		if (!buf->len) {
			strbuf_addstr(buf, "rev-");
			strbuf_add_unique_abbrev(buf, oid, default_abbrev);
		}
		label = buf->buf;

		if ((buf->len == the_hash_algo->hexsz &&
		     !get_oid_hex(label, &dummy)) ||
		    (buf->len == 1 && *label == '#') ||
		    hashmap_get_from_hash(&state->labels,
					  strihash(label), label)) {
			/*
			 * If the label already exists, or if the label is a
			 * valid full OID, or the label is a '#' (which we use
			 * as a separator between merge heads and oneline), we
			 * append a dash and a number to make it unique.
			 */
			size_t len = buf->len;

			for (i = 2; ; i++) {
				strbuf_setlen(buf, len);
				strbuf_addf(buf, "-%d", i);
				if (!hashmap_get_from_hash(&state->labels,
							   strihash(buf->buf),
							   buf->buf))
					break;
			}

			label = buf->buf;
		}
	}

	FLEX_ALLOC_STR(labels_entry, label, label);
	hashmap_entry_init(&labels_entry->entry, strihash(label));
	hashmap_add(&state->labels, &labels_entry->entry);

	FLEX_ALLOC_STR(string_entry, string, label);
	oidcpy(&string_entry->entry.oid, oid);
	oidmap_put(&state->commit2label, string_entry);

	return string_entry->string;
}

static int make_script_with_merges(struct pretty_print_context *pp,
				   struct rev_info *revs, struct strbuf *out,
				   unsigned flags)
{
	int keep_empty = flags & TODO_LIST_KEEP_EMPTY;
	int rebase_cousins = flags & TODO_LIST_REBASE_COUSINS;
	int root_with_onto = flags & TODO_LIST_ROOT_WITH_ONTO;
	int skipped_commit = 0;
	struct strbuf buf = STRBUF_INIT, oneline = STRBUF_INIT;
	struct strbuf label = STRBUF_INIT;
	struct commit_list *commits = NULL, **tail = &commits, *iter;
	struct commit_list *tips = NULL, **tips_tail = &tips;
	struct commit *commit;
	struct oidmap commit2todo = OIDMAP_INIT;
	struct string_entry *entry;
	struct oidset interesting = OIDSET_INIT, child_seen = OIDSET_INIT,
		shown = OIDSET_INIT;
	struct label_state state = { OIDMAP_INIT, { NULL }, STRBUF_INIT };

	int abbr = flags & TODO_LIST_ABBREVIATE_CMDS;
	const char *cmd_pick = abbr ? "p" : "pick",
		*cmd_label = abbr ? "l" : "label",
		*cmd_reset = abbr ? "t" : "reset",
		*cmd_merge = abbr ? "m" : "merge";

	oidmap_init(&commit2todo, 0);
	oidmap_init(&state.commit2label, 0);
	hashmap_init(&state.labels, labels_cmp, NULL, 0);
	strbuf_init(&state.buf, 32);

	if (revs->cmdline.nr && (revs->cmdline.rev[0].flags & BOTTOM)) {
		struct labels_entry *onto_label_entry;
		struct object_id *oid = &revs->cmdline.rev[0].item->oid;
		FLEX_ALLOC_STR(entry, string, "onto");
		oidcpy(&entry->entry.oid, oid);
		oidmap_put(&state.commit2label, entry);

		FLEX_ALLOC_STR(onto_label_entry, label, "onto");
		hashmap_entry_init(&onto_label_entry->entry, strihash("onto"));
		hashmap_add(&state.labels, &onto_label_entry->entry);
	}

	/*
	 * First phase:
	 * - get onelines for all commits
	 * - gather all branch tips (i.e. 2nd or later parents of merges)
	 * - label all branch tips
	 */
	while ((commit = get_revision(revs))) {
		struct commit_list *to_merge;
		const char *p1, *p2;
		struct object_id *oid;
		int is_empty;

		tail = &commit_list_insert(commit, tail)->next;
		oidset_insert(&interesting, &commit->object.oid);

		is_empty = is_original_commit_empty(commit);
		if (!is_empty && (commit->object.flags & PATCHSAME)) {
			if (flags & TODO_LIST_WARN_SKIPPED_CHERRY_PICKS)
				warning(_("skipped previously applied commit %s"),
					short_commit_name(commit));
			skipped_commit = 1;
			continue;
		}
		if (is_empty && !keep_empty)
			continue;

		strbuf_reset(&oneline);
		pretty_print_commit(pp, commit, &oneline);

		to_merge = commit->parents ? commit->parents->next : NULL;
		if (!to_merge) {
			/* non-merge commit: easy case */
			strbuf_reset(&buf);
			strbuf_addf(&buf, "%s %s %s", cmd_pick,
				    oid_to_hex(&commit->object.oid),
				    oneline.buf);
			if (is_empty)
				strbuf_addf(&buf, " %c empty",
					    comment_line_char);

			FLEX_ALLOC_STR(entry, string, buf.buf);
			oidcpy(&entry->entry.oid, &commit->object.oid);
			oidmap_put(&commit2todo, entry);

			continue;
		}

		/* Create a label */
		strbuf_reset(&label);
		if (skip_prefix(oneline.buf, "Merge ", &p1) &&
		    (p1 = strchr(p1, '\'')) &&
		    (p2 = strchr(++p1, '\'')))
			strbuf_add(&label, p1, p2 - p1);
		else if (skip_prefix(oneline.buf, "Merge pull request ",
				     &p1) &&
			 (p1 = strstr(p1, " from ")))
			strbuf_addstr(&label, p1 + strlen(" from "));
		else
			strbuf_addbuf(&label, &oneline);

		strbuf_reset(&buf);
		strbuf_addf(&buf, "%s -C %s",
			    cmd_merge, oid_to_hex(&commit->object.oid));

		/* label the tips of merged branches */
		for (; to_merge; to_merge = to_merge->next) {
			oid = &to_merge->item->object.oid;
			strbuf_addch(&buf, ' ');

			if (!oidset_contains(&interesting, oid)) {
				strbuf_addstr(&buf, label_oid(oid, NULL,
							      &state));
				continue;
			}

			tips_tail = &commit_list_insert(to_merge->item,
							tips_tail)->next;

			strbuf_addstr(&buf, label_oid(oid, label.buf, &state));
		}
		strbuf_addf(&buf, " # %s", oneline.buf);

		FLEX_ALLOC_STR(entry, string, buf.buf);
		oidcpy(&entry->entry.oid, &commit->object.oid);
		oidmap_put(&commit2todo, entry);
	}
	if (skipped_commit)
		advise_if_enabled(ADVICE_SKIPPED_CHERRY_PICKS,
				  _("use --reapply-cherry-picks to include skipped commits"));

	/*
	 * Second phase:
	 * - label branch points
	 * - add HEAD to the branch tips
	 */
	for (iter = commits; iter; iter = iter->next) {
		struct commit_list *parent = iter->item->parents;
		for (; parent; parent = parent->next) {
			struct object_id *oid = &parent->item->object.oid;
			if (!oidset_contains(&interesting, oid))
				continue;
			if (oidset_insert(&child_seen, oid))
				label_oid(oid, "branch-point", &state);
		}

		/* Add HEAD as implicit "tip of branch" */
		if (!iter->next)
			tips_tail = &commit_list_insert(iter->item,
							tips_tail)->next;
	}

	/*
	 * Third phase: output the todo list. This is a bit tricky, as we
	 * want to avoid jumping back and forth between revisions. To
	 * accomplish that goal, we walk backwards from the branch tips,
	 * gathering commits not yet shown, reversing the list on the fly,
	 * then outputting that list (labeling revisions as needed).
	 */
	strbuf_addf(out, "%s onto\n", cmd_label);
	for (iter = tips; iter; iter = iter->next) {
		struct commit_list *list = NULL, *iter2;

		commit = iter->item;
		if (oidset_contains(&shown, &commit->object.oid))
			continue;
		entry = oidmap_get(&state.commit2label, &commit->object.oid);

		if (entry)
			strbuf_addf(out, "\n%c Branch %s\n", comment_line_char, entry->string);
		else
			strbuf_addch(out, '\n');

		while (oidset_contains(&interesting, &commit->object.oid) &&
		       !oidset_contains(&shown, &commit->object.oid)) {
			commit_list_insert(commit, &list);
			if (!commit->parents) {
				commit = NULL;
				break;
			}
			commit = commit->parents->item;
		}

		if (!commit)
			strbuf_addf(out, "%s %s\n", cmd_reset,
				    rebase_cousins || root_with_onto ?
				    "onto" : "[new root]");
		else {
			const char *to = NULL;

			entry = oidmap_get(&state.commit2label,
					   &commit->object.oid);
			if (entry)
				to = entry->string;
			else if (!rebase_cousins)
				to = label_oid(&commit->object.oid, NULL,
					       &state);

			if (!to || !strcmp(to, "onto"))
				strbuf_addf(out, "%s onto\n", cmd_reset);
			else {
				strbuf_reset(&oneline);
				pretty_print_commit(pp, commit, &oneline);
				strbuf_addf(out, "%s %s # %s\n",
					    cmd_reset, to, oneline.buf);
			}
		}

		for (iter2 = list; iter2; iter2 = iter2->next) {
			struct object_id *oid = &iter2->item->object.oid;
			entry = oidmap_get(&commit2todo, oid);
			/* only show if not already upstream */
			if (entry)
				strbuf_addf(out, "%s\n", entry->string);
			entry = oidmap_get(&state.commit2label, oid);
			if (entry)
				strbuf_addf(out, "%s %s\n",
					    cmd_label, entry->string);
			oidset_insert(&shown, oid);
		}

		free_commit_list(list);
	}

	free_commit_list(commits);
	free_commit_list(tips);

	strbuf_release(&label);
	strbuf_release(&oneline);
	strbuf_release(&buf);

	oidmap_free(&commit2todo, 1);
	oidmap_free(&state.commit2label, 1);
	hashmap_clear_and_free(&state.labels, struct labels_entry, entry);
	strbuf_release(&state.buf);

	return 0;
}

int sequencer_make_script(struct repository *r, struct strbuf *out, int argc,
			  const char **argv, unsigned flags)
{
	char *format = NULL;
	struct pretty_print_context pp = {0};
	struct rev_info revs;
	struct commit *commit;
	int keep_empty = flags & TODO_LIST_KEEP_EMPTY;
	const char *insn = flags & TODO_LIST_ABBREVIATE_CMDS ? "p" : "pick";
	int rebase_merges = flags & TODO_LIST_REBASE_MERGES;
	int reapply_cherry_picks = flags & TODO_LIST_REAPPLY_CHERRY_PICKS;
	int skipped_commit = 0;
	int ret = 0;

	repo_init_revisions(r, &revs, NULL);
	revs.verbose_header = 1;
	if (!rebase_merges)
		revs.max_parents = 1;
	revs.cherry_mark = !reapply_cherry_picks;
	revs.limited = 1;
	revs.reverse = 1;
	revs.right_only = 1;
	revs.sort_order = REV_SORT_IN_GRAPH_ORDER;
	revs.topo_order = 1;

	revs.pretty_given = 1;
	git_config_get_string("rebase.instructionFormat", &format);
	if (!format || !*format) {
		free(format);
		format = xstrdup("%s");
	}
	get_commit_format(format, &revs);
	free(format);
	pp.fmt = revs.commit_format;
	pp.output_encoding = get_log_output_encoding();

	if (setup_revisions(argc, argv, &revs, NULL) > 1) {
		ret = error(_("make_script: unhandled options"));
		goto cleanup;
	}

	if (prepare_revision_walk(&revs) < 0) {
		ret = error(_("make_script: error preparing revisions"));
		goto cleanup;
	}

	if (rebase_merges) {
		ret = make_script_with_merges(&pp, &revs, out, flags);
		goto cleanup;
	}

	while ((commit = get_revision(&revs))) {
		int is_empty = is_original_commit_empty(commit);

		if (!is_empty && (commit->object.flags & PATCHSAME)) {
			if (flags & TODO_LIST_WARN_SKIPPED_CHERRY_PICKS)
				warning(_("skipped previously applied commit %s"),
					short_commit_name(commit));
			skipped_commit = 1;
			continue;
		}
		if (is_empty && !keep_empty)
			continue;
		strbuf_addf(out, "%s %s ", insn,
			    oid_to_hex(&commit->object.oid));
		pretty_print_commit(&pp, commit, out);
		if (is_empty)
			strbuf_addf(out, " %c empty", comment_line_char);
		strbuf_addch(out, '\n');
	}
	if (skipped_commit)
		advise_if_enabled(ADVICE_SKIPPED_CHERRY_PICKS,
				  _("use --reapply-cherry-picks to include skipped commits"));
cleanup:
	release_revisions(&revs);
	return ret;
}

/*
 * Add commands after pick and (series of) squash/fixup commands
 * in the todo list.
 */
static void todo_list_add_exec_commands(struct todo_list *todo_list,
					struct string_list *commands)
{
	struct strbuf *buf = &todo_list->buf;
	size_t base_offset = buf->len;
	int i, insert, nr = 0, alloc = 0;
	struct todo_item *items = NULL, *base_items = NULL;

	CALLOC_ARRAY(base_items, commands->nr);
	for (i = 0; i < commands->nr; i++) {
		size_t command_len = strlen(commands->items[i].string);

		strbuf_addstr(buf, commands->items[i].string);
		strbuf_addch(buf, '\n');

		base_items[i].command = TODO_EXEC;
		base_items[i].offset_in_buf = base_offset;
		base_items[i].arg_offset = base_offset;
		base_items[i].arg_len = command_len;

		base_offset += command_len + 1;
	}

	/*
	 * Insert <commands> after every pick. Here, fixup/squash chains
	 * are considered part of the pick, so we insert the commands *after*
	 * those chains if there are any.
	 *
	 * As we insert the exec commands immediately after rearranging
	 * any fixups and before the user edits the list, a fixup chain
	 * can never contain comments (any comments are empty picks that
	 * have been commented out because the user did not specify
	 * --keep-empty).  So, it is safe to insert an exec command
	 * without looking at the command following a comment.
	 */
	insert = 0;
	for (i = 0; i < todo_list->nr; i++) {
		enum todo_command command = todo_list->items[i].command;
		if (insert && !is_fixup(command)) {
			ALLOC_GROW(items, nr + commands->nr, alloc);
			COPY_ARRAY(items + nr, base_items, commands->nr);
			nr += commands->nr;

			insert = 0;
		}

		ALLOC_GROW(items, nr + 1, alloc);
		items[nr++] = todo_list->items[i];

		if (command == TODO_PICK || command == TODO_MERGE)
			insert = 1;
	}

	/* insert or append final <commands> */
	if (insert) {
		ALLOC_GROW(items, nr + commands->nr, alloc);
		COPY_ARRAY(items + nr, base_items, commands->nr);
		nr += commands->nr;
	}

	free(base_items);
	FREE_AND_NULL(todo_list->items);
	todo_list->items = items;
	todo_list->nr = nr;
	todo_list->alloc = alloc;
}

static void todo_list_to_strbuf(struct repository *r, struct todo_list *todo_list,
				struct strbuf *buf, int num, unsigned flags)
{
	struct todo_item *item;
	int i, max = todo_list->nr;

	if (num > 0 && num < max)
		max = num;

	for (item = todo_list->items, i = 0; i < max; i++, item++) {
		char cmd;

		/* if the item is not a command write it and continue */
		if (item->command >= TODO_COMMENT) {
			strbuf_addf(buf, "%.*s\n", item->arg_len,
				    todo_item_get_arg(todo_list, item));
			continue;
		}

		/* add command to the buffer */
		cmd = command_to_char(item->command);
		if ((flags & TODO_LIST_ABBREVIATE_CMDS) && cmd)
			strbuf_addch(buf, cmd);
		else
			strbuf_addstr(buf, command_to_string(item->command));

		/* add commit id */
		if (item->commit) {
			const char *oid = flags & TODO_LIST_SHORTEN_IDS ?
					  short_commit_name(item->commit) :
					  oid_to_hex(&item->commit->object.oid);

			if (item->command == TODO_FIXUP) {
				if (item->flags & TODO_EDIT_FIXUP_MSG)
					strbuf_addstr(buf, " -c");
				else if (item->flags & TODO_REPLACE_FIXUP_MSG) {
					strbuf_addstr(buf, " -C");
				}
			}

			if (item->command == TODO_MERGE) {
				if (item->flags & TODO_EDIT_MERGE_MSG)
					strbuf_addstr(buf, " -c");
				else
					strbuf_addstr(buf, " -C");
			}

			strbuf_addf(buf, " %s", oid);
		}

		/* add all the rest */
		if (!item->arg_len)
			strbuf_addch(buf, '\n');
		else
			strbuf_addf(buf, " %.*s\n", item->arg_len,
				    todo_item_get_arg(todo_list, item));
	}
}

int todo_list_write_to_file(struct repository *r, struct todo_list *todo_list,
			    const char *file, const char *shortrevisions,
			    const char *shortonto, int num, unsigned flags)
{
	int res;
	struct strbuf buf = STRBUF_INIT;

	todo_list_to_strbuf(r, todo_list, &buf, num, flags);
	if (flags & TODO_LIST_APPEND_TODO_HELP)
		append_todo_help(count_commands(todo_list),
				 shortrevisions, shortonto, &buf);

	res = write_message(buf.buf, buf.len, file, 0);
	strbuf_release(&buf);

	return res;
}

/* skip picking commits whose parents are unchanged */
static int skip_unnecessary_picks(struct repository *r,
				  struct todo_list *todo_list,
				  struct object_id *base_oid)
{
	struct object_id *parent_oid;
	int i;

	for (i = 0; i < todo_list->nr; i++) {
		struct todo_item *item = todo_list->items + i;

		if (item->command >= TODO_NOOP)
			continue;
		if (item->command != TODO_PICK)
			break;
		if (parse_commit(item->commit)) {
			return error(_("could not parse commit '%s'"),
				oid_to_hex(&item->commit->object.oid));
		}
		if (!item->commit->parents)
			break; /* root commit */
		if (item->commit->parents->next)
			break; /* merge commit */
		parent_oid = &item->commit->parents->item->object.oid;
		if (!oideq(parent_oid, base_oid))
			break;
		oidcpy(base_oid, &item->commit->object.oid);
	}
	if (i > 0) {
		const char *done_path = rebase_path_done();

		if (todo_list_write_to_file(r, todo_list, done_path, NULL, NULL, i, 0)) {
			error_errno(_("could not write to '%s'"), done_path);
			return -1;
		}

		MOVE_ARRAY(todo_list->items, todo_list->items + i, todo_list->nr - i);
		todo_list->nr -= i;
		todo_list->current = 0;
		todo_list->done_nr += i;

		if (is_fixup(peek_command(todo_list, 0)))
			record_in_rewritten(base_oid, peek_command(todo_list, 0));
	}

	return 0;
}

struct todo_add_branch_context {
	struct todo_item *items;
	size_t items_nr;
	size_t items_alloc;
	struct strbuf *buf;
	struct commit *commit;
	struct string_list refs_to_oids;
};

static int add_decorations_to_list(const struct commit *commit,
				   struct todo_add_branch_context *ctx)
{
	const struct name_decoration *decoration = get_name_decoration(&commit->object);
	const char *head_ref = resolve_ref_unsafe("HEAD",
						  RESOLVE_REF_READING,
						  NULL,
						  NULL);

	while (decoration) {
		struct todo_item *item;
		const char *path;
		size_t base_offset = ctx->buf->len;

		/*
		 * If the branch is the current HEAD, then it will be
		 * updated by the default rebase behavior.
		 */
		if (head_ref && !strcmp(head_ref, decoration->name)) {
			decoration = decoration->next;
			continue;
		}

		ALLOC_GROW(ctx->items,
			ctx->items_nr + 1,
			ctx->items_alloc);
		item = &ctx->items[ctx->items_nr];
		memset(item, 0, sizeof(*item));

		/* If the branch is checked out, then leave a comment instead. */
		if ((path = branch_checked_out(decoration->name))) {
			item->command = TODO_COMMENT;
			strbuf_addf(ctx->buf, "# Ref %s checked out at '%s'\n",
				    decoration->name, path);
		} else {
			struct string_list_item *sti;
			item->command = TODO_UPDATE_REF;
			strbuf_addf(ctx->buf, "%s\n", decoration->name);

			sti = string_list_insert(&ctx->refs_to_oids,
						 decoration->name);
			sti->util = init_update_ref_record(decoration->name);
		}

		item->offset_in_buf = base_offset;
		item->arg_offset = base_offset;
		item->arg_len = ctx->buf->len - base_offset;
		ctx->items_nr++;

		decoration = decoration->next;
	}

	return 0;
}

/*
 * For each 'pick' command, find out if the commit has a decoration in
 * refs/heads/. If so, then add a 'label for-update-refs/' command.
 */
static int todo_list_add_update_ref_commands(struct todo_list *todo_list)
{
	int i, res;
	static struct string_list decorate_refs_exclude = STRING_LIST_INIT_NODUP;
	static struct string_list decorate_refs_exclude_config = STRING_LIST_INIT_NODUP;
	static struct string_list decorate_refs_include = STRING_LIST_INIT_NODUP;
	struct decoration_filter decoration_filter = {
		.include_ref_pattern = &decorate_refs_include,
		.exclude_ref_pattern = &decorate_refs_exclude,
		.exclude_ref_config_pattern = &decorate_refs_exclude_config,
	};
	struct todo_add_branch_context ctx = {
		.buf = &todo_list->buf,
		.refs_to_oids = STRING_LIST_INIT_DUP,
	};

	ctx.items_alloc = 2 * todo_list->nr + 1;
	ALLOC_ARRAY(ctx.items, ctx.items_alloc);

	string_list_append(&decorate_refs_include, "refs/heads/");
	load_ref_decorations(&decoration_filter, 0);

	for (i = 0; i < todo_list->nr; ) {
		struct todo_item *item = &todo_list->items[i];

		/* insert ith item into new list */
		ALLOC_GROW(ctx.items,
			   ctx.items_nr + 1,
			   ctx.items_alloc);

		ctx.items[ctx.items_nr++] = todo_list->items[i++];

		if (item->commit) {
			ctx.commit = item->commit;
			add_decorations_to_list(item->commit, &ctx);
		}
	}

	res = write_update_refs_state(&ctx.refs_to_oids);

	string_list_clear(&ctx.refs_to_oids, 1);

	if (res) {
		/* we failed, so clean up the new list. */
		free(ctx.items);
		return res;
	}

	free(todo_list->items);
	todo_list->items = ctx.items;
	todo_list->nr = ctx.items_nr;
	todo_list->alloc = ctx.items_alloc;

	return 0;
}

int complete_action(struct repository *r, struct replay_opts *opts, unsigned flags,
		    const char *shortrevisions, const char *onto_name,
		    struct commit *onto, const struct object_id *orig_head,
		    struct string_list *commands, unsigned autosquash,
		    unsigned update_refs,
		    struct todo_list *todo_list)
{
	char shortonto[GIT_MAX_HEXSZ + 1];
	const char *todo_file = rebase_path_todo();
	struct todo_list new_todo = TODO_LIST_INIT;
	struct strbuf *buf = &todo_list->buf, buf2 = STRBUF_INIT;
	struct object_id oid = onto->object.oid;
	int res;

	find_unique_abbrev_r(shortonto, &oid, DEFAULT_ABBREV);

	if (buf->len == 0) {
		struct todo_item *item = append_new_todo(todo_list);
		item->command = TODO_NOOP;
		item->commit = NULL;
		item->arg_len = item->arg_offset = item->flags = item->offset_in_buf = 0;
	}

	if (update_refs && todo_list_add_update_ref_commands(todo_list))
		return -1;

	if (autosquash && todo_list_rearrange_squash(todo_list))
		return -1;

	if (commands->nr)
		todo_list_add_exec_commands(todo_list, commands);

	if (count_commands(todo_list) == 0) {
		apply_autostash(rebase_path_autostash());
		sequencer_remove_state(opts);

		return error(_("nothing to do"));
	}

	res = edit_todo_list(r, todo_list, &new_todo, shortrevisions,
			     shortonto, flags);
	if (res == -1)
		return -1;
	else if (res == -2) {
		apply_autostash(rebase_path_autostash());
		sequencer_remove_state(opts);

		return -1;
	} else if (res == -3) {
		apply_autostash(rebase_path_autostash());
		sequencer_remove_state(opts);
		todo_list_release(&new_todo);

		return error(_("nothing to do"));
	} else if (res == -4) {
		checkout_onto(r, opts, onto_name, &onto->object.oid, orig_head);
		todo_list_release(&new_todo);

		return -1;
	}

	/* Expand the commit IDs */
	todo_list_to_strbuf(r, &new_todo, &buf2, -1, 0);
	strbuf_swap(&new_todo.buf, &buf2);
	strbuf_release(&buf2);
	new_todo.total_nr -= new_todo.nr;
	if (todo_list_parse_insn_buffer(r, new_todo.buf.buf, &new_todo) < 0)
		BUG("invalid todo list after expanding IDs:\n%s",
		    new_todo.buf.buf);

	if (opts->allow_ff && skip_unnecessary_picks(r, &new_todo, &oid)) {
		todo_list_release(&new_todo);
		return error(_("could not skip unnecessary pick commands"));
	}

	if (todo_list_write_to_file(r, &new_todo, todo_file, NULL, NULL, -1,
				    flags & ~(TODO_LIST_SHORTEN_IDS))) {
		todo_list_release(&new_todo);
		return error_errno(_("could not write '%s'"), todo_file);
	}

	res = -1;

	if (checkout_onto(r, opts, onto_name, &oid, orig_head))
		goto cleanup;

	if (require_clean_work_tree(r, "rebase", "", 1, 1))
		goto cleanup;

	todo_list_write_total_nr(&new_todo);
	res = pick_commits(r, &new_todo, opts);

cleanup:
	todo_list_release(&new_todo);

	return res;
}

struct subject2item_entry {
	struct hashmap_entry entry;
	int i;
	char subject[FLEX_ARRAY];
};

static int subject2item_cmp(const void *fndata UNUSED,
			    const struct hashmap_entry *eptr,
			    const struct hashmap_entry *entry_or_key,
			    const void *key)
{
	const struct subject2item_entry *a, *b;

	a = container_of(eptr, const struct subject2item_entry, entry);
	b = container_of(entry_or_key, const struct subject2item_entry, entry);

	return key ? strcmp(a->subject, key) : strcmp(a->subject, b->subject);
}

define_commit_slab(commit_todo_item, struct todo_item *);

static int skip_fixupish(const char *subject, const char **p) {
	return skip_prefix(subject, "fixup! ", p) ||
	       skip_prefix(subject, "amend! ", p) ||
	       skip_prefix(subject, "squash! ", p);
}

/*
 * Rearrange the todo list that has both "pick commit-id msg" and "pick
 * commit-id fixup!/squash! msg" in it so that the latter is put immediately
 * after the former, and change "pick" to "fixup"/"squash".
 *
 * Note that if the config has specified a custom instruction format, each log
 * message will have to be retrieved from the commit (as the oneline in the
 * script cannot be trusted) in order to normalize the autosquash arrangement.
 */
int todo_list_rearrange_squash(struct todo_list *todo_list)
{
	struct hashmap subject2item;
	int rearranged = 0, *next, *tail, i, nr = 0, alloc = 0;
	char **subjects;
	struct commit_todo_item commit_todo;
	struct todo_item *items = NULL;

	init_commit_todo_item(&commit_todo);
	/*
	 * The hashmap maps onelines to the respective todo list index.
	 *
	 * If any items need to be rearranged, the next[i] value will indicate
	 * which item was moved directly after the i'th.
	 *
	 * In that case, last[i] will indicate the index of the latest item to
	 * be moved to appear after the i'th.
	 */
	hashmap_init(&subject2item, subject2item_cmp, NULL, todo_list->nr);
	ALLOC_ARRAY(next, todo_list->nr);
	ALLOC_ARRAY(tail, todo_list->nr);
	ALLOC_ARRAY(subjects, todo_list->nr);
	for (i = 0; i < todo_list->nr; i++) {
		struct strbuf buf = STRBUF_INIT;
		struct todo_item *item = todo_list->items + i;
		const char *commit_buffer, *subject, *p;
		size_t subject_len;
		int i2 = -1;
		struct subject2item_entry *entry;

		next[i] = tail[i] = -1;
		if (!item->commit || item->command == TODO_DROP) {
			subjects[i] = NULL;
			continue;
		}

		if (is_fixup(item->command)) {
			clear_commit_todo_item(&commit_todo);
			return error(_("the script was already rearranged."));
		}

		parse_commit(item->commit);
		commit_buffer = logmsg_reencode(item->commit, NULL, "UTF-8");
		find_commit_subject(commit_buffer, &subject);
		format_subject(&buf, subject, " ");
		subject = subjects[i] = strbuf_detach(&buf, &subject_len);
		unuse_commit_buffer(item->commit, commit_buffer);
		if (skip_fixupish(subject, &p)) {
			struct commit *commit2;

			for (;;) {
				while (isspace(*p))
					p++;
				if (!skip_fixupish(p, &p))
					break;
			}

			entry = hashmap_get_entry_from_hash(&subject2item,
						strhash(p), p,
						struct subject2item_entry,
						entry);
			if (entry)
				/* found by title */
				i2 = entry->i;
			else if (!strchr(p, ' ') &&
				 (commit2 =
				  lookup_commit_reference_by_name(p)) &&
				 *commit_todo_item_at(&commit_todo, commit2))
				/* found by commit name */
				i2 = *commit_todo_item_at(&commit_todo, commit2)
					- todo_list->items;
			else {
				/* copy can be a prefix of the commit subject */
				for (i2 = 0; i2 < i; i2++)
					if (subjects[i2] &&
					    starts_with(subjects[i2], p))
						break;
				if (i2 == i)
					i2 = -1;
			}
		}
		if (i2 >= 0) {
			rearranged = 1;
			if (starts_with(subject, "fixup!")) {
				todo_list->items[i].command = TODO_FIXUP;
			} else if (starts_with(subject, "amend!")) {
				todo_list->items[i].command = TODO_FIXUP;
				todo_list->items[i].flags = TODO_REPLACE_FIXUP_MSG;
			} else {
				todo_list->items[i].command = TODO_SQUASH;
			}
			if (tail[i2] < 0) {
				next[i] = next[i2];
				next[i2] = i;
			} else {
				next[i] = next[tail[i2]];
				next[tail[i2]] = i;
			}
			tail[i2] = i;
		} else if (!hashmap_get_from_hash(&subject2item,
						strhash(subject), subject)) {
			FLEX_ALLOC_MEM(entry, subject, subject, subject_len);
			entry->i = i;
			hashmap_entry_init(&entry->entry,
					strhash(entry->subject));
			hashmap_put(&subject2item, &entry->entry);
		}

		*commit_todo_item_at(&commit_todo, item->commit) = item;
	}

	if (rearranged) {
		for (i = 0; i < todo_list->nr; i++) {
			enum todo_command command = todo_list->items[i].command;
			int cur = i;

			/*
			 * Initially, all commands are 'pick's. If it is a
			 * fixup or a squash now, we have rearranged it.
			 */
			if (is_fixup(command))
				continue;

			while (cur >= 0) {
				ALLOC_GROW(items, nr + 1, alloc);
				items[nr++] = todo_list->items[cur];
				cur = next[cur];
			}
		}

		FREE_AND_NULL(todo_list->items);
		todo_list->items = items;
		todo_list->nr = nr;
		todo_list->alloc = alloc;
	}

	free(next);
	free(tail);
	for (i = 0; i < todo_list->nr; i++)
		free(subjects[i]);
	free(subjects);
	hashmap_clear_and_free(&subject2item, struct subject2item_entry, entry);

	clear_commit_todo_item(&commit_todo);

	return 0;
}

int sequencer_determine_whence(struct repository *r, enum commit_whence *whence)
{
	if (refs_ref_exists(get_main_ref_store(r), "CHERRY_PICK_HEAD")) {
		struct object_id cherry_pick_head, rebase_head;

		if (file_exists(git_path_seq_dir()))
			*whence = FROM_CHERRY_PICK_MULTI;
		if (file_exists(rebase_path()) &&
		    !get_oid("REBASE_HEAD", &rebase_head) &&
		    !get_oid("CHERRY_PICK_HEAD", &cherry_pick_head) &&
		    oideq(&rebase_head, &cherry_pick_head))
			*whence = FROM_REBASE_PICK;
		else
			*whence = FROM_CHERRY_PICK_SINGLE;

		return 1;
	}

	return 0;
}

int sequencer_get_update_refs_state(const char *wt_dir,
				    struct string_list *refs)
{
	int result = 0;
	FILE *fp = NULL;
	struct strbuf ref = STRBUF_INIT;
	struct strbuf hash = STRBUF_INIT;
	struct update_ref_record *rec = NULL;

	char *path = rebase_path_update_refs(wt_dir);

	fp = fopen(path, "r");
	if (!fp)
		goto cleanup;

	while (strbuf_getline(&ref, fp) != EOF) {
		struct string_list_item *item;

		CALLOC_ARRAY(rec, 1);

		if (strbuf_getline(&hash, fp) == EOF ||
		    get_oid_hex(hash.buf, &rec->before)) {
			warning(_("update-refs file at '%s' is invalid"),
				  path);
			result = -1;
			goto cleanup;
		}

		if (strbuf_getline(&hash, fp) == EOF ||
		    get_oid_hex(hash.buf, &rec->after)) {
			warning(_("update-refs file at '%s' is invalid"),
				  path);
			result = -1;
			goto cleanup;
		}

		item = string_list_insert(refs, ref.buf);
		item->util = rec;
		rec = NULL;
	}

cleanup:
	if (fp)
		fclose(fp);
	free(path);
	free(rec);
	strbuf_release(&ref);
	strbuf_release(&hash);
	return result;
}
