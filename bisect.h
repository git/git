#ifndef BISECT_H
#define BISECT_H

extern struct commit_list *find_bisection(struct commit_list *list,
					  int *reaches, int *all,
					  int find_all);

extern struct commit_list *filter_skipped(struct commit_list *list,
					  struct commit_list **tried,
					  int show_all);

/* bisect_show_flags flags in struct rev_list_info */
#define BISECT_SHOW_ALL		(1<<0)
#define BISECT_SHOW_TRIED	(1<<1)
#define BISECT_SHOW_STRINGED	(1<<2)

struct rev_list_info {
	struct rev_info *revs;
	int bisect_show_flags;
	int show_timestamp;
	int hdr_termination;
	const char *header_prefix;
};

extern int show_bisect_vars(struct rev_list_info *info, int reaches, int all);

extern int bisect_next_vars(const char *prefix);

#endif
