/*
 * "git rebase" builtin command
 *
 * Copyright (c) 2018 Pratik Karki
 */

#include "builtin.h"
#include "run-command.h"
#include "exec-cmd.h"
#include "argv-array.h"
#include "dir.h"
#include "packfile.h"
#include "refs.h"
#include "quote.h"
#include "config.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "lockfile.h"
#include "parse-options.h"
#include "commit.h"
#include "diff.h"
#include "wt-status.h"
#include "revision.h"
#include "commit-reach.h"
#include "rerere.h"
#include "branch.h"

static char const * const builtin_rebase_usage[] = {
	N_("git rebase [-i] [options] [--exec <cmd>] [--onto <newbase>] "
		"[<upstream>] [<branch>]"),
	N_("git rebase [-i] [options] [--exec <cmd>] [--onto <newbase>] "
		"--root [<branch>]"),
	N_("git rebase --continue | --abort | --skip | --edit-todo"),
	NULL
};

static GIT_PATH_FUNC(apply_dir, "rebase-apply")
static GIT_PATH_FUNC(merge_dir, "rebase-merge")

enum rebase_type {
	REBASE_UNSPECIFIED = -1,
	REBASE_AM,
	REBASE_MERGE,
	REBASE_INTERACTIVE,
	REBASE_PRESERVE_MERGES
};

static int use_builtin_rebase(void)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT;
	int ret, env = git_env_bool("GIT_TEST_REBASE_USE_BUILTIN", -1);

	if (env != -1)
		return env;

	argv_array_pushl(&cp.args,
			 "config", "--bool", "rebase.usebuiltin", NULL);
	cp.git_cmd = 1;
	if (capture_command(&cp, &out, 6)) {
		strbuf_release(&out);
		return 1;
	}

	strbuf_trim(&out);
	ret = !strcmp("true", out.buf);
	strbuf_release(&out);
	return ret;
}

struct rebase_options {
	enum rebase_type type;
	const char *state_dir;
	struct commit *upstream;
	const char *upstream_name;
	const char *upstream_arg;
	char *head_name;
	struct object_id orig_head;
	struct commit *onto;
	const char *onto_name;
	const char *revisions;
	const char *switch_to;
	int root;
	struct object_id *squash_onto;
	struct commit *restrict_revision;
	int dont_finish_rebase;
	enum {
		REBASE_NO_QUIET = 1<<0,
		REBASE_VERBOSE = 1<<1,
		REBASE_DIFFSTAT = 1<<2,
		REBASE_FORCE = 1<<3,
		REBASE_INTERACTIVE_EXPLICIT = 1<<4,
	} flags;
	struct argv_array git_am_opts;
	const char *action;
	int signoff;
	int allow_rerere_autoupdate;
	int keep_empty;
	int autosquash;
	char *gpg_sign_opt;
	int autostash;
	char *cmd;
	int allow_empty_message;
	int rebase_merges, rebase_cousins;
	char *strategy, *strategy_opts;
	struct strbuf git_format_patch_opt;
};

static int is_interactive(struct rebase_options *opts)
{
	return opts->type == REBASE_INTERACTIVE ||
		opts->type == REBASE_PRESERVE_MERGES;
}

static void imply_interactive(struct rebase_options *opts, const char *option)
{
	switch (opts->type) {
	case REBASE_AM:
		die(_("%s requires an interactive rebase"), option);
		break;
	case REBASE_INTERACTIVE:
	case REBASE_PRESERVE_MERGES:
		break;
	case REBASE_MERGE:
		/* we silently *upgrade* --merge to --interactive if needed */
	default:
		opts->type = REBASE_INTERACTIVE; /* implied */
		break;
	}
}

/* Returns the filename prefixed by the state_dir */
static const char *state_dir_path(const char *filename, struct rebase_options *opts)
{
	static struct strbuf path = STRBUF_INIT;
	static size_t prefix_len;

	if (!prefix_len) {
		strbuf_addf(&path, "%s/", opts->state_dir);
		prefix_len = path.len;
	}

	strbuf_setlen(&path, prefix_len);
	strbuf_addstr(&path, filename);
	return path.buf;
}

/* Read one file, then strip line endings */
static int read_one(const char *path, struct strbuf *buf)
{
	if (strbuf_read_file(buf, path, 0) < 0)
		return error_errno(_("could not read '%s'"), path);
	strbuf_trim_trailing_newline(buf);
	return 0;
}

/* Initialize the rebase options from the state directory. */
static int read_basic_state(struct rebase_options *opts)
{
	struct strbuf head_name = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;
	struct object_id oid;

	if (read_one(state_dir_path("head-name", opts), &head_name) ||
	    read_one(state_dir_path("onto", opts), &buf))
		return -1;
	opts->head_name = starts_with(head_name.buf, "refs/") ?
		xstrdup(head_name.buf) : NULL;
	strbuf_release(&head_name);
	if (get_oid(buf.buf, &oid))
		return error(_("could not get 'onto': '%s'"), buf.buf);
	opts->onto = lookup_commit_or_die(&oid, buf.buf);

	/*
	 * We always write to orig-head, but interactive rebase used to write to
	 * head. Fall back to reading from head to cover for the case that the
	 * user upgraded git with an ongoing interactive rebase.
	 */
	strbuf_reset(&buf);
	if (file_exists(state_dir_path("orig-head", opts))) {
		if (read_one(state_dir_path("orig-head", opts), &buf))
			return -1;
	} else if (read_one(state_dir_path("head", opts), &buf))
		return -1;
	if (get_oid(buf.buf, &opts->orig_head))
		return error(_("invalid orig-head: '%s'"), buf.buf);

	strbuf_reset(&buf);
	if (read_one(state_dir_path("quiet", opts), &buf))
		return -1;
	if (buf.len)
		opts->flags &= ~REBASE_NO_QUIET;
	else
		opts->flags |= REBASE_NO_QUIET;

	if (file_exists(state_dir_path("verbose", opts)))
		opts->flags |= REBASE_VERBOSE;

	if (file_exists(state_dir_path("signoff", opts))) {
		opts->signoff = 1;
		opts->flags |= REBASE_FORCE;
	}

	if (file_exists(state_dir_path("allow_rerere_autoupdate", opts))) {
		strbuf_reset(&buf);
		if (read_one(state_dir_path("allow_rerere_autoupdate", opts),
			    &buf))
			return -1;
		if (!strcmp(buf.buf, "--rerere-autoupdate"))
			opts->allow_rerere_autoupdate = 1;
		else if (!strcmp(buf.buf, "--no-rerere-autoupdate"))
			opts->allow_rerere_autoupdate = 0;
		else
			warning(_("ignoring invalid allow_rerere_autoupdate: "
				  "'%s'"), buf.buf);
	} else
		opts->allow_rerere_autoupdate = -1;

	if (file_exists(state_dir_path("gpg_sign_opt", opts))) {
		strbuf_reset(&buf);
		if (read_one(state_dir_path("gpg_sign_opt", opts),
			    &buf))
			return -1;
		free(opts->gpg_sign_opt);
		opts->gpg_sign_opt = xstrdup(buf.buf);
	}

	if (file_exists(state_dir_path("strategy", opts))) {
		strbuf_reset(&buf);
		if (read_one(state_dir_path("strategy", opts), &buf))
			return -1;
		free(opts->strategy);
		opts->strategy = xstrdup(buf.buf);
	}

	if (file_exists(state_dir_path("strategy_opts", opts))) {
		strbuf_reset(&buf);
		if (read_one(state_dir_path("strategy_opts", opts), &buf))
			return -1;
		free(opts->strategy_opts);
		opts->strategy_opts = xstrdup(buf.buf);
	}

	strbuf_release(&buf);

	return 0;
}

