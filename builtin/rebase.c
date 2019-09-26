/*
 * "git rebase" builtin command
 *
 * Copyright (c) 2018 Pratik Karki
 */

#define USE_THE_INDEX_COMPATIBILITY_MACROS
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
#include "sequencer.h"
#include "rebase-interactive.h"

static char const * const builtin_rebase_usage[] = {
	N_("git rebase [-i] [options] [--exec <cmd>] [--onto <newbase>] "
		"[<upstream>] [<branch>]"),
	N_("git rebase [-i] [options] [--exec <cmd>] [--onto <newbase>] "
		"--root [<branch>]"),
	N_("git rebase --continue | --abort | --skip | --edit-todo"),
	NULL
};

static GIT_PATH_FUNC(path_squash_onto, "rebase-merge/squash-onto")
static GIT_PATH_FUNC(path_interactive, "rebase-merge/interactive")
static GIT_PATH_FUNC(apply_dir, "rebase-apply")
static GIT_PATH_FUNC(merge_dir, "rebase-merge")

enum rebase_type {
	REBASE_UNSPECIFIED = -1,
	REBASE_AM,
	REBASE_MERGE,
	REBASE_INTERACTIVE,
	REBASE_PRESERVE_MERGES
};

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
	int root, root_with_onto;
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
	int reschedule_failed_exec;
	int use_legacy_rebase;
};

#define REBASE_OPTIONS_INIT {			  	\
		.type = REBASE_UNSPECIFIED,	  	\
		.flags = REBASE_NO_QUIET, 		\
		.git_am_opts = ARGV_ARRAY_INIT,		\
		.git_format_patch_opt = STRBUF_INIT	\
	}

static struct replay_opts get_replay_opts(const struct rebase_options *opts)
{
	struct replay_opts replay = REPLAY_OPTS_INIT;

	replay.action = REPLAY_INTERACTIVE_REBASE;
	sequencer_init_config(&replay);

	replay.signoff = opts->signoff;
	replay.allow_ff = !(opts->flags & REBASE_FORCE);
	if (opts->allow_rerere_autoupdate)
		replay.allow_rerere_auto = opts->allow_rerere_autoupdate;
	replay.allow_empty = 1;
	replay.allow_empty_message = opts->allow_empty_message;
	replay.verbose = opts->flags & REBASE_VERBOSE;
	replay.reschedule_failed_exec = opts->reschedule_failed_exec;
	replay.gpg_sign = xstrdup_or_null(opts->gpg_sign_opt);
	replay.strategy = opts->strategy;
	if (opts->strategy_opts)
		parse_strategy_opts(&replay, opts->strategy_opts);

	return replay;
}

enum action {
	ACTION_NONE = 0,
	ACTION_CONTINUE,
	ACTION_SKIP,
	ACTION_ABORT,
	ACTION_QUIT,
	ACTION_EDIT_TODO,
	ACTION_SHOW_CURRENT_PATCH,
	ACTION_SHORTEN_OIDS,
	ACTION_EXPAND_OIDS,
	ACTION_CHECK_TODO_LIST,
	ACTION_REARRANGE_SQUASH,
	ACTION_ADD_EXEC
};

static const char *action_names[] = { "undefined",
				      "continue",
				      "skip",
				      "abort",
				      "quit",
				      "edit_todo",
				      "show_current_patch" };

static int add_exec_commands(struct string_list *commands)
{
	const char *todo_file = rebase_path_todo();
	struct todo_list todo_list = TODO_LIST_INIT;
	int res;

	if (strbuf_read_file(&todo_list.buf, todo_file, 0) < 0)
		return error_errno(_("could not read '%s'."), todo_file);

	if (todo_list_parse_insn_buffer(the_repository, todo_list.buf.buf,
					&todo_list)) {
		todo_list_release(&todo_list);
		return error(_("unusable todo list: '%s'"), todo_file);
	}

	todo_list_add_exec_commands(&todo_list, commands);
	res = todo_list_write_to_file(the_repository, &todo_list,
				      todo_file, NULL, NULL, -1, 0);
	todo_list_release(&todo_list);

	if (res)
		return error_errno(_("could not write '%s'."), todo_file);
	return 0;
}

static int rearrange_squash_in_todo_file(void)
{
	const char *todo_file = rebase_path_todo();
	struct todo_list todo_list = TODO_LIST_INIT;
	int res = 0;

	if (strbuf_read_file(&todo_list.buf, todo_file, 0) < 0)
		return error_errno(_("could not read '%s'."), todo_file);
	if (todo_list_parse_insn_buffer(the_repository, todo_list.buf.buf,
					&todo_list)) {
		todo_list_release(&todo_list);
		return error(_("unusable todo list: '%s'"), todo_file);
	}

	res = todo_list_rearrange_squash(&todo_list);
	if (!res)
		res = todo_list_write_to_file(the_repository, &todo_list,
					      todo_file, NULL, NULL, -1, 0);

	todo_list_release(&todo_list);

	if (res)
		return error_errno(_("could not write '%s'."), todo_file);
	return 0;
}

