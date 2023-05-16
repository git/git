#include "cache.h"
#include "builtin.h"
#include "gettext.h"
#include "object-name.h"
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
	int i, dash_dash = -1, res = 0;
	struct strbuf range1 = STRBUF_INIT, range2 = STRBUF_INIT;
	struct object_id oid;
	const char *three_dots = NULL;

	git_config(git_diff_ui_config, NULL);

	repo_diff_setup(the_repository, &diffopt);

	options = add_diff_options(range_diff_options, &diffopt);
	argc = parse_options(argc, argv, prefix, options,
			     builtin_range_diff_usage, PARSE_OPT_KEEP_DASHDASH);

	diff_setup_done(&diffopt);

	/* force color when --dual-color was used */
	if (!simple_color)
		diffopt.use_color = 1;

	for (i = 0; i < argc; i++)
		if (!strcmp(argv[i], "--")) {
			dash_dash = i;
			break;
		}

	if (dash_dash == 3 ||
	    (dash_dash < 0 && argc > 2 &&
	     !repo_get_oid_committish(the_repository, argv[0], &oid) &&
	     !repo_get_oid_committish(the_repository, argv[1], &oid) &&
	     !repo_get_oid_committish(the_repository, argv[2], &oid))) {
		if (dash_dash < 0)
			; /* already validated arguments */
		else if (repo_get_oid_committish(the_repository, argv[0], &oid))
			usage_msg_optf(_("not a revision: '%s'"),
				       builtin_range_diff_usage, options,
				       argv[0]);
		else if (repo_get_oid_committish(the_repository, argv[1], &oid))
			usage_msg_optf(_("not a revision: '%s'"),
				       builtin_range_diff_usage, options,
				       argv[1]);
		else if (repo_get_oid_committish(the_repository, argv[2], &oid))
			usage_msg_optf(_("not a revision: '%s'"),
				       builtin_range_diff_usage, options,
				       argv[2]);

		strbuf_addf(&range1, "%s..%s", argv[0], argv[1]);
		strbuf_addf(&range2, "%s..%s", argv[0], argv[2]);

		strvec_pushv(&other_arg, argv +
			     (dash_dash < 0 ? 3 : dash_dash));
	} else if (dash_dash == 2 ||
		   (dash_dash < 0 && argc > 1 &&
		    is_range_diff_range(argv[0]) &&
		    is_range_diff_range(argv[1]))) {
		if (dash_dash < 0)
			; /* already validated arguments */
		else if (!is_range_diff_range(argv[0]))
			usage_msg_optf(_("not a commit range: '%s'"),
				       builtin_range_diff_usage, options,
				       argv[0]);
		else if (!is_range_diff_range(argv[1]))
			usage_msg_optf(_("not a commit range: '%s'"),
				       builtin_range_diff_usage, options,
				       argv[1]);

		strbuf_addstr(&range1, argv[0]);
		strbuf_addstr(&range2, argv[1]);

		strvec_pushv(&other_arg, argv +
			     (dash_dash < 0 ? 2 : dash_dash));
	} else if (dash_dash == 1 ||
		   (dash_dash < 0 && argc > 0 &&
		    (three_dots = strstr(argv[0], "...")))) {
		const char *a, *b;
		int a_len;

		if (dash_dash < 0)
			; /* already validated arguments */
		else if (!(three_dots = strstr(argv[0], "...")))
			usage_msg_optf(_("not a symmetric range: '%s'"),
					 builtin_range_diff_usage, options,
					 argv[0]);

		if (three_dots == argv[0]) {
			a = "HEAD";
			a_len = strlen(a);
		} else {
			a = argv[0];
			a_len = (int)(three_dots - a);
		}

		if (three_dots[3])
			b = three_dots + 3;
		else
			b = "HEAD";

		strbuf_addf(&range1, "%s..%.*s", b, a_len, a);
		strbuf_addf(&range2, "%.*s..%s", a_len, a, b);

		strvec_pushv(&other_arg, argv +
			     (dash_dash < 0 ? 1 : dash_dash));
	} else
		usage_msg_opt(_("need two commit ranges"),
			      builtin_range_diff_usage, options);
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
