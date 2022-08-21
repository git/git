#include "cache.h"
#include "builtin.h"
#include "parse-options.h"
#include "range-diff.h"
#include "config.h"
#include "revision.h"

static const char * const builtin_range_diff_usage[] = {
N_("git range-diff [<options>] <old-base>..<old-tip> <new-base>..<new-tip>"),
N_("git range-diff [<options>] <old-tip>...<new-tip>"),
N_("git range-diff [<options>] <base> <old-tip> <new-tip>"),
NULL
};

int cmd_range_diff(int argc, const char **argv, const char *prefix)
{
	struct diff_options diffopt = { NULL };
	struct strvec other_arg = STRVEC_INIT;
	struct range_diff_options range_diff_opts = {
		.creation_factor = RANGE_DIFF_CREATION_FACTOR_DEFAULT,
		.diffopt = &diffopt,
		.other_arg = &other_arg
	};
	int simple_color = -1, left_only = 0, right_only = 0;
	struct option range_diff_options[] = {
		OPT_INTEGER(0, "creation-factor",
			    &range_diff_opts.creation_factor,
			    N_("percentage by which creation is weighted")),
		OPT_BOOL(0, "no-dual-color", &simple_color,
			    N_("use simple diff colors")),
		OPT_PASSTHRU_ARGV(0, "notes", &other_arg,
				  N_("notes"), N_("passed to 'git log'"),
				  PARSE_OPT_OPTARG),
		OPT_BOOL(0, "left-only", &left_only,
			 N_("only emit output related to the first range")),
		OPT_BOOL(0, "right-only", &right_only,
			 N_("only emit output related to the second range")),
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
		if (!is_range_diff_range(argv[0]))
			die(_("not a commit range: '%s'"), argv[0]);
		strbuf_addstr(&range1, argv[0]);

		if (!is_range_diff_range(argv[1]))
			die(_("not a commit range: '%s'"), argv[1]);
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

	range_diff_opts.dual_color = simple_color < 1;
	range_diff_opts.left_only = left_only;
	range_diff_opts.right_only = right_only;
	res = show_range_diff(range1.buf, range2.buf, &range_diff_opts);

	strvec_clear(&other_arg);
	strbuf_release(&range1);
	strbuf_release(&range2);

	return res;
}
