#ifndef MERGE_H
#define MERGE_H

struct commit_list;
struct object_id;
struct repository;

int try_merge_command(struct repository *r,
		const char *strategy, size_t xopts_nr,
		const char **xopts, struct commit_list *common,
		const char *head_arg, struct commit_list *remotes);
int checkout_fast_forward(struct repository *r,
			  const struct object_id *from,
			  const struct object_id *to,
			  int overwrite_ignore);
int write_merge_labels(struct repository *r,
		       const char *base, const char *ours, const char *theirs);
int read_merge_labels(struct repository *r,
		      char **base, char **ours, char **theirs);

#endif /* MERGE_H */
