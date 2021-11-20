#ifndef SPARSE_INDEX_H__
#define SPARSE_INDEX_H__

struct index_state;
#define SPARSE_INDEX_MEMORY_ONLY (1 << 0)
int convert_to_sparse(struct index_state *istate, int flags);

/*
 * Some places in the codebase expect to search for a specific path.
 * This path might be outside of the sparse-checkout definition, in
 * which case a sparse-index may not contain a path for that index.
 *
 * Given an index and a path, check to see if a leading directory for
 * 'path' exists in the index as a sparse directory. In that case,
 * expand that sparse directory to a full range of cache entries and
 * populate the index accordingly.
 */
void expand_to_path(struct index_state *istate,
		    const char *path, size_t pathlen, int icase);

struct repository;
int set_sparse_index_config(struct repository *repo, int enable);

#endif
