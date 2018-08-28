#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "parse-options.h"
#include "sequencer.h"
#include "rebase-interactive.h"
#include "argv-array.h"
#include "refs.h"
#include "rerere.h"
#include "alias.h"

static GIT_PATH_FUNC(path_state_dir, "rebase-merge/")
static GIT_PATH_FUNC(path_squash_onto, "rebase-merge/squash-onto")
static GIT_PATH_FUNC(path_interactive, "rebase-merge/interactive")

static int get_revision_ranges(const char *upstream, const char *onto,
			       const char **head_hash,
			       char **revisions, char **shortrevisions)
{
	const char *base_rev = upstream ? upstream : onto;
	struct object_id orig_head;

	if (get_oid("HEAD", &orig_head))
		return error(_("no HEAD?"));

	*head_hash = find_unique_abbrev(&orig_head, GIT_MAX_HEXSZ);

	if (revisions)
		*revisions = xstrfmt("%s...%s", base_rev, *head_hash);
	if (shortrevisions) {
		const char *shorthead;

		shorthead = find_unique_abbrev(&orig_head, DEFAULT_ABBREV);

		if (upstream) {
			const char *shortrev;
			struct object_id rev_oid;

			get_oid(base_rev, &rev_oid);
			shortrev = find_unique_abbrev(&rev_oid, DEFAULT_ABBREV);

			*shortrevisions = xstrfmt("%s..%s", shortrev, shorthead);
		} else
			*shortrevisions = xstrdup(shorthead);
	}

	return 0;
}

static int init_basic_state(struct replay_opts *opts, const char *head_name,
			    const char *onto, const char *orig_head)
{
	FILE *interactive;

	if (!is_directory(path_state_dir()) && mkdir_in_gitdir(path_state_dir()))
		return error_errno(_("could not create temporary %s"), path_state_dir());

	delete_reflog("REBASE_HEAD");

	interactive = fopen(path_interactive(), "w");
	if (!interactive)
		return error_errno(_("could not mark as interactive"));
	fclose(interactive);

	return write_basic_state(opts, head_name, onto, orig_head);
}

static const char * const builtin_rebase_helper_usage[] = {
	N_("git rebase--helper [<options>]"),
	NULL
};