static int apply_autostash(struct rebase_options *opts)
{
	const char *path = state_dir_path("autostash", opts);
	struct strbuf autostash = STRBUF_INIT;
	struct child_process stash_apply = CHILD_PROCESS_INIT;

	if (!file_exists(path))
		return 0;

	if (read_one(path, &autostash))
		return error(_("Could not read '%s'"), path);
	/* Ensure that the hash is not mistaken for a number */
	strbuf_addstr(&autostash, "^0");
	argv_array_pushl(&stash_apply.args,
			 "stash", "apply", autostash.buf, NULL);
	stash_apply.git_cmd = 1;
	stash_apply.no_stderr = stash_apply.no_stdout =
		stash_apply.no_stdin = 1;
	if (!run_command(&stash_apply))
		printf(_("Applied autostash.\n"));
	else {
		struct argv_array args = ARGV_ARRAY_INIT;
		int res = 0;

		argv_array_pushl(&args,
				 "stash", "store", "-m", "autostash", "-q",
				 autostash.buf, NULL);
		if (run_command_v_opt(args.argv, RUN_GIT_CMD))
			res = error(_("Cannot store %s"), autostash.buf);
		argv_array_clear(&args);
		strbuf_release(&autostash);
		if (res)
			return res;

		fprintf(stderr,
			_("Applying autostash resulted in conflicts.\n"
			  "Your changes are safe in the stash.\n"
			  "You can run \"git stash pop\" or \"git stash drop\" "
			  "at any time.\n"));
	}

	strbuf_release(&autostash);
	return 0;
}

static int finish_rebase(struct rebase_options *opts)
{
	struct strbuf dir = STRBUF_INIT;
	const char *argv_gc_auto[] = { "gc", "--auto", NULL };

	delete_ref(NULL, "REBASE_HEAD", NULL, REF_NO_DEREF);
	apply_autostash(opts);
	close_all_packs(the_repository->objects);
	/*
	 * We ignore errors in 'gc --auto', since the
	 * user should see them.
	 */
	run_command_v_opt(argv_gc_auto, RUN_GIT_CMD);
	strbuf_addstr(&dir, opts->state_dir);
	remove_dir_recursively(&dir, 0);
	strbuf_release(&dir);

	return 0;
}

static struct commit *peel_committish(const char *name)
{
	struct object *obj;
	struct object_id oid;

	if (get_oid(name, &oid))
		return NULL;
	obj = parse_object(the_repository, &oid);
	return (struct commit *)peel_to_type(name, 0, obj, OBJ_COMMIT);
}

static void add_var(struct strbuf *buf, const char *name, const char *value)
{
	if (!value)
		strbuf_addf(buf, "unset %s; ", name);
	else {
		strbuf_addf(buf, "%s=", name);
		sq_quote_buf(buf, value);
		strbuf_addstr(buf, "; ");
	}
}

static const char *resolvemsg =
N_("Resolve all conflicts manually, mark them as resolved with\n"
"\"git add/rm <conflicted_files>\", then run \"git rebase --continue\".\n"
"You can instead skip this commit: run \"git rebase --skip\".\n"
"To abort and get back to the state before \"git rebase\", run "
"\"git rebase --abort\".");

