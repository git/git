/*
 * Copyright (c) 2005 Junio C Hamano
 */

#include "cache.h"
#include "diff.h"

static int diff_output_format = DIFF_FORMAT_HUMAN;
static int detect_rename = 0;
static int find_copies_harder = 0;
static int diff_setup_opt = 0;
static int diff_score_opt = 0;
static const char *pickaxe = NULL;
static int pickaxe_opts = 0;
static int diff_break_opt = -1;
static const char *orderfile = NULL;
static const char *diff_filter = NULL;

static char *diff_stages_usage =
"git-diff-stages [-p] [-r] [-z] [-R] [-B] [-M] [-C] [--find-copies-harder] [-O<orderfile>] [-S<string>] [--pickaxe-all] <stage1> <stage2> [<path>...]";

static void diff_stages(int stage1, int stage2)
{
	int i = 0;
	while (i < active_nr) {
		struct cache_entry *ce, *stages[4] = { NULL, };
		struct cache_entry *one, *two;
		const char *name;
		int len;
		ce = active_cache[i];
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
		if (!one && !two)
			continue;
		if (!one)
			diff_addremove('+', ntohl(two->ce_mode),
				       two->sha1, name, NULL);
		else if (!two)
			diff_addremove('-', ntohl(one->ce_mode),
				       one->sha1, name, NULL);
		else if (memcmp(one->sha1, two->sha1, 20) ||
			 (one->ce_mode != two->ce_mode) ||
			 find_copies_harder)
			diff_change(ntohl(one->ce_mode), ntohl(two->ce_mode),
				    one->sha1, two->sha1, name, NULL);
	}
}

int main(int ac, const char **av)
{
	int stage1, stage2;

	read_cache();
	while (1 < ac && av[1][0] == '-') {
		const char *arg = av[1];
		if (!strcmp(arg, "-r"))
			; /* as usual */
		else if (!strcmp(arg, "-p"))
			diff_output_format = DIFF_FORMAT_PATCH;
		else if (!strncmp(arg, "-B", 2)) {
			if ((diff_break_opt = diff_scoreopt_parse(arg)) == -1)
				usage(diff_stages_usage);
		}
		else if (!strncmp(arg, "-M", 2)) {
			detect_rename = DIFF_DETECT_RENAME;
			if ((diff_score_opt = diff_scoreopt_parse(arg)) == -1)
				usage(diff_stages_usage);
		}
		else if (!strncmp(arg, "-C", 2)) {
			detect_rename = DIFF_DETECT_COPY;
			if ((diff_score_opt = diff_scoreopt_parse(arg)) == -1)
				usage(diff_stages_usage);
		}
		else if (!strcmp(arg, "--find-copies-harder"))
			find_copies_harder = 1;
		else if (!strcmp(arg, "-z"))
			diff_output_format = DIFF_FORMAT_MACHINE;
		else if (!strcmp(arg, "-R"))
			diff_setup_opt |= DIFF_SETUP_REVERSE;
		else if (!strncmp(arg, "-S", 2))
			pickaxe = arg + 2;
		else if (!strncmp(arg, "-O", 2))
			orderfile = arg + 2;
		else if (!strncmp(arg, "--diff-filter=", 14))
			diff_filter = arg + 14;
		else if (!strcmp(arg, "--pickaxe-all"))
			pickaxe_opts = DIFF_PICKAXE_ALL;
		else
			usage(diff_stages_usage);
		ac--; av++;
	}

	if (ac < 3 ||
	    sscanf(av[1], "%d", &stage1) != 1 ||
	    ! (0 <= stage1 && stage1 <= 3) ||
	    sscanf(av[2], "%d", &stage2) != 1 ||
	    ! (0 <= stage2 && stage2 <= 3) ||
	    (find_copies_harder && detect_rename != DIFF_DETECT_COPY))
		usage(diff_stages_usage);

	av += 3; /* The rest from av[0] are for paths restriction. */
	diff_setup(diff_setup_opt);

	diff_stages(stage1, stage2);

	diffcore_std(av,
		     detect_rename, diff_score_opt,
		     pickaxe, pickaxe_opts,
		     diff_break_opt,
		     orderfile,
		     diff_filter);
	diff_flush(diff_output_format);
	return 0;
}
