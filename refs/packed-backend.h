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

/*
 * Return true if `transaction` really needs to be carried out against
 * the specified packed_ref_store, or false if it can be skipped
 * (i.e., because it is an obvious NOOP). `ref_store` must be locked
 * before calling this function.
 */
int is_packed_transaction_needed(struct ref_store *ref_store,
				 struct ref_transaction *transaction);

#endif /* REFS_PACKED_BACKEND_H */
