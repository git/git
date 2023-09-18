#ifndef REFS_PACKED_BACKEND_H
#define REFS_PACKED_BACKEND_H

struct repository;
struct ref_transaction;

/*
 * Support for storing references in a `packed-refs` file.
 *
 * Note that this backend doesn't check for D/F conflicts, because it
 * doesn't care about them. But usually it should be wrapped in a
 * `files_ref_store` that prevents D/F conflicts from being created,
 * even among packed refs.
 */

struct ref_store *packed_ref_store_create(struct repository *repo,
					  const char *gitdir,
					  unsigned int store_flags);

#endif /* REFS_PACKED_BACKEND_H */
