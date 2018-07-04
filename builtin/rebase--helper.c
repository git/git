#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "parse-options.h"
#include "sequencer.h"
#include "rebase-interactive.h"

static GIT_PATH_FUNC(path_squash_onto, "rebase-merge/squash-onto")

static const char * const builtin_rebase_helper_usage[] = {
	N_("git rebase--helper [<options>]"),
	NULL
};

int cmd_rebase__helper(int argc, const char **argv, const char *prefix)
{
	struct replay_opts opts = REPLAY_OPTS_INIT;
	unsigned flags = 0, keep_empty = 0, rebase_merges = 0, verbose = 0,
		autosquash = 0;
	int abbreviate_commands = 0, rebase_cousins = -1;
	const char *head_hash, *onto = NULL, *squash_onto = NULL, *upstream = NULL;
	struct strbuf revisions = STRBUF_INIT, shortrevisions = STRBUF_INIT;
	enum {
		CONTINUE = 1, ABORT, MAKE_SCRIPT, SHORTEN_OIDS, EXPAND_OIDS,
		CHECK_TODO_LIST, REARRANGE_SQUASH, ADD_EXEC, EDIT_TODO,
		PREPARE_BRANCH, COMPLETE_ACTION
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
			 N_("move commits thas begin with squash!/fixup!")),
		OPT__VERBOSE(&verbose, N_("be verbose")),
		OPT_CMDMODE(0, "continue", &command, N_("continue rebase"),
				CONTINUE),
		OPT_CMDMODE(0, "abort", &command, N_("abort rebase"),
				ABORT),
		OPT_CMDMODE(0, "make-script", &command,
			N_("make rebase script"), MAKE_SCRIPT),
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
		OPT_STRING(0, "onto", &onto, N_("onto"), N_("onto")),
		OPT_STRING(0, "squash-onto", &squash_onto, N_("squash-onto"),
			   N_("squash onto")),
		OPT_STRING(0, "upstream", &upstream, N_("upstream"),
			   N_("the upstream commit")),
		OPT_END()
	};

	sequencer_init_config(&opts);
	git_config_get_bool("rebase.abbreviatecommands", &abbreviate_commands);

	opts.action = REPLAY_INTERACTIVE_REBASE;
	opts.allow_ff = 1;
	opts.allow_empty = 1;
	opts.verbose = verbose;

	argc = parse_options(argc, argv, NULL, options,
			builtin_rebase_helper_usage, PARSE_OPT_KEEP_ARGV0);

	flags |= keep_empty ? TODO_LIST_KEEP_EMPTY : 0;
	flags |= abbreviate_commands ? TODO_LIST_ABBREVIATE_CMDS : 0;
	flags |= rebase_merges ? TODO_LIST_REBASE_MERGES : 0;
	flags |= rebase_cousins > 0 ? TODO_LIST_REBASE_COUSINS : 0;

	if (rebase_cousins >= 0 && !rebase_merges)
		warning(_("--[no-]rebase-cousins has no effect without "
			  "--rebase-merges"));

	if (command == MAKE_SCRIPT && !upstream && squash_onto) {
		FILE *squash_onto_file;
		int ret;

		squash_onto_file = fopen(path_squash_onto(), "w");
		if (squash_onto_file == NULL)
			return error_errno(_("could not open '%s'"), path_squash_onto());

		ret = fputs(squash_onto, squash_onto_file);
		fclose(squash_onto_file);

		if (ret < 0)
			return error_errno(_("could not write to '%s'"), path_squash_onto());
	}

	if (command == COMPLETE_ACTION || command == MAKE_SCRIPT) {
		const char *shortupstream, *shorthead;
		struct object_id orig_head;

		if (get_oid("HEAD", &orig_head))
			die(_("No HEAD?"));

		head_hash = find_unique_abbrev(&orig_head, GIT_MAX_HEXSZ);
		shorthead = find_unique_abbrev(&orig_head, DEFAULT_ABBREV);

		if (upstream) {
			struct object_id upstream_oid;

			get_oid(upstream, &upstream_oid);
			shortupstream = find_unique_abbrev(&upstream_oid, DEFAULT_ABBREV);
			strbuf_addf(&revisions, "%s...%s", upstream, head_hash);
			strbuf_addf(&shortrevisions, "%s..%s", shortupstream, shorthead);
		} else {
			strbuf_addf(&revisions, "%s...%s", onto, head_hash);
			strbuf_add(&shortrevisions, shorthead, strlen(shorthead));
		}
	}

	if (command == CONTINUE && argc == 1)
		return !!sequencer_continue(&opts);
	if (command == ABORT && argc == 1)
		return !!sequencer_remove_state(&opts);
	if (command == MAKE_SCRIPT && (argc == 1 || argc == 2)) {
		const char *restrict_rev = (argc == 2) ? argv[1] : NULL;
		const char *f_argv[3] = {argv[0], revisions.buf, restrict_rev};

		return !!sequencer_make_script(stdout, argc + 1, f_argv, flags);
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
	if (command == COMPLETE_ACTION && argc == 3)
		return !!complete_action(&opts, flags, shortrevisions.buf, argv[1], onto,
					 head_hash, argv[2], autosquash, keep_empty);

	usage_with_options(builtin_rebase_helper_usage, options);
}
