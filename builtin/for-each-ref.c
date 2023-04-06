#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "gettext.h"
#include "refs.h"
#include "object.h"
#include "parse-options.h"
#include "ref-filter.h"
#include "strvec.h"
#include "commit-reach.h"

static char const * const for_each_ref_usage[] = {
	N_("git for-each-ref [<options>] [<pattern>]"),
	N_("git for-each-ref [--points-at <object>]"),
	N_("git for-each-ref [--merged [<commit>]] [--no-merged [<commit>]]"),
	N_("git for-each-ref [--contains [<commit>]] [--no-contains [<commit>]]"),
	NULL
};

int cmd_for_each_ref(int argc, const char **argv, const char *prefix)
{
	int i;
	struct ref_sorting *sorting;
	struct string_list sorting_options = STRING_LIST_INIT_DUP;
	int maxcount = 0, icase = 0;
	struct ref_array array;
	struct ref_filter filter;
	struct ref_format format = REF_FORMAT_INIT;
	struct strbuf output = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;
	int from_stdin = 0;
	struct strvec vec = STRVEC_INIT;

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
		OPT_REF_SORT(&sorting_options),
		OPT_CALLBACK(0, "points-at", &filter.points_at,
			     N_("object"), N_("print only refs which points at the given object"),
			     parse_opt_object_name),
		OPT_MERGED(&filter, N_("print only refs that are merged")),
		OPT_NO_MERGED(&filter, N_("print only refs that are not merged")),
		OPT_CONTAINS(&filter.with_commit, N_("print only refs which contain the commit")),
		OPT_NO_CONTAINS(&filter.no_commit, N_("print only refs which don't contain the commit")),
		OPT_BOOL(0, "ignore-case", &icase, N_("sorting and filtering are case insensitive")),
		OPT_BOOL(0, "stdin", &from_stdin, N_("read reference patterns from stdin")),
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

	sorting = ref_sorting_options(&sorting_options);
	ref_sorting_set_sort_flags_all(sorting, REF_SORTING_ICASE, icase);
	filter.ignore_case = icase;

	if (from_stdin) {
		struct strbuf line = STRBUF_INIT;

		if (argv[0])
			die(_("unknown arguments supplied with --stdin"));

		while (strbuf_getline(&line, stdin) != EOF)
			strvec_push(&vec, line.buf);

		strbuf_release(&line);

		/* vec.v is NULL-terminated, just like 'argv'. */
		filter.name_patterns = vec.v;
	} else {
		filter.name_patterns = argv;
	}

	filter.match_as_path = 1;
	filter_refs(&array, &filter, FILTER_REFS_ALL);
	filter_ahead_behind(the_repository, &format, &array);

	ref_array_sort(sorting, &array);

	if (!maxcount || array.nr < maxcount)
		maxcount = array.nr;
	for (i = 0; i < maxcount; i++) {
		strbuf_reset(&err);
		strbuf_reset(&output);
		if (format_ref_array_item(array.items[i], &format, &output, &err))
			die("%s", err.buf);
		fwrite(output.buf, 1, output.len, stdout);
		putchar('\n');
	}

	strbuf_release(&err);
	strbuf_release(&output);
	ref_array_clear(&array);
	free_commit_list(filter.with_commit);
	free_commit_list(filter.no_commit);
	ref_sorting_release(sorting);
	strvec_clear(&vec);
	return 0;
}