static int transform_todo_file(unsigned flags)
{
	const char *todo_file = rebase_path_todo();
	struct todo_list todo_list = TODO_LIST_INIT;
	int res;

	if (strbuf_read_file(&todo_list.buf, todo_file, 0) < 0)
		return error_errno(_("could not read '%s'."), todo_file);

	if (todo_list_parse_insn_buffer(the_repository, todo_list.buf.buf,
					&todo_list)) {
		todo_list_release(&todo_list);
		return error(_("unusable todo list: '%s'"), todo_file);
	}

	res = todo_list_write_to_file(the_repository, &todo_list, todo_file,
				      NULL, NULL, -1, flags);
	todo_list_release(&todo_list);

	if (res)
		return error_errno(_("could not write '%s'."), todo_file);
	return 0;
}

static int edit_todo_file(unsigned flags)
{
	const char *todo_file = rebase_path_todo();
	struct todo_list todo_list = TODO_LIST_INIT,
		new_todo = TODO_LIST_INIT;
	int res = 0;

	if (strbuf_read_file(&todo_list.buf, todo_file, 0) < 0)
		return error_errno(_("could not read '%s'."), todo_file);

	strbuf_stripspace(&todo_list.buf, 1);
	res = edit_todo_list(the_repository, &todo_list, &new_todo, NULL, NULL, flags);
	if (!res && todo_list_write_to_file(the_repository, &new_todo, todo_file,
					    NULL, NULL, -1, flags & ~(TODO_LIST_SHORTEN_IDS)))
		res = error_errno(_("could not write '%s'"), todo_file);

	todo_list_release(&todo_list);
	todo_list_release(&new_todo);

	return res;
}

static int get_revision_ranges(struct commit *upstream, struct commit *onto,
			       const char **head_hash,
			       char **revisions, char **shortrevisions)
{
	struct commit *base_rev = upstream ? upstream : onto;
	const char *shorthead;
	struct object_id orig_head;

	if (get_oid("HEAD", &orig_head))
		return error(_("no HEAD?"));

	*head_hash = find_unique_abbrev(&orig_head, GIT_MAX_HEXSZ);
	*revisions = xstrfmt("%s...%s", oid_to_hex(&base_rev->object.oid),
						   *head_hash);

	shorthead = find_unique_abbrev(&orig_head, DEFAULT_ABBREV);

	if (upstream) {
		const char *shortrev;

		shortrev = find_unique_abbrev(&base_rev->object.oid,
					      DEFAULT_ABBREV);

		*shortrevisions = xstrfmt("%s..%s", shortrev, shorthead);
	} else
		*shortrevisions = xstrdup(shorthead);

	return 0;
}

static int init_basic_state(struct replay_opts *opts, const char *head_name,
			    struct commit *onto, const char *orig_head)
{
	FILE *interactive;

	if (!is_directory(merge_dir()) && mkdir_in_gitdir(merge_dir()))
		return error_errno(_("could not create temporary %s"), merge_dir());

	delete_reflog("REBASE_HEAD");

	interactive = fopen(path_interactive(), "w");
	if (!interactive)
		return error_errno(_("could not mark as interactive"));
	fclose(interactive);

	return write_basic_state(opts, head_name, onto, orig_head);
}

static void split_exec_commands(const char *cmd, struct string_list *commands)
{
	if (cmd && *cmd) {
		string_list_split(commands, cmd, '\n', -1);

		/* rebase.c adds a new line to cmd after every command,
		 * so here the last command is always empty */
		string_list_remove_empty_items(commands, 0);
	}
}

static int do_interactive_rebase(struct rebase_options *opts, unsigned flags)
{
	int ret;
	const char *head_hash = NULL;
	char *revisions = NULL, *shortrevisions = NULL;
	struct argv_array make_script_args = ARGV_ARRAY_INIT;
	struct todo_list todo_list = TODO_LIST_INIT;
	struct replay_opts replay = get_replay_opts(opts);
	struct string_list commands = STRING_LIST_INIT_DUP;

	if (prepare_branch_to_be_rebased(the_repository, &replay,
					 opts->switch_to))
		return -1;

	if (get_revision_ranges(opts->upstream, opts->onto, &head_hash,
				&revisions, &shortrevisions))
		return -1;

	if (init_basic_state(&replay,
			     opts->head_name ? opts->head_name : "detached HEAD",
			     opts->onto, head_hash)) {
		free(revisions);
		free(shortrevisions);

		return -1;
	}

	if (!opts->upstream && opts->squash_onto)
		write_file(path_squash_onto(), "%s\n",
			   oid_to_hex(opts->squash_onto));

	argv_array_pushl(&make_script_args, "", revisions, NULL);
	if (opts->restrict_revision)
		argv_array_push(&make_script_args,
				oid_to_hex(&opts->restrict_revision->object.oid));

	ret = sequencer_make_script(the_repository, &todo_list.buf,
				    make_script_args.argc, make_script_args.argv,
				    flags);

	if (ret)
		error(_("could not generate todo list"));
	else {
		discard_cache();
		if (todo_list_parse_insn_buffer(the_repository, todo_list.buf.buf,
						&todo_list))
			BUG("unusable todo list");

		split_exec_commands(opts->cmd, &commands);
		ret = complete_action(the_repository, &replay, flags,
			shortrevisions, opts->onto_name, opts->onto, head_hash,
			&commands, opts->autosquash, &todo_list);
	}

	string_list_clear(&commands, 0);
	free(revisions);
	free(shortrevisions);
	todo_list_release(&todo_list);
	argv_array_clear(&make_script_args);

	return ret;
}

