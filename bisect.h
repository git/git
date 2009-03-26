#ifndef BISECT_H
#define BISECT_H

extern struct commit_list *find_bisection(struct commit_list *list,
					  int *reaches, int *all,
					  int find_all);

extern int show_bisect_vars(struct rev_info *revs, int reaches, int all,
			    int show_all);

#endif
