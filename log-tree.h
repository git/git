#ifndef LOG_TREE_H
#define LOG_TREE_H

#include "revision.h"

struct log_info {
	struct commit *commit, *parent;
};

void init_log_tree_opt(struct rev_info *);
int log_tree_diff_flush(struct rev_info *);
int log_tree_commit(struct rev_info *, struct commit *);
int log_tree_opt_parse(struct rev_info *, const char **, int);
void show_log(struct rev_info *opt);
void show_decorations(struct commit *commit);
void log_write_email_headers(struct rev_info *opt, const char *name,
			     const char **subject_p,
			     const char **extra_headers_p,
			     int *need_8bit_cte_p);

#endif
