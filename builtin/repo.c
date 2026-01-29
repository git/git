#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "environment.h"
#include "hex.h"
#include "odb.h"
#include "parse-options.h"
#include "path-walk.h"
#include "progress.h"
#include "quote.h"
#include "ref-filter.h"
#include "refs.h"
#include "revision.h"
#include "strbuf.h"
#include "string-list.h"
#include "shallow.h"
#include "utf8.h"

static const char *const repo_usage[] = {
	"git repo info [--format=(lines|nul) | -z] [--all | <key>...]",
	"git repo info --keys [--format=(lines|nul) | -z]",
	"git repo structure [--format=(table|lines|nul) | -z]",
	NULL
};

typedef int get_value_fn(struct repository *repo, struct strbuf *buf);

enum output_format {
	FORMAT_TABLE,
	FORMAT_LINES,
	FORMAT_NUL_TERMINATED,
};

struct field {
	const char *key;
	get_value_fn *get_value;
};

static int get_layout_bare(struct repository *repo UNUSED, struct strbuf *buf)
{
	strbuf_addstr(buf, is_bare_repository() ? "true" : "false");
	return 0;
}

static int get_layout_shallow(struct repository *repo, struct strbuf *buf)
{
	strbuf_addstr(buf,
		      is_repository_shallow(repo) ? "true" : "false");
	return 0;
}

static int get_object_format(struct repository *repo, struct strbuf *buf)
{
	strbuf_addstr(buf, repo->hash_algo->name);
	return 0;
}

static int get_references_format(struct repository *repo, struct strbuf *buf)
{
	strbuf_addstr(buf,
		      ref_storage_format_to_name(repo->ref_storage_format));
	return 0;
}

/* repo_info_fields keys must be in lexicographical order */
static const struct field repo_info_fields[] = {
	{ "layout.bare", get_layout_bare },
	{ "layout.shallow", get_layout_shallow },
	{ "object.format", get_object_format },
	{ "references.format", get_references_format },
};

static int repo_info_fields_cmp(const void *va, const void *vb)
{
	const struct field *a = va;
	const struct field *b = vb;

	return strcmp(a->key, b->key);
}

static get_value_fn *get_value_fn_for_key(const char *key)
{
	const struct field search_key = { key, NULL };
	const struct field *found = bsearch(&search_key, repo_info_fields,
					    ARRAY_SIZE(repo_info_fields),
					    sizeof(*found),
					    repo_info_fields_cmp);
	return found ? found->get_value : NULL;
}

static void print_field(enum output_format format, const char *key,
			const char *value)
{
	switch (format) {
	case FORMAT_LINES:
		printf("%s=", key);
		quote_c_style(value, NULL, stdout, 0);
		putchar('\n');
		break;
	case FORMAT_NUL_TERMINATED:
		printf("%s\n%s%c", key, value, '\0');
		break;
	default:
		BUG("not a valid output format: %d", format);
	}
}

static int print_fields(int argc, const char **argv,
			struct repository *repo,
			enum output_format format)
{
	int ret = 0;
	struct strbuf valbuf = STRBUF_INIT;

	for (int i = 0; i < argc; i++) {
		get_value_fn *get_value;
		const char *key = argv[i];

		get_value = get_value_fn_for_key(key);

		if (!get_value) {
			ret = error(_("key '%s' not found"), key);
			continue;
		}

		strbuf_reset(&valbuf);
		get_value(repo, &valbuf);
		print_field(format, key, valbuf.buf);
	}

	strbuf_release(&valbuf);
	return ret;
}

static int print_all_fields(struct repository *repo,
			    enum output_format format)
{
	struct strbuf valbuf = STRBUF_INIT;

	for (size_t i = 0; i < ARRAY_SIZE(repo_info_fields); i++) {
		const struct field *field = &repo_info_fields[i];

		strbuf_reset(&valbuf);
		field->get_value(repo, &valbuf);
		print_field(format, field->key, valbuf.buf);
	}

	strbuf_release(&valbuf);
	return 0;
}

static int print_keys(enum output_format format)
{
	char sep;

	switch (format) {
	case FORMAT_LINES:
		sep = '\n';
		break;
	case FORMAT_NUL_TERMINATED:
		sep = '\0';
		break;
	default:
		die(_("--keys can only be used with --format=lines or --format=nul"));
	}

	for (size_t i = 0; i < ARRAY_SIZE(repo_info_fields); i++) {
		const struct field *field = &repo_info_fields[i];
		printf("%s%c", field->key, sep);
	}

	return 0;
}

