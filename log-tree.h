#ifndef LOG_TREE_H
#define LOG_TREE_H

#include "revision.h"

struct log_tree_opt {
	struct diff_options diffopt;
	int show_root_diff;
	int no_commit_id;
	int verbose_header;
	int ignore_merges;
	int combine_merges;
	int dense_combined_merges;
	int always_show_header;
	const char *header_prefix;
	const char *header;
	enum cmit_fmt commit_format;
};

void init_log_tree_opt(struct log_tree_opt *);
int log_tree_diff_flush(struct log_tree_opt *);
int log_tree_commit(struct log_tree_opt *, struct commit *);
int log_tree_opt_parse(struct log_tree_opt *, const char **, int);

struct whatchanged_opt {
	struct rev_info revopt;
	struct log_tree_opt logopt;
	int abbrev;
	int abbrev_commit;
	int do_diff;
	int full_diff;
};

int parse_whatchanged_opt(int, const char **, struct whatchanged_opt *);

#endif