static int run_specific_rebase(struct rebase_options *opts)
{
	const char *argv[] = { NULL, NULL };
	struct strbuf script_snippet = STRBUF_INIT, buf = STRBUF_INIT;
	int status;
	const char *backend, *backend_func;

	if (opts->type == REBASE_INTERACTIVE) {
		/* Run builtin interactive rebase */
		struct child_process child = CHILD_PROCESS_INIT;

		argv_array_pushf(&child.env_array, "GIT_CHERRY_PICK_HELP=%s",
				 resolvemsg);
		if (!(opts->flags & REBASE_INTERACTIVE_EXPLICIT)) {
			argv_array_push(&child.env_array, "GIT_EDITOR=:");
			opts->autosquash = 0;
		}

		child.git_cmd = 1;
		argv_array_push(&child.args, "rebase--interactive");

		if (opts->action)
			argv_array_pushf(&child.args, "--%s", opts->action);
		if (opts->keep_empty)
			argv_array_push(&child.args, "--keep-empty");
		if (opts->rebase_merges)
			argv_array_push(&child.args, "--rebase-merges");
		if (opts->rebase_cousins)
			argv_array_push(&child.args, "--rebase-cousins");
		if (opts->autosquash)
			argv_array_push(&child.args, "--autosquash");
		if (opts->flags & REBASE_VERBOSE)
			argv_array_push(&child.args, "--verbose");
		if (opts->flags & REBASE_FORCE)
			argv_array_push(&child.args, "--no-ff");
		if (opts->restrict_revision)
			argv_array_pushf(&child.args,
					 "--restrict-revision=^%s",
					 oid_to_hex(&opts->restrict_revision->object.oid));
		if (opts->upstream)
			argv_array_pushf(&child.args, "--upstream=%s",
					 oid_to_hex(&opts->upstream->object.oid));
		if (opts->onto)
			argv_array_pushf(&child.args, "--onto=%s",
					 oid_to_hex(&opts->onto->object.oid));
		if (opts->squash_onto)
			argv_array_pushf(&child.args, "--squash-onto=%s",
					 oid_to_hex(opts->squash_onto));
		if (opts->onto_name)
			argv_array_pushf(&child.args, "--onto-name=%s",
					 opts->onto_name);
		argv_array_pushf(&child.args, "--head-name=%s",
				 opts->head_name ?
				 opts->head_name : "detached HEAD");
		if (opts->strategy)
			argv_array_pushf(&child.args, "--strategy=%s",
					 opts->strategy);
		if (opts->strategy_opts)
			argv_array_pushf(&child.args, "--strategy-opts=%s",
					 opts->strategy_opts);
		if (opts->switch_to)
			argv_array_pushf(&child.args, "--switch-to=%s",
					 opts->switch_to);
		if (opts->cmd)
			argv_array_pushf(&child.args, "--cmd=%s", opts->cmd);
		if (opts->allow_empty_message)
			argv_array_push(&child.args, "--allow-empty-message");
		if (opts->allow_rerere_autoupdate > 0)
			argv_array_push(&child.args, "--rerere-autoupdate");
		else if (opts->allow_rerere_autoupdate == 0)
			argv_array_push(&child.args, "--no-rerere-autoupdate");
		if (opts->gpg_sign_opt)
			argv_array_push(&child.args, opts->gpg_sign_opt);
		if (opts->signoff)
			argv_array_push(&child.args, "--signoff");

		status = run_command(&child);
		goto finished_rebase;
	}

	add_var(&script_snippet, "GIT_DIR", absolute_path(get_git_dir()));
	add_var(&script_snippet, "state_dir", opts->state_dir);

	add_var(&script_snippet, "upstream_name", opts->upstream_name);
	add_var(&script_snippet, "upstream", opts->upstream ?
		oid_to_hex(&opts->upstream->object.oid) : NULL);
	add_var(&script_snippet, "head_name",
		opts->head_name ? opts->head_name : "detached HEAD");
	add_var(&script_snippet, "orig_head", oid_to_hex(&opts->orig_head));
	add_var(&script_snippet, "onto", opts->onto ?
		oid_to_hex(&opts->onto->object.oid) : NULL);
	add_var(&script_snippet, "onto_name", opts->onto_name);
	add_var(&script_snippet, "revisions", opts->revisions);
	add_var(&script_snippet, "restrict_revision", opts->restrict_revision ?
		oid_to_hex(&opts->restrict_revision->object.oid) : NULL);
	add_var(&script_snippet, "GIT_QUIET",
		opts->flags & REBASE_NO_QUIET ? "" : "t");
	sq_quote_argv_pretty(&buf, opts->git_am_opts.argv);
	add_var(&script_snippet, "git_am_opt", buf.buf);
	strbuf_release(&buf);
	add_var(&script_snippet, "verbose",
		opts->flags & REBASE_VERBOSE ? "t" : "");
	add_var(&script_snippet, "diffstat",
		opts->flags & REBASE_DIFFSTAT ? "t" : "");
	add_var(&script_snippet, "force_rebase",
		opts->flags & REBASE_FORCE ? "t" : "");
	if (opts->switch_to)
		add_var(&script_snippet, "switch_to", opts->switch_to);
	add_var(&script_snippet, "action", opts->action ? opts->action : "");
	add_var(&script_snippet, "signoff", opts->signoff ? "--signoff" : "");
	add_var(&script_snippet, "allow_rerere_autoupdate",
		opts->allow_rerere_autoupdate < 0 ? "" :
		opts->allow_rerere_autoupdate ?
		"--rerere-autoupdate" : "--no-rerere-autoupdate");
	add_var(&script_snippet, "keep_empty", opts->keep_empty ? "yes" : "");
	add_var(&script_snippet, "autosquash", opts->autosquash ? "t" : "");
	add_var(&script_snippet, "gpg_sign_opt", opts->gpg_sign_opt);
	add_var(&script_snippet, "cmd", opts->cmd);
	add_var(&script_snippet, "allow_empty_message",
		opts->allow_empty_message ?  "--allow-empty-message" : "");
	add_var(&script_snippet, "rebase_merges",
		opts->rebase_merges ? "t" : "");
	add_var(&script_snippet, "rebase_cousins",
		opts->rebase_cousins ? "t" : "");
	add_var(&script_snippet, "strategy", opts->strategy);
	add_var(&script_snippet, "strategy_opts", opts->strategy_opts);
	add_var(&script_snippet, "rebase_root", opts->root ? "t" : "");
	add_var(&script_snippet, "squash_onto",
		opts->squash_onto ? oid_to_hex(opts->squash_onto) : "");
	add_var(&script_snippet, "git_format_patch_opt",
		opts->git_format_patch_opt.buf);

	if (is_interactive(opts) &&
	    !(opts->flags & REBASE_INTERACTIVE_EXPLICIT)) {
		strbuf_addstr(&script_snippet,
			      "GIT_EDITOR=:; export GIT_EDITOR; ");
		opts->autosquash = 0;
	}

	switch (opts->type) {
	case REBASE_AM:
		backend = "git-rebase--am";
		backend_func = "git_rebase__am";
		break;
	case REBASE_MERGE:
		backend = "git-rebase--merge";
		backend_func = "git_rebase__merge";
		break;
	case REBASE_PRESERVE_MERGES:
		backend = "git-rebase--preserve-merges";
		backend_func = "git_rebase__preserve_merges";
		break;
	default:
		BUG("Unhandled rebase type %d", opts->type);
		break;
	}

	strbuf_addf(&script_snippet,
		    ". git-sh-setup && . git-rebase--common &&"
		    " . %s && %s", backend, backend_func);
	argv[0] = script_snippet.buf;

	status = run_command_v_opt(argv, RUN_USING_SHELL);
finished_rebase:
	if (opts->dont_finish_rebase)
		; /* do nothing */
	else if (opts->type == REBASE_INTERACTIVE)
		; /* interactive rebase cleans up after itself */
	else if (status == 0) {
		if (!file_exists(state_dir_path("stopped-sha", opts)))
			finish_rebase(opts);
	} else if (status == 2) {
		struct strbuf dir = STRBUF_INIT;

		apply_autostash(opts);
		strbuf_addstr(&dir, opts->state_dir);
		remove_dir_recursively(&dir, 0);
		strbuf_release(&dir);
		die("Nothing to do");
	}

	strbuf_release(&script_snippet);

	return status ? -1 : 0;
}

#define GIT_REFLOG_ACTION_ENVIRONMENT "GIT_REFLOG_ACTION"

#define RESET_HEAD_DETACH (1<<0)
#define RESET_HEAD_HARD (1<<1)

