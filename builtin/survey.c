#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "config.h"
#include "parse-options.h"

static const char * const survey_usage[] = {
	N_("(EXPERIMENTAL!) git survey <options>"),
	NULL,
};

struct survey_opts {
	int verbose;
	int show_progress;
};

struct survey_context {
	struct repository *repo;

	/* Options that control what is done. */
	struct survey_opts opts;
};

static int survey_load_config_cb(const char *var, const char *value,
				 const struct config_context *cctx, void *pvoid)
{
	struct survey_context *ctx = pvoid;

	if (!strcmp(var, "survey.verbose")) {
		ctx->opts.verbose = git_config_bool(var, value);
		return 0;
	}
	if (!strcmp(var, "survey.progress")) {
		ctx->opts.show_progress = git_config_bool(var, value);
		return 0;
	}

	return git_default_config(var, value, cctx, pvoid);
}

static void survey_load_config(struct survey_context *ctx)
{
	git_config(survey_load_config_cb, ctx);
}

int cmd_survey(int argc, const char **argv, const char *prefix, struct repository *repo)
{
	static struct survey_context ctx = {
		.opts = {
			.verbose = 0,
			.show_progress = -1, /* defaults to isatty(2) */
		},
	};

	static struct option survey_options[] = {
		OPT__VERBOSE(&ctx.opts.verbose, N_("verbose output")),
		OPT_BOOL(0, "progress", &ctx.opts.show_progress, N_("show progress")),
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(survey_usage, survey_options);

	ctx.repo = repo;

	prepare_repo_settings(ctx.repo);
	survey_load_config(&ctx);

	argc = parse_options(argc, argv, prefix, survey_options, survey_usage, 0);

	if (ctx.opts.show_progress < 0)
		ctx.opts.show_progress = isatty(2);

	return 0;
}
