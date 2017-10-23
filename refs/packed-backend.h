#ifndef REFS_PACKED_BACKEND_H
#define REFS_PACKED_BACKEND_H

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

void add_packed_ref(struct ref_store *ref_store,
		    const char *refname, const struct object_id *oid);

int commit_packed_refs(struct ref_store *ref_store, struct strbuf *err);

int repack_without_refs(struct ref_store *ref_store,
			struct string_list *refnames, struct strbuf *err);

#endif /* REFS_PACKED_BACKEND_H */