static int reset_head(struct object_id *oid, const char *action,
		      const char *switch_to_branch, unsigned flags,
		      const char *reflog_orig_head, const char *reflog_head)
{
	unsigned detach_head = flags & RESET_HEAD_DETACH;
	unsigned reset_hard = flags & RESET_HEAD_HARD;
	struct object_id head_oid;
	struct tree_desc desc[2] = { { NULL }, { NULL } };
	struct lock_file lock = LOCK_INIT;
	struct unpack_trees_options unpack_tree_opts;
	struct tree *tree;
	const char *reflog_action;
	struct strbuf msg = STRBUF_INIT;
	size_t prefix_len;
	struct object_id *orig = NULL, oid_orig,
		*old_orig = NULL, oid_old_orig;
	int ret = 0, nr = 0;

	if (switch_to_branch && !starts_with(switch_to_branch, "refs/"))
		BUG("Not a fully qualified branch: '%s'", switch_to_branch);

	if (hold_locked_index(&lock, LOCK_REPORT_ON_ERROR) < 0) {
		ret = -1;
		goto leave_reset_head;
	}

	if ((!oid || !reset_hard) && get_oid("HEAD", &head_oid)) {
		ret = error(_("could not determine HEAD revision"));
		goto leave_reset_head;
	}

	if (!oid)
		oid = &head_oid;

	memset(&unpack_tree_opts, 0, sizeof(unpack_tree_opts));
	setup_unpack_trees_porcelain(&unpack_tree_opts, action);
	unpack_tree_opts.head_idx = 1;
	unpack_tree_opts.src_index = the_repository->index;
	unpack_tree_opts.dst_index = the_repository->index;
	unpack_tree_opts.fn = reset_hard ? oneway_merge : twoway_merge;
	unpack_tree_opts.update = 1;
	unpack_tree_opts.merge = 1;
	if (!detach_head)
		unpack_tree_opts.reset = 1;

	if (read_index_unmerged(the_repository->index) < 0) {
		ret = error(_("could not read index"));
		goto leave_reset_head;
	}

	if (!reset_hard && !fill_tree_descriptor(&desc[nr++], &head_oid)) {
		ret = error(_("failed to find tree of %s"),
			    oid_to_hex(&head_oid));
		goto leave_reset_head;
	}

	if (!fill_tree_descriptor(&desc[nr++], oid)) {
		ret = error(_("failed to find tree of %s"), oid_to_hex(oid));
		goto leave_reset_head;
	}

	if (unpack_trees(nr, desc, &unpack_tree_opts)) {
		ret = -1;
		goto leave_reset_head;
	}

	tree = parse_tree_indirect(oid);
	prime_cache_tree(the_repository, the_repository->index, tree);

	if (write_locked_index(the_repository->index, &lock, COMMIT_LOCK) < 0) {
		ret = error(_("could not write index"));
		goto leave_reset_head;
	}

	reflog_action = getenv(GIT_REFLOG_ACTION_ENVIRONMENT);
	strbuf_addf(&msg, "%s: ", reflog_action ? reflog_action : "rebase");
	prefix_len = msg.len;

	if (!get_oid("ORIG_HEAD", &oid_old_orig))
		old_orig = &oid_old_orig;
	if (!get_oid("HEAD", &oid_orig)) {
		orig = &oid_orig;
		if (!reflog_orig_head) {
			strbuf_addstr(&msg, "updating ORIG_HEAD");
			reflog_orig_head = msg.buf;
		}
		update_ref(reflog_orig_head, "ORIG_HEAD", orig, old_orig, 0,
			   UPDATE_REFS_MSG_ON_ERR);
	} else if (old_orig)
		delete_ref(NULL, "ORIG_HEAD", old_orig, 0);
	if (!reflog_head) {
		strbuf_setlen(&msg, prefix_len);
		strbuf_addstr(&msg, "updating HEAD");
		reflog_head = msg.buf;
	}
	if (!switch_to_branch)
		ret = update_ref(reflog_head, "HEAD", oid, orig,
				 detach_head ? REF_NO_DEREF : 0,
				 UPDATE_REFS_MSG_ON_ERR);
	else {
		ret = create_symref("HEAD", switch_to_branch, msg.buf);
		if (!ret)
			ret = update_ref(reflog_head, "HEAD", oid, NULL, 0,
					 UPDATE_REFS_MSG_ON_ERR);
	}

leave_reset_head:
	strbuf_release(&msg);
	rollback_lock_file(&lock);
	while (nr)
		free((void *)desc[--nr].buffer);
	return ret;
}

static int rebase_config(const char *var, const char *value, void *data)
{
	struct rebase_options *opts = data;

	if (!strcmp(var, "rebase.stat")) {
		if (git_config_bool(var, value))
			opts->flags |= REBASE_DIFFSTAT;
		else
			opts->flags &= !REBASE_DIFFSTAT;
		return 0;
	}

	if (!strcmp(var, "rebase.autosquash")) {
		opts->autosquash = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "commit.gpgsign")) {
		free(opts->gpg_sign_opt);
		opts->gpg_sign_opt = git_config_bool(var, value) ?
			xstrdup("-S") : NULL;
		return 0;
	}

	if (!strcmp(var, "rebase.autostash")) {
		opts->autostash = git_config_bool(var, value);
		return 0;
	}

	return git_default_config(var, value, data);
}

/*
 * Determines whether the commits in from..to are linear, i.e. contain
 * no merge commits. This function *expects* `from` to be an ancestor of
 * `to`.
 */
static int is_linear_history(struct commit *from, struct commit *to)
{
	while (to && to != from) {
		parse_commit(to);
		if (!to->parents)
			return 1;
		if (to->parents->next)
			return 0;
		to = to->parents->item;
	}
	return 1;
}

static int can_fast_forward(struct commit *onto, struct object_id *head_oid,
			    struct object_id *merge_base)
{
	struct commit *head = lookup_commit(the_repository, head_oid);
	struct commit_list *merge_bases;
	int res;

	if (!head)
		return 0;

	merge_bases = get_merge_bases(onto, head);
	if (merge_bases && !merge_bases->next) {
		oidcpy(merge_base, &merge_bases->item->object.oid);
		res = oideq(merge_base, &onto->object.oid);
	} else {
		oidcpy(merge_base, &null_oid);
		res = 0;
	}
	free_commit_list(merge_bases);
	return res && is_linear_history(onto, head);
}

/* -i followed by -m is still -i */
static int parse_opt_merge(const struct option *opt, const char *arg, int unset)
{
	struct rebase_options *opts = opt->value;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);

	if (!is_interactive(opts))
		opts->type = REBASE_MERGE;

	return 0;
}

/* -i followed by -p is still explicitly interactive, but -p alone is not */
static int parse_opt_interactive(const struct option *opt, const char *arg,
				 int unset)
{
	struct rebase_options *opts = opt->value;

	BUG_ON_OPT_NEG(unset);
	BUG_ON_OPT_ARG(arg);

	opts->type = REBASE_INTERACTIVE;
	opts->flags |= REBASE_INTERACTIVE_EXPLICIT;

	return 0;
}

static void NORETURN error_on_missing_default_upstream(void)
{
	struct branch *current_branch = branch_get(NULL);

	printf(_("%s\n"
		 "Please specify which branch you want to rebase against.\n"
		 "See git-rebase(1) for details.\n"
		 "\n"
		 "    git rebase '<branch>'\n"
		 "\n"),
		current_branch ? _("There is no tracking information for "
			"the current branch.") :
			_("You are not currently on a branch."));

	if (current_branch) {
		const char *remote = current_branch->remote_name;

		if (!remote)
			remote = _("<remote>");

		printf(_("If you wish to set tracking information for this "
			 "branch you can do so with:\n"
			 "\n"
			 "    git branch --set-upstream-to=%s/<branch> %s\n"
			 "\n"),
		       remote, current_branch->name);
	}
	exit(1);
}

static void set_reflog_action(struct rebase_options *options)
{
	const char *env;
	struct strbuf buf = STRBUF_INIT;

	if (!is_interactive(options))
		return;

	env = getenv(GIT_REFLOG_ACTION_ENVIRONMENT);
	if (env && strcmp("rebase", env))
		return; /* only override it if it is "rebase" */

	strbuf_addf(&buf, "rebase -i (%s)", options->action);
	setenv(GIT_REFLOG_ACTION_ENVIRONMENT, buf.buf, 1);
	strbuf_release(&buf);
}

