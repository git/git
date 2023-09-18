#ifndef REFS_REFTABLE_BACKEND_H
#define REFS_REFTABLE_BACKEND_H

struct ref_store *git_reftable_ref_store_create(struct repository *repo,
						const char *gitdir,
						unsigned int store_flags);

#endif /* REFS_REFTABLE_BACKEND_H */