static int parse_format_cb(const struct option *opt,
			   const char *arg, int unset UNUSED)
{
	enum output_format *format = opt->value;

	if (opt->short_name == 'z')
		*format = FORMAT_NUL_TERMINATED;
	else if (!strcmp(arg, "nul"))
		*format = FORMAT_NUL_TERMINATED;
	else if (!strcmp(arg, "lines"))
		*format = FORMAT_LINES;
	else if (!strcmp(arg, "table"))
		*format = FORMAT_TABLE;
	else
		die(_("invalid format '%s'"), arg);

	return 0;
}

static int cmd_repo_info(int argc, const char **argv, const char *prefix,
			 struct repository *repo)
{
	enum output_format format = FORMAT_LINES;
	int all_keys = 0;
	int show_keys = 0;
	struct option options[] = {
		OPT_CALLBACK_F(0, "format", &format, N_("format"),
			       N_("output format"),
			       PARSE_OPT_NONEG, parse_format_cb),
		OPT_CALLBACK_F('z', NULL, &format, NULL,
			       N_("synonym for --format=nul"),
			       PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			       parse_format_cb),
		OPT_BOOL(0, "all", &all_keys, N_("print all keys/values")),
		OPT_BOOL(0, "keys", &show_keys, N_("show keys")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, repo_usage, 0);

	if (show_keys && (all_keys || argc))
		die(_("--keys cannot be used with a <key> or --all"));

	if (show_keys)
		return print_keys(format);

	if (format != FORMAT_LINES && format != FORMAT_NUL_TERMINATED)
		die(_("unsupported output format"));

	if (all_keys && argc)
		die(_("--all and <key> cannot be used together"));

	if (all_keys)
		return print_all_fields(repo, format);
	else
		return print_fields(argc, argv, repo, format);
}

struct ref_stats {
	size_t branches;
	size_t remotes;
	size_t tags;
	size_t others;
};

struct object_values {
	size_t tags;
	size_t commits;
	size_t trees;
	size_t blobs;
};

struct object_stats {
	struct object_values type_counts;
	struct object_values inflated_sizes;
	struct object_values disk_sizes;
};

struct repo_structure {
	struct ref_stats refs;
	struct object_stats objects;
};

struct stats_table {
	struct string_list rows;

	int name_col_width;
	int value_col_width;
	int unit_col_width;
};

/*
 * Holds column data that gets stored for each row.
 */
struct stats_table_entry {
	char *value;
	const char *unit;
};

static void stats_table_vaddf(struct stats_table *table,
			      struct stats_table_entry *entry,
			      const char *format, va_list ap)
{
	struct strbuf buf = STRBUF_INIT;
	struct string_list_item *item;
	char *formatted_name;
	int name_width;

	strbuf_vaddf(&buf, format, ap);
	formatted_name = strbuf_detach(&buf, NULL);
	name_width = utf8_strwidth(formatted_name);

	item = string_list_append_nodup(&table->rows, formatted_name);
	item->util = entry;

	if (name_width > table->name_col_width)
		table->name_col_width = name_width;
	if (!entry)
		return;
	if (entry->value) {
		int value_width = utf8_strwidth(entry->value);
		if (value_width > table->value_col_width)
			table->value_col_width = value_width;
	}
	if (entry->unit) {
		int unit_width = utf8_strwidth(entry->unit);
		if (unit_width > table->unit_col_width)
			table->unit_col_width = unit_width;
	}
}

static void stats_table_addf(struct stats_table *table, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	stats_table_vaddf(table, NULL, format, ap);
	va_end(ap);
}

static void stats_table_count_addf(struct stats_table *table, size_t value,
				   const char *format, ...)
{
	struct stats_table_entry *entry;
	va_list ap;

	CALLOC_ARRAY(entry, 1);
	humanise_count(value, &entry->value, &entry->unit);

	va_start(ap, format);
	stats_table_vaddf(table, entry, format, ap);
	va_end(ap);
}

static void stats_table_size_addf(struct stats_table *table, size_t value,
				  const char *format, ...)
{
	struct stats_table_entry *entry;
	va_list ap;

	CALLOC_ARRAY(entry, 1);
	humanise_bytes(value, &entry->value, &entry->unit, HUMANISE_COMPACT);

	va_start(ap, format);
	stats_table_vaddf(table, entry, format, ap);
	va_end(ap);
}

static inline size_t get_total_reference_count(struct ref_stats *stats)
{
	return stats->branches + stats->remotes + stats->tags + stats->others;
}

static inline size_t get_total_object_values(struct object_values *values)
{
	return values->tags + values->commits + values->trees + values->blobs;
}

static void stats_table_setup_structure(struct stats_table *table,
					struct repo_structure *stats)
{
	struct object_stats *objects = &stats->objects;
	struct ref_stats *refs = &stats->refs;
	size_t inflated_object_total;
	size_t object_count_total;
	size_t disk_object_total;
	size_t ref_total;

