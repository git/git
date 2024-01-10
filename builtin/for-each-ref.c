#include "builtin.h"
#include "commit.h"
#include "config.h"
#include "gettext.h"
#include "object.h"
#include "parse-options.h"
#include "ref-filter.h"
#include "strbuf.h"
#include "strvec.h"

static char const * const for_each_ref_usage[] = {
	N_("git for-each-ref [<options>] [<pattern>]"),
	N_("git for-each-ref [--points-at <object>]"),
	N_("git for-each-ref [--merged [<commit>]] [--no-merged [<commit>]]"),
	N_("git for-each-ref [--contains [<commit>]] [--no-contains [<commit>]]"),
	NULL
};

int cmd_for_each_ref(int argc, const char **argv, const char *prefix)
{
	struct ref_sorting *sorting;
	struct string_list sorting_options = STRING_LIST_INIT_DUP;
	int icase = 0;
	struct ref_filter filter = REF_FILTER_INIT;
	struct ref_format format = REF_FORMAT_INIT;
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
		OPT_BOOL(0, "omit-empty",  &format.array_opts.omit_empty,
			N_("do not output a newline after empty formatted refs")),

		OPT_GROUP(""),
		OPT_INTEGER( 0 , "count", &format.array_opts.max_count, N_("show only <n> matched refs")),
		OPT_STRING(  0 , "format", &format.format, N_("format"), N_("format to use for the output")),
		OPT__COLOR(&format.use_color, N_("respect format colors")),
		OPT_REF_FILTER_EXCLUDE(&filter),
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

	format.format = "%(objectname) %(objecttype)\t%(refname)";

	git_config(git_default_config, NULL);

	/* Set default (refname) sorting */
	string_list_append(&sorting_options, "refname");

	parse_options(argc, argv, prefix, opts, for_each_ref_usage, 0);
	if (format.array_opts.max_count < 0) {
		error("invalid --count argument: `%d'", format.array_opts.max_count);
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
	filter_and_format_refs(&filter, FILTER_REFS_ALL, sorting, &format);

	ref_filter_clear(&filter);
	ref_sorting_release(sorting);
	strvec_clear(&vec);
	return 0;
}
