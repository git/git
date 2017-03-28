#include "cache.h"
#include "lockfile.h"
#include "sequencer.h"
#include "dir.h"
#include "object.h"
#include "commit.h"
#include "tag.h"
#include "run-command.h"
#include "exec_cmd.h"
#include "utf8.h"
#include "cache-tree.h"
#include "diff.h"
#include "revision.h"
#include "rerere.h"
#include "merge-recursive.h"
#include "refs.h"
#include "argv-array.h"
#include "quote.h"
#include "trailer.h"
#include "log-tree.h"
#include "wt-status.h"

#define GIT_REFLOG_ACTION "GIT_REFLOG_ACTION"

const char sign_off_header[] = "Signed-off-by: ";
static const char cherry_picked_prefix[] = "(cherry picked from commit ";

GIT_PATH_FUNC(git_path_seq_dir, "sequencer")

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
static GIT_PATH_FUNC(rebase_path_todo, "rebase-merge/git-rebase-todo")
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
static GIT_PATH_FUNC(rebase_path_msgnum, "rebase-merge/msgnum");
/*
 * The file to keep track of how many commands are to be processed in total
 * (e.g. for the prompt).
 */
static GIT_PATH_FUNC(rebase_path_msgtotal, "rebase-merge/end");
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
 *
 * The first line of the file is of the form
 *     # This is a combination of $count commits.
 * where $count is the number of commits whose messages have been
 * written to the file so far (including the initial "pick" commit).
 * Each time that a commit message is processed, this line is read and
 * updated. It is deleted just before the combined commit is made.
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
 * the abbreviated commit name of the corresponding patch.
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
 * The following files are written by git-rebase just after parsing the
 * command-line (and are only consumed, not modified, by the sequencer).
 */