static int run_rebase_interactive(struct rebase_options *opts,
				  enum action command)
{
	unsigned flags = 0;
	int abbreviate_commands = 0, ret = 0;

	git_config_get_bool("rebase.abbreviatecommands", &abbreviate_commands);

	flags |= opts->keep_empty ? TODO_LIST_KEEP_EMPTY : 0;
	flags |= abbreviate_commands ? TODO_LIST_ABBREVIATE_CMDS : 0;
	flags |= opts->rebase_merges ? TODO_LIST_REBASE_MERGES : 0;
	flags |= opts->rebase_cousins > 0 ? TODO_LIST_REBASE_COUSINS : 0;
	flags |= opts->root_with_onto ? TODO_LIST_ROOT_WITH_ONTO : 0;
	flags |= command == ACTION_SHORTEN_OIDS ? TODO_LIST_SHORTEN_IDS : 0;

	switch (command) {
	case ACTION_NONE: {
		if (!opts->onto && !opts->upstream)
			die(_("a base commit must be provided with --upstream or --onto"));

		ret = do_interactive_rebase(opts, flags);
		break;
	}
	case ACTION_SKIP: {
		struct string_list merge_rr = STRING_LIST_INIT_DUP;

		rerere_clear(the_repository, &merge_rr);
	}
		/* fallthrough */
	case ACTION_CONTINUE: {
		struct replay_opts replay_opts = get_replay_opts(opts);

		ret = sequencer_continue(the_repository, &replay_opts);
		break;
	}
	case ACTION_EDIT_TODO:
		ret = edit_todo_file(flags);
		break;
	case ACTION_SHOW_CURRENT_PATCH: {
		struct child_process cmd = CHILD_PROCESS_INIT;

		cmd.git_cmd = 1;
		argv_array_pushl(&cmd.args, "show", "REBASE_HEAD", "--", NULL);
		ret = run_command(&cmd);

		break;
	}
	case ACTION_SHORTEN_OIDS:
	case ACTION_EXPAND_OIDS:
		ret = transform_todo_file(flags);
		break;
	case ACTION_CHECK_TODO_LIST:
		ret = check_todo_list_from_file(the_repository);
		break;
	case ACTION_REARRANGE_SQUASH:
		ret = rearrange_squash_in_todo_file();
		break;
	case ACTION_ADD_EXEC: {
		struct string_list commands = STRING_LIST_INIT_DUP;

		split_exec_commands(opts->cmd, &commands);
		ret = add_exec_commands(&commands);
		string_list_clear(&commands, 0);
		break;
	}
	default:
		BUG("invalid command '%d'", command);
	}

	return ret;
}

static const char * const builtin_rebase_interactive_usage[] = {
	N_("git rebase--interactive [<options>]"),
	NULL
};

