#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "config.h"
#include "environment.h"
#include "hex.h"
#include "object.h"
#include "object-name.h"
#include "object-store-ll.h"
#include "parse-options.h"
#include "path-walk.h"
#include "progress.h"
#include "ref-filter.h"
#include "refs.h"
#include "revision.h"
#include "strbuf.h"
#include "strvec.h"
#include "tag.h"
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

struct survey_report_object_summary {
	size_t commits_nr;
	size_t tags_nr;
	size_t trees_nr;
	size_t blobs_nr;
};

/**
 * For some category given by 'label', count the number of objects
 * that match that label along with the on-disk size and the size
 * after decompressing (both with delta bases and zlib).
 */
struct survey_report_object_size_summary {
	char *label;
	size_t nr;
	size_t disk_size;
	size_t inflated_size;
	size_t num_missing;
};

/**
 * This struct contains all of the information that needs to be printed
 * at the end of the exploration of the repository and its references.
 */
struct survey_report {
	struct survey_report_ref_summary refs;
	struct survey_report_object_summary reachable_objects;

	struct survey_report_object_size_summary *by_type;
};

#define REPORT_TYPE_COMMIT 0
#define REPORT_TYPE_TREE 1
#define REPORT_TYPE_BLOB 2
#define REPORT_TYPE_TAG 3
#define REPORT_TYPE_COUNT 4

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
	struct ref_array ref_array;
};

static void clear_survey_context(struct survey_context *ctx)
{
	ref_array_clear(&ctx->ref_array);
	strvec_clear(&ctx->refs);
}

struct survey_table {
	const char *table_name;
	struct strvec header;
	struct strvec *rows;
	size_t rows_nr;
	size_t rows_alloc;
};

#define SURVEY_TABLE_INIT {	\
	.header = STRVEC_INIT,	\
}

static void clear_table(struct survey_table *table)
{
	strvec_clear(&table->header);
	for (size_t i = 0; i < table->rows_nr; i++)
		strvec_clear(&table->rows[i]);
	free(table->rows);
}

static void insert_table_rowv(struct survey_table *table, ...)
{
	va_list ap;
	char *arg;
	ALLOC_GROW(table->rows, table->rows_nr + 1, table->rows_alloc);

	memset(&table->rows[table->rows_nr], 0, sizeof(struct strvec));

	va_start(ap, table);
	while ((arg = va_arg(ap, char *)))
		strvec_push(&table->rows[table->rows_nr], arg);
	va_end(ap);

	table->rows_nr++;
}

#define SECTION_SEGMENT "========================================"
#define SECTION_SEGMENT_LEN 40
static const char *section_line = SECTION_SEGMENT
				  SECTION_SEGMENT
				  SECTION_SEGMENT
				  SECTION_SEGMENT;
static const size_t section_len = 4 * SECTION_SEGMENT_LEN;

static void print_table_title(const char *name, size_t *widths, size_t nr)
{
	size_t width = 3 * (nr - 1);
	size_t min_width = strlen(name);

	for (size_t i = 0; i < nr; i++)
		width += widths[i];

	if (width < min_width)
		width = min_width;

	if (width > section_len)
		width = section_len;

	printf("\n%s\n%.*s\n", name, (int)width, section_line);
}

static void print_row_plaintext(struct strvec *row, size_t *widths)
{
	static struct strbuf line = STRBUF_INIT;
	strbuf_setlen(&line, 0);

	for (size_t i = 0; i < row->nr; i++) {
		const char *str = row->v[i];
		size_t len = strlen(str);
		if (i)
			strbuf_add(&line, " | ", 3);
		strbuf_addchars(&line, ' ', widths[i] - len);
		strbuf_add(&line, str, len);
	}
	printf("%s\n", line.buf);
}

static void print_divider_plaintext(size_t *widths, size_t nr)
{
	static struct strbuf line = STRBUF_INIT;
	strbuf_setlen(&line, 0);

	for (size_t i = 0; i < nr; i++) {
		if (i)
			strbuf_add(&line, "-+-", 3);
		strbuf_addchars(&line, '-', widths[i]);
	}
	printf("%s\n", line.buf);
}

static void print_table_plaintext(struct survey_table *table)
{
	size_t *column_widths;
	size_t columns_nr = table->header.nr;
	CALLOC_ARRAY(column_widths, columns_nr);

	for (size_t i = 0; i < columns_nr; i++) {
		column_widths[i] = strlen(table->header.v[i]);

		for (size_t j = 0; j < table->rows_nr; j++) {
			size_t rowlen = strlen(table->rows[j].v[i]);
			if (column_widths[i] < rowlen)
				column_widths[i] = rowlen;
		}
	}

	print_table_title(table->table_name, column_widths, columns_nr);
	print_row_plaintext(&table->header, column_widths);
	print_divider_plaintext(column_widths, columns_nr);

	for (size_t j = 0; j < table->rows_nr; j++)
		print_row_plaintext(&table->rows[j], column_widths);

	free(column_widths);
}

