/*
 * Builtin "git log" and related commands (show, whatchanged)
 *
 * (C) Copyright 2006 Linus Torvalds
 *		 2006 Junio Hamano
 */
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "log-tree.h"

static int cmd_log_wc(int argc, const char **argv, char **envp,
		      struct rev_info *rev)
{
	struct commit *commit;

	rev->abbrev = DEFAULT_ABBREV;
	rev->commit_format = CMIT_FMT_DEFAULT;
	rev->verbose_header = 1;
	argc = setup_revisions(argc, argv, rev, "HEAD");
	if (rev->always_show_header) {
		if (rev->diffopt.pickaxe || rev->diffopt.filter) {
			rev->always_show_header = 0;
			if (rev->diffopt.output_format == DIFF_FORMAT_RAW)
				rev->diffopt.output_format = DIFF_FORMAT_NO_OUTPUT;
		}
	}

	if (argc > 1)
		die("unrecognized argument: %s", argv[1]);

	prepare_revision_walk(rev);
	setup_pager();
	while ((commit = get_revision(rev)) != NULL) {
		log_tree_commit(rev, commit);
		free(commit->buffer);
		commit->buffer = NULL;
	}
	return 0;
}

int cmd_whatchanged(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.diff = 1;
	rev.diffopt.recursive = 1;
	return cmd_log_wc(argc, argv, envp, &rev);
}

int cmd_show(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.diff = 1;
	rev.diffopt.recursive = 1;
	rev.combine_merges = 1;
	rev.dense_combined_merges = 1;
	rev.always_show_header = 1;
	rev.ignore_merges = 0;
	rev.no_walk = 1;
	return cmd_log_wc(argc, argv, envp, &rev);
}

int cmd_log(int argc, const char **argv, char **envp)
{
	struct rev_info rev;

	init_revisions(&rev);
	rev.always_show_header = 1;
	rev.diffopt.recursive = 1;
	return cmd_log_wc(argc, argv, envp, &rev);
}
