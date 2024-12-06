#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "builtin.h"
#include "parse-options.h"
#include "diff.h"
#include "gettext.h"
#include "revision.h"
#include "rerere.h"
#include "sequencer.h"
#include "branch.h"

/*
 * This implements the builtins revert and cherry-pick.
 *
 * Copyright (c) 2007 Johannes E. Schindelin
 *
 * Based on git-revert.sh, which is
 *
 * Copyright (c) 2005 Linus Torvalds
 * Copyright (c) 2005 Junio C Hamano
 */

static const char * const revert_usage[] = {
	N_("git revert [--[no-]edit] [-n] [-m <parent-number>] [-s] [-S[<keyid>]] <commit>..."),
	N_("git revert (--continue | --skip | --abort | --quit)"),
	NULL
};

static const char * const cherry_pick_usage[] = {
	N_("git cherry-pick [--edit] [-n] [-m <parent-number>] [-s] [-x] [--ff]\n"
	   "                [-S[<keyid>]] <commit>..."),
	N_("git cherry-pick (--continue | --skip | --abort | --quit)"),
	NULL
};

static const char *action_name(const struct replay_opts *opts)
{
	return opts->action == REPLAY_REVERT ? "revert" : "cherry-pick";
}

static const char * const *revert_or_cherry_pick_usage(struct replay_opts *opts)
{
	return opts->action == REPLAY_REVERT ? revert_usage : cherry_pick_usage;
}

enum empty_action {
	EMPTY_COMMIT_UNSPECIFIED = -1,
	STOP_ON_EMPTY_COMMIT,      /* output errors and stop in the middle of a cherry-pick */
	DROP_EMPTY_COMMIT,         /* skip with a notice message */
	KEEP_EMPTY_COMMIT,         /* keep recording as empty commits */
};

static int parse_opt_empty(const struct option *opt, const char *arg, int unset)
{
	int *opt_value = opt->value;

	BUG_ON_OPT_NEG(unset);

	if (!strcmp(arg, "stop"))
		*opt_value = STOP_ON_EMPTY_COMMIT;
	else if (!strcmp(arg, "drop"))
		*opt_value = DROP_EMPTY_COMMIT;
	else if (!strcmp(arg, "keep"))
		*opt_value = KEEP_EMPTY_COMMIT;
	else
		return error(_("invalid value for '%s': '%s'"), "--empty", arg);

	return 0;
}

static int option_parse_m(const struct option *opt,
			  const char *arg, int unset)
{
	struct replay_opts *replay = opt->value;
	char *end;

	if (unset) {
		replay->mainline = 0;
		return 0;
	}

	replay->mainline = strtol(arg, &end, 10);
	if (*end || replay->mainline <= 0)
		return error(_("option `%s' expects a number greater than zero"),
			     opt->long_name);

	return 0;
}

LAST_ARG_MUST_BE_NULL
static void verify_opt_compatible(const char *me, const char *base_opt, ...)
{
	const char *this_opt;
	va_list ap;

	va_start(ap, base_opt);
	while ((this_opt = va_arg(ap, const char *))) {
		if (va_arg(ap, int))
			break;
	}
	va_end(ap);

	if (this_opt)
		die(_("%s: %s cannot be used with %s"), me, this_opt, base_opt);
}