int cmd_rebase(int argc, const char **argv, const char *prefix)
{
	struct rebase_options options = {
		.type = REBASE_UNSPECIFIED,
		.flags = REBASE_NO_QUIET,
		.git_am_opts = ARGV_ARRAY_INIT,
		.allow_rerere_autoupdate  = -1,
		.allow_empty_message = 1,
		.git_format_patch_opt = STRBUF_INIT,
	};
	const char *branch_name;
	int ret, flags, total_argc, in_progress = 0;
	int ok_to_skip_pre_rebase = 0;
	struct strbuf msg = STRBUF_INIT;
	struct strbuf revisions = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;
	struct object_id merge_base;
	enum {
		NO_ACTION,
		ACTION_CONTINUE,
		ACTION_SKIP,
		ACTION_ABORT,
		ACTION_QUIT,
		ACTION_EDIT_TODO,
		ACTION_SHOW_CURRENT_PATCH,
	} action = NO_ACTION;
	const char *gpg_sign = NULL;
	struct string_list exec = STRING_LIST_INIT_NODUP;
	const char *rebase_merges = NULL;
	int fork_point = -1;
	struct string_list strategy_options = STRING_LIST_INIT_NODUP;
	struct object_id squash_onto;
	char *squash_onto_name = NULL;
	struct option builtin_rebase_options[] = {
		OPT_STRING(0, "onto", &options.onto_name,
			   N_("revision"),
			   N_("rebase onto given branch instead of upstream")),
		OPT_BOOL(0, "no-verify", &ok_to_skip_pre_rebase,
			 N_("allow pre-rebase hook to run")),
		OPT_NEGBIT('q', "quiet", &options.flags,
			   N_("be quiet. implies --no-stat"),
			   REBASE_NO_QUIET| REBASE_VERBOSE | REBASE_DIFFSTAT),
		OPT_BIT('v', "verbose", &options.flags,
			N_("display a diffstat of what changed upstream"),
			REBASE_NO_QUIET | REBASE_VERBOSE | REBASE_DIFFSTAT),
		{OPTION_NEGBIT, 'n', "no-stat", &options.flags, NULL,
			N_("do not show diffstat of what changed upstream"),
			PARSE_OPT_NOARG, NULL, REBASE_DIFFSTAT },
		OPT_BOOL(0, "signoff", &options.signoff,
			 N_("add a Signed-off-by: line to each commit")),
		OPT_PASSTHRU_ARGV(0, "ignore-whitespace", &options.git_am_opts,
				  NULL, N_("passed to 'git am'"),
				  PARSE_OPT_NOARG),
		OPT_PASSTHRU_ARGV(0, "committer-date-is-author-date",
				  &options.git_am_opts, NULL,
				  N_("passed to 'git am'"), PARSE_OPT_NOARG),
		OPT_PASSTHRU_ARGV(0, "ignore-date", &options.git_am_opts, NULL,
				  N_("passed to 'git am'"), PARSE_OPT_NOARG),
		OPT_PASSTHRU_ARGV('C', NULL, &options.git_am_opts, N_("n"),
				  N_("passed to 'git apply'"), 0),
		OPT_PASSTHRU_ARGV(0, "whitespace", &options.git_am_opts,
				  N_("action"), N_("passed to 'git apply'"), 0),
		OPT_BIT('f', "force-rebase", &options.flags,
			N_("cherry-pick all commits, even if unchanged"),
			REBASE_FORCE),
		OPT_BIT(0, "no-ff", &options.flags,
			N_("cherry-pick all commits, even if unchanged"),
			REBASE_FORCE),
		OPT_CMDMODE(0, "continue", &action, N_("continue"),
			    ACTION_CONTINUE),
		OPT_CMDMODE(0, "skip", &action,
			    N_("skip current patch and continue"), ACTION_SKIP),
		OPT_CMDMODE(0, "abort", &action,
			    N_("abort and check out the original branch"),
			    ACTION_ABORT),
		OPT_CMDMODE(0, "quit", &action,
			    N_("abort but keep HEAD where it is"), ACTION_QUIT),
		OPT_CMDMODE(0, "edit-todo", &action, N_("edit the todo list "
			    "during an interactive rebase"), ACTION_EDIT_TODO),
		OPT_CMDMODE(0, "show-current-patch", &action,
			    N_("show the patch file being applied or merged"),
			    ACTION_SHOW_CURRENT_PATCH),
		{ OPTION_CALLBACK, 'm', "merge", &options, NULL,
			N_("use merging strategies to rebase"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			parse_opt_merge },
		{ OPTION_CALLBACK, 'i', "interactive", &options, NULL,
			N_("let the user edit the list of commits to rebase"),
			PARSE_OPT_NOARG | PARSE_OPT_NONEG,
			parse_opt_interactive },
		OPT_SET_INT('p', "preserve-merges", &options.type,
			    N_("try to recreate merges instead of ignoring "
			       "them"), REBASE_PRESERVE_MERGES),
		OPT_BOOL(0, "rerere-autoupdate",
			 &options.allow_rerere_autoupdate,
			 N_("allow rerere to update index with resolved "
			    "conflict")),
		OPT_BOOL('k', "keep-empty", &options.keep_empty,
			 N_("preserve empty commits during rebase")),
		OPT_BOOL(0, "autosquash", &options.autosquash,
			 N_("move commits that begin with "
			    "squash!/fixup! under -i")),
		{ OPTION_STRING, 'S', "gpg-sign", &gpg_sign, N_("key-id"),
			N_("GPG-sign commits"),
			PARSE_OPT_OPTARG, NULL, (intptr_t) "" },
		OPT_BOOL(0, "autostash", &options.autostash,
			 N_("automatically stash/stash pop before and after")),
		OPT_STRING_LIST('x', "exec", &exec, N_("exec"),
				N_("add exec lines after each commit of the "
				   "editable list")),
		OPT_BOOL(0, "allow-empty-message",
			 &options.allow_empty_message,
			 N_("allow rebasing commits with empty messages")),
		{OPTION_STRING, 'r', "rebase-merges", &rebase_merges,
			N_("mode"),
			N_("try to rebase merges instead of skipping them"),
			PARSE_OPT_OPTARG, NULL, (intptr_t)""},
		OPT_BOOL(0, "fork-point", &fork_point,
			 N_("use 'merge-base --fork-point' to refine upstream")),
		OPT_STRING('s', "strategy", &options.strategy,
			   N_("strategy"), N_("use the given merge strategy")),
		OPT_STRING_LIST('X', "strategy-option", &strategy_options,
				N_("option"),
				N_("pass the argument through to the merge "
				   "strategy")),
		OPT_BOOL(0, "root", &options.root,
			 N_("rebase all reachable commits up to the root(s)")),
		OPT_END(),
	};
	int i;

	/*
	 * NEEDSWORK: Once the builtin rebase has been tested enough
	 * and git-legacy-rebase.sh is retired to contrib/, this preamble
	 * can be removed.
	 */

	if (!use_builtin_rebase()) {
		const char *path = mkpath("%s/git-legacy-rebase",
					  git_exec_path());

		if (sane_execvp(path, (char **)argv) < 0)
			die_errno(_("could not exec %s"), path);
		else
			BUG("sane_execvp() returned???");
	}

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_rebase_usage,
				   builtin_rebase_options);

	prefix = setup_git_directory();
	trace_repo_setup(prefix);
	setup_work_tree();

	git_config(rebase_config, &options);

	strbuf_reset(&buf);
	strbuf_addf(&buf, "%s/applying", apply_dir());
	if(file_exists(buf.buf))
		die(_("It looks like 'git am' is in progress. Cannot rebase."));

	if (is_directory(apply_dir())) {
		options.type = REBASE_AM;
		options.state_dir = apply_dir();
	} else if (is_directory(merge_dir())) {
		strbuf_reset(&buf);
		strbuf_addf(&buf, "%s/rewritten", merge_dir());
		if (is_directory(buf.buf)) {
			options.type = REBASE_PRESERVE_MERGES;
			options.flags |= REBASE_INTERACTIVE_EXPLICIT;
		} else {
			strbuf_reset(&buf);
			strbuf_addf(&buf, "%s/interactive", merge_dir());
			if(file_exists(buf.buf)) {
				options.type = REBASE_INTERACTIVE;
				options.flags |= REBASE_INTERACTIVE_EXPLICIT;
			} else
				options.type = REBASE_MERGE;
		}
		options.state_dir = merge_dir();
	}

	if (options.type != REBASE_UNSPECIFIED)
		in_progress = 1;

	total_argc = argc;
	argc = parse_options(argc, argv, prefix,
			     builtin_rebase_options,
			     builtin_rebase_usage, 0);

	if (action != NO_ACTION && total_argc != 2) {
		usage_with_options(builtin_rebase_usage,
				   builtin_rebase_options);
	}

	if (argc > 2)
		usage_with_options(builtin_rebase_usage,
				   builtin_rebase_options);

	if (action != NO_ACTION && !in_progress)
		die(_("No rebase in progress?"));
	setenv(GIT_REFLOG_ACTION_ENVIRONMENT, "rebase", 0);

	if (action == ACTION_EDIT_TODO && !is_interactive(&options))
		die(_("The --edit-todo action can only be used during "
		      "interactive rebase."));

	switch (action) {
	case ACTION_CONTINUE: {
		struct object_id head;
		struct lock_file lock_file = LOCK_INIT;
		int fd;

		options.action = "continue";
		set_reflog_action(&options);

		/* Sanity check */
		if (get_oid("HEAD", &head))
			die(_("Cannot read HEAD"));

		fd = hold_locked_index(&lock_file, 0);
		if (read_index(the_repository->index) < 0)
			die(_("could not read index"));
		refresh_index(the_repository->index, REFRESH_QUIET, NULL, NULL,
			      NULL);
		if (0 <= fd)
			update_index_if_able(the_repository->index,
					     &lock_file);
		rollback_lock_file(&lock_file);

		if (has_unstaged_changes(the_repository, 1)) {
			puts(_("You must edit all merge conflicts and then\n"
			       "mark them as resolved using git add"));
			exit(1);
		}
		if (read_basic_state(&options))
			exit(1);
		goto run_rebase;
	}
	case ACTION_SKIP: {
		struct string_list merge_rr = STRING_LIST_INIT_DUP;

		options.action = "skip";
		set_reflog_action(&options);

		rerere_clear(the_repository, &merge_rr);
		string_list_clear(&merge_rr, 1);

		if (reset_head(NULL, "reset", NULL, RESET_HEAD_HARD,
			       NULL, NULL) < 0)
			die(_("could not discard worktree changes"));
		remove_branch_state(the_repository);
		if (read_basic_state(&options))
			exit(1);
		goto run_rebase;
	}
	case ACTION_ABORT: {
		struct string_list merge_rr = STRING_LIST_INIT_DUP;
		options.action = "abort";
		set_reflog_action(&options);

		rerere_clear(the_repository, &merge_rr);
		string_list_clear(&merge_rr, 1);

		if (read_basic_state(&options))
			exit(1);
		if (reset_head(&options.orig_head, "reset",
			       options.head_name, RESET_HEAD_HARD,
			       NULL, NULL) < 0)
			die(_("could not move back to %s"),
			    oid_to_hex(&options.orig_head));
		remove_branch_state(the_repository);
		ret = finish_rebase(&options);
		goto cleanup;
	}
	case ACTION_QUIT: {
		strbuf_reset(&buf);
		strbuf_addstr(&buf, options.state_dir);
		ret = !!remove_dir_recursively(&buf, 0);
		if (ret)
			die(_("could not remove '%s'"), options.state_dir);
		goto cleanup;
	}
	case ACTION_EDIT_TODO:
		options.action = "edit-todo";
		options.dont_finish_rebase = 1;
		goto run_rebase;
	case ACTION_SHOW_CURRENT_PATCH:
		options.action = "show-current-patch";
		options.dont_finish_rebase = 1;
		goto run_rebase;
	case NO_ACTION:
		break;
	default:
		BUG("action: %d", action);
	}

	/* Make sure no rebase is in progress */
	if (in_progress) {
		const char *last_slash = strrchr(options.state_dir, '/');
		const char *state_dir_base =
			last_slash ? last_slash + 1 : options.state_dir;
		const char *cmd_live_rebase =
			"git rebase (--continue | --abort | --skip)";
		strbuf_reset(&buf);
		strbuf_addf(&buf, "rm -fr \"%s\"", options.state_dir);
		die(_("It seems that there is already a %s directory, and\n"
		      "I wonder if you are in the middle of another rebase.  "
		      "If that is the\n"
		      "case, please try\n\t%s\n"
		      "If that is not the case, please\n\t%s\n"
		      "and run me again.  I am stopping in case you still "
		      "have something\n"
		      "valuable there.\n"),
		    state_dir_base, cmd_live_rebase, buf.buf);
	}

	for (i = 0; i < options.git_am_opts.argc; i++) {
		const char *option = options.git_am_opts.argv[i], *p;
		if (!strcmp(option, "--committer-date-is-author-date") ||
		    !strcmp(option, "--ignore-date") ||
		    !strcmp(option, "--whitespace=fix") ||
		    !strcmp(option, "--whitespace=strip"))
			options.flags |= REBASE_FORCE;
		else if (skip_prefix(option, "-C", &p)) {
			while (*p)
				if (!isdigit(*(p++)))
					die(_("switch `C' expects a "
					      "numerical value"));
		} else if (skip_prefix(option, "--whitespace=", &p)) {
			if (*p && strcmp(p, "warn") && strcmp(p, "nowarn") &&
			    strcmp(p, "error") && strcmp(p, "error-all"))
				die("Invalid whitespace option: '%s'", p);
		}
	}

	if (!(options.flags & REBASE_NO_QUIET))
		argv_array_push(&options.git_am_opts, "-q");

	if (options.keep_empty)
		imply_interactive(&options, "--keep-empty");

	if (gpg_sign) {
		free(options.gpg_sign_opt);
		options.gpg_sign_opt = xstrfmt("-S%s", gpg_sign);
	}

	if (exec.nr) {
		int i;

		imply_interactive(&options, "--exec");

		strbuf_reset(&buf);
		for (i = 0; i < exec.nr; i++)
			strbuf_addf(&buf, "exec %s\n", exec.items[i].string);
		options.cmd = xstrdup(buf.buf);
	}

	if (rebase_merges) {
		if (!*rebase_merges)
			; /* default mode; do nothing */
		else if (!strcmp("rebase-cousins", rebase_merges))
			options.rebase_cousins = 1;
		else if (strcmp("no-rebase-cousins", rebase_merges))
			die(_("Unknown mode: %s"), rebase_merges);
		options.rebase_merges = 1;
		imply_interactive(&options, "--rebase-merges");
	}

	if (strategy_options.nr) {
		int i;

		if (!options.strategy)
			options.strategy = "recursive";

		strbuf_reset(&buf);
		for (i = 0; i < strategy_options.nr; i++)
			strbuf_addf(&buf, " --%s",
				    strategy_options.items[i].string);
		options.strategy_opts = xstrdup(buf.buf);
	}

	if (options.strategy) {
		options.strategy = xstrdup(options.strategy);
		switch (options.type) {
		case REBASE_AM:
			die(_("--strategy requires --merge or --interactive"));
		case REBASE_MERGE:
		case REBASE_INTERACTIVE:
		case REBASE_PRESERVE_MERGES:
			/* compatible */
			break;
		case REBASE_UNSPECIFIED:
			options.type = REBASE_MERGE;
			break;
		default:
			BUG("unhandled rebase type (%d)", options.type);
		}
	}

	if (options.root && !options.onto_name)
		imply_interactive(&options, "--root without --onto");

	if (isatty(2) && options.flags & REBASE_NO_QUIET)
		strbuf_addstr(&options.git_format_patch_opt, " --progress");

	switch (options.type) {
	case REBASE_MERGE:
	case REBASE_INTERACTIVE:
	case REBASE_PRESERVE_MERGES:
		options.state_dir = merge_dir();
		break;
	case REBASE_AM:
		options.state_dir = apply_dir();
		break;
	default:
		/* the default rebase backend is `--am` */
		options.type = REBASE_AM;
		options.state_dir = apply_dir();
		break;
	}

	if (options.git_am_opts.argc) {
		/* all am options except -q are compatible only with --am */
		for (i = options.git_am_opts.argc - 1; i >= 0; i--)
			if (strcmp(options.git_am_opts.argv[i], "-q"))
				break;

		if (is_interactive(&options) && i >= 0)
			die(_("error: cannot combine interactive options "
			      "(--interactive, --exec, --rebase-merges, "
			      "--preserve-merges, --keep-empty, --root + "
			      "--onto) with am options (%s)"), buf.buf);
		if (options.type == REBASE_MERGE && i >= 0)
			die(_("error: cannot combine merge options (--merge, "
			      "--strategy, --strategy-option) with am options "
			      "(%s)"), buf.buf);
	}

	if (options.signoff) {
		if (options.type == REBASE_PRESERVE_MERGES)
			die("cannot combine '--signoff' with "
			    "'--preserve-merges'");
		argv_array_push(&options.git_am_opts, "--signoff");
		options.flags |= REBASE_FORCE;
	}

	if (options.type == REBASE_PRESERVE_MERGES)
		/*
		 * Note: incompatibility with --signoff handled in signoff block above
		 * Note: incompatibility with --interactive is just a strong warning;
		 *       git-rebase.txt caveats with "unless you know what you are doing"
		 */
		if (options.rebase_merges)
			die(_("error: cannot combine '--preserve-merges' with "
			      "'--rebase-merges'"));

	if (options.rebase_merges) {
		if (strategy_options.nr)
			die(_("error: cannot combine '--rebase-merges' with "
			      "'--strategy-option'"));
		if (options.strategy)
			die(_("error: cannot combine '--rebase-merges' with "
			      "'--strategy'"));
	}

	if (!options.root) {
		if (argc < 1) {
			struct branch *branch;

			branch = branch_get(NULL);
			options.upstream_name = branch_get_upstream(branch,
								    NULL);
			if (!options.upstream_name)
				error_on_missing_default_upstream();
			if (fork_point < 0)
				fork_point = 1;
		} else {
			options.upstream_name = argv[0];
			argc--;
			argv++;
			if (!strcmp(options.upstream_name, "-"))
				options.upstream_name = "@{-1}";
		}
		options.upstream = peel_committish(options.upstream_name);
		if (!options.upstream)
			die(_("invalid upstream '%s'"), options.upstream_name);
		options.upstream_arg = options.upstream_name;
	} else {
		if (!options.onto_name) {
			if (commit_tree("", 0, the_hash_algo->empty_tree, NULL,
					&squash_onto, NULL, NULL) < 0)
				die(_("Could not create new root commit"));
			options.squash_onto = &squash_onto;
			options.onto_name = squash_onto_name =
				xstrdup(oid_to_hex(&squash_onto));
		}
		options.upstream_name = NULL;
		options.upstream = NULL;
		if (argc > 1)
			usage_with_options(builtin_rebase_usage,
					   builtin_rebase_options);
		options.upstream_arg = "--root";
	}

	/* Make sure the branch to rebase onto is valid. */
	if (!options.onto_name)
		options.onto_name = options.upstream_name;
	if (strstr(options.onto_name, "...")) {
		if (get_oid_mb(options.onto_name, &merge_base) < 0)
			die(_("'%s': need exactly one merge base"),
			    options.onto_name);
		options.onto = lookup_commit_or_die(&merge_base,
						    options.onto_name);
	} else {
		options.onto = peel_committish(options.onto_name);
		if (!options.onto)
			die(_("Does not point to a valid commit '%s'"),
				options.onto_name);
	}

	/*
	 * If the branch to rebase is given, that is the branch we will rebase
	 * branch_name -- branch/commit being rebased, or
	 * 		  HEAD (already detached)
	 * orig_head -- commit object name of tip of the branch before rebasing
	 * head_name -- refs/heads/<that-branch> or NULL (detached HEAD)
	 */
	if (argc == 1) {
		/* Is it "rebase other branchname" or "rebase other commit"? */
		branch_name = argv[0];
		options.switch_to = argv[0];

		/* Is it a local branch? */
		strbuf_reset(&buf);
		strbuf_addf(&buf, "refs/heads/%s", branch_name);
		if (!read_ref(buf.buf, &options.orig_head))
			options.head_name = xstrdup(buf.buf);
		/* If not is it a valid ref (branch or commit)? */
		else if (!get_oid(branch_name, &options.orig_head))
			options.head_name = NULL;
		else
			die(_("fatal: no such branch/commit '%s'"),
			    branch_name);
	} else if (argc == 0) {
		/* Do not need to switch branches, we are already on it. */
		options.head_name =
			xstrdup_or_null(resolve_ref_unsafe("HEAD", 0, NULL,
					 &flags));
		if (!options.head_name)
			die(_("No such ref: %s"), "HEAD");
		if (flags & REF_ISSYMREF) {
			if (!skip_prefix(options.head_name,
					 "refs/heads/", &branch_name))
				branch_name = options.head_name;

		} else {
			free(options.head_name);
			options.head_name = NULL;
			branch_name = "HEAD";
		}
		if (get_oid("HEAD", &options.orig_head))
			die(_("Could not resolve HEAD to a revision"));
	} else
		BUG("unexpected number of arguments left to parse");

	if (fork_point > 0) {
		struct commit *head =
			lookup_commit_reference(the_repository,
						&options.orig_head);
		options.restrict_revision =
			get_fork_point(options.upstream_name, head);
	}

	if (read_index(the_repository->index) < 0)
		die(_("could not read index"));

	if (options.autostash) {
		struct lock_file lock_file = LOCK_INIT;
		int fd;

		fd = hold_locked_index(&lock_file, 0);
		refresh_cache(REFRESH_QUIET);
		if (0 <= fd)
			update_index_if_able(&the_index, &lock_file);
		rollback_lock_file(&lock_file);

		if (has_unstaged_changes(the_repository, 1) ||
		    has_uncommitted_changes(the_repository, 1)) {
			const char *autostash =
				state_dir_path("autostash", &options);
			struct child_process stash = CHILD_PROCESS_INIT;
			struct object_id oid;
			struct commit *head =
				lookup_commit_reference(the_repository,
							&options.orig_head);

			argv_array_pushl(&stash.args,
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

			if (safe_create_leading_directories_const(autostash))
				die(_("Could not create directory for '%s'"),
				    options.state_dir);
			write_file(autostash, "%s", oid_to_hex(&oid));
			printf(_("Created autostash: %s\n"), buf.buf);
			if (reset_head(&head->object.oid, "reset --hard",
				       NULL, RESET_HEAD_HARD, NULL, NULL) < 0)
				die(_("could not reset --hard"));
			printf(_("HEAD is now at %s"),
			       find_unique_abbrev(&head->object.oid,
						  DEFAULT_ABBREV));
			strbuf_reset(&buf);
			pp_commit_easy(CMIT_FMT_ONELINE, head, &buf);
			if (buf.len > 0)
				printf(" %s", buf.buf);
			putchar('\n');

			if (discard_index(the_repository->index) < 0 ||
				read_index(the_repository->index) < 0)
				die(_("could not read index"));
		}
	}

	if (require_clean_work_tree(the_repository, "rebase",
				    _("Please commit or stash them."), 1, 1)) {
		ret = 1;
		goto cleanup;
	}

	/*
	 * Now we are rebasing commits upstream..orig_head (or with --root,
	 * everything leading up to orig_head) on top of onto.
	 */

	/*
	 * Check if we are already based on onto with linear history,
	 * but this should be done only when upstream and onto are the same
	 * and if this is not an interactive rebase.
	 */
	if (can_fast_forward(options.onto, &options.orig_head, &merge_base) &&
	    !is_interactive(&options) && !options.restrict_revision &&
	    options.upstream &&
	    !oidcmp(&options.upstream->object.oid, &options.onto->object.oid)) {
		int flag;

		if (!(options.flags & REBASE_FORCE)) {
			/* Lazily switch to the target branch if needed... */
			if (options.switch_to) {
				struct object_id oid;

				if (get_oid(options.switch_to, &oid) < 0) {
					ret = !!error(_("could not parse '%s'"),
						      options.switch_to);
					goto cleanup;
				}

				strbuf_reset(&buf);
				strbuf_addf(&buf, "%s: checkout %s",
					    getenv(GIT_REFLOG_ACTION_ENVIRONMENT),
					    options.switch_to);
				if (reset_head(&oid, "checkout",
					       options.head_name, 0,
					       NULL, buf.buf) < 0) {
					ret = !!error(_("could not switch to "
							"%s"),
						      options.switch_to);
					goto cleanup;
				}
			}

			if (!(options.flags & REBASE_NO_QUIET))
				; /* be quiet */
			else if (!strcmp(branch_name, "HEAD") &&
				 resolve_ref_unsafe("HEAD", 0, NULL, &flag))
				puts(_("HEAD is up to date."));
			else
				printf(_("Current branch %s is up to date.\n"),
				       branch_name);
			ret = !!finish_rebase(&options);
			goto cleanup;
		} else if (!(options.flags & REBASE_NO_QUIET))
			; /* be quiet */
		else if (!strcmp(branch_name, "HEAD") &&
			 resolve_ref_unsafe("HEAD", 0, NULL, &flag))
			puts(_("HEAD is up to date, rebase forced."));
		else
			printf(_("Current branch %s is up to date, rebase "
				 "forced.\n"), branch_name);
	}

	/* If a hook exists, give it a chance to interrupt*/
	if (!ok_to_skip_pre_rebase &&
	    run_hook_le(NULL, "pre-rebase", options.upstream_arg,
			argc ? argv[0] : NULL, NULL))
		die(_("The pre-rebase hook refused to rebase."));

	if (options.flags & REBASE_DIFFSTAT) {
		struct diff_options opts;

		if (options.flags & REBASE_VERBOSE) {
			if (is_null_oid(&merge_base))
				printf(_("Changes to %s:\n"),
				       oid_to_hex(&options.onto->object.oid));
			else
				printf(_("Changes from %s to %s:\n"),
				       oid_to_hex(&merge_base),
				       oid_to_hex(&options.onto->object.oid));
		}

		/* We want color (if set), but no pager */
		diff_setup(&opts);
		opts.stat_width = -1; /* use full terminal width */
		opts.stat_graph_width = -1; /* respect statGraphWidth config */
		opts.output_format |=
			DIFF_FORMAT_SUMMARY | DIFF_FORMAT_DIFFSTAT;
		opts.detect_rename = DIFF_DETECT_RENAME;
		diff_setup_done(&opts);
		diff_tree_oid(is_null_oid(&merge_base) ?
			      the_hash_algo->empty_tree : &merge_base,
			      &options.onto->object.oid, "", &opts);
		diffcore_std(&opts);
		diff_flush(&opts);
	}

	if (is_interactive(&options))
		goto run_rebase;

	/* Detach HEAD and reset the tree */
	if (options.flags & REBASE_NO_QUIET)
		printf(_("First, rewinding head to replay your work on top of "
			 "it...\n"));

	strbuf_addf(&msg, "%s: checkout %s",
		    getenv(GIT_REFLOG_ACTION_ENVIRONMENT), options.onto_name);
	if (reset_head(&options.onto->object.oid, "checkout", NULL,
		       RESET_HEAD_DETACH, NULL, msg.buf))
		die(_("Could not detach HEAD"));
	strbuf_release(&msg);

	/*
	 * If the onto is a proper descendant of the tip of the branch, then
	 * we just fast-forwarded.
	 */
	strbuf_reset(&msg);
	if (!oidcmp(&merge_base, &options.orig_head)) {
		printf(_("Fast-forwarded %s to %s.\n"),
			branch_name, options.onto_name);
		strbuf_addf(&msg, "rebase finished: %s onto %s",
			options.head_name ? options.head_name : "detached HEAD",
			oid_to_hex(&options.onto->object.oid));
		reset_head(NULL, "Fast-forwarded", options.head_name, 0,
			   "HEAD", msg.buf);
		strbuf_release(&msg);
		ret = !!finish_rebase(&options);
		goto cleanup;
	}

	strbuf_addf(&revisions, "%s..%s",
		    options.root ? oid_to_hex(&options.onto->object.oid) :
		    (options.restrict_revision ?
		     oid_to_hex(&options.restrict_revision->object.oid) :
		     oid_to_hex(&options.upstream->object.oid)),
		    oid_to_hex(&options.orig_head));

	options.revisions = revisions.buf;

run_rebase:
	ret = !!run_specific_rebase(&options);

cleanup:
	strbuf_release(&revisions);
	free(options.head_name);
	free(options.gpg_sign_opt);
	free(options.cmd);
	free(squash_onto_name);
	return ret;
}
