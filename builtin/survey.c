#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "config.h"
#include "object.h"
#include "object-store-ll.h"
#include "parse-options.h"
#include "progress.h"
#include "ref-filter.h"
#include "strvec.h"
#include "trace2.h"

static const char * const survey_usage[] = {
	N_("(EXPERIMENTAL!) git survey <options>"),
	NULL,
};

struct survey_refs_wanted {
	int want_all_refs; /* special override */

	int want_branches;
	int want_tags;
	int want_remotes;
	int want_detached;
	int want_other; /* see FILTER_REFS_OTHERS -- refs/notes/, refs/stash/ */
};

static struct survey_refs_wanted default_ref_options = {
	.want_all_refs = 1,
};

struct survey_opts {
	int verbose;
	int show_progress;
	struct survey_refs_wanted refs;
};

struct survey_report_ref_summary {
	size_t refs_nr;
	size_t branches_nr;
	size_t remote_refs_nr;
	size_t tags_nr;
	size_t tags_annotated_nr;
	size_t others_nr;
	size_t unknown_nr;
};

/**
 * This struct contains all of the information that needs to be printed
 * at the end of the exploration of the repository and its references.
 */
struct survey_report {
	struct survey_report_ref_summary refs;
};

struct survey_context {
	struct repository *repo;

	/* Options that control what is done. */
	struct survey_opts opts;

	/* Info for output only. */
	struct survey_report report;

	/*
	 * The rest of the members are about enabling the activity
	 * of the 'git survey' command, including ref listings, object
	 * pointers, and progress.
	 */

	struct progress *progress;
	size_t progress_nr;
	size_t progress_total;

	struct strvec refs;
};

static void clear_survey_context(struct survey_context *ctx)
{
	strvec_clear(&ctx->refs);
}

/*
 * After parsing the command line arguments, figure out which refs we
 * should scan.
 *
 * If ANY were given in positive sense, then we ONLY include them and
 * do not use the builtin values.
 */
static void fixup_refs_wanted(struct survey_context *ctx)
{
	struct survey_refs_wanted *rw = &ctx->opts.refs;

	/*
	 * `--all-refs` overrides and enables everything.
	 */
	if (rw->want_all_refs == 1) {
		rw->want_branches = 1;
		rw->want_tags = 1;
		rw->want_remotes = 1;
		rw->want_detached = 1;
		rw->want_other = 1;
		return;
	}

	/*
	 * If none of the `--<ref-type>` were given, we assume all
	 * of the builtin unspecified values.
	 */
	if (rw->want_branches == -1 &&
	    rw->want_tags == -1 &&
	    rw->want_remotes == -1 &&
	    rw->want_detached == -1 &&
	    rw->want_other == -1) {
		*rw = default_ref_options;
		return;
	}

	/*
	 * Since we only allow positive boolean values on the command
	 * line, we will only have true values where they specified
	 * a `--<ref-type>`.
	 *
	 * So anything that still has an unspecified value should be
	 * set to false.
	 */
	if (rw->want_branches == -1)
		rw->want_branches = 0;
	if (rw->want_tags == -1)
		rw->want_tags = 0;
	if (rw->want_remotes == -1)
		rw->want_remotes = 0;
	if (rw->want_detached == -1)
		rw->want_detached = 0;
	if (rw->want_other == -1)
		rw->want_other = 0;
}

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

static void do_load_refs(struct survey_context *ctx,
			 struct ref_array *ref_array)
{
	struct ref_filter filter = REF_FILTER_INIT;
	struct ref_sorting *sorting;
	struct string_list sorting_options = STRING_LIST_INIT_DUP;

	string_list_append(&sorting_options, "objectname");
	sorting = ref_sorting_options(&sorting_options);

	if (ctx->opts.refs.want_detached)
		strvec_push(&ctx->refs, "HEAD");

	if (ctx->opts.refs.want_all_refs) {
		strvec_push(&ctx->refs, "refs/");
	} else {
		if (ctx->opts.refs.want_branches)
			strvec_push(&ctx->refs, "refs/heads/");
		if (ctx->opts.refs.want_tags)
			strvec_push(&ctx->refs, "refs/tags/");
		if (ctx->opts.refs.want_remotes)
			strvec_push(&ctx->refs, "refs/remotes/");
		if (ctx->opts.refs.want_other) {
			strvec_push(&ctx->refs, "refs/notes/");
			strvec_push(&ctx->refs, "refs/stash/");
		}
	}