static int run_sequencer(int argc, const char **argv, const char *prefix,
			 struct replay_opts *opts)
{
	const char * const * usage_str = revert_or_cherry_pick_usage(opts);
	const char *me = action_name(opts);
	const char *cleanup_arg = NULL;
	const char sentinel_value;
	const char *strategy = &sentinel_value;
	const char *gpg_sign = &sentinel_value;
	enum empty_action empty_opt = EMPTY_COMMIT_UNSPECIFIED;
	int cmd = 0;
	struct option base_options[] = {
		OPT_CMDMODE(0, "quit", &cmd, N_("end revert or cherry-pick sequence"), 'q'),
		OPT_CMDMODE(0, "continue", &cmd, N_("resume revert or cherry-pick sequence"), 'c'),
		OPT_CMDMODE(0, "abort", &cmd, N_("cancel revert or cherry-pick sequence"), 'a'),
		OPT_CMDMODE(0, "skip", &cmd, N_("skip current commit and continue"), 's'),
		OPT_CLEANUP(&cleanup_arg),
		OPT_BOOL('n', "no-commit", &opts->no_commit, N_("don't automatically commit")),
		OPT_BOOL('e', "edit", &opts->edit, N_("edit the commit message")),
		OPT_NOOP_NOARG('r', NULL),
		OPT_BOOL('s', "signoff", &opts->signoff, N_("add a Signed-off-by trailer")),
		OPT_CALLBACK('m', "mainline", opts, N_("parent-number"),
			     N_("select mainline parent"), option_parse_m),
		OPT_RERERE_AUTOUPDATE(&opts->allow_rerere_auto),
		OPT_STRING(0, "strategy", &strategy, N_("strategy"), N_("merge strategy")),
		OPT_STRVEC('X', "strategy-option", &opts->xopts, N_("option"),
			N_("option for merge strategy")),
		{ OPTION_STRING, 'S', "gpg-sign", &gpg_sign, N_("key-id"),
		  N_("GPG sign commit"), PARSE_OPT_OPTARG, NULL, (intptr_t) "" },
		OPT_END()
	};
	struct option *options = base_options;

	if (opts->action == REPLAY_PICK) {
		struct option cp_extra[] = {
			OPT_BOOL('x', NULL, &opts->record_origin, N_("append commit name")),
			OPT_BOOL(0, "ff", &opts->allow_ff, N_("allow fast-forward")),
			OPT_BOOL(0, "allow-empty", &opts->allow_empty, N_("preserve initially empty commits")),
			OPT_BOOL(0, "allow-empty-message", &opts->allow_empty_message, N_("allow commits with empty messages")),
			OPT_BOOL(0, "keep-redundant-commits", &opts->keep_redundant_commits, N_("deprecated: use --empty=keep instead")),
			OPT_CALLBACK_F(0, "empty", &empty_opt, "(stop|drop|keep)",
				       N_("how to handle commits that become empty"),
				       PARSE_OPT_NONEG, parse_opt_empty),
			OPT_END(),
		};
		options = parse_options_concat(options, cp_extra);
	} else if (opts->action == REPLAY_REVERT) {
		struct option cp_extra[] = {
			OPT_BOOL(0, "reference", &opts->commit_use_reference,
				 N_("use the 'reference' format to refer to commits")),
			OPT_END(),
		};
		options = parse_options_concat(options, cp_extra);
	}

	argc = parse_options(argc, argv, prefix, options, usage_str,
			PARSE_OPT_KEEP_ARGV0 |
			PARSE_OPT_KEEP_UNKNOWN_OPT);

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;

	if (opts->action == REPLAY_PICK) {
		opts->drop_redundant_commits = (empty_opt == DROP_EMPTY_COMMIT);
		opts->keep_redundant_commits = opts->keep_redundant_commits || (empty_opt == KEEP_EMPTY_COMMIT);
	}

	/* implies allow_empty */
	if (opts->keep_redundant_commits)
		opts->allow_empty = 1;

	if (cleanup_arg) {
		opts->default_msg_cleanup = get_cleanup_mode(cleanup_arg, 1);
		opts->explicit_cleanup = 1;
	}