static GIT_PATH_FUNC(rebase_path_gpg_sign_opt, "rebase-merge/gpg_sign_opt")
static GIT_PATH_FUNC(rebase_path_orig_head, "rebase-merge/orig-head")
static GIT_PATH_FUNC(rebase_path_verbose, "rebase-merge/verbose")
static GIT_PATH_FUNC(rebase_path_head_name, "rebase-merge/head-name")
static GIT_PATH_FUNC(rebase_path_onto, "rebase-merge/onto")
static GIT_PATH_FUNC(rebase_path_autostash, "rebase-merge/autostash")
static GIT_PATH_FUNC(rebase_path_strategy, "rebase-merge/strategy")
static GIT_PATH_FUNC(rebase_path_strategy_opts, "rebase-merge/strategy_opts")

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
	int ignore_footer)
{
	struct trailer_info info;
	int i;
	int found_sob = 0, found_sob_last = 0;

	trailer_info_get(&info, sb->buf);

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

int sequencer_remove_state(struct replay_opts *opts)
{
	struct strbuf dir = STRBUF_INIT;
	int i;

	free(opts->gpg_sign);
	free(opts->strategy);
	for (i = 0; i < opts->xopts_nr; i++)
		free(opts->xopts[i]);
	free(opts->xopts);

	strbuf_addf(&dir, "%s", get_dir(opts));
	remove_dir_recursively(&dir, 0);
	strbuf_release(&dir);

	return 0;
}

static const char *action_name(const struct replay_opts *opts)
{
	switch (opts->action) {
	case REPLAY_REVERT:
		return N_("revert");
	case REPLAY_PICK:
		return N_("cherry-pick");
	case REPLAY_INTERACTIVE_REBASE:
		return N_("rebase -i");
	}
	die(_("Unknown action: %d"), opts->action);
}

struct commit_message {
	char *parent_label;
	char *label;
	char *subject;
	const char *message;
};

static const char *short_commit_name(struct commit *commit)
{
	return find_unique_abbrev(commit->object.oid.hash, DEFAULT_ABBREV);
}

static int get_message(struct commit *commit, struct commit_message *out)
{
	const char *abbrev, *subject;
	int subject_len;

	out->message = logmsg_reencode(commit, NULL, get_commit_output_encoding());
	abbrev = short_commit_name(commit);

	subject_len = find_commit_subject(out->message, &subject);

	out->subject = xmemdupz(subject, subject_len);
	out->label = xstrfmt("%s... %s", abbrev, out->subject);
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

static void print_advice(int show_hint, struct replay_opts *opts)
{
	char *msg = getenv("GIT_CHERRY_PICK_HELP");

	if (msg) {
		fprintf(stderr, "%s\n", msg);
		/*
		 * A conflict has occurred but the porcelain
		 * (typically rebase --interactive) wants to take care
		 * of the commit itself so remove CHERRY_PICK_HEAD
		 */
		unlink(git_path_cherry_pick_head());
		return;
	}

	if (show_hint) {
		if (opts->no_commit)
			advise(_("after resolving the conflicts, mark the corrected paths\n"
				 "with 'git add <paths>' or 'git rm <paths>'"));
		else
			advise(_("after resolving the conflicts, mark the corrected paths\n"
				 "with 'git add <paths>' or 'git rm <paths>'\n"
				 "and commit the result with 'git commit'"));
	}
}

static int write_message(const void *buf, size_t len, const char *filename,
			 int append_eol)
{
	static struct lock_file msg_file;

	int msg_fd = hold_lock_file_for_update(&msg_file, filename, 0);
	if (msg_fd < 0)
		return error_errno(_("could not lock '%s'"), filename);
	if (write_in_full(msg_fd, buf, len) < 0) {
		rollback_lock_file(&msg_file);
		return error_errno(_("could not write to '%s'"), filename);
	}
	if (append_eol && write(msg_fd, "\n", 1) < 0) {
		rollback_lock_file(&msg_file);
		return error_errno(_("could not write eol to '%s'"), filename);
	}
	if (commit_lock_file(&msg_file) < 0) {
		rollback_lock_file(&msg_file);
		return error(_("failed to finalize '%s'."), filename);
	}

	return 0;
}

/*
 * Reads a file that was presumably written by a shell script, i.e. with an
 * end-of-line marker that needs to be stripped.
 *
 * Note that only the last end-of-line marker is stripped, consistent with the
 * behavior of "$(cat path)" in a shell script.
 *
 * Returns 1 if the file was read, 0 if it could not be read or does not exist.
 */
static int read_oneliner(struct strbuf *buf,
	const char *path, int skip_if_empty)
{
	int orig_len = buf->len;

	if (!file_exists(path))
		return 0;

	if (strbuf_read_file(buf, path, 0) < 0) {
		warning_errno(_("could not read '%s'"), path);
		return 0;
	}

	if (buf->len > orig_len && buf->buf[buf->len - 1] == '\n') {
		if (--buf->len > orig_len && buf->buf[buf->len - 1] == '\r')
			--buf->len;
		buf->buf[buf->len] = '\0';
	}

	if (skip_if_empty && buf->len == orig_len)
		return 0;

	return 1;
}

static struct tree *empty_tree(void)
{
	return lookup_tree(EMPTY_TREE_SHA1_BIN);
}

static int error_dirty_index(struct replay_opts *opts)
{
	if (read_cache_unmerged())
		return error_resolve_conflict(_(action_name(opts)));

	error(_("your local changes would be overwritten by %s."),
		_(action_name(opts)));

	if (advice_commit_before_merge)
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

static int fast_forward_to(const unsigned char *to, const unsigned char *from,
			int unborn, struct replay_opts *opts)
{
	struct ref_transaction *transaction;
	struct strbuf sb = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;

	read_cache();
	if (checkout_fast_forward(from, to, 1))
		return -1; /* the callee should have complained already */

	strbuf_addf(&sb, _("%s: fast-forward"), _(action_name(opts)));

	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, "HEAD",
				   to, unborn ? null_sha1 : from,
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

void append_conflicts_hint(struct strbuf *msgbuf)
{
	int i;

	strbuf_addch(msgbuf, '\n');
	strbuf_commented_addf(msgbuf, "Conflicts:\n");
	for (i = 0; i < active_nr;) {
		const struct cache_entry *ce = active_cache[i++];
		if (ce_stage(ce)) {
			strbuf_commented_addf(msgbuf, "\t%s\n", ce->name);
			while (i < active_nr && !strcmp(ce->name,
							active_cache[i]->name))
				i++;
		}
	}
}

static int do_recursive_merge(struct commit *base, struct commit *next,
			      const char *base_label, const char *next_label,
			      unsigned char *head, struct strbuf *msgbuf,
			      struct replay_opts *opts)
{
	struct merge_options o;
	struct tree *result, *next_tree, *base_tree, *head_tree;
	int clean;
	char **xopt;
	static struct lock_file index_lock;

	hold_locked_index(&index_lock, LOCK_DIE_ON_ERROR);

	read_cache();

	init_merge_options(&o);
	o.ancestor = base ? base_label : "(empty tree)";
	o.branch1 = "HEAD";
	o.branch2 = next ? next_label : "(empty tree)";
	if (is_rebase_i(opts))
		o.buffer_output = 2;

	head_tree = parse_tree_indirect(head);
	next_tree = next ? next->tree : empty_tree();
	base_tree = base ? base->tree : empty_tree();

	for (xopt = opts->xopts; xopt != opts->xopts + opts->xopts_nr; xopt++)
		parse_merge_opt(&o, *xopt);

	clean = merge_trees(&o,
			    head_tree,
			    next_tree, base_tree, &result);
	if (is_rebase_i(opts) && clean <= 0)
		fputs(o.obuf.buf, stdout);
	strbuf_release(&o.obuf);
	if (clean < 0)
		return clean;

	if (active_cache_changed &&
	    write_locked_index(&the_index, &index_lock, COMMIT_LOCK))
		/* TRANSLATORS: %s will be "revert", "cherry-pick" or
		 * "rebase -i".
		 */
		return error(_("%s: Unable to write new index file"),
			_(action_name(opts)));
	rollback_lock_file(&index_lock);

	if (opts->signoff)
		append_signoff(msgbuf, 0, 0);

	if (!clean)
		append_conflicts_hint(msgbuf);

	return !clean;
}

static int is_index_unchanged(void)
{
	unsigned char head_sha1[20];
	struct commit *head_commit;

	if (!resolve_ref_unsafe("HEAD", RESOLVE_REF_READING, head_sha1, NULL))
		return error(_("could not resolve HEAD commit\n"));

	head_commit = lookup_commit(head_sha1);

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

	if (!active_cache_tree)
		active_cache_tree = cache_tree();

	if (!cache_tree_fully_valid(active_cache_tree))
		if (cache_tree_update(&the_index, 0))
			return error(_("unable to update cache tree\n"));

	return !hashcmp(active_cache_tree->sha1, head_commit->tree->object.oid.hash);
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
			strbuf_addf(&buf, "'\\\\%c'", *(message++));
	strbuf_addstr(&buf, "'\nGIT_AUTHOR_EMAIL='");
	while (*message && *message != '\n' && *message != '\r')
		if (skip_prefix(message, "> ", &message))
			break;
		else if (*message != '\'')
			strbuf_addch(&buf, *(message++));
		else
			strbuf_addf(&buf, "'\\\\%c'", *(message++));
	strbuf_addstr(&buf, "'\nGIT_AUTHOR_DATE='@");
	while (*message && *message != '\n' && *message != '\r')
		if (*message != '\'')
			strbuf_addch(&buf, *(message++));
		else
			strbuf_addf(&buf, "'\\\\%c'", *(message++));
	res = write_message(buf.buf, buf.len, rebase_path_author_script(), 1);
	strbuf_release(&buf);
	return res;
}

/*
 * Read a list of environment variable assignments (such as the author-script
 * file) into an environment block. Returns -1 on error, 0 otherwise.
 */
static int read_env_script(struct argv_array *env)
{
	struct strbuf script = STRBUF_INIT;
	int i, count = 0;
	char *p, *p2;

	if (strbuf_read_file(&script, rebase_path_author_script(), 256) <= 0)
		return -1;

	for (p = script.buf; *p; p++)
		if (skip_prefix(p, "'\\\\''", (const char **)&p2))
			strbuf_splice(&script, p - script.buf, p2 - p, "'", 1);
		else if (*p == '\'')
			strbuf_splice(&script, p-- - script.buf, 1, "", 0);
		else if (*p == '\n') {
			*p = '\0';
			count++;
		}

	for (i = 0, p = script.buf; i < count; i++) {
		argv_array_push(env, p);
		p += strlen(p) + 1;
	}

	return 0;
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
static int run_git_commit(const char *defmsg, struct replay_opts *opts,
			  unsigned int flags)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	const char *value;

	cmd.git_cmd = 1;

	if (is_rebase_i(opts)) {
		if (!(flags & EDIT_MSG)) {
			cmd.stdout_to_stderr = 1;
			cmd.err = -1;
		}

		if (read_env_script(&cmd.env_array)) {
			const char *gpg_opt = gpg_sign_opt_quoted(opts);

			return error(_(staged_changes_advice),
				     gpg_opt, gpg_opt);
		}
	}

	argv_array_push(&cmd.args, "commit");

	if (!(flags & VERIFY_MSG))
		argv_array_push(&cmd.args, "-n");
	if ((flags & AMEND_MSG))
		argv_array_push(&cmd.args, "--amend");
	if (opts->gpg_sign)
		argv_array_pushf(&cmd.args, "-S%s", opts->gpg_sign);
	if (opts->signoff)
		argv_array_push(&cmd.args, "-s");
	if (defmsg)
		argv_array_pushl(&cmd.args, "-F", defmsg, NULL);
	if ((flags & CLEANUP_MSG))
		argv_array_push(&cmd.args, "--cleanup=strip");
	if ((flags & EDIT_MSG))
		argv_array_push(&cmd.args, "-e");
	else if (!(flags & CLEANUP_MSG) &&
		 !opts->signoff && !opts->record_origin &&
		 git_config_get_value("commit.cleanup", &value))
		argv_array_push(&cmd.args, "--cleanup=verbatim");

	if ((flags & ALLOW_EMPTY))
		argv_array_push(&cmd.args, "--allow-empty");

	if (opts->allow_empty_message)
		argv_array_push(&cmd.args, "--allow-empty-message");

	if (cmd.err == -1) {
		/* hide stderr on success */
		struct strbuf buf = STRBUF_INIT;
		int rc = pipe_command(&cmd,
				      NULL, 0,
				      /* stdout is already redirected */
				      NULL, 0,
				      &buf, 0);
		if (rc)
			fputs(buf.buf, stderr);
		strbuf_release(&buf);
		return rc;
	}

	return run_command(&cmd);
}

static int is_original_commit_empty(struct commit *commit)
{
	const unsigned char *ptree_sha1;

	if (parse_commit(commit))
		return error(_("could not parse commit %s\n"),
			     oid_to_hex(&commit->object.oid));
	if (commit->parents) {
		struct commit *parent = commit->parents->item;
		if (parse_commit(parent))
			return error(_("could not parse parent commit %s\n"),
				oid_to_hex(&parent->object.oid));
		ptree_sha1 = parent->tree->object.oid.hash;
	} else {
		ptree_sha1 = EMPTY_TREE_SHA1_BIN; /* commit is root */
	}

	return !hashcmp(ptree_sha1, commit->tree->object.oid.hash);
}

/*
 * Do we run "git commit" with "--allow-empty"?
 */
static int allow_empty(struct replay_opts *opts, struct commit *commit)
{
	int index_unchanged, empty_commit;

	/*
	 * Three cases:
	 *
	 * (1) we do not allow empty at all and error out.
	 *
	 * (2) we allow ones that were initially empty, but
	 * forbid the ones that become empty;
	 *
	 * (3) we allow both.
	 */
	if (!opts->allow_empty)
		return 0; /* let "git commit" barf as necessary */

	index_unchanged = is_index_unchanged();
	if (index_unchanged < 0)
		return index_unchanged;
	if (!index_unchanged)
		return 0; /* we do not have to say --allow-empty */

	if (opts->keep_redundant_commits)
		return 1;

	empty_commit = is_original_commit_empty(commit);
	if (empty_commit < 0)
		return empty_commit;
	if (!empty_commit)
		return 0;
	else
		return 1;
}

/*
 * Note that ordering matters in this enum. Not only must it match the mapping
 * below, it is also divided into several sections that matter.  When adding
 * new commands, make sure you add it in the right section.
 */
enum todo_command {
	/* commands that handle commits */
	TODO_PICK = 0,
	TODO_REVERT,
	TODO_EDIT,
	TODO_REWORD,
	TODO_FIXUP,
	TODO_SQUASH,
	/* commands that do something else than handling a single commit */
	TODO_EXEC,
	/* commands that do nothing but are counted for reporting progress */
	TODO_NOOP,
	TODO_DROP,
	/* comments (not counted for reporting progress) */
	TODO_COMMENT
};

static struct {
	char c;
	const char *str;
} todo_command_info[] = {
	{ 'p', "pick" },
	{ 0,   "revert" },
	{ 'e', "edit" },
	{ 'r', "reword" },
	{ 'f', "fixup" },
	{ 's', "squash" },
	{ 'x', "exec" },
	{ 0,   "noop" },
	{ 'd', "drop" },
	{ 0,   NULL }
};

static const char *command_to_string(const enum todo_command command)
{
	if (command < TODO_COMMENT)
		return todo_command_info[command].str;
	die("Unknown command: %d", command);
}

static int is_noop(const enum todo_command command)
{
	return TODO_NOOP <= command;
}

static int is_fixup(enum todo_command command)
{
	return command == TODO_FIXUP || command == TODO_SQUASH;
}

static int update_squash_messages(enum todo_command command,
		struct commit *commit, struct replay_opts *opts)
{
	struct strbuf buf = STRBUF_INIT;
	int count, res;
	const char *message, *body;

	if (file_exists(rebase_path_squash_msg())) {
		struct strbuf header = STRBUF_INIT;
		char *eol, *p;

		if (strbuf_read_file(&buf, rebase_path_squash_msg(), 2048) <= 0)
			return error(_("could not read '%s'"),
				rebase_path_squash_msg());

		p = buf.buf + 1;
		eol = strchrnul(buf.buf, '\n');
		if (buf.buf[0] != comment_line_char ||
		    (p += strcspn(p, "0123456789\n")) == eol)
			return error(_("unexpected 1st line of squash message:"
				       "\n\n\t%.*s"),
				     (int)(eol - buf.buf), buf.buf);
		count = strtol(p, NULL, 10);

		if (count < 1)
			return error(_("invalid 1st line of squash message:\n"
				       "\n\t%.*s"),
				     (int)(eol - buf.buf), buf.buf);

		strbuf_addf(&header, "%c ", comment_line_char);
		strbuf_addf(&header,
			    _("This is a combination of %d commits."), ++count);
		strbuf_splice(&buf, 0, eol - buf.buf, header.buf, header.len);
		strbuf_release(&header);
	} else {
		unsigned char head[20];
		struct commit *head_commit;
		const char *head_message, *body;

		if (get_sha1("HEAD", head))
			return error(_("need a HEAD to fixup"));
		if (!(head_commit = lookup_commit_reference(head)))
			return error(_("could not read HEAD"));
		if (!(head_message = get_commit_buffer(head_commit, NULL)))
			return error(_("could not read HEAD's commit message"));

		find_commit_subject(head_message, &body);
		if (write_message(body, strlen(body),
				  rebase_path_fixup_msg(), 0)) {
			unuse_commit_buffer(head_commit, head_message);
			return error(_("cannot write '%s'"),
				     rebase_path_fixup_msg());
		}

		count = 2;
		strbuf_addf(&buf, "%c ", comment_line_char);
		strbuf_addf(&buf, _("This is a combination of %d commits."),
			    count);
		strbuf_addf(&buf, "\n%c ", comment_line_char);
		strbuf_addstr(&buf, _("This is the 1st commit message:"));
		strbuf_addstr(&buf, "\n\n");
		strbuf_addstr(&buf, body);

		unuse_commit_buffer(head_commit, head_message);
	}

	if (!(message = get_commit_buffer(commit, NULL)))
		return error(_("could not read commit message of %s"),
			     oid_to_hex(&commit->object.oid));
	find_commit_subject(message, &body);

	if (command == TODO_SQUASH) {
		unlink(rebase_path_fixup_msg());
		strbuf_addf(&buf, "\n%c ", comment_line_char);
		strbuf_addf(&buf, _("This is the commit message #%d:"), count);
		strbuf_addstr(&buf, "\n\n");
		strbuf_addstr(&buf, body);
	} else if (command == TODO_FIXUP) {
		strbuf_addf(&buf, "\n%c ", comment_line_char);
		strbuf_addf(&buf, _("The commit message #%d will be skipped:"),
			    count);
		strbuf_addstr(&buf, "\n\n");
		strbuf_add_commented_lines(&buf, body, strlen(body));
	} else
		return error(_("unknown command: %d"), command);
	unuse_commit_buffer(commit, message);

	res = write_message(buf.buf, buf.len, rebase_path_squash_msg(), 0);
	strbuf_release(&buf);
	return res;
}

static void flush_rewritten_pending(void) {
	struct strbuf buf = STRBUF_INIT;
	unsigned char newsha1[20];
	FILE *out;

	if (strbuf_read_file(&buf, rebase_path_rewritten_pending(), 82) > 0 &&
			!get_sha1("HEAD", newsha1) &&
			(out = fopen(rebase_path_rewritten_list(), "a"))) {
		char *bol = buf.buf, *eol;

		while (*bol) {
			eol = strchrnul(bol, '\n');
			fprintf(out, "%.*s %s\n", (int)(eol - bol),
					bol, sha1_to_hex(newsha1));
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
		enum todo_command next_command) {
	FILE *out = fopen(rebase_path_rewritten_pending(), "a");

	if (!out)
		return;

	fprintf(out, "%s\n", oid_to_hex(oid));
	fclose(out);

	if (!is_fixup(next_command))
		flush_rewritten_pending();
}

static int do_pick_commit(enum todo_command command, struct commit *commit,
		struct replay_opts *opts, int final_fixup)
{
	unsigned int flags = opts->edit ? EDIT_MSG : 0;
	const char *msg_file = opts->edit ? NULL : git_path_merge_msg();
	unsigned char head[20];
	struct commit *base, *next, *parent;
	const char *base_label, *next_label;
	struct commit_message msg = { NULL, NULL, NULL, NULL };
	struct strbuf msgbuf = STRBUF_INIT;
	int res, unborn = 0, allow;

	if (opts->no_commit) {
		/*
		 * We do not intend to commit immediately.  We just want to
		 * merge the differences in, so let's compute the tree
		 * that represents the "current" state for merge-recursive
		 * to work on.
		 */
		if (write_cache_as_tree(head, 0, NULL))
			return error(_("your index file is unmerged."));
	} else {
		unborn = get_sha1("HEAD", head);
		if (unborn)
			hashcpy(head, EMPTY_TREE_SHA1_BIN);
		if (index_differs_from(unborn ? EMPTY_TREE_SHA1_HEX : "HEAD", 0, 0))
			return error_dirty_index(opts);
	}
	discard_cache();

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
	} else if (0 < opts->mainline)
		return error(_("mainline was specified but commit %s is not a merge."),
			oid_to_hex(&commit->object.oid));
	else
		parent = commit->parents->item;

	if (get_message(commit, &msg) != 0)
		return error(_("cannot get commit message for %s"),
			oid_to_hex(&commit->object.oid));

	if (opts->allow_ff && !is_fixup(command) &&
	    ((parent && !hashcmp(parent->object.oid.hash, head)) ||
	     (!parent && unborn))) {
		if (is_rebase_i(opts))
			write_author_script(msg.message);
		res = fast_forward_to(commit->object.oid.hash, head, unborn,
			opts);
		if (res || command != TODO_REWORD)
			goto leave;
		flags |= EDIT_MSG | AMEND_MSG;
		if (command == TODO_REWORD)
			flags |= VERIFY_MSG;
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
		strbuf_addstr(&msgbuf, "Revert \"");
		strbuf_addstr(&msgbuf, msg.subject);
		strbuf_addstr(&msgbuf, "\"\n\nThis reverts commit ");
		strbuf_addstr(&msgbuf, oid_to_hex(&commit->object.oid));

		if (commit->parents && commit->parents->next) {
			strbuf_addstr(&msgbuf, ", reversing\nchanges made to ");
			strbuf_addstr(&msgbuf, oid_to_hex(&parent->object.oid));
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
			if (!has_conforming_footer(&msgbuf, NULL, 0))
				strbuf_addch(&msgbuf, '\n');
			strbuf_addstr(&msgbuf, cherry_picked_prefix);
			strbuf_addstr(&msgbuf, oid_to_hex(&commit->object.oid));
			strbuf_addstr(&msgbuf, ")\n");
		}
	}

	if (command == TODO_REWORD)
		flags |= EDIT_MSG | VERIFY_MSG;
	else if (is_fixup(command)) {
		if (update_squash_messages(command, commit, opts))
			return -1;
		flags |= AMEND_MSG;
		if (!final_fixup)
			msg_file = rebase_path_squash_msg();
		else if (file_exists(rebase_path_fixup_msg())) {
			flags |= CLEANUP_MSG;
			msg_file = rebase_path_fixup_msg();
		} else {
			const char *dest = git_path("SQUASH_MSG");
			unlink(dest);
			if (copy_file(dest, rebase_path_squash_msg(), 0666))
				return error(_("could not rename '%s' to '%s'"),
					     rebase_path_squash_msg(), dest);
			unlink(git_path("MERGE_MSG"));
			msg_file = dest;
			flags |= EDIT_MSG;
		}
	}

	if (is_rebase_i(opts) && write_author_script(msg.message) < 0)
		res = -1;
	else if (!opts->strategy || !strcmp(opts->strategy, "recursive") || command == TODO_REVERT) {
		res = do_recursive_merge(base, next, base_label, next_label,
					 head, &msgbuf, opts);
		if (res < 0)
			return res;
		res |= write_message(msgbuf.buf, msgbuf.len,
				     git_path_merge_msg(), 0);
	} else {
		struct commit_list *common = NULL;
		struct commit_list *remotes = NULL;

		res = write_message(msgbuf.buf, msgbuf.len,
				    git_path_merge_msg(), 0);

		commit_list_insert(base, &common);
		commit_list_insert(next, &remotes);
		res |= try_merge_command(opts->strategy,
					 opts->xopts_nr, (const char **)opts->xopts,
					common, sha1_to_hex(head), remotes);
		free_commit_list(common);
		free_commit_list(remotes);
	}
	strbuf_release(&msgbuf);

	/*
	 * If the merge was clean or if it failed due to conflict, we write
	 * CHERRY_PICK_HEAD for the subsequent invocation of commit to use.
	 * However, if the merge did not even start, then we don't want to
	 * write it at all.
	 */
	if (command == TODO_PICK && !opts->no_commit && (res == 0 || res == 1) &&
	    update_ref(NULL, "CHERRY_PICK_HEAD", commit->object.oid.hash, NULL,
		       REF_NODEREF, UPDATE_REFS_MSG_ON_ERR))
		res = -1;
	if (command == TODO_REVERT && ((opts->no_commit && res == 0) || res == 1) &&
	    update_ref(NULL, "REVERT_HEAD", commit->object.oid.hash, NULL,
		       REF_NODEREF, UPDATE_REFS_MSG_ON_ERR))
		res = -1;

	if (res) {
		error(command == TODO_REVERT
		      ? _("could not revert %s... %s")
		      : _("could not apply %s... %s"),
		      short_commit_name(commit), msg.subject);
		print_advice(res == 1, opts);
		rerere(opts->allow_rerere_auto);
		goto leave;
	}

	allow = allow_empty(opts, commit);
	if (allow < 0) {
		res = allow;
		goto leave;
	} else if (allow)
		flags |= ALLOW_EMPTY;
	if (!opts->no_commit)
fast_forward_edit:
		res = run_git_commit(msg_file, opts, flags);

	if (!res && final_fixup) {
		unlink(rebase_path_fixup_msg());
		unlink(rebase_path_squash_msg());
	}

leave:
	free_message(commit, &msg);
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

	if (!opts->revs->commits)
		return error(_("empty commit set passed"));
	return 0;
}

static int read_and_refresh_cache(struct replay_opts *opts)
{
	static struct lock_file index_lock;
	int index_fd = hold_locked_index(&index_lock, 0);
	if (read_index_preload(&the_index, NULL) < 0) {
		rollback_lock_file(&index_lock);
		return error(_("git %s: failed to read the index"),
			_(action_name(opts)));
	}
	refresh_index(&the_index, REFRESH_QUIET|REFRESH_UNMERGED, NULL, NULL, NULL);
	if (the_index.cache_changed && index_fd >= 0) {
		if (write_locked_index(&the_index, &index_lock, COMMIT_LOCK)) {
			rollback_lock_file(&index_lock);
			return error(_("git %s: failed to refresh the index"),
				_(action_name(opts)));
		}
	}
	rollback_lock_file(&index_lock);
	return 0;
}

struct todo_item {
	enum todo_command command;
	struct commit *commit;
	const char *arg;
	int arg_len;
	size_t offset_in_buf;
};

struct todo_list {
	struct strbuf buf;
	struct todo_item *items;
	int nr, alloc, current;
	int done_nr, total_nr;
};

#define TODO_LIST_INIT { STRBUF_INIT }

static void todo_list_release(struct todo_list *todo_list)
{
	strbuf_release(&todo_list->buf);
	free(todo_list->items);
	todo_list->items = NULL;
	todo_list->nr = todo_list->alloc = 0;
}

static struct todo_item *append_new_todo(struct todo_list *todo_list)
{
	ALLOC_GROW(todo_list->items, todo_list->nr + 1, todo_list->alloc);
	return todo_list->items + todo_list->nr++;
}

static int parse_insn_line(struct todo_item *item, const char *bol, char *eol)
{
	unsigned char commit_sha1[20];
	char *end_of_object_name;
	int i, saved, status, padding;

	/* left-trim */
	bol += strspn(bol, " \t");

	if (bol == eol || *bol == '\r' || *bol == comment_line_char) {
		item->command = TODO_COMMENT;
		item->commit = NULL;
		item->arg = bol;
		item->arg_len = eol - bol;
		return 0;
	}

	for (i = 0; i < TODO_COMMENT; i++)
		if (skip_prefix(bol, todo_command_info[i].str, &bol)) {
			item->command = i;
			break;
		} else if (bol[1] == ' ' && *bol == todo_command_info[i].c) {
			bol++;
			item->command = i;
			break;
		}
	if (i >= TODO_COMMENT)
		return -1;

	if (item->command == TODO_NOOP) {
		item->commit = NULL;
		item->arg = bol;
		item->arg_len = eol - bol;
		return 0;
	}

	/* Eat up extra spaces/ tabs before object name */
	padding = strspn(bol, " \t");
	if (!padding)
		return -1;
	bol += padding;

	if (item->command == TODO_EXEC) {
		item->arg = bol;
		item->arg_len = (int)(eol - bol);
		return 0;
	}

	end_of_object_name = (char *) bol + strcspn(bol, " \t\n");
	saved = *end_of_object_name;
	*end_of_object_name = '\0';
	status = get_sha1(bol, commit_sha1);
	*end_of_object_name = saved;

	item->arg = end_of_object_name + strspn(end_of_object_name, " \t");
	item->arg_len = (int)(eol - item->arg);

	if (status < 0)
		return -1;

	item->commit = lookup_commit_reference(commit_sha1);
	return !item->commit;
}

static int parse_insn_buffer(char *buf, struct todo_list *todo_list)
{
	struct todo_item *item;
	char *p = buf, *next_p;
	int i, res = 0, fixup_okay = file_exists(rebase_path_done());

	for (i = 1; *p; i++, p = next_p) {
		char *eol = strchrnul(p, '\n');

		next_p = *eol ? eol + 1 /* skip LF */ : eol;

		if (p != eol && eol[-1] == '\r')
			eol--; /* strip Carriage Return */

		item = append_new_todo(todo_list);
		item->offset_in_buf = p - todo_list->buf.buf;
		if (parse_insn_line(item, p, eol)) {
			res = error(_("invalid line %d: %.*s"),
				i, (int)(eol - p), p);
			item->command = TODO_NOOP;
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

static int read_populate_todo(struct todo_list *todo_list,
			struct replay_opts *opts)
{
	const char *todo_file = get_todo_path(opts);
	int fd, res;

	strbuf_reset(&todo_list->buf);
	fd = open(todo_file, O_RDONLY);
	if (fd < 0)
		return error_errno(_("could not open '%s'"), todo_file);
	if (strbuf_read(&todo_list->buf, fd, 0) < 0) {
		close(fd);
		return error(_("could not read '%s'."), todo_file);
	}
	close(fd);

	res = parse_insn_buffer(todo_list->buf.buf, todo_list);
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
		FILE *f = fopen(rebase_path_msgtotal(), "w");

		if (strbuf_read_file(&done.buf, rebase_path_done(), 0) > 0 &&
				!parse_insn_buffer(done.buf.buf, &done))
			todo_list->done_nr = count_commands(&done);
		else
			todo_list->done_nr = 0;

		todo_list->total_nr = todo_list->done_nr
			+ count_commands(todo_list);
		todo_list_release(&done);

		if (f) {
			fprintf(f, "%d\n", todo_list->total_nr);
			fclose(f);
		}
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
	} else
		return error(_("invalid key: %s"), key);

	if (!error_flag)
		return error(_("invalid value for %s: %s"), key, value);

	return 0;
}

static void read_strategy_opts(struct replay_opts *opts, struct strbuf *buf)
{
	int i;

	strbuf_reset(buf);
	if (!read_oneliner(buf, rebase_path_strategy(), 0))
		return;
	opts->strategy = strbuf_detach(buf, NULL);
	if (!read_oneliner(buf, rebase_path_strategy_opts(), 0))
		return;

	opts->xopts_nr = split_cmdline(buf->buf, (const char ***)&opts->xopts);
	for (i = 0; i < opts->xopts_nr; i++) {
		const char *arg = opts->xopts[i];

		skip_prefix(arg, "--", &arg);
		opts->xopts[i] = xstrdup(arg);
	}
}

static int read_populate_opts(struct replay_opts *opts)
{
	if (is_rebase_i(opts)) {
		struct strbuf buf = STRBUF_INIT;

		if (read_oneliner(&buf, rebase_path_gpg_sign_opt(), 1)) {
			if (!starts_with(buf.buf, "-S"))
				strbuf_reset(&buf);
			else {
				free(opts->gpg_sign);
				opts->gpg_sign = xstrdup(buf.buf + 2);
			}
		}

		if (file_exists(rebase_path_verbose()))
			opts->verbose = 1;

		read_strategy_opts(opts, &buf);
		strbuf_release(&buf);

		return 0;
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

static int walk_revs_populate_todo(struct todo_list *todo_list,
				struct replay_opts *opts)
{
	enum todo_command command = opts->action == REPLAY_PICK ?
		TODO_PICK : TODO_REVERT;
	const char *command_string = todo_command_info[command].str;
	struct commit *commit;

	if (prepare_revs(opts))
		return -1;

	while ((commit = get_revision(opts->revs))) {
		struct todo_item *item = append_new_todo(todo_list);
		const char *commit_buffer = get_commit_buffer(commit, NULL);
		const char *subject;
		int subject_len;

		item->command = command;
		item->commit = commit;
		item->arg = NULL;
		item->arg_len = 0;
		item->offset_in_buf = todo_list->buf.len;
		subject_len = find_commit_subject(commit_buffer, &subject);
		strbuf_addf(&todo_list->buf, "%s %s %.*s\n", command_string,
			short_commit_name(commit), subject_len, subject);
		unuse_commit_buffer(commit, commit_buffer);
	}
	return 0;
}

static int create_seq_dir(void)
{
	if (file_exists(git_path_seq_dir())) {
		error(_("a cherry-pick or revert is already in progress"));
		advise(_("try \"git cherry-pick (--continue | --quit | --abort)\""));
		return -1;
	} else if (mkdir(git_path_seq_dir(), 0777) < 0)
		return error_errno(_("could not create sequencer directory '%s'"),
				   git_path_seq_dir());
	return 0;
}

static int save_head(const char *head)
{
	static struct lock_file head_lock;
	struct strbuf buf = STRBUF_INIT;
	int fd;

	fd = hold_lock_file_for_update(&head_lock, git_path_head_file(), 0);
	if (fd < 0) {
		rollback_lock_file(&head_lock);
		return error_errno(_("could not lock HEAD"));
	}
	strbuf_addf(&buf, "%s\n", head);
	if (write_in_full(fd, buf.buf, buf.len) < 0) {
		rollback_lock_file(&head_lock);
		return error_errno(_("could not write to '%s'"),
				   git_path_head_file());
	}
	if (commit_lock_file(&head_lock) < 0) {
		rollback_lock_file(&head_lock);
		return error(_("failed to finalize '%s'."), git_path_head_file());
	}
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

	return !oidcmp(&actual_head, &expected_head);
}

static int reset_for_rollback(const unsigned char *sha1)
{
	const char *argv[4];	/* reset --merge <arg> + NULL */

	argv[0] = "reset";
	argv[1] = "--merge";
	argv[2] = sha1_to_hex(sha1);
	argv[3] = NULL;
	return run_command_v_opt(argv, RUN_GIT_CMD);
}

static int rollback_single_pick(void)
{
	unsigned char head_sha1[20];

	if (!file_exists(git_path_cherry_pick_head()) &&
	    !file_exists(git_path_revert_head()))
		return error(_("no cherry-pick or revert in progress"));
	if (read_ref_full("HEAD", 0, head_sha1, NULL))
		return error(_("cannot resolve HEAD"));
	if (is_null_sha1(head_sha1))
		return error(_("cannot abort from a branch yet to be born"));
	return reset_for_rollback(head_sha1);
}

int sequencer_rollback(struct replay_opts *opts)
{
	FILE *f;
	unsigned char sha1[20];
	struct strbuf buf = STRBUF_INIT;

	f = fopen(git_path_head_file(), "r");
	if (!f && errno == ENOENT) {
		/*
		 * There is no multiple-cherry-pick in progress.
		 * If CHERRY_PICK_HEAD or REVERT_HEAD indicates
		 * a single-cherry-pick in progress, abort that.
		 */
		return rollback_single_pick();
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
	if (get_sha1_hex(buf.buf, sha1) || buf.buf[40] != '\0') {
		error(_("stored pre-cherry-pick HEAD file '%s' is corrupt"),
			git_path_head_file());
		goto fail;
	}
	if (is_null_sha1(sha1)) {
		error(_("cannot abort from a branch yet to be born"));
		goto fail;
	}

	if (!rollback_is_safe()) {
		/* Do not error, just do not rollback */
		warning(_("You seem to have moved HEAD. "
			  "Not rewinding, check your HEAD!"));
	} else
	if (reset_for_rollback(sha1))
		goto fail;
	strbuf_release(&buf);
	return sequencer_remove_state(opts);
fail:
	strbuf_release(&buf);
	return -1;
}

static int save_todo(struct todo_list *todo_list, struct replay_opts *opts)
{
	static struct lock_file todo_lock;
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
	offset = next < todo_list->nr ?
		todo_list->items[next].offset_in_buf : todo_list->buf.len;
	if (write_in_full(fd, todo_list->buf.buf + offset,
			todo_list->buf.len - offset) < 0)
		return error_errno(_("could not write to '%s'"), todo_path);
	if (commit_lock_file(&todo_lock) < 0)
		return error(_("failed to finalize '%s'."), todo_path);

	if (is_rebase_i(opts)) {
		const char *done_path = rebase_path_done();
		int fd = open(done_path, O_CREAT | O_WRONLY | O_APPEND, 0666);
		int prev_offset = !next ? 0 :
			todo_list->items[next - 1].offset_in_buf;

		if (fd >= 0 && offset > prev_offset &&
		    write_in_full(fd, todo_list->buf.buf + prev_offset,
				  offset - prev_offset) < 0) {
			close(fd);
			return error_errno(_("could not write to '%s'"),
					   done_path);
		}
		if (fd >= 0)
			close(fd);
	}
	return 0;
}

static int save_opts(struct replay_opts *opts)
{
	const char *opts_file = git_path_opts_file();
	int res = 0;

	if (opts->no_commit)
		res |= git_config_set_in_file_gently(opts_file, "options.no-commit", "true");
	if (opts->edit)
		res |= git_config_set_in_file_gently(opts_file, "options.edit", "true");
	if (opts->signoff)
		res |= git_config_set_in_file_gently(opts_file, "options.signoff", "true");
	if (opts->record_origin)
		res |= git_config_set_in_file_gently(opts_file, "options.record-origin", "true");
	if (opts->allow_ff)
		res |= git_config_set_in_file_gently(opts_file, "options.allow-ff", "true");
	if (opts->mainline) {
		struct strbuf buf = STRBUF_INIT;
		strbuf_addf(&buf, "%d", opts->mainline);
		res |= git_config_set_in_file_gently(opts_file, "options.mainline", buf.buf);
		strbuf_release(&buf);
	}
	if (opts->strategy)
		res |= git_config_set_in_file_gently(opts_file, "options.strategy", opts->strategy);
	if (opts->gpg_sign)
		res |= git_config_set_in_file_gently(opts_file, "options.gpg-sign", opts->gpg_sign);
	if (opts->xopts) {
		int i;
		for (i = 0; i < opts->xopts_nr; i++)
			res |= git_config_set_multivar_in_file_gently(opts_file,
							"options.strategy-option",
							opts->xopts[i], "^$", 0);
	}
	return res;
}

static int make_patch(struct commit *commit, struct replay_opts *opts)
{
	struct strbuf buf = STRBUF_INIT;
	struct rev_info log_tree_opt;
	const char *subject, *p;
	int res = 0;

	p = short_commit_name(commit);
	if (write_message(p, strlen(p), rebase_path_stopped_sha(), 1) < 0)
		return -1;

	strbuf_addf(&buf, "%s/patch", get_dir(opts));
	memset(&log_tree_opt, 0, sizeof(log_tree_opt));
	init_revisions(&log_tree_opt, NULL);
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
		const char *commit_buffer = get_commit_buffer(commit, NULL);
		find_commit_subject(commit_buffer, &subject);
		res |= write_message(subject, strlen(subject), buf.buf, 1);
		unuse_commit_buffer(commit, commit_buffer);
	}
	strbuf_release(&buf);

	return res;
}

static int intend_to_amend(void)
{
	unsigned char head[20];
	char *p;

	if (get_sha1("HEAD", head))
		return error(_("cannot read HEAD"));

	p = sha1_to_hex(head);
	return write_message(p, strlen(p), rebase_path_amend(), 1);
}

static int error_with_patch(struct commit *commit,
	const char *subject, int subject_len,
	struct replay_opts *opts, int exit_code, int to_amend)
{
	if (make_patch(commit, opts))
		return -1;

	if (to_amend) {
		if (intend_to_amend())
			return -1;

		fprintf(stderr, "You can amend the commit now, with\n"
			"\n"
			"  git commit --amend %s\n"
			"\n"
			"Once you are satisfied with your changes, run\n"
			"\n"
			"  git rebase --continue\n", gpg_sign_opt_quoted(opts));
	} else if (exit_code)
		fprintf(stderr, "Could not apply %s... %.*s\n",
			short_commit_name(commit), subject_len, subject);

	return exit_code;
}

static int error_failed_squash(struct commit *commit,
	struct replay_opts *opts, int subject_len, const char *subject)
{
	if (rename(rebase_path_squash_msg(), rebase_path_message()))
		return error(_("could not rename '%s' to '%s'"),
			rebase_path_squash_msg(), rebase_path_message());
	unlink(rebase_path_fixup_msg());
	unlink(git_path("MERGE_MSG"));
	if (copy_file(git_path("MERGE_MSG"), rebase_path_message(), 0666))
		return error(_("could not copy '%s' to '%s'"),
			     rebase_path_message(), git_path("MERGE_MSG"));
	return error_with_patch(commit, subject, subject_len, opts, 1, 0);
}

static int do_exec(const char *command_line)
{
	const char *child_argv[] = { NULL, NULL };
	int dirty, status;

	fprintf(stderr, "Executing: %s\n", command_line);
	child_argv[0] = command_line;
	status = run_command_v_opt(child_argv, RUN_USING_SHELL);

	/* force re-reading of the cache */
	if (discard_cache() < 0 || read_cache() < 0)
		return error(_("could not read index"));

	dirty = require_clean_work_tree("rebase", NULL, 1, 1);

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

static int apply_autostash(struct replay_opts *opts)
{
	struct strbuf stash_sha1 = STRBUF_INIT;
	struct child_process child = CHILD_PROCESS_INIT;
	int ret = 0;

	if (!read_oneliner(&stash_sha1, rebase_path_autostash(), 1)) {
		strbuf_release(&stash_sha1);
		return 0;
	}
	strbuf_trim(&stash_sha1);

	child.git_cmd = 1;
	argv_array_push(&child.args, "stash");
	argv_array_push(&child.args, "apply");
	argv_array_push(&child.args, stash_sha1.buf);
	if (!run_command(&child))
		printf(_("Applied autostash."));
	else {
		struct child_process store = CHILD_PROCESS_INIT;

		store.git_cmd = 1;
		argv_array_push(&store.args, "stash");
		argv_array_push(&store.args, "store");
		argv_array_push(&store.args, "-m");
		argv_array_push(&store.args, "autostash");
		argv_array_push(&store.args, "-q");
		argv_array_push(&store.args, stash_sha1.buf);
		if (run_command(&store))
			ret = error(_("cannot store %s"), stash_sha1.buf);
		else
			printf(_("Applying autostash resulted in conflicts.\n"
				"Your changes are safe in the stash.\n"
				"You can run \"git stash pop\" or"
				" \"git stash drop\" at any time.\n"));
	}

	strbuf_release(&stash_sha1);
	return ret;
}

static const char *reflog_message(struct replay_opts *opts,
	const char *sub_action, const char *fmt, ...)
{
	va_list ap;
	static struct strbuf buf = STRBUF_INIT;

	va_start(ap, fmt);
	strbuf_reset(&buf);
	strbuf_addstr(&buf, action_name(opts));
	if (sub_action)
		strbuf_addf(&buf, " (%s)", sub_action);
	if (fmt) {
		strbuf_addstr(&buf, ": ");
		strbuf_vaddf(&buf, fmt, ap);
	}
	va_end(ap);

	return buf.buf;
}

static int pick_commits(struct todo_list *todo_list, struct replay_opts *opts)
{
	int res = 0;

	setenv(GIT_REFLOG_ACTION, action_name(opts), 0);
	if (opts->allow_ff)
		assert(!(opts->signoff || opts->no_commit ||
				opts->record_origin || opts->edit));
	if (read_and_refresh_cache(opts))
		return -1;

	while (todo_list->current < todo_list->nr) {
		struct todo_item *item = todo_list->items + todo_list->current;
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
				fprintf(stderr, "Rebasing (%d/%d)%s",
					todo_list->done_nr,
					todo_list->total_nr,
					opts->verbose ? "\n" : "\r");
			}
			unlink(rebase_path_message());
			unlink(rebase_path_author_script());
			unlink(rebase_path_stopped_sha());
			unlink(rebase_path_amend());
		}
		if (item->command <= TODO_SQUASH) {
			if (is_rebase_i(opts))
				setenv("GIT_REFLOG_ACTION", reflog_message(opts,
					command_to_string(item->command), NULL),
					1);
			res = do_pick_commit(item->command, item->commit,
					opts, is_final_fixup(todo_list));
			if (is_rebase_i(opts) && res < 0) {
				/* Reschedule */
				todo_list->current--;
				if (save_todo(todo_list, opts))
					return -1;
			}
			if (item->command == TODO_EDIT) {
				struct commit *commit = item->commit;
				if (!res)
					fprintf(stderr,
						_("Stopped at %s...  %.*s\n"),
						short_commit_name(commit),
						item->arg_len, item->arg);
				return error_with_patch(commit,
					item->arg, item->arg_len, opts, res,
					!res);
			}
			if (is_rebase_i(opts) && !res)
				record_in_rewritten(&item->commit->object.oid,
					peek_command(todo_list, 1));
			if (res && is_fixup(item->command)) {
				if (res == 1)
					intend_to_amend();
				return error_failed_squash(item->commit, opts,
					item->arg_len, item->arg);
			} else if (res && is_rebase_i(opts))
				return res | error_with_patch(item->commit,
					item->arg, item->arg_len, opts, res,
					item->command == TODO_REWORD);
		} else if (item->command == TODO_EXEC) {
			char *end_of_arg = (char *)(item->arg + item->arg_len);
			int saved = *end_of_arg;

			*end_of_arg = '\0';
			res = do_exec(item->arg);
			*end_of_arg = saved;
		} else if (!is_noop(item->command))
			return error(_("unknown command %d"), item->command);

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
			unsigned char head[20], orig[20];
			int res;

			if (get_sha1("HEAD", head)) {
				res = error(_("cannot read HEAD"));
cleanup_head_ref:
				strbuf_release(&head_ref);
				strbuf_release(&buf);
				return res;
			}
			if (!read_oneliner(&buf, rebase_path_orig_head(), 0) ||
					get_sha1_hex(buf.buf, orig)) {
				res = error(_("could not read orig-head"));
				goto cleanup_head_ref;
			}
			if (!read_oneliner(&buf, rebase_path_onto(), 0)) {
				res = error(_("could not read 'onto'"));
				goto cleanup_head_ref;
			}
			msg = reflog_message(opts, "finish", "%s onto %s",
				head_ref.buf, buf.buf);
			if (update_ref(msg, head_ref.buf, head, orig,
					REF_NODEREF, UPDATE_REFS_MSG_ON_ERR)) {
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
			init_revisions(&log_tree_opt, NULL);
			log_tree_opt.diff = 1;
			log_tree_opt.diffopt.output_format =
				DIFF_FORMAT_DIFFSTAT;
			log_tree_opt.disable_stdin = 1;

			if (read_oneliner(&buf, rebase_path_orig_head(), 0) &&
			    !get_sha1(buf.buf, orig.hash) &&
			    !get_sha1("HEAD", head.hash)) {
				diff_tree_sha1(orig.hash, head.hash,
					       "", &log_tree_opt.diffopt);
				log_tree_diff_flush(&log_tree_opt);
			}
		}
		flush_rewritten_pending();
		if (!stat(rebase_path_rewritten_list(), &st) &&
				st.st_size > 0) {
			struct child_process child = CHILD_PROCESS_INIT;
			const char *post_rewrite_hook =
				find_hook("post-rewrite");

			child.in = open(rebase_path_rewritten_list(), O_RDONLY);
			child.git_cmd = 1;
			argv_array_push(&child.args, "notes");
			argv_array_push(&child.args, "copy");
			argv_array_push(&child.args, "--for-rewrite=rebase");
			/* we don't care if this copying failed */
			run_command(&child);

			if (post_rewrite_hook) {
				struct child_process hook = CHILD_PROCESS_INIT;

				hook.in = open(rebase_path_rewritten_list(),
					O_RDONLY);
				hook.stdout_to_stderr = 1;
				argv_array_push(&hook.args, post_rewrite_hook);
				argv_array_push(&hook.args, "rebase");
				/* we don't care if this hook failed */
				run_command(&hook);
			}
		}
		apply_autostash(opts);

		fprintf(stderr, "Successfully rebased and updated %s.\n",
			head_ref.buf);

		strbuf_release(&buf);
		strbuf_release(&head_ref);
	}

	/*
	 * Sequence of picks finished successfully; cleanup by
	 * removing the .git/sequencer directory
	 */
	return sequencer_remove_state(opts);
}

static int continue_single_pick(void)
{
	const char *argv[] = { "commit", NULL };

	if (!file_exists(git_path_cherry_pick_head()) &&
	    !file_exists(git_path_revert_head()))
		return error(_("no cherry-pick or revert in progress"));
	return run_command_v_opt(argv, RUN_GIT_CMD);
}

static int commit_staged_changes(struct replay_opts *opts)
{
	unsigned int flags = ALLOW_EMPTY | EDIT_MSG;

	if (has_unstaged_changes(1))
		return error(_("cannot rebase: You have unstaged changes."));
	if (!has_uncommitted_changes(0)) {
		const char *cherry_pick_head = git_path("CHERRY_PICK_HEAD");

		if (file_exists(cherry_pick_head) && unlink(cherry_pick_head))
			return error(_("could not remove CHERRY_PICK_HEAD"));
		return 0;
	}

	if (file_exists(rebase_path_amend())) {
		struct strbuf rev = STRBUF_INIT;
		unsigned char head[20], to_amend[20];

		if (get_sha1("HEAD", head))
			return error(_("cannot amend non-existing commit"));
		if (!read_oneliner(&rev, rebase_path_amend(), 0))
			return error(_("invalid file: '%s'"), rebase_path_amend());
		if (get_sha1_hex(rev.buf, to_amend))
			return error(_("invalid contents: '%s'"),
				rebase_path_amend());
		if (hashcmp(head, to_amend))
			return error(_("\nYou have uncommitted changes in your "
				       "working tree. Please, commit them\n"
				       "first and then run 'git rebase "
				       "--continue' again."));

		strbuf_release(&rev);
		flags |= AMEND_MSG;
	}

	if (run_git_commit(rebase_path_message(), opts, flags))
		return error(_("could not commit staged changes."));
	unlink(rebase_path_amend());
	return 0;
}

int sequencer_continue(struct replay_opts *opts)
{
	struct todo_list todo_list = TODO_LIST_INIT;
	int res;

	if (read_and_refresh_cache(opts))
		return -1;

	if (is_rebase_i(opts)) {
		if (commit_staged_changes(opts))
			return -1;
	} else if (!file_exists(get_todo_path(opts)))
		return continue_single_pick();
	if (read_populate_opts(opts))
		return -1;
	if ((res = read_populate_todo(&todo_list, opts)))
		goto release_todo_list;

	if (!is_rebase_i(opts)) {
		/* Verify that the conflict has been resolved */
		if (file_exists(git_path_cherry_pick_head()) ||
		    file_exists(git_path_revert_head())) {
			res = continue_single_pick();
			if (res)
				goto release_todo_list;
		}
		if (index_differs_from("HEAD", 0, 0)) {
			res = error_dirty_index(opts);
			goto release_todo_list;
		}
		todo_list.current++;
	} else if (file_exists(rebase_path_stopped_sha())) {
		struct strbuf buf = STRBUF_INIT;
		struct object_id oid;

		if (read_oneliner(&buf, rebase_path_stopped_sha(), 1) &&
		    !get_sha1_committish(buf.buf, oid.hash))
			record_in_rewritten(&oid, peek_command(&todo_list, 0));
		strbuf_release(&buf);
	}

	res = pick_commits(&todo_list, opts);
release_todo_list:
	todo_list_release(&todo_list);
	return res;
}

static int single_pick(struct commit *cmit, struct replay_opts *opts)
{
	setenv(GIT_REFLOG_ACTION, action_name(opts), 0);
	return do_pick_commit(opts->action == REPLAY_PICK ?
		TODO_PICK : TODO_REVERT, cmit, opts, 0);
}

int sequencer_pick_revisions(struct replay_opts *opts)
{
	struct todo_list todo_list = TODO_LIST_INIT;
	unsigned char sha1[20];
	int i, res;

	assert(opts->revs);
	if (read_and_refresh_cache(opts))
		return -1;

	for (i = 0; i < opts->revs->pending.nr; i++) {
		unsigned char sha1[20];
		const char *name = opts->revs->pending.objects[i].name;

		/* This happens when using --stdin. */
		if (!strlen(name))
			continue;

		if (!get_sha1(name, sha1)) {
			if (!lookup_commit_reference_gently(sha1, 1)) {
				enum object_type type = sha1_object_info(sha1, NULL);
				return error(_("%s: can't cherry-pick a %s"),
					name, typename(type));
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
		if (!cmit || get_revision(opts->revs))
			return error("BUG: expected exactly one commit from walk");
		return single_pick(cmit, opts);
	}

	/*
	 * Start a new cherry-pick/ revert sequence; but
	 * first, make sure that an existing one isn't in
	 * progress
	 */

	if (walk_revs_populate_todo(&todo_list, opts) ||
			create_seq_dir() < 0)
		return -1;
	if (get_sha1("HEAD", sha1) && (opts->action == REPLAY_REVERT))
		return error(_("can't revert as initial commit"));
	if (save_head(sha1_to_hex(sha1)))
		return -1;
	if (save_opts(opts))
		return -1;
	update_abort_safety_file();
	res = pick_commits(&todo_list, opts);
	todo_list_release(&todo_list);
	return res;
}

void append_signoff(struct strbuf *msgbuf, int ignore_footer, unsigned flag)
{
	unsigned no_dup_sob = flag & APPEND_SIGNOFF_DEDUP;
	struct strbuf sob = STRBUF_INIT;
	int has_footer;

	strbuf_addstr(&sob, sign_off_header);
	strbuf_addstr(&sob, fmt_name(getenv("GIT_COMMITTER_NAME"),
				getenv("GIT_COMMITTER_EMAIL")));
	strbuf_addch(&sob, '\n');

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
		} else if (msgbuf->buf[len - 1] != '\n') {
			/*
			 * Incomplete line.  Complete the line and add a
			 * blank one so that there is an empty line between
			 * the message body and the sob.
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
