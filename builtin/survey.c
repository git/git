#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "color.h"
#include "exec-cmd.h"
#include "gettext.h"
#include "parse-options.h"
#include "strvec.h"

static const char * const survey_usage[] = {
	N_("(DEPRECATED!) git survey <options>"),
	NULL,
};

/*
 * `git survey` has been superseded by `git repo structure`. To keep
 * older callers working while the migration completes, accept the
 * `git survey` command line, translate the options into the
 * equivalent `git repo structure` invocation, and re-exec.
 */
int cmd_survey(int argc, const char **argv, const char *prefix,
	       struct repository *repo UNUSED)
{
	int verbose = 0;
	int show_progress = -1;
	int top_nr = 10;
	int want_all_refs = -1;
	int want_branches = -1;
	int want_tags = -1;
	int want_remotes = -1;
	int want_detached = -1;
	int want_other = -1;
	struct strvec child_argv = STRVEC_INIT;
	struct option options[] = {
		OPT__VERBOSE(&verbose, N_("verbose output (ignored)")),
		OPT_BOOL(0, "progress", &show_progress, N_("show progress")),
		OPT_INTEGER('n', "top", &top_nr,
			    N_("number of entries to include in "
			       "detail tables")),
		OPT_BOOL_F(0, "all-refs", &want_all_refs,
			   N_("include all refs"), PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "branches", &want_branches,
			   N_("include branches"), PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "tags", &want_tags,
			   N_("include tags"), PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "remotes", &want_remotes,
			   N_("include remote refs"), PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "detached", &want_detached,
			   N_("include detached HEAD (ignored)"),
			   PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "other", &want_other,
			   N_("include notes and stashes"), PARSE_OPT_NONEG),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, survey_usage, 0);
	if (top_nr < 0)
		die(_("--top=<n> must be non-negative"));
	if (argc)
		usage(_("'git survey' takes no positional arguments"));

	warning(_("'git survey' is deprecated; "
		  "use 'git repo structure' instead"));
	if (verbose)
		warning(_("--verbose is ignored by 'git repo structure'"));
	if (want_detached != -1)
		warning(_("--detached is ignored by 'git repo structure'"));

	strvec_pushl(&child_argv, "repo", "structure", NULL);
	if (show_progress == 1)
		strvec_push(&child_argv, "--progress");
	else if (show_progress == 0)
		strvec_push(&child_argv, "--no-progress");
	strvec_pushf(&child_argv, "--top=%d", top_nr);

	/*
	 * Survey's default ref scope is branches+tags+remotes (not "other").
	 * `--all-refs` widens to literally everything; the per-kind flags
	 * select specific subsets. `git repo structure` defaults to all
	 * refs and accepts a repeatable --ref-filter=<pattern>, so the
	 * translation is straightforward.
	 */
	if (want_all_refs != 1) {
		int branches = want_branches == 1;
		int tags = want_tags == 1;
		int remotes = want_remotes == 1;
		int other = want_other == 1;

		if (!branches && !tags && !remotes && !other)
			branches = tags = remotes = 1;

		if (branches)
			strvec_push(&child_argv,
				    "--ref-filter=refs/heads/*");
		if (tags)
			strvec_push(&child_argv,
				    "--ref-filter=refs/tags/*");
		if (remotes)
			strvec_push(&child_argv,
				    "--ref-filter=refs/remotes/*");
		if (other) {
			strvec_push(&child_argv,
				    "--ref-filter=refs/notes/*");
			strvec_push(&child_argv,
				    "--ref-filter=refs/stash");
		}
	}

	execv_git_cmd(child_argv.v);
	/* unreachable: execv_git_cmd dies on failure */
	strvec_clear(&child_argv);
	return 1;
}
