/*
 * Copyright (c) 2005 Junio C Hamano
 */

#include "cache.h"
#include "diff.h"
#include "builtin.h"

static struct diff_options diff_options;

static const char diff_stages_usage[] =
"git-diff-stages [<common diff options>] <stage1> <stage2> [<path>...]"
COMMON_DIFF_OPTIONS_HELP;

static void diff_stages(int stage1, int stage2, const char **pathspec)
{
	int i = 0;
	while (i < active_nr) {
		struct cache_entry *ce, *stages[4] = { NULL, };
		struct cache_entry *one, *two;
		const char *name;
		int len, skip;

		ce = active_cache[i];
		skip = !ce_path_match(ce, pathspec);
		len = ce_namelen(ce);
		name = ce->name;
		for (;;) {
			int stage = ce_stage(ce);
			stages[stage] = ce;
			if (active_nr <= ++i)
				break;
			ce = active_cache[i];
			if (ce_namelen(ce) != len ||
			    memcmp(name, ce->name, len))
				break;
		}
		one = stages[stage1];
		two = stages[stage2];

		if (skip || (!one && !two))
			continue;
		if (!one)
			diff_addremove(&diff_options, '+', ntohl(two->ce_mode),
				       two->sha1, name, NULL);
		else if (!two)
			diff_addremove(&diff_options, '-', ntohl(one->ce_mode),
				       one->sha1, name, NULL);
		else if (memcmp(one->sha1, two->sha1, 20) ||
			 (one->ce_mode != two->ce_mode) ||
			 diff_options.find_copies_harder)
			diff_change(&diff_options,
				    ntohl(one->ce_mode), ntohl(two->ce_mode),
				    one->sha1, two->sha1, name, NULL);
	}
}

int cmd_diff_stages(int ac, const char **av, char **envp)
{
	int stage1, stage2;
	const char *prefix = setup_git_directory();
	const char **pathspec = NULL;

	git_config(git_diff_config);
	read_cache();
	diff_setup(&diff_options);
	while (1 < ac && av[1][0] == '-') {
		const char *arg = av[1];
		if (!strcmp(arg, "-r"))
			; /* as usual */
		else {
			int diff_opt_cnt;
			diff_opt_cnt = diff_opt_parse(&diff_options,
						      av+1, ac-1);
			if (diff_opt_cnt < 0)
				usage(diff_stages_usage);
			else if (diff_opt_cnt) {
				av += diff_opt_cnt;
				ac -= diff_opt_cnt;
				continue;
			}
			else
				usage(diff_stages_usage);
		}
		ac--; av++;
	}

	if (!diff_options.output_format)
		diff_options.output_format = DIFF_FORMAT_RAW;

	if (ac < 3 ||
	    sscanf(av[1], "%d", &stage1) != 1 ||
	    ! (0 <= stage1 && stage1 <= 3) ||
	    sscanf(av[2], "%d", &stage2) != 1 ||
	    ! (0 <= stage2 && stage2 <= 3))
		usage(diff_stages_usage);

	av += 3; /* The rest from av[0] are for paths restriction. */
	pathspec = get_pathspec(prefix, av);

	if (diff_setup_done(&diff_options) < 0)
		usage(diff_stages_usage);

	diff_stages(stage1, stage2, pathspec);
	diffcore_std(&diff_options);
	diff_flush(&diff_options);
	return 0;
}
