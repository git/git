#ifndef DIFF_MERGES_H
#define DIFF_MERGES_H

/*
 * diff-merges - utility module to handle command-line options for
 * selection of particular diff format of merge commits
 * representation.
 */

struct rev_info;

int diff_merges_config(const char *value);

void diff_merges_suppress_m_parsing(void);

int diff_merges_parse_opts(struct rev_info *revs, const char **argv);

void diff_merges_suppress(struct rev_info *revs);

void diff_merges_default_to_first_parent(struct rev_info *revs);

void diff_merges_default_to_dense_combined(struct rev_info *revs);

void diff_merges_set_dense_combined_if_unset(struct rev_info *revs);

void diff_merges_setup_revs(struct rev_info *revs);

#endif
