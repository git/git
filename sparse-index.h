#ifndef SPARSE_INDEX_H__
#define SPARSE_INDEX_H__

struct index_state;
void ensure_full_index(struct index_state *istate);
int convert_to_sparse(struct index_state *istate);

struct repository;
int set_sparse_index_config(struct repository *repo, int enable);

#endif
