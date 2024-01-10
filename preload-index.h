#ifndef PRELOAD_INDEX_H
#define PRELOAD_INDEX_H

struct index_state;
struct pathspec;
struct repository;

void preload_index(struct index_state *index,
		   const struct pathspec *pathspec,
		   unsigned int refresh_flags);
int repo_read_index_preload(struct repository *,
			    const struct pathspec *pathspec,
			    unsigned refresh_flags);

#endif /* PRELOAD_INDEX_H */
