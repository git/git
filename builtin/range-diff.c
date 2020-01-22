#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "range-diff.h"
#include "config.h"

static const char * const builtin_range_diff_usage[] = {
N_("git range-diff [<options>] <old-base>..<old-tip> <new-base>..<new-tip>"),
N_("git range-diff [<options>] <old-tip>...<new-tip>"),
N_("git range-diff [<options>] <base> <old-tip> <new-tip>"),
NULL
};

int cmd_range_diff(int argc, const char **argv, const char *prefix)
{
	int creation_factor = RANGE_DIFF_CREATION_FACTOR_DEFAULT;
	struct diff_options diffopt = { NULL };
	struct argv_array other_arg = ARGV_ARRAY_INIT;
	int simple_color = -1;
	struct option range_diff_options[] = {
		OPT_INTEGER(0, "creation-factor", &creation_factor,
			    N_("Percentage by which creation is weighted")),
		OPT_BOOL(0, "no-dual-color", &simple_color,
			    N_("use simple diff colors")),
		OPT_PASSTHRU_ARGV(0, "notes", &other_arg,
				  N_("notes"), N_("passed to 'git log'"),
				  PARSE_OPT_OPTARG),
		OPT_END()
	};
	struct option *options;
	int res = 0;
	struct strbuf range1 = STRBUF_INIT, range2 = STRBUF_INIT;

	git_config(git_diff_ui_config, NULL);

	repo_diff_setup(the_repository, &diffopt);

	options = parse_options_concat(range_diff_options, diffopt.parseopts);
	argc = parse_options(argc, argv, prefix, options,
			     builtin_range_diff_usage, 0);

	diff_setup_done(&diffopt);

	/* force color when --dual-color was used */
	if (!simple_color)
		diffopt.use_color = 1;

	if (argc == 2) {
		if (!strstr(argv[0], ".."))
			die(_("no .. in range: '%s'"), argv[0]);
		strbuf_addstr(&range1, argv[0]);

		if (!strstr(argv[1], ".."))
			die(_("no .. in range: '%s'"), argv[1]);
		strbuf_addstr(&range2, argv[1]);
	} else if (argc == 3) {
		strbuf_addf(&range1, "%s..%s", argv[0], argv[1]);
		strbuf_addf(&range2, "%s..%s", argv[0], argv[2]);
	} else if (argc == 1) {
		const char *b = strstr(argv[0], "..."), *a = argv[0];
		int a_len;

		if (!b) {
			error(_("single arg format must be symmetric range"));
			usage_with_options(builtin_range_diff_usage, options);
		}

		a_len = (int)(b - a);
		if (!a_len) {
			a = "HEAD";
			a_len = strlen(a);
		}
		b += 3;
		if (!*b)
			b = "HEAD";
		strbuf_addf(&range1, "%s..%.*s", b, a_len, a);
		strbuf_addf(&range2, "%.*s..%s", a_len, a, b);
	} else {
		error(_("need two commit ranges"));
		usage_with_options(builtin_range_diff_usage, options);
	}
	FREE_AND_NULL(options);

	res = show_range_diff(range1.buf, range2.buf, creation_factor,
			      simple_color < 1, &diffopt, &other_arg);

	argv_array_clear(&other_arg);
	strbuf_release(&range1);
	strbuf_release(&range2);

	return res;
}
