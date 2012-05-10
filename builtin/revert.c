#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "diff.h"
#include "revision.h"
#include "rerere.h"
#include "dir.h"
#include "sequencer.h"

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
	"git revert [options] <commit-ish>",
	"git revert <subcommand>",
	NULL
};

static const char * const cherry_pick_usage[] = {
	"git cherry-pick [options] <commit-ish>",
	"git cherry-pick <subcommand>",
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

static int option_parse_x(const struct option *opt,
			  const char *arg, int unset)
{
	struct replay_opts **opts_ptr = opt->value;
	struct replay_opts *opts = *opts_ptr;

	if (unset)
		return 0;

	ALLOC_GROW(opts->xopts, opts->xopts_nr + 1, opts->xopts_alloc);
	opts->xopts[opts->xopts_nr++] = xstrdup(arg);
	return 0;
}

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

static void verify_opt_mutually_compatible(const char *me, ...)
{
	const char *opt1, *opt2 = NULL;
	va_list ap;

	va_start(ap, me);
	while ((opt1 = va_arg(ap, const char *))) {
		if (va_arg(ap, int))
			break;
	}
	if (opt1) {
		while ((opt2 = va_arg(ap, const char *))) {
			if (va_arg(ap, int))
				break;
		}
	}
	va_end(ap);

	if (opt1 && opt2)
		die(_("%s: %s cannot be used with %s"),	me, opt1, opt2);
}

static void parse_args(int argc, const char **argv, struct replay_opts *opts)
{
	const char * const * usage_str = revert_or_cherry_pick_usage(opts);
	const char *me = action_name(opts);
	int remove_state = 0;
	int contin = 0;
	int rollback = 0;
	struct option options[] = {
		OPT_BOOLEAN(0, "quit", &remove_state, "end revert or cherry-pick sequence"),
		OPT_BOOLEAN(0, "continue", &contin, "resume revert or cherry-pick sequence"),
		OPT_BOOLEAN(0, "abort", &rollback, "cancel revert or cherry-pick sequence"),
		OPT_BOOLEAN('n', "no-commit", &opts->no_commit, "don't automatically commit"),
		OPT_BOOLEAN('e', "edit", &opts->edit, "edit the commit message"),
		OPT_NOOP_NOARG('r', NULL),
		OPT_BOOLEAN('s', "signoff", &opts->signoff, "add Signed-off-by:"),
		OPT_INTEGER('m', "mainline", &opts->mainline, "parent number"),
		OPT_RERERE_AUTOUPDATE(&opts->allow_rerere_auto),
		OPT_STRING(0, "strategy", &opts->strategy, "strategy", "merge strategy"),
		OPT_CALLBACK('X', "strategy-option", &opts, "option",
			"option for merge strategy", option_parse_x),
		OPT_END(),
		OPT_END(),
		OPT_END(),
	};

	if (opts->action == REPLAY_PICK) {
		struct option cp_extra[] = {
			OPT_BOOLEAN('x', NULL, &opts->record_origin, "append commit name"),
			OPT_BOOLEAN(0, "ff", &opts->allow_ff, "allow fast-forward"),
			OPT_END(),
		};
		if (parse_options_concat(options, ARRAY_SIZE(options), cp_extra))
			die(_("program error"));
	}

	argc = parse_options(argc, argv, NULL, options, usage_str,
			PARSE_OPT_KEEP_ARGV0 |
			PARSE_OPT_KEEP_UNKNOWN);

	/* Check for incompatible subcommands */
	verify_opt_mutually_compatible(me,
				"--quit", remove_state,
				"--continue", contin,
				"--abort", rollback,
				NULL);

	/* Set the subcommand */
	if (remove_state)
		opts->subcommand = REPLAY_REMOVE_STATE;
	else if (contin)
		opts->subcommand = REPLAY_CONTINUE;
	else if (rollback)
		opts->subcommand = REPLAY_ROLLBACK;
	else
		opts->subcommand = REPLAY_NONE;

	/* Check for incompatible command line arguments */
	if (opts->subcommand != REPLAY_NONE) {
		char *this_operation;
		if (opts->subcommand == REPLAY_REMOVE_STATE)
			this_operation = "--quit";
		else if (opts->subcommand == REPLAY_CONTINUE)
			this_operation = "--continue";
		else {
			assert(opts->subcommand == REPLAY_ROLLBACK);
			this_operation = "--abort";
		}

		verify_opt_compatible(me, this_operation,
				"--no-commit", opts->no_commit,
				"--signoff", opts->signoff,
				"--mainline", opts->mainline,
				"--strategy", opts->strategy ? 1 : 0,
				"--strategy-option", opts->xopts ? 1 : 0,
				"-x", opts->record_origin,
				"--ff", opts->allow_ff,
				NULL);
	}

	if (opts->allow_ff)
		verify_opt_compatible(me, "--ff",
				"--signoff", opts->signoff,
				"--no-commit", opts->no_commit,
				"-x", opts->record_origin,
				"--edit", opts->edit,
				NULL);

	if (opts->subcommand != REPLAY_NONE) {
		opts->revs = NULL;
	} else {
		struct setup_revision_opt s_r_opt;
		opts->revs = xmalloc(sizeof(*opts->revs));
		init_revisions(opts->revs, NULL);
		opts->revs->no_walk = 1;
		if (argc < 2)
			usage_with_options(usage_str, options);
		memset(&s_r_opt, 0, sizeof(s_r_opt));
		s_r_opt.assume_dashdash = 1;
		argc = setup_revisions(argc, argv, opts->revs, &s_r_opt);
	}

	if (argc > 1)
		usage_with_options(usage_str, options);
}

int cmd_revert(int argc, const char **argv, const char *prefix)
{
	struct replay_opts opts;
	int res;

	memset(&opts, 0, sizeof(opts));
	if (isatty(0))
		opts.edit = 1;
	opts.action = REPLAY_REVERT;
	git_config(git_default_config, NULL);
	parse_args(argc, argv, &opts);
	res = sequencer_pick_revisions(&opts);
	if (res < 0)
		die(_("revert failed"));
	return res;
}

int cmd_cherry_pick(int argc, const char **argv, const char *prefix)
{
	struct replay_opts opts;
	int res;

	memset(&opts, 0, sizeof(opts));
	opts.action = REPLAY_PICK;
	git_config(git_default_config, NULL);
	parse_args(argc, argv, &opts);
	res = sequencer_pick_revisions(&opts);
	if (res < 0)
		die(_("cherry-pick failed"));
	return res;
}