static void survey_report_plaintext_refs(struct survey_context *ctx)
{
	struct survey_report_ref_summary *refs = &ctx->report.refs;
	struct survey_table table = SURVEY_TABLE_INIT;

	table.table_name = _("REFERENCES SUMMARY");

	strvec_push(&table.header, _("Ref Type"));
	strvec_push(&table.header, _("Count"));

	if (ctx->opts.refs.want_all_refs || ctx->opts.refs.want_branches) {
		char *fmt = xstrfmt("%"PRIuMAX"", (uintmax_t)refs->branches_nr);
		insert_table_rowv(&table, _("Branches"), fmt, NULL);
		free(fmt);
	}

	if (ctx->opts.refs.want_all_refs || ctx->opts.refs.want_remotes) {
		char *fmt = xstrfmt("%"PRIuMAX"", (uintmax_t)refs->remote_refs_nr);
		insert_table_rowv(&table, _("Remote refs"), fmt, NULL);
		free(fmt);
	}

	if (ctx->opts.refs.want_all_refs || ctx->opts.refs.want_tags) {
		char *fmt = xstrfmt("%"PRIuMAX"", (uintmax_t)refs->tags_nr);
		insert_table_rowv(&table, _("Tags (all)"), fmt, NULL);
		free(fmt);
		fmt = xstrfmt("%"PRIuMAX"", (uintmax_t)refs->tags_annotated_nr);
		insert_table_rowv(&table, _("Tags (annotated)"), fmt, NULL);
		free(fmt);
	}

	print_table_plaintext(&table);
	clear_table(&table);
}

static void survey_report_plaintext_reachable_object_summary(struct survey_context *ctx)
{
	struct survey_report_object_summary *objs = &ctx->report.reachable_objects;
	struct survey_table table = SURVEY_TABLE_INIT;
	char *fmt;

	table.table_name = _("REACHABLE OBJECT SUMMARY");

	strvec_push(&table.header, _("Object Type"));
	strvec_push(&table.header, _("Count"));

	fmt = xstrfmt("%"PRIuMAX"", (uintmax_t)objs->tags_nr);
	insert_table_rowv(&table, _("Tags"), fmt, NULL);
	free(fmt);

	fmt = xstrfmt("%"PRIuMAX"", (uintmax_t)objs->commits_nr);
	insert_table_rowv(&table, _("Commits"), fmt, NULL);
	free(fmt);

	fmt = xstrfmt("%"PRIuMAX"", (uintmax_t)objs->trees_nr);
	insert_table_rowv(&table, _("Trees"), fmt, NULL);
	free(fmt);

	fmt = xstrfmt("%"PRIuMAX"", (uintmax_t)objs->blobs_nr);
	insert_table_rowv(&table, _("Blobs"), fmt, NULL);
	free(fmt);

	print_table_plaintext(&table);
	clear_table(&table);
}

static void survey_report_object_sizes(const char *title,
				       const char *categories,
				       struct survey_report_object_size_summary *summary,
				       size_t summary_nr)
{
	struct survey_table table = SURVEY_TABLE_INIT;
	table.table_name = title;

	strvec_push(&table.header, categories);
	strvec_push(&table.header, _("Count"));
	strvec_push(&table.header, _("Disk Size"));
	strvec_push(&table.header, _("Inflated Size"));

	for (size_t i = 0; i < summary_nr; i++) {
		char *label_str =  xstrdup(summary[i].label);
		char *nr_str = xstrfmt("%"PRIuMAX, (uintmax_t)summary[i].nr);
		char *disk_str = xstrfmt("%"PRIuMAX, (uintmax_t)summary[i].disk_size);
		char *inflate_str = xstrfmt("%"PRIuMAX, (uintmax_t)summary[i].inflated_size);

		insert_table_rowv(&table, label_str, nr_str,
				  disk_str, inflate_str, NULL);

		free(label_str);
		free(nr_str);
		free(disk_str);
		free(inflate_str);
	}

	print_table_plaintext(&table);
	clear_table(&table);
}

