#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "commit.h"
#include "config.h"
#include "environment.h"
#include "hash.h"
#include "hex.h"
#include "odb.h"
#include "parse-options.h"
#include "path.h"
#include "path-walk.h"
#include "progress.h"
#include "quote.h"
#include "ref-filter.h"
#include "refs.h"
#include "revision.h"
#include "setup.h"
#include "strbuf.h"
#include "string-list.h"
#include "shallow.h"
#include "tree.h"
#include "tree-walk.h"
#include "utf8.h"
#include "wildmatch.h"

#define REPO_INFO_USAGE \
	"git repo info [--format=(lines|nul) | -z] [--all | <key>...]", \
	"git repo info --keys [--format=(lines|nul) | -z]"

#define REPO_STRUCTURE_USAGE \
	"git repo structure [<options>]"

static const char *const repo_usage[] = {
	REPO_INFO_USAGE,
	REPO_STRUCTURE_USAGE,
	NULL,
};

static const char *const repo_info_usage[] = {
	REPO_INFO_USAGE,
	NULL,
};

static const char *const repo_structure_usage[] = {
	REPO_STRUCTURE_USAGE,
	NULL,
};

typedef int get_value_fn(struct repository *repo, struct strbuf *buf);

enum output_format {
	FORMAT_TABLE,
	FORMAT_NEWLINE_TERMINATED,
	FORMAT_NUL_TERMINATED,
};

struct repo_info_field {
	const char *key;
	get_value_fn *get_value;
};

