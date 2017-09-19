#ifndef REFS_PACKED_BACKEND_H
#define REFS_PACKED_BACKEND_H

/*
 * Support for storing references in a `packed-refs` file.
 *
 * Note that this backend doesn't check for D/F conflicts, because it
 * doesn't care about them. But usually it should be wrapped in a
 * `files_ref_store` that prevents D/F conflicts from being created,
 * even among packed refs.
 */

struct ref_store *packed_ref_store_create(const char *path,
					  unsigned int store_flags);

/*
 * Lock the packed-refs file for writing. Flags is passed to
 * hold_lock_file_for_update(). Return 0 on success. On errors, write
 * an error message to `err` and return a nonzero value.
 */
int packed_refs_lock(struct ref_store *ref_store, int flags, struct strbuf *err);

void packed_refs_unlock(struct ref_store *ref_store);
int packed_refs_is_locked(struct ref_store *ref_store);

#endif /* REFS_PACKED_BACKEND_H */
