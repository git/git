#include "builtin.h"
#include "cache.h"
#include "refs.h"
#include "object.h"
#include "parse-options.h"
#include "ref-filter.h"

static char const * const for_each_ref_usage[] = {
	N_("git for-each-ref [<options>] [<pattern>]"),
	NULL
};

int cmd_for_each_ref(int argc, const char **argv, const char *prefix)
{
	int i;
	const char *format = "%(objectname) %(objecttype)\t%(refname)";
	struct ref_sorting *sorting = NULL, **sorting_tail = &sorting;
	int maxcount = 0, quote_style = 0;
	struct ref_filter_cbdata ref_cbdata;

	struct option opts[] = {
		OPT_BIT('s', "shell", &quote_style,
			N_("quote placeholders suitably for shells"), QUOTE_SHELL),
		OPT_BIT('p', "perl",  &quote_style,
			N_("quote placeholders suitably for perl"), QUOTE_PERL),
		OPT_BIT(0 , "python", &quote_style,
			N_("quote placeholders suitably for python"), QUOTE_PYTHON),
		OPT_BIT(0 , "tcl",  &quote_style,
			N_("quote placeholders suitably for Tcl"), QUOTE_TCL),

		OPT_GROUP(""),
		OPT_INTEGER( 0 , "count", &maxcount, N_("show only <n> matched refs")),
		OPT_STRING(  0 , "format", &format, N_("format"), N_("format to use for the output")),
		OPT_CALLBACK(0 , "sort", sorting_tail, N_("key"),
			    N_("field name to sort on"), &parse_opt_ref_sorting),
		OPT_END(),
	};

	parse_options(argc, argv, prefix, opts, for_each_ref_usage, 0);
	if (maxcount < 0) {
		error("invalid --count argument: `%d'", maxcount);
		usage_with_options(for_each_ref_usage, opts);
	}
	if (HAS_MULTI_BITS(quote_style)) {
		error("more than one quoting style?");
		usage_with_options(for_each_ref_usage, opts);
	}
	if (verify_ref_format(format))
		usage_with_options(for_each_ref_usage, opts);

	if (!sorting)
		sorting = ref_default_sorting();

	/* for warn_ambiguous_refs */
	git_config(git_default_config, NULL);

	memset(&ref_cbdata, 0, sizeof(ref_cbdata));
	ref_cbdata.filter.name_patterns = argv;
	for_each_rawref(ref_filter_handler, &ref_cbdata);

	ref_array_sort(sorting, &ref_cbdata.array);

	if (!maxcount || ref_cbdata.array.nr < maxcount)
		maxcount = ref_cbdata.array.nr;
	for (i = 0; i < maxcount; i++)
		show_ref_array_item(ref_cbdata.array.items[i], format, quote_style);
	ref_array_clear(&ref_cbdata.array);
	return 0;
}