int cmd_rebase__interactive(int argc, const char **argv, const char *prefix)
{
	struct rebase_options opts = REBASE_OPTIONS_INIT;
	struct object_id squash_onto = null_oid;
	enum action command = ACTION_NONE;
	struct option options[] = {
		OPT_NEGBIT(0, "ff", &opts.flags, N_("allow fast-forward"),
			   REBASE_FORCE),
		OPT_BOOL(0, "keep-empty", &opts.keep_empty, N_("keep empty commits")),
		OPT_BOOL(0, "allow-empty-message", &opts.allow_empty_message,
			 N_("allow commits with empty messages")),
		OPT_BOOL(0, "rebase-merges", &opts.rebase_merges, N_("rebase merge commits")),
		OPT_BOOL(0, "rebase-cousins", &opts.rebase_cousins,
			 N_("keep original branch points of cousins")),
		OPT_BOOL(0, "autosquash", &opts.autosquash,
			 N_("move commits that begin with squash!/fixup!")),
		OPT_BOOL(0, "signoff", &opts.signoff, N_("sign commits")),
		OPT_BIT('v', "verbose", &opts.flags,
			N_("display a diffstat of what changed upstream"),
			REBASE_NO_QUIET | REBASE_VERBOSE | REBASE_DIFFSTAT),
		OPT_CMDMODE(0, "continue", &command, N_("continue rebase"),
			    ACTION_CONTINUE),
		OPT_CMDMODE(0, "skip", &command, N_("skip commit"), ACTION_SKIP),
		OPT_CMDMODE(0, "edit-todo", &command, N_("edit the todo list"),
			    ACTION_EDIT_TODO),
		OPT_CMDMODE(0, "show-current-patch", &command, N_("show the current patch"),
			    ACTION_SHOW_CURRENT_PATCH),
		OPT_CMDMODE(0, "shorten-ids", &command,
			N_("shorten commit ids in the todo list"), ACTION_SHORTEN_OIDS),
		OPT_CMDMODE(0, "expand-ids", &command,
			N_("expand commit ids in the todo list"), ACTION_EXPAND_OIDS),
		OPT_CMDMODE(0, "check-todo-list", &command,
			N_("check the todo list"), ACTION_CHECK_TODO_LIST),
		OPT_CMDMODE(0, "rearrange-squash", &command,
			N_("rearrange fixup/squash lines"), ACTION_REARRANGE_SQUASH),
		OPT_CMDMODE(0, "add-exec-commands", &command,
			N_("insert exec commands in todo list"), ACTION_ADD_EXEC),
		{ OPTION_CALLBACK, 0, "onto", &opts.onto, N_("onto"), N_("onto"),
		  PARSE_OPT_NONEG, parse_opt_commit, 0 },
		{ OPTION_CALLBACK, 0, "restrict-revision", &opts.restrict_revision,
		  N_("restrict-revision"), N_("restrict revision"),
		  PARSE_OPT_NONEG, parse_opt_commit, 0 },
		{ OPTION_CALLBACK, 0, "squash-onto", &squash_onto, N_("squash-onto"),
		  N_("squash onto"), PARSE_OPT_NONEG, parse_opt_object_id, 0 },
		{ OPTION_CALLBACK, 0, "upstream", &opts.upstream, N_("upstream"),
		  N_("the upstream commit"), PARSE_OPT_NONEG, parse_opt_commit,
		  0 },
		OPT_STRING(0, "head-name", &opts.head_name, N_("head-name"), N_("head name")),
		{ OPTION_STRING, 'S', "gpg-sign", &opts.gpg_sign_opt, N_("key-id"),
			N_("GPG-sign commits"),
			PARSE_OPT_OPTARG, NULL, (intptr_t) "" },
		OPT_STRING(0, "strategy", &opts.strategy, N_("strategy"),
			   N_("rebase strategy")),
		OPT_STRING(0, "strategy-opts", &opts.strategy_opts, N_("strategy-opts"),
			   N_("strategy options")),
		OPT_STRING(0, "switch-to", &opts.switch_to, N_("switch-to"),
			   N_("the branch or commit to checkout")),
		OPT_STRING(0, "onto-name", &opts.onto_name, N_("onto-name"), N_("onto name")),
		OPT_STRING(0, "cmd", &opts.cmd, N_("cmd"), N_("the command to run")),
		OPT_RERERE_AUTOUPDATE(&opts.allow_rerere_autoupdate),
		OPT_BOOL(0, "reschedule-failed-exec", &opts.reschedule_failed_exec,
			 N_("automatically re-schedule any `exec` that fails")),
		OPT_END()
	};

	opts.rebase_cousins = -1;

	if (argc == 1)
		usage_with_options(builtin_rebase_interactive_usage, options);

	argc = parse_options(argc, argv, prefix, options,
			builtin_rebase_interactive_usage, PARSE_OPT_KEEP_ARGV0);

	if (!is_null_oid(&squash_onto))
		opts.squash_onto = &squash_onto;

	if (opts.rebase_cousins >= 0 && !opts.rebase_merges)
		warning(_("--[no-]rebase-cousins has no effect without "
			  "--rebase-merges"));

	return !!run_rebase_interactive(&opts, command);
}

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
		/* we now implement --merge via --interactive */
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

	if (file_exists(state_dir_path("quiet", opts)))
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
			opts->allow_rerere_autoupdate = RERERE_AUTOUPDATE;
		else if (!strcmp(buf.buf, "--no-rerere-autoupdate"))
			opts->allow_rerere_autoupdate = RERERE_NOAUTOUPDATE;
		else
			warning(_("ignoring invalid allow_rerere_autoupdate: "
				  "'%s'"), buf.buf);
	}

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

