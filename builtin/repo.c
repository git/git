#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "environment.h"
#include "parse-options.h"
#include "quote.h"
#include "ref-filter.h"
#include "refs.h"
#include "strbuf.h"
#include "string-list.h"
#include "shallow.h"
#include "utf8.h"

static const char *const repo_usage[] = {
	"git repo info [--format=(keyvalue|nul)] [-z] [<key>...]",
	"git repo structure",
	NULL
};

typedef int get_value_fn(struct repository *repo, struct strbuf *buf);

enum output_format {
	FORMAT_KEYVALUE,
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

static int print_fields(int argc, const char **argv,
			struct repository *repo,
			enum output_format format)
{
	int ret = 0;
	struct strbuf valbuf = STRBUF_INIT;
	struct strbuf quotbuf = STRBUF_INIT;

	for (int i = 0; i < argc; i++) {
		get_value_fn *get_value;
		const char *key = argv[i];

		get_value = get_value_fn_for_key(key);

		if (!get_value) {
			ret = error(_("key '%s' not found"), key);
			continue;
		}

		strbuf_reset(&valbuf);
		strbuf_reset(&quotbuf);

		get_value(repo, &valbuf);

		switch (format) {
		case FORMAT_KEYVALUE:
			quote_c_style(valbuf.buf, &quotbuf, NULL, 0);
			printf("%s=%s\n", key, quotbuf.buf);
			break;
		case FORMAT_NUL_TERMINATED:
			printf("%s\n%s%c", key, valbuf.buf, '\0');
			break;
		default:
			BUG("not a valid output format: %d", format);
		}
	}

	strbuf_release(&valbuf);
	strbuf_release(&quotbuf);
	return ret;
}

static int parse_format_cb(const struct option *opt,
			   const char *arg, int unset UNUSED)
{
	enum output_format *format = opt->value;

	if (opt->short_name == 'z')
		*format = FORMAT_NUL_TERMINATED;
	else if (!strcmp(arg, "nul"))
		*format = FORMAT_NUL_TERMINATED;
	else if (!strcmp(arg, "keyvalue"))
		*format = FORMAT_KEYVALUE;
	else
		die(_("invalid format '%s'"), arg);

	return 0;
}

static int cmd_repo_info(int argc, const char **argv, const char *prefix,
			 struct repository *repo)
{
	enum output_format format = FORMAT_KEYVALUE;
	struct option options[] = {
		OPT_CALLBACK_F(0, "format", &format, N_("format"),
			       N_("output format"),
			       PARSE_OPT_NONEG, parse_format_cb),
		OPT_CALLBACK_F('z', NULL, &format, NULL,
			       N_("synonym for --format=nul"),
			       PARSE_OPT_NONEG | PARSE_OPT_NOARG,
			       parse_format_cb),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, repo_usage, 0);

	return print_fields(argc, argv, repo, format);
}

struct ref_stats {
	size_t branches;
	size_t remotes;
	size_t tags;
	size_t others;
};

struct stats_table {
	struct string_list rows;

	int name_col_width;
	int value_col_width;
};

/*
 * Holds column data that gets stored for each row.
 */
struct stats_table_entry {
	char *value;
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
	if (entry) {
		int value_width = utf8_strwidth(entry->value);
		if (value_width > table->value_col_width)
			table->value_col_width = value_width;
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
	entry->value = xstrfmt("%" PRIuMAX, (uintmax_t)value);

	va_start(ap, format);
	stats_table_vaddf(table, entry, format, ap);
	va_end(ap);
}

static inline size_t get_total_reference_count(struct ref_stats *stats)
{
	return stats->branches + stats->remotes + stats->tags + stats->others;
}

static void stats_table_setup_structure(struct stats_table *table,
					struct ref_stats *refs)
{
	size_t ref_total;

	ref_total = get_total_reference_count(refs);
	stats_table_addf(table, "* %s", _("References"));
	stats_table_count_addf(table, ref_total, "  * %s", _("Count"));
	stats_table_count_addf(table, refs->branches, "    * %s", _("Branches"));
	stats_table_count_addf(table, refs->tags, "    * %s", _("Tags"));
	stats_table_count_addf(table, refs->remotes, "    * %s", _("Remotes"));
	stats_table_count_addf(table, refs->others, "    * %s", _("Others"));
}

static void stats_table_print_structure(const struct stats_table *table)
{
	const char *name_col_title = _("Repository structure");
	const char *value_col_title = _("Value");
	int name_col_width = utf8_strwidth(name_col_title);
	int value_col_width = utf8_strwidth(value_col_title);
	struct string_list_item *item;

	if (table->name_col_width > name_col_width)
		name_col_width = table->name_col_width;
	if (table->value_col_width > value_col_width)
		value_col_width = table->value_col_width;

	printf("| %-*s | %-*s |\n", name_col_width, name_col_title,
	       value_col_width, value_col_title);
	printf("| ");
	for (int i = 0; i < name_col_width; i++)
		putchar('-');
	printf(" | ");
	for (int i = 0; i < value_col_width; i++)
		putchar('-');
	printf(" |\n");

	for_each_string_list_item(item, &table->rows) {
		struct stats_table_entry *entry = item->util;
		const char *value = "";

		if (entry) {
			value = entry->value;
		}

		printf("| %-*s | %*s |\n", name_col_width, item->string,
		       value_col_width, value);
	}
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

static int count_references(const char *refname,
			    const char *referent UNUSED,
			    const struct object_id *oid UNUSED,
			    int flags UNUSED, void *cb_data)
{
	struct ref_stats *stats = cb_data;

	switch (ref_kind_from_refname(refname)) {
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

	return 0;
}

static void structure_count_references(struct ref_stats *stats,
				       struct repository *repo)
{
	refs_for_each_ref(get_main_ref_store(repo), count_references, &stats);
}

static int cmd_repo_structure(int argc, const char **argv, const char *prefix,
			      struct repository *repo)
{
	struct stats_table table = {
		.rows = STRING_LIST_INIT_DUP,
	};
	struct ref_stats stats = { 0 };
	struct option options[] = { 0 };

	argc = parse_options(argc, argv, prefix, options, repo_usage, 0);
	if (argc)
		usage(_("too many arguments"));

	structure_count_references(&stats, repo);

	stats_table_setup_structure(&table, &stats);
	stats_table_print_structure(&table);

	stats_table_clear(&table);

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
