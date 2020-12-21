#ifndef DIFF_MERGES_H
#define DIFF_MERGES_H

/*
 * diff-merges - utility module to handle command-line options for
 * selection of particular diff format of merge commits
 * representation.
 */

struct rev_info;

void diff_merges_init_revs(struct rev_info *revs);

int diff_merges_parse_opts(struct rev_info *revs, const char **argv);

void diff_merges_setup_revs(struct rev_info *revs);

void diff_merges_default_to_dense_combined(struct rev_info *revs);

void diff_merges_first_parent_defaults_to_enable(struct rev_info *revs);


#endif
