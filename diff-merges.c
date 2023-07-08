#include "git-compat-util.h"
#include "diff-merges.h"

#include "gettext.h"
#include "revision.h"

typedef void (*diff_merges_setup_func_t)(struct rev_info *);
static void set_separate(struct rev_info *revs);

static diff_merges_setup_func_t set_to_default = set_separate;
static int suppress_m_parsing;

static void suppress(struct rev_info *revs)
{
	revs->separate_merges = 0;
	revs->first_parent_merges = 0;
	revs->combine_merges = 0;
	revs->dense_combined_merges = 0;
	revs->combined_all_paths = 0;
	revs->merges_imply_patch = 0;
	revs->merges_need_diff = 0;
	revs->remerge_diff = 0;
}

static void common_setup(struct rev_info *revs)
{
	suppress(revs);
	revs->merges_need_diff = 1;
}

static void set_none(struct rev_info *revs)
{
	suppress(revs);
}

static void set_separate(struct rev_info *revs)
{
	common_setup(revs);
	revs->separate_merges = 1;
	revs->simplify_history = 0;
}

static void set_first_parent(struct rev_info *revs)
{
	set_separate(revs);
	revs->first_parent_merges = 1;
}

static void set_combined(struct rev_info *revs)
{
	common_setup(revs);
	revs->combine_merges = 1;
	revs->dense_combined_merges = 0;
}

static void set_dense_combined(struct rev_info *revs)
{
	common_setup(revs);
	revs->combine_merges = 1;
	revs->dense_combined_merges = 1;
}

static void set_remerge_diff(struct rev_info *revs)
{
	common_setup(revs);
	revs->remerge_diff = 1;
	revs->simplify_history = 0;
}

static diff_merges_setup_func_t func_by_opt(const char *optarg)
{
	if (!strcmp(optarg, "off") || !strcmp(optarg, "none"))
		return set_none;
	if (!strcmp(optarg, "1") || !strcmp(optarg, "first-parent"))
		return set_first_parent;
	if (!strcmp(optarg, "separate"))
		return set_separate;
	if (!strcmp(optarg, "c") || !strcmp(optarg, "combined"))
		return set_combined;
	if (!strcmp(optarg, "cc") || !strcmp(optarg, "dense-combined"))
		return set_dense_combined;
	if (!strcmp(optarg, "r") || !strcmp(optarg, "remerge"))
		return set_remerge_diff;
	if (!strcmp(optarg, "m") || !strcmp(optarg, "on"))
		return set_to_default;
	return NULL;
}

static void set_diff_merges(struct rev_info *revs, const char *optarg)
{
	diff_merges_setup_func_t func = func_by_opt(optarg);

	if (!func)
		die(_("invalid value for '%s': '%s'"), "--diff-merges", optarg);

	func(revs);
}

/*
 * Public functions. They are in the order they are called.
 */

int diff_merges_config(const char *value)
{
	diff_merges_setup_func_t func = func_by_opt(value);

	if (!func)
		return -1;

	set_to_default = func;
	return 0;
}

void diff_merges_suppress_m_parsing(void)
{
	suppress_m_parsing = 1;
}

int diff_merges_parse_opts(struct rev_info *revs, const char **argv)
{
	int argcount = 1;
	const char *optarg;
	const char *arg = argv[0];

	if (!suppress_m_parsing && !strcmp(arg, "-m")) {
		set_to_default(revs);
		revs->merges_need_diff = 0;
	} else if (!strcmp(arg, "-c")) {
		set_combined(revs);
		revs->merges_imply_patch = 1;
	} else if (!strcmp(arg, "--cc")) {
		set_dense_combined(revs);
		revs->merges_imply_patch = 1;
	} else if (!strcmp(arg, "--remerge-diff")) {
		set_remerge_diff(revs);
		revs->merges_imply_patch = 1;
	} else if (!strcmp(arg, "--no-diff-merges")) {
		set_none(revs);
	} else if (!strcmp(arg, "--combined-all-paths")) {
		revs->combined_all_paths = 1;
	} else if ((argcount = parse_long_opt("diff-merges", argv, &optarg))) {
		set_diff_merges(revs, optarg);
	} else
		return 0;

	revs->explicit_diff_merges = 1;
	return argcount;
}

void diff_merges_suppress(struct rev_info *revs)
{
	set_none(revs);
}

void diff_merges_default_to_first_parent(struct rev_info *revs)
{
	if (!revs->explicit_diff_merges)
		revs->separate_merges = 1;
	if (revs->separate_merges)
		revs->first_parent_merges = 1;
}

void diff_merges_default_to_dense_combined(struct rev_info *revs)
{
	if (!revs->explicit_diff_merges)
		set_dense_combined(revs);
}

void diff_merges_set_dense_combined_if_unset(struct rev_info *revs)
{
	if (!revs->combine_merges)
		set_dense_combined(revs);
}

void diff_merges_setup_revs(struct rev_info *revs)
{
	if (revs->combine_merges == 0)
		revs->dense_combined_merges = 0;
	if (revs->separate_merges == 0)
		revs->first_parent_merges = 0;
	if (revs->combined_all_paths && !revs->combine_merges)
		die("--combined-all-paths makes no sense without -c or --cc");
	if (revs->merges_imply_patch)
		revs->diff = 1;
	if (revs->merges_imply_patch || revs->merges_need_diff) {
		if (!revs->diffopt.output_format)
			revs->diffopt.output_format = DIFF_FORMAT_PATCH;
	}
}