	filter.name_patterns = ctx->refs.v;
	filter.ignore_case = 0;
	filter.match_as_path = 1;

	if (ctx->opts.show_progress) {
		ctx->progress_total = 0;
		ctx->progress = start_progress(_("Scanning refs..."), 0);
	}

	filter_refs(ref_array, &filter, FILTER_REFS_KIND_MASK);

	if (ctx->opts.show_progress) {
		ctx->progress_total = ref_array->nr;
		display_progress(ctx->progress, ctx->progress_total);
	}

	ref_array_sort(sorting, ref_array);

	stop_progress(&ctx->progress);
	ref_filter_clear(&filter);
	ref_sorting_release(sorting);
}

/*
 * The REFS phase:
 *
 * Load the set of requested refs and assess them for scalablity problems.
 * Use that set to start a treewalk to all reachable objects and assess
 * them.
 *
 * This data will give us insights into the repository itself (the number
 * of refs, the size and shape of the DAG, the number and size of the
 * objects).
 *
 * Theoretically, this data is independent of the on-disk representation
 * (e.g. independent of packing concerns).
 */
static void survey_phase_refs(struct survey_context *ctx)
{
	struct ref_array ref_array = { 0 };

	trace2_region_enter("survey", "phase/refs", ctx->repo);
	do_load_refs(ctx, &ref_array);

	ctx->report.refs.refs_nr = ref_array.nr;
	for (size_t i = 0; i < ref_array.nr; i++) {
		unsigned long size;
		struct ref_array_item *item = ref_array.items[i];

		switch (item->kind) {
		case FILTER_REFS_TAGS:
			ctx->report.refs.tags_nr++;
			if (oid_object_info(ctx->repo,
					    &item->objectname,
					    &size) == OBJ_TAG)
				ctx->report.refs.tags_annotated_nr++;
			break;

		case FILTER_REFS_BRANCHES:
			ctx->report.refs.branches_nr++;
			break;

		case FILTER_REFS_REMOTES:
			ctx->report.refs.remote_refs_nr++;
			break;

		case FILTER_REFS_OTHERS:
			ctx->report.refs.others_nr++;
			break;

		default:
			ctx->report.refs.unknown_nr++;
			break;
		}
	}

	trace2_region_leave("survey", "phase/refs", ctx->repo);

	ref_array_clear(&ref_array);
}

int cmd_survey(int argc, const char **argv, const char *prefix, struct repository *repo)
{
	static struct survey_context ctx = {
		.opts = {
			.verbose = 0,
			.show_progress = -1, /* defaults to isatty(2) */

			.refs.want_all_refs = -1,

			.refs.want_branches = -1, /* default these to undefined */
			.refs.want_tags = -1,
			.refs.want_remotes = -1,
			.refs.want_detached = -1,
			.refs.want_other = -1,
		},
		.refs = STRVEC_INIT,
	};

	static struct option survey_options[] = {
		OPT__VERBOSE(&ctx.opts.verbose, N_("verbose output")),
		OPT_BOOL(0, "progress", &ctx.opts.show_progress, N_("show progress")),

		OPT_BOOL_F(0, "all-refs", &ctx.opts.refs.want_all_refs, N_("include all refs"),          PARSE_OPT_NONEG),

		OPT_BOOL_F(0, "branches", &ctx.opts.refs.want_branches, N_("include branches"),          PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "tags",     &ctx.opts.refs.want_tags,     N_("include tags"),              PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "remotes",  &ctx.opts.refs.want_remotes,  N_("include all remotes refs"),  PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "detached", &ctx.opts.refs.want_detached, N_("include detached HEAD"),     PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "other",    &ctx.opts.refs.want_other,    N_("include notes and stashes"), PARSE_OPT_NONEG),

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

	fixup_refs_wanted(&ctx);

	survey_phase_refs(&ctx);

	clear_survey_context(&ctx);
	return 0;
}