static int get_layout_bare(struct repository *repo UNUSED, struct strbuf *buf)
{
	strbuf_addstr(buf, is_bare_repository(the_repository) ? "true" : "false");
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

static int get_path_commondir_absolute(struct repository *repo, struct strbuf *buf)
{
	const char *common_dir = repo_get_common_dir(repo);

	if (!common_dir)
		return error(_("unable to get common directory"));

	format_path(buf, common_dir, repo->prefix, PATH_FORMAT_CANONICAL);
	return 0;
}

static int get_path_commondir_relative(struct repository *repo, struct strbuf *buf)
{
	const char *common_dir = repo_get_common_dir(repo);

	if (!common_dir)
		return error(_("unable to get common directory"));

	format_path(buf, common_dir, repo->prefix, PATH_FORMAT_RELATIVE);
	return 0;
}

static int get_path_gitdir_absolute(struct repository *repo, struct strbuf *buf)
{
	const char *git_dir = repo_get_git_dir(repo);

	if (!git_dir)
		return error(_("unable to get git directory"));

	format_path(buf, git_dir, repo->prefix, PATH_FORMAT_CANONICAL);
	return 0;
}

static int get_path_gitdir_relative(struct repository *repo, struct strbuf *buf)
{
	const char *git_dir = repo_get_git_dir(repo);

	if (!git_dir)
		return error(_("unable to get git directory"));

	format_path(buf, git_dir, repo->prefix, PATH_FORMAT_RELATIVE);
	return 0;
}

static int get_references_format(struct repository *repo, struct strbuf *buf)
{
	strbuf_addstr(buf,
		      ref_storage_format_to_name(repo->ref_storage_format));
	return 0;
}

/* repo_info_field keys must be in lexicographical order */
static const struct repo_info_field repo_info_field[] = {
	{ "layout.bare", get_layout_bare },
	{ "layout.shallow", get_layout_shallow },
	{ "object.format", get_object_format },
	{ "path.commondir.absolute", get_path_commondir_absolute },
	{ "path.commondir.relative", get_path_commondir_relative },
	{ "path.gitdir.absolute", get_path_gitdir_absolute },
	{ "path.gitdir.relative", get_path_gitdir_relative },
	{ "references.format", get_references_format },
};

static int repo_info_field_cmp(const void *va, const void *vb)
{
	const struct repo_info_field *a = va;
	const struct repo_info_field *b = vb;

	return strcmp(a->key, b->key);
}

static const struct repo_info_field *get_repo_info_field(const char *key)
{
	const struct repo_info_field search_key = { key, NULL };
	const struct repo_info_field *found = bsearch(&search_key,
						      repo_info_field,
						      ARRAY_SIZE(repo_info_field),
						      sizeof(*found),
						      repo_info_field_cmp);

	return found;
}

static void print_field(enum output_format format, const char *key,
			const char *value)
{
	switch (format) {
	case FORMAT_NEWLINE_TERMINATED:
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
		const char *key = argv[i];
		const struct repo_info_field *field = get_repo_info_field(key);

		if (!field) {
			ret = error(_("key '%s' not found"), key);
			continue;
		}

		strbuf_reset(&valbuf);
		field->get_value(repo, &valbuf);
		print_field(format, key, valbuf.buf);
	}

	strbuf_release(&valbuf);
	return ret;
}

static int print_all_fields(struct repository *repo,
			    enum output_format format)
{
	struct strbuf valbuf = STRBUF_INIT;

	for (size_t i = 0; i < ARRAY_SIZE(repo_info_field); i++) {
		const struct repo_info_field *field = &repo_info_field[i];

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
	case FORMAT_NEWLINE_TERMINATED:
		sep = '\n';
		break;
	case FORMAT_NUL_TERMINATED:
		sep = '\0';
		break;
	default:
		die(_("--keys can only be used with --format=lines or --format=nul"));
	}

	for (size_t i = 0; i < ARRAY_SIZE(repo_info_field); i++) {
		const struct repo_info_field *field = &repo_info_field[i];
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
		*format = FORMAT_NEWLINE_TERMINATED;
	else if (!strcmp(arg, "table"))
		*format = FORMAT_TABLE;
	else
		die(_("invalid format '%s'"), arg);

	return 0;
}

static int cmd_repo_info(int argc, const char **argv, const char *prefix,
			 struct repository *repo)
{
	enum output_format format = FORMAT_NEWLINE_TERMINATED;
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

	argc = parse_options(argc, argv, prefix, options, repo_info_usage, 0);

	if (show_keys && (all_keys || argc))
		die(_("--keys cannot be used with a <key> or --all"));

	if (show_keys)
		return print_keys(format);

	if (format != FORMAT_NEWLINE_TERMINATED && format != FORMAT_NUL_TERMINATED)
		die(_("unsupported output format"));

	if (all_keys && argc)
		die(_("--all and <key> cannot be used together"));

	if (all_keys)
		return print_all_fields(repo, format);
	else
		return print_fields(argc, argv, repo, format);
}

struct object_data {
	struct object_id oid;
	size_t value;
};

struct largest_objects {
	struct object_data tag_size;
	struct object_data commit_size;
	struct object_data tree_size;
	struct object_data blob_size;

	struct object_data parent_count;
	struct object_data tree_entries;
};

/*
 * Per-path summary of all objects that share a given (path, type) under the
 * path-walk traversal: the count of objects, their on-disk size, and their
 * inflated size.
 */
struct path_size_summary {
	char *path;
	size_t nr;
	size_t disk_size;
	size_t inflated_size;
};

typedef int (*path_summary_cmp)(const struct path_size_summary *,
				const struct path_size_summary *);

/*
 * A bounded, descending-sorted list of the largest summaries seen so far,
 * with a fixed comparison function defining "largest". New summaries are
 * inserted with maybe_insert_into_top_paths(); smaller ones fall off the
 * end of the list.
 */
struct top_paths_table {
	path_summary_cmp cmp_fn;
	size_t nr;
	size_t alloc;
	struct path_size_summary *data;
};

struct top_paths {
	struct top_paths_table by_count;
	struct top_paths_table by_disk;
	struct top_paths_table by_inflated;
};

struct ref_stats {
	size_t branches;
	size_t remotes;
	size_t tags;
	size_t annotated_tags;
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
	struct largest_objects largest;
	struct top_paths top_trees;
	struct top_paths top_blobs;
};

struct repo_structure {
	struct ref_stats refs;
	struct object_stats objects;
};

struct stats_table {
	struct string_list rows;
	struct string_list annotations;

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
	size_t index;
	struct object_id *oid;
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
	if (entry->oid) {
		entry->index = table->annotations.nr + 1;
		strbuf_addf(&buf, "[%" PRIuMAX "] %s", (uintmax_t)entry->index,
			    oid_to_hex(entry->oid));
		string_list_append_nodup(&table->annotations, strbuf_detach(&buf, NULL));
	}
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

	strbuf_release(&buf);
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

static void stats_table_object_count_addf(struct stats_table *table,
					  struct object_id *oid, size_t value,
					  const char *format, ...)
{
	struct stats_table_entry *entry;
	va_list ap;

	CALLOC_ARRAY(entry, 1);
	humanise_count(value, &entry->value, &entry->unit);

	/*
	 * A NULL OID should not have a table annotation.
	 */
	if (!is_null_oid(oid))
		entry->oid = oid;

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

static void stats_table_object_size_addf(struct stats_table *table,
					 struct object_id *oid, size_t value,
					 const char *format, ...)
{
	struct stats_table_entry *entry;
	va_list ap;

	CALLOC_ARRAY(entry, 1);
	humanise_bytes(value, &entry->value, &entry->unit, HUMANISE_COMPACT);

	/*
	 * A NULL OID should not have a table annotation.
	 */
	if (!is_null_oid(oid))
		entry->oid = oid;

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
	stats_table_count_addf(table, refs->annotated_tags,
			       "      * %s", _("Annotated"));
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

	stats_table_addf(table, "");
	stats_table_addf(table, "* %s", _("Largest objects"));
	stats_table_addf(table, "  * %s", _("Commits"));
	stats_table_object_size_addf(table,
				     &objects->largest.commit_size.oid,
				     objects->largest.commit_size.value,
				     "    * %s", _("Maximum size"));
	stats_table_object_count_addf(table,
				      &objects->largest.parent_count.oid,
				      objects->largest.parent_count.value,
				      "    * %s", _("Maximum parents"));
	stats_table_addf(table, "  * %s", _("Trees"));
	stats_table_object_size_addf(table,
				     &objects->largest.tree_size.oid,
				     objects->largest.tree_size.value,
				     "    * %s", _("Maximum size"));
	stats_table_object_count_addf(table,
				      &objects->largest.tree_entries.oid,
				      objects->largest.tree_entries.value,
				      "    * %s", _("Maximum entries"));
	stats_table_addf(table, "  * %s", _("Blobs"));
	stats_table_object_size_addf(table,
				     &objects->largest.blob_size.oid,
				     objects->largest.blob_size.value,
				     "    * %s", _("Maximum size"));
	stats_table_addf(table, "  * %s", _("Tags"));
	stats_table_object_size_addf(table,
				     &objects->largest.tag_size.oid,
				     objects->largest.tag_size.value,
				     "    * %s", _("Maximum size"));
}

static void stats_table_add_top_paths(struct stats_table *table,
				      const struct top_paths *top,
				      const char *header)
{
	if (!top->by_count.nr && !top->by_disk.nr && !top->by_inflated.nr)
		return;

	stats_table_addf(table, "");
	stats_table_addf(table, "* %s", header);

	stats_table_addf(table, "  * %s", _("Top by count"));
	for (size_t i = 0; i < top->by_count.nr; i++)
		stats_table_count_addf(table, top->by_count.data[i].nr,
				       "    * %s", top->by_count.data[i].path);

	stats_table_addf(table, "  * %s", _("Top by disk size"));
	for (size_t i = 0; i < top->by_disk.nr; i++)
		stats_table_size_addf(table, top->by_disk.data[i].disk_size,
				      "    * %s", top->by_disk.data[i].path);

	stats_table_addf(table, "  * %s", _("Top by inflated size"));
	for (size_t i = 0; i < top->by_inflated.nr; i++)
		stats_table_size_addf(table,
				      top->by_inflated.data[i].inflated_size,
				      "    * %s",
				      top->by_inflated.data[i].path);
}

static void stats_table_setup_top_paths(struct stats_table *table,
					struct object_stats *objects)
{
	stats_table_add_top_paths(table, &objects->top_trees, _("Top trees"));
	stats_table_add_top_paths(table, &objects->top_blobs, _("Top blobs"));
}

#define INDEX_WIDTH 4

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
	strbuf_utf8_align(&buf, ALIGN_LEFT, name_col_width + INDEX_WIDTH,
			  name_col_title);
	strbuf_addstr(&buf, " | ");
	strbuf_utf8_align(&buf, ALIGN_LEFT,
			  value_col_width + unit_col_width + 1, value_col_title);
	strbuf_addstr(&buf, " |");
	printf("%s\n", buf.buf);

	printf("| ");
	for (int i = 0; i < name_col_width + INDEX_WIDTH; i++)
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
			value = entry->value;
			if (entry->unit)
				unit = entry->unit;
		}

		strbuf_reset(&buf);
		strbuf_addstr(&buf, "| ");
		strbuf_utf8_align(&buf, ALIGN_LEFT, name_col_width, item->string);

		if (entry && entry->oid)
			strbuf_addf(&buf, " [%" PRIuMAX "]",
				    (uintmax_t)entry->index);
		else
			strbuf_addchars(&buf, ' ', INDEX_WIDTH);

		strbuf_addstr(&buf, " | ");
		strbuf_utf8_align(&buf, ALIGN_RIGHT, value_col_width, value);
		strbuf_addch(&buf, ' ');
		strbuf_utf8_align(&buf, ALIGN_LEFT, unit_col_width, unit);
		strbuf_addstr(&buf, " |");
		printf("%s\n", buf.buf);
	}

	if (table->annotations.nr) {
		printf("\n");
		for_each_string_list_item(item, &table->annotations)
			printf("%s\n", item->string);
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
	string_list_clear(&table->annotations, 1);
}

static inline void print_keyvalue(const char *key, char key_delim, size_t value,
				  char value_delim)
{
	printf("%s%c%" PRIuMAX "%c", key, key_delim, (uintmax_t)value,
	       value_delim);
}

static void print_object_data(const char *key, char key_delim,
			      struct object_data *data, char value_delim)
{
	print_keyvalue(key, key_delim, data->value, value_delim);
	printf("%s_oid%c%s%c", key, key_delim, oid_to_hex(&data->oid),
	       value_delim);
}

static void print_keyvalue_path(const char *key, char key_delim,
				const char *path, char value_delim)
{
	printf("%s%c", key, key_delim);
	if (key_delim == '=')
		quote_c_style(path, NULL, stdout, 0);
	else
		fputs(path, stdout);
	fputc(value_delim, stdout);
}

static void top_paths_keyvalue_print(const char *prefix,
				     const struct top_paths *top,
				     char key_delim, char value_delim)
{
	for (size_t i = 0; i < top->by_count.nr; i++) {
		printf("%s.by_count.%" PRIuMAX ".",
		       prefix, (uintmax_t)(i + 1));
		print_keyvalue_path("path", key_delim,
				    top->by_count.data[i].path, value_delim);
		printf("%s.by_count.%" PRIuMAX ".",
		       prefix, (uintmax_t)(i + 1));
		print_keyvalue("count", key_delim,
			       top->by_count.data[i].nr, value_delim);
	}
	for (size_t i = 0; i < top->by_disk.nr; i++) {
		printf("%s.by_disk_size.%" PRIuMAX ".",
		       prefix, (uintmax_t)(i + 1));
		print_keyvalue_path("path", key_delim,
				    top->by_disk.data[i].path, value_delim);
		printf("%s.by_disk_size.%" PRIuMAX ".",
		       prefix, (uintmax_t)(i + 1));
		print_keyvalue("disk_size", key_delim,
			       top->by_disk.data[i].disk_size, value_delim);
	}
	for (size_t i = 0; i < top->by_inflated.nr; i++) {
		printf("%s.by_inflated_size.%" PRIuMAX ".",
		       prefix, (uintmax_t)(i + 1));
		print_keyvalue_path("path", key_delim,
				    top->by_inflated.data[i].path, value_delim);
		printf("%s.by_inflated_size.%" PRIuMAX ".",
		       prefix, (uintmax_t)(i + 1));
		print_keyvalue("inflated_size", key_delim,
			       top->by_inflated.data[i].inflated_size,
			       value_delim);
	}
}

static void structure_keyvalue_print(struct repo_structure *stats,
				     char key_delim, char value_delim)
{
	print_keyvalue("references.branches.count", key_delim,
		       stats->refs.branches, value_delim);
	print_keyvalue("references.tags.count", key_delim,
		       stats->refs.tags, value_delim);
	print_keyvalue("references.tags.annotated.count", key_delim,
		       stats->refs.annotated_tags, value_delim);
	print_keyvalue("references.remotes.count", key_delim,
		       stats->refs.remotes, value_delim);
	print_keyvalue("references.others.count", key_delim,
		       stats->refs.others, value_delim);

	print_keyvalue("objects.commits.count", key_delim,
		       stats->objects.type_counts.commits, value_delim);
	print_keyvalue("objects.trees.count", key_delim,
		       stats->objects.type_counts.trees, value_delim);
	print_keyvalue("objects.blobs.count", key_delim,
		       stats->objects.type_counts.blobs, value_delim);
	print_keyvalue("objects.tags.count", key_delim,
		       stats->objects.type_counts.tags, value_delim);

	print_keyvalue("objects.commits.inflated_size", key_delim,
		       stats->objects.inflated_sizes.commits, value_delim);
	print_keyvalue("objects.trees.inflated_size", key_delim,
		       stats->objects.inflated_sizes.trees, value_delim);
	print_keyvalue("objects.blobs.inflated_size", key_delim,
		       stats->objects.inflated_sizes.blobs, value_delim);
	print_keyvalue("objects.tags.inflated_size", key_delim,
		       stats->objects.inflated_sizes.tags, value_delim);

	print_keyvalue("objects.commits.disk_size", key_delim,
		       stats->objects.disk_sizes.commits, value_delim);
	print_keyvalue("objects.trees.disk_size", key_delim,
		       stats->objects.disk_sizes.trees, value_delim);
	print_keyvalue("objects.blobs.disk_size", key_delim,
		       stats->objects.disk_sizes.blobs, value_delim);
	print_keyvalue("objects.tags.disk_size", key_delim,
		       stats->objects.disk_sizes.tags, value_delim);

	print_object_data("objects.commits.max_size", key_delim,
			  &stats->objects.largest.commit_size, value_delim);
	print_object_data("objects.trees.max_size", key_delim,
			  &stats->objects.largest.tree_size, value_delim);
	print_object_data("objects.blobs.max_size", key_delim,
			  &stats->objects.largest.blob_size, value_delim);
	print_object_data("objects.tags.max_size", key_delim,
			  &stats->objects.largest.tag_size, value_delim);

	print_object_data("objects.commits.max_parents", key_delim,
			  &stats->objects.largest.parent_count, value_delim);
	print_object_data("objects.trees.max_entries", key_delim,
			  &stats->objects.largest.tree_entries, value_delim);

	top_paths_keyvalue_print("objects.trees.top", &stats->objects.top_trees,
				 key_delim, value_delim);
	top_paths_keyvalue_print("objects.blobs.top", &stats->objects.top_blobs,
				 key_delim, value_delim);

	fflush(stdout);
}

struct count_references_data {
	struct ref_stats *stats;
	struct rev_info *revs;
	struct repository *repo;
	const struct string_list *filters;
	struct progress *progress;
};

static int ref_matches_any_filter(const char *refname,
				  const struct string_list *filters)
{
	if (!filters->nr)
		return 1;
	for (size_t i = 0, namelen = strlen(refname); i < filters->nr; i++) {
		const char *p = filters->items[i].string;
		size_t plen = strlen(p);
		if (plen <= namelen &&
		    !strncmp(refname, p, plen) &&
		    (refname[plen] == '\0' || refname[plen] == '/' ||
		     (plen && p[plen - 1] == '/')))
			return 1;
		if (!wildmatch(p, refname, WM_PATHNAME))
			return 1;
	}
	return 0;
}

static int count_references(const struct reference *ref, void *cb_data)
{
	struct count_references_data *data = cb_data;
	struct ref_stats *stats = data->stats;
	size_t ref_count;

	if (!ref_matches_any_filter(ref->name, data->filters))
		return 0;

	switch (ref_kind_from_refname(ref->name)) {
	case FILTER_REFS_BRANCHES:
		stats->branches++;
		break;
	case FILTER_REFS_REMOTES:
		stats->remotes++;
		break;
	case FILTER_REFS_TAGS:
		stats->tags++;
		if (odb_read_object_info(data->repo->objects,
					 ref->oid, NULL) == OBJ_TAG)
			stats->annotated_tags++;
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
				       const struct string_list *filters,
				       int show_progress)
{
	struct count_references_data data = {
		.stats = stats,
		.revs = revs,
		.repo = repo,
		.filters = filters,
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
	size_t top_nr;
};

static int cmp_by_nr(const struct path_size_summary *s1,
		     const struct path_size_summary *s2)
{
	return (s1->nr > s2->nr) - (s1->nr < s2->nr);
}

static int cmp_by_disk_size(const struct path_size_summary *s1,
			    const struct path_size_summary *s2)
{
	return (s1->disk_size > s2->disk_size) -
	       (s1->disk_size < s2->disk_size);
}

static int cmp_by_inflated_size(const struct path_size_summary *s1,
				const struct path_size_summary *s2)
{
	return (s1->inflated_size > s2->inflated_size) -
	       (s1->inflated_size < s2->inflated_size);
}

static void init_top_paths_table(struct top_paths_table *top, size_t limit,
				 path_summary_cmp cmp)
{
	top->cmp_fn = cmp;
	top->alloc = limit;
	top->nr = 0;
	CALLOC_ARRAY(top->data, limit);
}

static void init_top_paths(struct top_paths *top, size_t limit)
{
	init_top_paths_table(&top->by_count, limit, cmp_by_nr);
	init_top_paths_table(&top->by_disk, limit, cmp_by_disk_size);
	init_top_paths_table(&top->by_inflated, limit, cmp_by_inflated_size);
}

static void clear_top_paths_table(struct top_paths_table *top)
{
	for (size_t i = 0; i < top->nr; i++)
		free(top->data[i].path);
	free(top->data);
}

static void clear_top_paths(struct top_paths *top)
{
	clear_top_paths_table(&top->by_count);
	clear_top_paths_table(&top->by_disk);
	clear_top_paths_table(&top->by_inflated);
}

/*
 * Insert 'summary' into 'top' if it ranks among the top alloc entries by the
 * table's comparator. The list is kept sorted from largest (index 0) to
 * smallest. If the table is already full, the smallest entry is evicted to
 * make room.
 */
static void maybe_insert_into_top_paths(struct top_paths_table *top,
					const struct path_size_summary *summary)
{
	size_t pos = top->nr;

	while (pos > 0 && top->cmp_fn(&top->data[pos - 1], summary) < 0)
		pos--;

	if (pos >= top->alloc)
		return;

	if (top->nr == top->alloc)
		free(top->data[top->nr - 1].path);
	else
		top->nr++;

	for (size_t i = top->nr - 1; i > pos; i--)
		top->data[i] = top->data[i - 1];

	top->data[pos] = *summary;
	top->data[pos].path = xstrdup(summary->path);
}

static void check_largest(struct object_data *data, struct object_id *oid,
			  size_t value)
{
	if (value > data->value || is_null_oid(&data->oid)) {
		oidcpy(&data->oid, oid);
		data->value = value;
	}
}

static size_t count_tree_entries(struct object *obj)
{
	struct tree *t = object_as_type(obj, OBJ_TREE, 0);
	struct name_entry entry;
	struct tree_desc desc;
	size_t count = 0;

	init_tree_desc(&desc, &t->object.oid, t->buffer, t->size);
	while (tree_entry(&desc, &entry))
		count++;

	return count;
}

static int count_objects(const char *path, struct oid_array *oids,
			 enum object_type type, void *cb_data)
{
	struct count_objects_data *data = cb_data;
	struct object_stats *stats = data->stats;
	struct path_size_summary summary = { .path = (char *)path };
	size_t object_count;

	for (size_t i = 0; i < oids->nr; i++) {
		struct object_info oi = OBJECT_INFO_INIT;
		size_t inflated;
		struct commit *commit;
		struct object *obj;
		void *content;
		off_t disk;
		int eaten;

		oi.sizep = &inflated;
		oi.disk_sizep = &disk;
		oi.contentp = &content;

		if (odb_read_object_info_extended(data->odb, &oids->oid[i], &oi,
						  OBJECT_INFO_SKIP_FETCH_OBJECT |
						  OBJECT_INFO_QUICK) < 0)
			continue;

		obj = parse_object_buffer(the_repository, &oids->oid[i], type,
					  inflated, content, &eaten);

		summary.nr++;
		summary.disk_size += disk;
		summary.inflated_size += inflated;

		switch (type) {
		case OBJ_TAG:
			stats->type_counts.tags++;
			stats->inflated_sizes.tags += inflated;
			stats->disk_sizes.tags += disk;
			check_largest(&stats->largest.tag_size, &oids->oid[i],
				      inflated);
			break;
		case OBJ_COMMIT:
			commit = object_as_type(obj, OBJ_COMMIT, 0);
			stats->type_counts.commits++;
			stats->inflated_sizes.commits += inflated;
			stats->disk_sizes.commits += disk;
			check_largest(&stats->largest.commit_size, &oids->oid[i],
				      inflated);
			check_largest(&stats->largest.parent_count, &oids->oid[i],
				      commit_list_count(commit->parents));
			break;
		case OBJ_TREE:
			stats->type_counts.trees++;
			stats->inflated_sizes.trees += inflated;
			stats->disk_sizes.trees += disk;
			check_largest(&stats->largest.tree_size, &oids->oid[i],
				      inflated);
			check_largest(&stats->largest.tree_entries, &oids->oid[i],
				      count_tree_entries(obj));
			break;
		case OBJ_BLOB:
			stats->type_counts.blobs++;
			stats->inflated_sizes.blobs += inflated;
			stats->disk_sizes.blobs += disk;
			check_largest(&stats->largest.blob_size, &oids->oid[i],
				      inflated);
			break;
		default:
			BUG("invalid object type");
		}

		if (!eaten)
			free(content);
	}

	if (data->top_nr) {
		struct top_paths *top = NULL;

		if (type == OBJ_TREE)
			top = &stats->top_trees;
		else if (type == OBJ_BLOB)
			top = &stats->top_blobs;

		if (top) {
			maybe_insert_into_top_paths(&top->by_count, &summary);
			maybe_insert_into_top_paths(&top->by_disk, &summary);
			maybe_insert_into_top_paths(&top->by_inflated,
						    &summary);
		}
	}

	object_count = get_total_object_values(&stats->type_counts);
	display_progress(data->progress, object_count);

	return 0;
}

static void structure_count_objects(struct object_stats *stats,
				    struct rev_info *revs,
				    struct repository *repo, size_t top_nr,
				    int show_progress)
{
	struct path_walk_info info = PATH_WALK_INFO_INIT;
	struct count_objects_data data = {
		.odb = repo->objects,
		.stats = stats,
		.top_nr = top_nr,
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

static int repo_structure_config_cb(const char *var, const char *value,
				    const struct config_context *cctx,
				    void *cb)
{
	int *top_nr = cb;

	if (!strcmp(var, "repo.structure.top")) {
		*top_nr = git_config_int(var, value, cctx->kvi);
		if (*top_nr < 0)
			die(_("repo.structure.top must be non-negative"));
		return 0;
	}

	return git_default_config(var, value, cctx, NULL);
}

static int cmd_repo_structure(int argc, const char **argv, const char *prefix,
			      struct repository *repo)
{
	struct stats_table table = {
		.rows = STRING_LIST_INIT_DUP,
		.annotations = STRING_LIST_INIT_DUP,
	};
	enum output_format format = FORMAT_TABLE;
	struct repo_structure stats = { 0 };
	struct rev_info revs;
	int show_progress = -1;
	int top_nr = 0;
	struct string_list ref_filters = STRING_LIST_INIT_DUP;
	struct option options[] = {
		OPT_CALLBACK_F(0, "format", &format, N_("format"),
			       N_("output format"),
			       PARSE_OPT_NONEG, parse_format_cb),
		OPT_CALLBACK_F('z', NULL, &format, NULL,
			       N_("synonym for --format=nul"),
			       PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			       parse_format_cb),
		OPT_BOOL(0, "progress", &show_progress, N_("show progress")),
		OPT_STRING_LIST(0, "ref-filter", &ref_filters, N_("pattern"),
				N_("only count refs matching <pattern>; "
				   "repeat to union multiple patterns")),
		OPT_INTEGER(0, "top", &top_nr,
			    N_("report the top <n> largest paths "
			       "per category")),
		OPT_END()
	};

	repo_config(repo, repo_structure_config_cb, &top_nr);

	argc = parse_options(argc, argv, prefix, options, repo_structure_usage, 0);
	if (argc)
		usage(_("too many arguments"));
	if (top_nr < 0)
		die(_("--top=<n> must be non-negative"));

	repo_init_revisions(repo, &revs, prefix);

	if (show_progress < 0)
		show_progress = isatty(2);

	if (top_nr) {
		init_top_paths(&stats.objects.top_trees, top_nr);
		init_top_paths(&stats.objects.top_blobs, top_nr);
	}

	structure_count_references(&stats.refs, &revs, repo, &ref_filters,
				   show_progress);
	structure_count_objects(&stats.objects, &revs, repo, top_nr,
				show_progress);

	switch (format) {
	case FORMAT_TABLE:
		stats_table_setup_structure(&table, &stats);
		stats_table_setup_top_paths(&table, &stats.objects);
		stats_table_print_structure(&table);
		break;
	case FORMAT_NEWLINE_TERMINATED:
		structure_keyvalue_print(&stats, '=', '\n');
		break;
	case FORMAT_NUL_TERMINATED:
		structure_keyvalue_print(&stats, '\n', '\0');
		break;
	default:
		BUG("invalid output format");
	}

	stats_table_clear(&table);
	string_list_clear(&ref_filters, 0);
	if (top_nr) {
		clear_top_paths(&stats.objects.top_trees);
		clear_top_paths(&stats.objects.top_blobs);
	}
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