static void survey_report_plaintext(struct survey_context *ctx)
{
	printf("GIT SURVEY for \"%s\"\n", ctx->repo->worktree);
	printf("-----------------------------------------------------\n");
	survey_report_plaintext_refs(ctx);
	survey_report_plaintext_reachable_object_summary(ctx);
	survey_report_object_sizes(_("TOTAL OBJECT SIZES BY TYPE"),
				   _("Object Type"),
				   ctx->report.by_type,
				   REPORT_TYPE_COUNT);
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
	trace2_region_enter("survey", "phase/refs", ctx->repo);
	do_load_refs(ctx, &ctx->ref_array);

	ctx->report.refs.refs_nr = ctx->ref_array.nr;
	for (size_t i = 0; i < ctx->ref_array.nr; i++) {
		unsigned long size;
		struct ref_array_item *item = ctx->ref_array.items[i];

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
}

static void increment_object_counts(
		struct survey_report_object_summary *summary,
		enum object_type type,
		size_t nr)
{
	switch (type) {
	case OBJ_COMMIT:
		summary->commits_nr += nr;
		break;

	case OBJ_TREE:
		summary->trees_nr += nr;
		break;

	case OBJ_BLOB:
		summary->blobs_nr += nr;
		break;

	case OBJ_TAG:
		summary->tags_nr += nr;
		break;

	default:
		break;
	}
}

static void increment_totals(struct survey_context *ctx,
			     struct oid_array *oids,
			     struct survey_report_object_size_summary *summary)
{
	for (size_t i = 0; i < oids->nr; i++) {
		struct object_info oi = OBJECT_INFO_INIT;
		unsigned oi_flags = OBJECT_INFO_FOR_PREFETCH;
		unsigned long object_length = 0;
		off_t disk_sizep = 0;
		enum object_type type;

		oi.typep = &type;
		oi.sizep = &object_length;
		oi.disk_sizep = &disk_sizep;

		if (oid_object_info_extended(ctx->repo, &oids->oid[i],
					     &oi, oi_flags) < 0) {
			summary->num_missing++;
		} else {
			summary->nr++;
			summary->disk_size += disk_sizep;
			summary->inflated_size += object_length;
		}
	}
}

static void increment_object_totals(struct survey_context *ctx,
				    struct oid_array *oids,
				    enum object_type type)
{
	struct survey_report_object_size_summary *total;
	struct survey_report_object_size_summary summary = { 0 };

	increment_totals(ctx, oids, &summary);

	switch (type) {
	case OBJ_COMMIT:
		total = &ctx->report.by_type[REPORT_TYPE_COMMIT];
		break;

	case OBJ_TREE:
		total = &ctx->report.by_type[REPORT_TYPE_TREE];
		break;

	case OBJ_BLOB:
		total = &ctx->report.by_type[REPORT_TYPE_BLOB];
		break;

	case OBJ_TAG:
		total = &ctx->report.by_type[REPORT_TYPE_TAG];
		break;

	default:
		BUG("No other type allowed");
	}

	total->nr += summary.nr;
	total->disk_size += summary.disk_size;
	total->inflated_size += summary.inflated_size;
	total->num_missing += summary.num_missing;
}

static int survey_objects_path_walk_fn(const char *path,
				       struct oid_array *oids,
				       enum object_type type,
				       void *data)
{
	struct survey_context *ctx = data;

	increment_object_counts(&ctx->report.reachable_objects,
				type, oids->nr);
	increment_object_totals(ctx, oids, type);

	return 0;
}

static void initialize_report(struct survey_context *ctx)
{
	CALLOC_ARRAY(ctx->report.by_type, REPORT_TYPE_COUNT);
	ctx->report.by_type[REPORT_TYPE_COMMIT].label = xstrdup(_("Commits"));
	ctx->report.by_type[REPORT_TYPE_TREE].label = xstrdup(_("Trees"));
	ctx->report.by_type[REPORT_TYPE_BLOB].label = xstrdup(_("Blobs"));
	ctx->report.by_type[REPORT_TYPE_TAG].label = xstrdup(_("Tags"));
}

static void survey_phase_objects(struct survey_context *ctx)
{
	struct rev_info revs = REV_INFO_INIT;
	struct path_walk_info info = PATH_WALK_INFO_INIT;
	unsigned int add_flags = 0;

	trace2_region_enter("survey", "phase/objects", ctx->repo);

	info.revs = &revs;
	info.path_fn = survey_objects_path_walk_fn;
	info.path_fn_data = ctx;

	initialize_report(ctx);

	repo_init_revisions(ctx->repo, &revs, "");
	revs.tag_objects = 1;

	for (size_t i = 0; i < ctx->ref_array.nr; i++) {
		struct ref_array_item *item = ctx->ref_array.items[i];
		add_pending_oid(&revs, NULL, &item->objectname, add_flags);
		display_progress(ctx->progress, ++(ctx->progress_nr));
	}

	walk_objects_by_path(&info);

	release_revisions(&revs);
	trace2_region_leave("survey", "phase/objects", ctx->repo);
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

	survey_phase_objects(&ctx);

	survey_report_plaintext(&ctx);

	clear_survey_context(&ctx);
	return 0;
}