static int rebase_write_basic_state(struct rebase_options *opts)
{
	write_file(state_dir_path("head-name", opts), "%s",
		   opts->head_name ? opts->head_name : "detached HEAD");
	write_file(state_dir_path("onto", opts), "%s",
		   opts->onto ? oid_to_hex(&opts->onto->object.oid) : "");
	write_file(state_dir_path("orig-head", opts), "%s",
		   oid_to_hex(&opts->orig_head));
	write_file(state_dir_path("quiet", opts), "%s",
		   opts->flags & REBASE_NO_QUIET ? "" : "t");
	if (opts->flags & REBASE_VERBOSE)
		write_file(state_dir_path("verbose", opts), "%s", "");
	if (opts->strategy)
		write_file(state_dir_path("strategy", opts), "%s",
			   opts->strategy);
	if (opts->strategy_opts)
		write_file(state_dir_path("strategy_opts", opts), "%s",
			   opts->strategy_opts);
	if (opts->allow_rerere_autoupdate > 0)
		write_file(state_dir_path("allow_rerere_autoupdate", opts),
			   "-%s-rerere-autoupdate",
			   opts->allow_rerere_autoupdate == RERERE_AUTOUPDATE ?
				"" : "-no");
	if (opts->gpg_sign_opt)
		write_file(state_dir_path("gpg_sign_opt", opts), "%s",
			   opts->gpg_sign_opt);
	if (opts->signoff)
		write_file(state_dir_path("strategy", opts), "--signoff");

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
	int ret = 0;

	delete_ref(NULL, "REBASE_HEAD", NULL, REF_NO_DEREF);
	apply_autostash(opts);
	close_object_store(the_repository->objects);
	/*
	 * We ignore errors in 'gc --auto', since the
	 * user should see them.
	 */
	run_command_v_opt(argv_gc_auto, RUN_GIT_CMD);
	if (opts->type == REBASE_INTERACTIVE) {
		struct replay_opts replay = REPLAY_OPTS_INIT;

		replay.action = REPLAY_INTERACTIVE_REBASE;
		ret = sequencer_remove_state(&replay);
	} else {
		strbuf_addstr(&dir, opts->state_dir);
		if (remove_dir_recursively(&dir, 0))
			ret = error(_("could not remove '%s'"),
				    opts->state_dir);
		strbuf_release(&dir);
	}

	return ret;
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

#define GIT_REFLOG_ACTION_ENVIRONMENT "GIT_REFLOG_ACTION"

#define RESET_HEAD_DETACH (1<<0)
#define RESET_HEAD_HARD (1<<1)
#define RESET_HEAD_RUN_POST_CHECKOUT_HOOK (1<<2)
#define RESET_HEAD_REFS_ONLY (1<<3)
#define RESET_ORIG_HEAD (1<<4)

static int reset_head(struct object_id *oid, const char *action,
		      const char *switch_to_branch, unsigned flags,
		      const char *reflog_orig_head, const char *reflog_head)
{
	unsigned detach_head = flags & RESET_HEAD_DETACH;
	unsigned reset_hard = flags & RESET_HEAD_HARD;
	unsigned run_hook = flags & RESET_HEAD_RUN_POST_CHECKOUT_HOOK;
	unsigned refs_only = flags & RESET_HEAD_REFS_ONLY;
	unsigned update_orig_head = flags & RESET_ORIG_HEAD;
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

	if (!refs_only && hold_locked_index(&lock, LOCK_REPORT_ON_ERROR) < 0) {
		ret = -1;
		goto leave_reset_head;
	}

	if ((!oid || !reset_hard) && get_oid("HEAD", &head_oid)) {
		ret = error(_("could not determine HEAD revision"));
		goto leave_reset_head;
	}

	if (!oid)
		oid = &head_oid;

	if (refs_only)
		goto reset_head_refs;

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

	if (repo_read_index_unmerged(the_repository) < 0) {
		ret = error(_("could not read index"));
		goto leave_reset_head;
	}

	if (!reset_hard && !fill_tree_descriptor(the_repository, &desc[nr++], &head_oid)) {
		ret = error(_("failed to find tree of %s"),
			    oid_to_hex(&head_oid));
		goto leave_reset_head;
	}

	if (!fill_tree_descriptor(the_repository, &desc[nr++], oid)) {
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

reset_head_refs:
	reflog_action = getenv(GIT_REFLOG_ACTION_ENVIRONMENT);
	strbuf_addf(&msg, "%s: ", reflog_action ? reflog_action : "rebase");
	prefix_len = msg.len;

	if (update_orig_head) {
		if (!get_oid("ORIG_HEAD", &oid_old_orig))
			old_orig = &oid_old_orig;
		if (!get_oid("HEAD", &oid_orig)) {
			orig = &oid_orig;
			if (!reflog_orig_head) {
				strbuf_addstr(&msg, "updating ORIG_HEAD");
				reflog_orig_head = msg.buf;
			}
			update_ref(reflog_orig_head, "ORIG_HEAD", orig,
				   old_orig, 0, UPDATE_REFS_MSG_ON_ERR);
		} else if (old_orig)
			delete_ref(NULL, "ORIG_HEAD", old_orig, 0);
	}

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
		ret = update_ref(reflog_head, switch_to_branch, oid,
				 NULL, 0, UPDATE_REFS_MSG_ON_ERR);
		if (!ret)
			ret = create_symref("HEAD", switch_to_branch,
					    reflog_head);
	}
	if (run_hook)
		run_hook_le(NULL, "post-checkout",
			    oid_to_hex(orig ? orig : &null_oid),
			    oid_to_hex(oid), "1", NULL);

leave_reset_head:
	strbuf_release(&msg);
	rollback_lock_file(&lock);
	while (nr)
		free((void *)desc[--nr].buffer);
	return ret;
}

static int move_to_original_branch(struct rebase_options *opts)
{
	struct strbuf orig_head_reflog = STRBUF_INIT, head_reflog = STRBUF_INIT;
	int ret;

	if (!opts->head_name)
		return 0; /* nothing to move back to */

	if (!opts->onto)
		BUG("move_to_original_branch without onto");

	strbuf_addf(&orig_head_reflog, "rebase finished: %s onto %s",
		    opts->head_name, oid_to_hex(&opts->onto->object.oid));
	strbuf_addf(&head_reflog, "rebase finished: returning to %s",
		    opts->head_name);
	ret = reset_head(NULL, "", opts->head_name, RESET_HEAD_REFS_ONLY,
			 orig_head_reflog.buf, head_reflog.buf);

	strbuf_release(&orig_head_reflog);
	strbuf_release(&head_reflog);
	return ret;
}

static const char *resolvemsg =
N_("Resolve all conflicts manually, mark them as resolved with\n"
"\"git add/rm <conflicted_files>\", then run \"git rebase --continue\".\n"
"You can instead skip this commit: run \"git rebase --skip\".\n"
"To abort and get back to the state before \"git rebase\", run "
"\"git rebase --abort\".");

static int run_am(struct rebase_options *opts)
{
	struct child_process am = CHILD_PROCESS_INIT;
	struct child_process format_patch = CHILD_PROCESS_INIT;
	struct strbuf revisions = STRBUF_INIT;
	int status;
	char *rebased_patches;

	am.git_cmd = 1;
	argv_array_push(&am.args, "am");

	if (opts->action && !strcmp("continue", opts->action)) {
		argv_array_push(&am.args, "--resolved");
		argv_array_pushf(&am.args, "--resolvemsg=%s", resolvemsg);
		if (opts->gpg_sign_opt)
			argv_array_push(&am.args, opts->gpg_sign_opt);
		status = run_command(&am);
		if (status)
			return status;

		return move_to_original_branch(opts);
	}
	if (opts->action && !strcmp("skip", opts->action)) {
		argv_array_push(&am.args, "--skip");
		argv_array_pushf(&am.args, "--resolvemsg=%s", resolvemsg);
		status = run_command(&am);
		if (status)
			return status;

		return move_to_original_branch(opts);
	}
	if (opts->action && !strcmp("show-current-patch", opts->action)) {
		argv_array_push(&am.args, "--show-current-patch");
		return run_command(&am);
	}

	strbuf_addf(&revisions, "%s...%s",
		    oid_to_hex(opts->root ?
			       /* this is now equivalent to !opts->upstream */
			       &opts->onto->object.oid :
			       &opts->upstream->object.oid),
		    oid_to_hex(&opts->orig_head));

	rebased_patches = xstrdup(git_path("rebased-patches"));
	format_patch.out = open(rebased_patches,
				O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (format_patch.out < 0) {
		status = error_errno(_("could not open '%s' for writing"),
				     rebased_patches);
		free(rebased_patches);
		argv_array_clear(&am.args);
		return status;
	}

	format_patch.git_cmd = 1;
	argv_array_pushl(&format_patch.args, "format-patch", "-k", "--stdout",
			 "--full-index", "--cherry-pick", "--right-only",
			 "--src-prefix=a/", "--dst-prefix=b/", "--no-renames",
			 "--no-cover-letter", "--pretty=mboxrd", "--topo-order", NULL);
	if (opts->git_format_patch_opt.len)
		argv_array_split(&format_patch.args,
				 opts->git_format_patch_opt.buf);
	argv_array_push(&format_patch.args, revisions.buf);
	if (opts->restrict_revision)
		argv_array_pushf(&format_patch.args, "^%s",
				 oid_to_hex(&opts->restrict_revision->object.oid));

	status = run_command(&format_patch);
	if (status) {
		unlink(rebased_patches);
		free(rebased_patches);
		argv_array_clear(&am.args);

		reset_head(&opts->orig_head, "checkout", opts->head_name, 0,
			   "HEAD", NULL);
		error(_("\ngit encountered an error while preparing the "
			"patches to replay\n"
			"these revisions:\n"
			"\n    %s\n\n"
			"As a result, git cannot rebase them."),
		      opts->revisions);

		strbuf_release(&revisions);
		return status;
	}
	strbuf_release(&revisions);

	am.in = open(rebased_patches, O_RDONLY);
	if (am.in < 0) {
		status = error_errno(_("could not open '%s' for reading"),
				     rebased_patches);
		free(rebased_patches);
		argv_array_clear(&am.args);
		return status;
	}

	argv_array_pushv(&am.args, opts->git_am_opts.argv);
	argv_array_push(&am.args, "--rebasing");
	argv_array_pushf(&am.args, "--resolvemsg=%s", resolvemsg);
	argv_array_push(&am.args, "--patch-format=mboxrd");
	if (opts->allow_rerere_autoupdate == RERERE_AUTOUPDATE)
		argv_array_push(&am.args, "--rerere-autoupdate");
	else if (opts->allow_rerere_autoupdate == RERERE_NOAUTOUPDATE)
		argv_array_push(&am.args, "--no-rerere-autoupdate");
	if (opts->gpg_sign_opt)
		argv_array_push(&am.args, opts->gpg_sign_opt);
	status = run_command(&am);
	unlink(rebased_patches);
	free(rebased_patches);

	if (!status) {
		return move_to_original_branch(opts);
	}

	if (is_directory(opts->state_dir))
		rebase_write_basic_state(opts);

	return status;
}

static int run_specific_rebase(struct rebase_options *opts, enum action action)
{
	const char *argv[] = { NULL, NULL };
	struct strbuf script_snippet = STRBUF_INIT, buf = STRBUF_INIT;
	int status;
	const char *backend, *backend_func;

	if (opts->type == REBASE_INTERACTIVE) {
		/* Run builtin interactive rebase */
		setenv("GIT_CHERRY_PICK_HELP", resolvemsg, 1);
		if (!(opts->flags & REBASE_INTERACTIVE_EXPLICIT)) {
			setenv("GIT_SEQUENCE_EDITOR", ":", 1);
			opts->autosquash = 0;
		}
		if (opts->gpg_sign_opt) {
			/* remove the leading "-S" */
			char *tmp = xstrdup(opts->gpg_sign_opt + 2);
			free(opts->gpg_sign_opt);
			opts->gpg_sign_opt = tmp;
		}

		status = run_rebase_interactive(opts, action);
		goto finished_rebase;
	}

	if (opts->type == REBASE_AM) {
		status = run_am(opts);
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
		opts->allow_rerere_autoupdate ?
			opts->allow_rerere_autoupdate == RERERE_AUTOUPDATE ?
			"--rerere-autoupdate" : "--no-rerere-autoupdate" : "");
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
			      "GIT_SEQUENCE_EDITOR=:; export GIT_SEQUENCE_EDITOR; ");
		opts->autosquash = 0;
	}

	switch (opts->type) {
	case REBASE_PRESERVE_MERGES:
		backend = "git-rebase--preserve-merges";
		backend_func = "git_rebase__preserve_merges";
		break;
	default:
		BUG("Unhandled rebase type %d", opts->type);
		break;
	}

	strbuf_addf(&script_snippet,
		    ". git-sh-setup && . %s && %s", backend, backend_func);
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

static int rebase_config(const char *var, const char *value, void *data)
{
	struct rebase_options *opts = data;

	if (!strcmp(var, "rebase.stat")) {
		if (git_config_bool(var, value))
			opts->flags |= REBASE_DIFFSTAT;
		else
			opts->flags &= ~REBASE_DIFFSTAT;
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

	if (!strcmp(var, "rebase.reschedulefailedexec")) {
		opts->reschedule_failed_exec = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "rebase.usebuiltin")) {
		opts->use_legacy_rebase = !git_config_bool(var, value);
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

static int check_exec_cmd(const char *cmd)
{
	if (strchr(cmd, '\n'))
		return error(_("exec commands cannot contain newlines"));

	/* Does the command consist purely of whitespace? */
	if (!cmd[strspn(cmd, " \t\r\f\v")])
		return error(_("empty exec command"));

	return 0;
}


int cmd_rebase(int argc, const char **argv, const char *prefix)
{
	struct rebase_options options = REBASE_OPTIONS_INIT;
	const char *branch_name;
	int ret, flags, total_argc, in_progress = 0;
	int ok_to_skip_pre_rebase = 0;
	struct strbuf msg = STRBUF_INIT;
	struct strbuf revisions = STRBUF_INIT;
	struct strbuf buf = STRBUF_INIT;
	struct object_id merge_base;
	enum action action = ACTION_NONE;
	const char *gpg_sign = NULL;
	struct string_list exec = STRING_LIST_INIT_NODUP;
	const char *rebase_merges = NULL;
	int fork_point = -1;
	struct string_list strategy_options = STRING_LIST_INIT_NODUP;
	struct object_id squash_onto;
	char *squash_onto_name = NULL;
	int reschedule_failed_exec = -1;
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
			    N_("(DEPRECATED) try to recreate merges instead of "
			       "ignoring them"), REBASE_PRESERVE_MERGES),
		OPT_RERERE_AUTOUPDATE(&options.allow_rerere_autoupdate),
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
		OPT_BOOL(0, "reschedule-failed-exec",
			 &reschedule_failed_exec,
			 N_("automatically re-schedule any `exec` that fails")),
		OPT_END(),
	};
	int i;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_rebase_usage,
				   builtin_rebase_options);

	options.allow_empty_message = 1;
	git_config(rebase_config, &options);

	if (options.use_legacy_rebase ||
	    !git_env_bool("GIT_TEST_REBASE_USE_BUILTIN", -1))
		warning(_("the rebase.useBuiltin support has been removed!\n"
			  "See its entry in 'git help config' for details."));

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

	if (action != ACTION_NONE && total_argc != 2) {
		usage_with_options(builtin_rebase_usage,
				   builtin_rebase_options);
	}

	if (argc > 2)
		usage_with_options(builtin_rebase_usage,
				   builtin_rebase_options);

	if (options.type == REBASE_PRESERVE_MERGES)
		warning(_("git rebase --preserve-merges is deprecated. "
			  "Use --rebase-merges instead."));

	if (action != ACTION_NONE && !in_progress)
		die(_("No rebase in progress?"));
	setenv(GIT_REFLOG_ACTION_ENVIRONMENT, "rebase", 0);

	if (action == ACTION_EDIT_TODO && !is_interactive(&options))
		die(_("The --edit-todo action can only be used during "
		      "interactive rebase."));

	if (trace2_is_enabled()) {
		if (is_interactive(&options))
			trace2_cmd_mode("interactive");
		else if (exec.nr)
			trace2_cmd_mode("interactive-exec");
		else
			trace2_cmd_mode(action_names[action]);
	}

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
		if (repo_read_index(the_repository) < 0)
			die(_("could not read index"));
		refresh_index(the_repository->index, REFRESH_QUIET, NULL, NULL,
			      NULL);
		if (0 <= fd)
			repo_update_index_if_able(the_repository, &lock_file);
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
		remove_branch_state(the_repository, 0);
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
		remove_branch_state(the_repository, 0);
		ret = !!finish_rebase(&options);
		goto cleanup;
	}
	case ACTION_QUIT: {
		if (options.type == REBASE_INTERACTIVE) {
			struct replay_opts replay = REPLAY_OPTS_INIT;

			replay.action = REPLAY_INTERACTIVE_REBASE;
			ret = !!sequencer_remove_state(&replay);
		} else {
			strbuf_reset(&buf);
			strbuf_addstr(&buf, options.state_dir);
			ret = !!remove_dir_recursively(&buf, 0);
			if (ret)
				error(_("could not remove '%s'"),
				       options.state_dir);
		}
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
	case ACTION_NONE:
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

	for (i = 0; i < exec.nr; i++)
		if (check_exec_cmd(exec.items[i].string))
			exit(1);

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

	if (options.type == REBASE_MERGE)
		imply_interactive(&options, "--merge");

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

	if (reschedule_failed_exec > 0 && !is_interactive(&options))
		die(_("--reschedule-failed-exec requires "
		      "--exec or --interactive"));
	if (reschedule_failed_exec >= 0)
		options.reschedule_failed_exec = reschedule_failed_exec;

	if (options.git_am_opts.argc) {
		/* all am options except -q are compatible only with --am */
		for (i = options.git_am_opts.argc - 1; i >= 0; i--)
			if (strcmp(options.git_am_opts.argv[i], "-q"))
				break;

		if (is_interactive(&options) && i >= 0)
			die(_("cannot combine am options with either "
			      "interactive or merge options"));
	}

	if (options.signoff) {
		if (options.type == REBASE_PRESERVE_MERGES)
			die("cannot combine '--signoff' with "
			    "'--preserve-merges'");
		argv_array_push(&options.git_am_opts, "--signoff");
		options.flags |= REBASE_FORCE;
	}

	if (options.type == REBASE_PRESERVE_MERGES) {
		/*
		 * Note: incompatibility with --signoff handled in signoff block above
		 * Note: incompatibility with --interactive is just a strong warning;
		 *       git-rebase.txt caveats with "unless you know what you are doing"
		 */
		if (options.rebase_merges)
			die(_("cannot combine '--preserve-merges' with "
			      "'--rebase-merges'"));

		if (options.reschedule_failed_exec)
			die(_("error: cannot combine '--preserve-merges' with "
			      "'--reschedule-failed-exec'"));
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
		} else
			options.root_with_onto = 1;

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
			FREE_AND_NULL(options.head_name);
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

	if (repo_read_index(the_repository) < 0)
		die(_("could not read index"));

	if (options.autostash) {
		struct lock_file lock_file = LOCK_INIT;
		int fd;

		fd = hold_locked_index(&lock_file, 0);
		refresh_cache(REFRESH_QUIET);
		if (0 <= fd)
			repo_update_index_if_able(the_repository, &lock_file);
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
				repo_read_index(the_repository) < 0)
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
					       options.head_name,
					       RESET_HEAD_RUN_POST_CHECKOUT_HOOK,
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
		       RESET_HEAD_DETACH | RESET_ORIG_HEAD |
		       RESET_HEAD_RUN_POST_CHECKOUT_HOOK,
		       NULL, msg.buf))
		die(_("Could not detach HEAD"));
	strbuf_release(&msg);

	/*
	 * If the onto is a proper descendant of the tip of the branch, then
	 * we just fast-forwarded.
	 */
	strbuf_reset(&msg);
	if (oideq(&merge_base, &options.orig_head)) {
		printf(_("Fast-forwarded %s to %s.\n"),
			branch_name, options.onto_name);
		strbuf_addf(&msg, "rebase finished: %s onto %s",
			options.head_name ? options.head_name : "detached HEAD",
			oid_to_hex(&options.onto->object.oid));
		reset_head(NULL, "Fast-forwarded", options.head_name,
			   RESET_HEAD_REFS_ONLY, "HEAD", msg.buf);
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
	ret = !!run_specific_rebase(&options, action);

cleanup:
	strbuf_release(&buf);
	strbuf_release(&revisions);
	free(options.head_name);
	free(options.gpg_sign_opt);
	free(options.cmd);
	free(squash_onto_name);
	return ret;
}