	ref_total = get_total_reference_count(refs);
	stats_table_addf(table, "* %s", _("References"));
	stats_table_count_addf(table, ref_total, "  * %s", _("Count"));
	stats_table_count_addf(table, refs->branches, "    * %s", _("Branches"));
	stats_table_count_addf(table, refs->tags, "    * %s", _("Tags"));
	stats_table_count_addf(table, refs->remotes, "    * %s", _("Remotes"));
	stats_table_count_addf(table, refs->others, "    * %s", _("Others"));

	object_count_total = get_total_object_values(&objects->type_counts);
	stats_table_addf(table, "");
	stats_table_addf(table, "* %s", _("Reachable objects"));
	stats_table_count_addf(table, object_count_total, "  * %s", _("Count"));
	stats_table_count_addf(table, objects->type_counts.commits,
			       "    * %s", _("Commits"));
	stats_table_count_addf(table, objects->type_counts.trees,
			       "    * %s", _("Trees"));
	stats_table_count_addf(table, objects->type_counts.blobs,
			       "    * %s", _("Blobs"));
	stats_table_count_addf(table, objects->type_counts.tags,
			       "    * %s", _("Tags"));

	inflated_object_total = get_total_object_values(&objects->inflated_sizes);
	stats_table_size_addf(table, inflated_object_total,
			      "  * %s", _("Inflated size"));
	stats_table_size_addf(table, objects->inflated_sizes.commits,
			      "    * %s", _("Commits"));
	stats_table_size_addf(table, objects->inflated_sizes.trees,
			      "    * %s", _("Trees"));
	stats_table_size_addf(table, objects->inflated_sizes.blobs,
			      "    * %s", _("Blobs"));
	stats_table_size_addf(table, objects->inflated_sizes.tags,
			      "    * %s", _("Tags"));

