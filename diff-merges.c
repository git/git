#include "diff-merges.h"

#include "revision.h"

void diff_merges_init_revs(struct rev_info *revs)
{
	revs->ignore_merges = -1;
}

int diff_merges_parse_opts(struct rev_info *revs, const char **argv)
{
	int argcount = 1;
	const char *optarg;
	const char *arg = argv[0];

	if (!strcmp(arg, "-m")) {
		/*
		 * To "diff-index", "-m" means "match missing", and to the "log"
		 * family of commands, it means "show full diff for merges". Set
		 * both fields appropriately.
		 */
		revs->ignore_merges = 0;
		revs->match_missing = 1;
	} else if (!strcmp(arg, "-c")) {
		revs->diff = 1;
		revs->dense_combined_merges = 0;
		revs->combine_merges = 1;
	} else if (!strcmp(arg, "--cc")) {
		revs->diff = 1;
		revs->dense_combined_merges = 1;
		revs->combine_merges = 1;
	} else if (!strcmp(arg, "--no-diff-merges")) {
		revs->ignore_merges = 1;
	} else if (!strcmp(arg, "--combined-all-paths")) {
		revs->diff = 1;
		revs->combined_all_paths = 1;
	} else if ((argcount = parse_long_opt("diff-merges", argv, &optarg))) {
		if (!strcmp(optarg, "off")) {
			revs->ignore_merges = 1;
		} else {
			die(_("unknown value for --diff-merges: %s"), optarg);
		}
	} else
		argcount = 0;

	return argcount;
}

void diff_merges_setup_revs(struct rev_info *revs)
{
	if (revs->combine_merges && revs->ignore_merges < 0)
		revs->ignore_merges = 0;
	if (revs->ignore_merges < 0)
		revs->ignore_merges = 1;
	if (revs->combined_all_paths && !revs->combine_merges)
		die("--combined-all-paths makes no sense without -c or --cc");
}

void diff_merges_default_to_enable(struct rev_info *revs)
{
	if (revs->ignore_merges < 0)		/* No -m */
		revs->ignore_merges = 0;
}

void diff_merges_default_to_dense_combined(struct rev_info *revs)
{
	if (revs->ignore_merges < 0) {		/* No -m */
		revs->ignore_merges = 0;
		if (!revs->combine_merges) {	/* No -c/--cc" */
			revs->combine_merges = 1;
			revs->dense_combined_merges = 1;
		}
	}
}