int cmd_rebase__helper(int argc, const char **argv, const char *prefix)
{
	struct replay_opts opts = REPLAY_OPTS_INIT;
	unsigned flags = 0, keep_empty = 0, rebase_merges = 0, autosquash = 0;
	int abbreviate_commands = 0, rebase_cousins = -1, ret;
	const char *head_hash = NULL, *onto = NULL, *restrict_revision = NULL,
		*squash_onto = NULL, *upstream = NULL, *head_name = NULL;
	char *raw_strategies = NULL;
	enum {
		CONTINUE = 1, ABORT, MAKE_SCRIPT, SHORTEN_OIDS, EXPAND_OIDS,
		CHECK_TODO_LIST, REARRANGE_SQUASH, ADD_EXEC, EDIT_TODO, PREPARE_BRANCH,
		COMPLETE_ACTION, INIT_BASIC_STATE
	} command = 0;
	struct option options[] = {
		OPT_BOOL(0, "ff", &opts.allow_ff, N_("allow fast-forward")),
		OPT_BOOL(0, "keep-empty", &keep_empty, N_("keep empty commits")),
		OPT_BOOL(0, "allow-empty-message", &opts.allow_empty_message,
			N_("allow commits with empty messages")),
		OPT_BOOL(0, "rebase-merges", &rebase_merges, N_("rebase merge commits")),
		OPT_BOOL(0, "rebase-cousins", &rebase_cousins,
			 N_("keep original branch points of cousins")),
		OPT_BOOL(0, "autosquash", &autosquash,
			 N_("move commits that begin with squash!/fixup!")),
		OPT_BOOL(0, "signoff", &opts.signoff, N_("sign commits")),
		OPT__VERBOSE(&opts.verbose, N_("be verbose")),
		OPT_CMDMODE(0, "continue", &command, N_("continue rebase"),
				CONTINUE),
		OPT_CMDMODE(0, "abort", &command, N_("abort rebase"),
				ABORT),
		OPT_CMDMODE(0, "make-script", &command,
			N_("make rebase script"), MAKE_SCRIPT),
		OPT_CMDMODE(0, "shorten-ids", &command,
			N_("shorten commit ids in the todo list"), SHORTEN_OIDS),
		OPT_CMDMODE(0, "expand-ids", &command,
			N_("expand commit ids in the todo list"), EXPAND_OIDS),
		OPT_CMDMODE(0, "check-todo-list", &command,
			N_("check the todo list"), CHECK_TODO_LIST),
		OPT_CMDMODE(0, "rearrange-squash", &command,
			N_("rearrange fixup/squash lines"), REARRANGE_SQUASH),
		OPT_CMDMODE(0, "add-exec-commands", &command,
			N_("insert exec commands in todo list"), ADD_EXEC),
		OPT_CMDMODE(0, "edit-todo", &command,
			    N_("edit the todo list during an interactive rebase"),
			    EDIT_TODO),
		OPT_CMDMODE(0, "prepare-branch", &command,
			    N_("prepare the branch to be rebased"), PREPARE_BRANCH),
		OPT_CMDMODE(0, "complete-action", &command,
			    N_("complete the action"), COMPLETE_ACTION),
		OPT_CMDMODE(0, "init-basic-state", &command,
			    N_("initialise the rebase state"), INIT_BASIC_STATE),
		OPT_STRING(0, "onto", &onto, N_("onto"), N_("onto")),
		OPT_STRING(0, "restrict-revision", &restrict_revision,
			   N_("restrict-revision"), N_("restrict revision")),
		OPT_STRING(0, "squash-onto", &squash_onto, N_("squash-onto"),
			   N_("squash onto")),
		OPT_STRING(0, "upstream", &upstream, N_("upstream"),
			   N_("the upstream commit")),
		OPT_STRING(0, "head-name", &head_name, N_("head-name"), N_("head name")),
		OPT_STRING('S', "gpg-sign", &opts.gpg_sign, N_("gpg-sign"),
			   N_("GPG-sign commits")),
		OPT_STRING(0, "strategy", &opts.strategy, N_("strategy"),
			   N_("rebase strategy")),
		OPT_STRING(0, "strategy-opts", &raw_strategies, N_("strategy-opts"),
			   N_("strategy options")),
		OPT_RERERE_AUTOUPDATE(&opts.allow_rerere_auto),
		OPT_END()
	};

	sequencer_init_config(&opts);
	git_config_get_bool("rebase.abbreviatecommands", &abbreviate_commands);

	opts.action = REPLAY_INTERACTIVE_REBASE;
	opts.allow_ff = 1;
	opts.allow_empty = 1;

	argc = parse_options(argc, argv, NULL, options,
			builtin_rebase_helper_usage, PARSE_OPT_KEEP_ARGV0);

	flags |= keep_empty ? TODO_LIST_KEEP_EMPTY : 0;
	flags |= abbreviate_commands ? TODO_LIST_ABBREVIATE_CMDS : 0;
	flags |= rebase_merges ? TODO_LIST_REBASE_MERGES : 0;
	flags |= rebase_cousins > 0 ? TODO_LIST_REBASE_COUSINS : 0;
	flags |= command == SHORTEN_OIDS ? TODO_LIST_SHORTEN_IDS : 0;

	if (rebase_cousins >= 0 && !rebase_merges)
		warning(_("--[no-]rebase-cousins has no effect without "
			  "--rebase-merges"));

	if (command == CONTINUE && argc == 1)
		return !!sequencer_continue(&opts);
	if (command == ABORT && argc == 1)
		return !!sequencer_remove_state(&opts);
	if (command == MAKE_SCRIPT && argc == 1) {
		char *revisions = NULL;
		struct argv_array make_script_args = ARGV_ARRAY_INIT;

		if (!upstream && squash_onto)
			write_file(path_squash_onto(), "%s\n", squash_onto);

		ret = get_revision_ranges(upstream, onto, &head_hash, &revisions, NULL);
		if (ret)
			return ret;

		argv_array_pushl(&make_script_args, "", revisions, NULL);
		if (restrict_revision)
			argv_array_push(&make_script_args, restrict_revision);

		ret = sequencer_make_script(stdout,
					    make_script_args.argc, make_script_args.argv,
					    flags);

		free(revisions);
		argv_array_clear(&make_script_args);

		return !!ret;
	}
	if ((command == SHORTEN_OIDS || command == EXPAND_OIDS) && argc == 1)
		return !!transform_todos(flags);
	if (command == CHECK_TODO_LIST && argc == 1)
		return !!check_todo_list();
	if (command == REARRANGE_SQUASH && argc == 1)
		return !!rearrange_squash();
	if (command == ADD_EXEC && argc == 2)
		return !!sequencer_add_exec_commands(argv[1]);
	if (command == EDIT_TODO && argc == 1)
		return !!edit_todo_list(flags);
	if (command == PREPARE_BRANCH && argc == 2)
		return !!prepare_branch_to_be_rebased(&opts, argv[1]);
	if (command == COMPLETE_ACTION && argc == 3) {
		char *shortrevisions = NULL;

		ret = get_revision_ranges(upstream, onto, &head_hash, NULL, &shortrevisions);
		if (ret)
			return ret;

		ret = complete_action(&opts, flags, shortrevisions, argv[1], onto,
				      head_hash, argv[2], autosquash);

		free(shortrevisions);
		return !!ret;
	}
	if (command == INIT_BASIC_STATE) {
		if (raw_strategies)
			parse_strategy_opts(&opts, raw_strategies);

		ret = get_revision_ranges(upstream, onto, &head_hash, NULL, NULL);
		if (ret)
			return ret;

		return !!init_basic_state(&opts, head_name, onto, head_hash);
	}

	usage_with_options(builtin_rebase_helper_usage, options);
}