	disk_object_total = get_total_object_values(&objects->disk_sizes);
	stats_table_size_addf(table, disk_object_total,
			      "  * %s", _("Disk size"));
	stats_table_size_addf(table, objects->disk_sizes.commits,
			      "    * %s", _("Commits"));
	stats_table_size_addf(table, objects->disk_sizes.trees,
			      "    * %s", _("Trees"));
	stats_table_size_addf(table, objects->disk_sizes.blobs,
			      "    * %s", _("Blobs"));
	stats_table_size_addf(table, objects->disk_sizes.tags,
			      "    * %s", _("Tags"));
}

static void stats_table_print_structure(const struct stats_table *table)
{
	const char *name_col_title = _("Repository structure");
	const char *value_col_title = _("Value");
	int title_name_width = utf8_strwidth(name_col_title);
	int title_value_width = utf8_strwidth(value_col_title);
	int name_col_width = table->name_col_width;
	int value_col_width = table->value_col_width;
	int unit_col_width = table->unit_col_width;
	struct string_list_item *item;
	struct strbuf buf = STRBUF_INIT;

	if (title_name_width > name_col_width)
		name_col_width = title_name_width;
	if (title_value_width > value_col_width + unit_col_width + 1)
		value_col_width = title_value_width - unit_col_width;

	strbuf_addstr(&buf, "| ");
	strbuf_utf8_align(&buf, ALIGN_LEFT, name_col_width, name_col_title);
	strbuf_addstr(&buf, " | ");
	strbuf_utf8_align(&buf, ALIGN_LEFT,
			  value_col_width + unit_col_width + 1, value_col_title);
	strbuf_addstr(&buf, " |");
	printf("%s\n", buf.buf);

	printf("| ");
	for (int i = 0; i < name_col_width; i++)
		putchar('-');
	printf(" | ");
	for (int i = 0; i < value_col_width + unit_col_width + 1; i++)
		putchar('-');
	printf(" |\n");

	for_each_string_list_item(item, &table->rows) {
		struct stats_table_entry *entry = item->util;
		const char *value = "";
		const char *unit = "";

		if (entry) {
			struct stats_table_entry *entry = item->util;
			value = entry->value;
			if (entry->unit)
				unit = entry->unit;
		}

		strbuf_reset(&buf);
		strbuf_addstr(&buf, "| ");
		strbuf_utf8_align(&buf, ALIGN_LEFT, name_col_width, item->string);
		strbuf_addstr(&buf, " | ");
		strbuf_utf8_align(&buf, ALIGN_RIGHT, value_col_width, value);
		strbuf_addch(&buf, ' ');
		strbuf_utf8_align(&buf, ALIGN_LEFT, unit_col_width, unit);
		strbuf_addstr(&buf, " |");
		printf("%s\n", buf.buf);
	}

	strbuf_release(&buf);
}

static void stats_table_clear(struct stats_table *table)
{
	struct stats_table_entry *entry;
	struct string_list_item *item;

	for_each_string_list_item(item, &table->rows) {
		entry = item->util;
		if (entry)
			free(entry->value);
	}

	string_list_clear(&table->rows, 1);
}

static void structure_keyvalue_print(struct repo_structure *stats,
				     char key_delim, char value_delim)
{
	printf("references.branches.count%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->refs.branches, value_delim);
	printf("references.tags.count%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->refs.tags, value_delim);
	printf("references.remotes.count%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->refs.remotes, value_delim);
	printf("references.others.count%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->refs.others, value_delim);

	printf("objects.commits.count%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.type_counts.commits, value_delim);
	printf("objects.trees.count%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.type_counts.trees, value_delim);
	printf("objects.blobs.count%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.type_counts.blobs, value_delim);
	printf("objects.tags.count%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.type_counts.tags, value_delim);

	printf("objects.commits.inflated_size%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.inflated_sizes.commits, value_delim);
	printf("objects.trees.inflated_size%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.inflated_sizes.trees, value_delim);
	printf("objects.blobs.inflated_size%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.inflated_sizes.blobs, value_delim);
	printf("objects.tags.inflated_size%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.inflated_sizes.tags, value_delim);

	printf("objects.commits.disk_size%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.disk_sizes.commits, value_delim);
	printf("objects.trees.disk_size%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.disk_sizes.trees, value_delim);
	printf("objects.blobs.disk_size%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.disk_sizes.blobs, value_delim);
	printf("objects.tags.disk_size%c%" PRIuMAX "%c", key_delim,
	       (uintmax_t)stats->objects.disk_sizes.tags, value_delim);

	fflush(stdout);
}

struct count_references_data {
	struct ref_stats *stats;
	struct rev_info *revs;
	struct progress *progress;
};

static int count_references(const struct reference *ref, void *cb_data)
{
	struct count_references_data *data = cb_data;
	struct ref_stats *stats = data->stats;
	size_t ref_count;

	switch (ref_kind_from_refname(ref->name)) {
	case FILTER_REFS_BRANCHES:
		stats->branches++;
		break;
	case FILTER_REFS_REMOTES:
		stats->remotes++;
		break;
	case FILTER_REFS_TAGS:
		stats->tags++;
		break;
	case FILTER_REFS_OTHERS:
		stats->others++;
		break;
	default:
		BUG("unexpected reference type");
	}

	/*
	 * While iterating through references for counting, also add OIDs in
	 * preparation for the path walk.
	 */
	add_pending_oid(data->revs, NULL, ref->oid, 0);

	ref_count = get_total_reference_count(stats);
	display_progress(data->progress, ref_count);

	return 0;
}

static void structure_count_references(struct ref_stats *stats,
				       struct rev_info *revs,
				       struct repository *repo,
				       int show_progress)
{
	struct count_references_data data = {
		.stats = stats,
		.revs = revs,
	};

	if (show_progress)
		data.progress = start_delayed_progress(repo,
						       _("Counting references"), 0);

	refs_for_each_ref(get_main_ref_store(repo), count_references, &data);
	stop_progress(&data.progress);
}

struct count_objects_data {
	struct object_database *odb;
	struct object_stats *stats;
	struct progress *progress;
};

static int count_objects(const char *path UNUSED, struct oid_array *oids,
			 enum object_type type, void *cb_data)
{
	struct count_objects_data *data = cb_data;
	struct object_stats *stats = data->stats;
	size_t inflated_total = 0;
	size_t disk_total = 0;
	size_t object_count;

	for (size_t i = 0; i < oids->nr; i++) {
		struct object_info oi = OBJECT_INFO_INIT;
		unsigned long inflated;
		off_t disk;

		oi.sizep = &inflated;
		oi.disk_sizep = &disk;

		if (odb_read_object_info_extended(data->odb, &oids->oid[i], &oi,
						  OBJECT_INFO_SKIP_FETCH_OBJECT |
						  OBJECT_INFO_QUICK) < 0)
			continue;

		inflated_total += inflated;
		disk_total += disk;
	}

	switch (type) {
	case OBJ_TAG:
		stats->type_counts.tags += oids->nr;
		stats->inflated_sizes.tags += inflated_total;
		stats->disk_sizes.tags += disk_total;
		break;
	case OBJ_COMMIT:
		stats->type_counts.commits += oids->nr;
		stats->inflated_sizes.commits += inflated_total;
		stats->disk_sizes.commits += disk_total;
		break;
	case OBJ_TREE:
		stats->type_counts.trees += oids->nr;
		stats->inflated_sizes.trees += inflated_total;
		stats->disk_sizes.trees += disk_total;
		break;
	case OBJ_BLOB:
		stats->type_counts.blobs += oids->nr;
		stats->inflated_sizes.blobs += inflated_total;
		stats->disk_sizes.blobs += disk_total;
		break;
	default:
		BUG("invalid object type");
	}

	object_count = get_total_object_values(&stats->type_counts);
	display_progress(data->progress, object_count);

	return 0;
}

static void structure_count_objects(struct object_stats *stats,
				    struct rev_info *revs,
				    struct repository *repo, int show_progress)
{
	struct path_walk_info info = PATH_WALK_INFO_INIT;
	struct count_objects_data data = {
		.odb = repo->objects,
		.stats = stats,
	};

	info.revs = revs;
	info.path_fn = count_objects;
	info.path_fn_data = &data;

	if (show_progress)
		data.progress = start_delayed_progress(repo, _("Counting objects"), 0);

	walk_objects_by_path(&info);
	path_walk_info_clear(&info);
	stop_progress(&data.progress);
}

static int cmd_repo_structure(int argc, const char **argv, const char *prefix,
			      struct repository *repo)
{
	struct stats_table table = {
		.rows = STRING_LIST_INIT_DUP,
	};
	enum output_format format = FORMAT_TABLE;
	struct repo_structure stats = { 0 };
	struct rev_info revs;
	int show_progress = -1;
	struct option options[] = {
		OPT_CALLBACK_F(0, "format", &format, N_("format"),
			       N_("output format"),
			       PARSE_OPT_NONEG, parse_format_cb),
		OPT_CALLBACK_F('z', NULL, &format, NULL,
			       N_("synonym for --format=nul"),
			       PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			       parse_format_cb),
		OPT_BOOL(0, "progress", &show_progress, N_("show progress")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, repo_usage, 0);
	if (argc)
		usage(_("too many arguments"));

	repo_init_revisions(repo, &revs, prefix);

	if (show_progress < 0)
		show_progress = isatty(2);

	structure_count_references(&stats.refs, &revs, repo, show_progress);
	structure_count_objects(&stats.objects, &revs, repo, show_progress);

	switch (format) {
	case FORMAT_TABLE:
		stats_table_setup_structure(&table, &stats);
		stats_table_print_structure(&table);
		break;
	case FORMAT_LINES:
		structure_keyvalue_print(&stats, '=', '\n');
		break;
	case FORMAT_NUL_TERMINATED:
		structure_keyvalue_print(&stats, '\n', '\0');
		break;
	default:
		BUG("invalid output format");
	}

	stats_table_clear(&table);
	release_revisions(&revs);

	return 0;
}

int cmd_repo(int argc, const char **argv, const char *prefix,
	     struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("info", &fn, cmd_repo_info),
		OPT_SUBCOMMAND("structure", &fn, cmd_repo_structure),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, repo_usage, 0);

	return fn(argc, argv, prefix, repo);
}