	/* Check for incompatible command line arguments */
	if (cmd) {
		const char *this_operation;
		if (cmd == 'q')
			this_operation = "--quit";
		else if (cmd == 'c')
			this_operation = "--continue";
		else if (cmd == 's')
			this_operation = "--skip";
		else {
			assert(cmd == 'a');
			this_operation = "--abort";
		}

		verify_opt_compatible(me, this_operation,
				"--no-commit", opts->no_commit,
				"--signoff", opts->signoff,
				"--mainline", opts->mainline,
				"--strategy", opts->strategy ? 1 : 0,
				"--strategy-option", opts->xopts.nr ? 1 : 0,
				"-x", opts->record_origin,
				"--ff", opts->allow_ff,
				"--rerere-autoupdate", opts->allow_rerere_auto == RERERE_AUTOUPDATE,
				"--no-rerere-autoupdate", opts->allow_rerere_auto == RERERE_NOAUTOUPDATE,
				"--keep-redundant-commits", opts->keep_redundant_commits,
				"--empty", empty_opt != EMPTY_COMMIT_UNSPECIFIED,
				NULL);
	}

	if (!opts->strategy && opts->default_strategy) {
		opts->strategy = opts->default_strategy;
		opts->default_strategy = NULL;
	}

	if (opts->allow_ff)
		verify_opt_compatible(me, "--ff",
				"--signoff", opts->signoff,
				"--no-commit", opts->no_commit,
				"-x", opts->record_origin,
				"--edit", opts->edit > 0,
				NULL);

	if (cmd) {
		opts->revs = NULL;
	} else {
		struct setup_revision_opt s_r_opt;
		opts->revs = xmalloc(sizeof(*opts->revs));
		repo_init_revisions(the_repository, opts->revs, NULL);
		opts->revs->no_walk = 1;
		opts->revs->unsorted_input = 1;
		if (argc < 2)
			usage_with_options(usage_str, options);
		if (!strcmp(argv[1], "-"))
			argv[1] = "@{-1}";
		memset(&s_r_opt, 0, sizeof(s_r_opt));
		s_r_opt.assume_dashdash = 1;
		argc = setup_revisions(argc, argv, opts->revs, &s_r_opt);
	}

	if (argc > 1)
		usage_with_options(usage_str, options);

	/* These option values will be free()d */
	if (gpg_sign != &sentinel_value) {
		free(opts->gpg_sign);
		opts->gpg_sign = xstrdup_or_null(gpg_sign);
	}
	if (strategy != &sentinel_value) {
		free(opts->strategy);
		opts->strategy = xstrdup_or_null(strategy);
	}
	if (!opts->strategy && getenv("GIT_TEST_MERGE_ALGORITHM"))
		opts->strategy = xstrdup(getenv("GIT_TEST_MERGE_ALGORITHM"));
	free(options);

	if (cmd == 'q') {
		int ret = sequencer_remove_state(opts);
		if (!ret)
			remove_branch_state(the_repository, 0);
		return ret;
	}
	if (cmd == 'c')
		return sequencer_continue(the_repository, opts);
	if (cmd == 'a')
		return sequencer_rollback(the_repository, opts);
	if (cmd == 's')
		return sequencer_skip(the_repository, opts);
	return sequencer_pick_revisions(the_repository, opts);
}

int cmd_revert(int argc,
	       const char **argv,
	       const char *prefix,
	       struct repository *repo UNUSED)
{
	struct replay_opts opts = REPLAY_OPTS_INIT;
	int res;

	opts.action = REPLAY_REVERT;
	sequencer_init_config(&opts);
	res = run_sequencer(argc, argv, prefix, &opts);
	if (res < 0)
		die(_("revert failed"));
	replay_opts_release(&opts);
	return res;
}

int cmd_cherry_pick(int argc,
const char **argv,
const char *prefix,
struct repository *repo UNUSED)
{
	struct replay_opts opts = REPLAY_OPTS_INIT;
	int res;

	opts.action = REPLAY_PICK;
	sequencer_init_config(&opts);
	res = run_sequencer(argc, argv, prefix, &opts);
	if (res < 0)
		die(_("cherry-pick failed"));
	replay_opts_release(&opts);
	return res;
}
