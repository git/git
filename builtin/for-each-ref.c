#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "refs.h"
#include "object.h"
#include "parse-options.h"
#include "ref-filter.h"

static char const * const for_each_ref_usage[] = {
	N_("git for-each-ref [<options>] [<pattern>]"),
	N_("git for-each-ref [--points-at <object>]"),
	N_("git for-each-ref [(--merged | --no-merged) [<commit>]]"),
	N_("git for-each-ref [--contains [<commit>]] [--no-contains [<commit>]]"),
	NULL
};

int cmd_for_each_ref(int argc, const char **argv, const char *prefix)
{
	int i;
	struct ref_sorting *sorting = NULL, **sorting_tail = &sorting;
	int maxcount = 0, icase = 0;
	struct ref_array array;
	struct ref_filter filter;
	struct ref_format format = REF_FORMAT_INIT;

	struct option opts[] = {
		OPT_BIT('s', "shell", &format.quote_style,
			N_("quote placeholders suitably for shells"), QUOTE_SHELL),
		OPT_BIT('p', "perl",  &format.quote_style,
			N_("quote placeholders suitably for perl"), QUOTE_PERL),
		OPT_BIT(0 , "python", &format.quote_style,
			N_("quote placeholders suitably for python"), QUOTE_PYTHON),
		OPT_BIT(0 , "tcl",  &format.quote_style,
			N_("quote placeholders suitably for Tcl"), QUOTE_TCL),

		OPT_GROUP(""),
		OPT_INTEGER( 0 , "count", &maxcount, N_("show only <n> matched refs")),
		OPT_STRING(  0 , "format", &format.format, N_("format"), N_("format to use for the output")),
		OPT__COLOR(&format.use_color, N_("respect format colors")),
		OPT_CALLBACK(0 , "sort", sorting_tail, N_("key"),
			    N_("field name to sort on"), &parse_opt_ref_sorting),
		OPT_CALLBACK(0, "points-at", &filter.points_at,
			     N_("object"), N_("print only refs which points at the given object"),
			     parse_opt_object_name),
		OPT_MERGED(&filter, N_("print only refs that are merged")),
		OPT_NO_MERGED(&filter, N_("print only refs that are not merged")),
		OPT_CONTAINS(&filter.with_commit, N_("print only refs which contain the commit")),
		OPT_NO_CONTAINS(&filter.no_commit, N_("print only refs which don't contain the commit")),
		OPT_BOOL(0, "ignore-case", &icase, N_("sorting and filtering are case insensitive")),
		OPT_END(),
	};

	memset(&array, 0, sizeof(array));
	memset(&filter, 0, sizeof(filter));

	format.format = "%(objectname) %(objecttype)\t%(refname)";

	git_config(git_default_config, NULL);

	parse_options(argc, argv, prefix, opts, for_each_ref_usage, 0);
	if (maxcount < 0) {
		error("invalid --count argument: `%d'", maxcount);
		usage_with_options(for_each_ref_usage, opts);
	}
	if (HAS_MULTI_BITS(format.quote_style)) {
		error("more than one quoting style?");
		usage_with_options(for_each_ref_usage, opts);
	}
	if (verify_ref_format(&format))
		usage_with_options(for_each_ref_usage, opts);

	if (!sorting)
		sorting = ref_default_sorting();
	sorting->ignore_case = icase;
	filter.ignore_case = icase;

	filter.name_patterns = argv;
	filter.match_as_path = 1;
	filter_refs(&array, &filter, FILTER_REFS_ALL | FILTER_REFS_INCLUDE_BROKEN);
	ref_array_sort(sorting, &array);

	if (!maxcount || array.nr < maxcount)
		maxcount = array.nr;
	for (i = 0; i < maxcount; i++)
		show_ref_array_item(array.items[i], &format);
	ref_array_clear(&array);
	return 0;
}
