#ifndef REFS_PACKED_BACKEND_H
#define REFS_PACKED_BACKEND_H

struct ref_store *packed_ref_store_create(const char *path,
					  unsigned int store_flags);

/*
 * Lock the packed-refs file for writing. Flags is passed to
 * hold_lock_file_for_update(). Return 0 on success. On errors, set
 * errno appropriately and return a nonzero value.
 */
int lock_packed_refs(struct ref_store *ref_store, int flags);

void add_packed_ref(struct ref_store *ref_store,
		    const char *refname, const struct object_id *oid);

int commit_packed_refs(struct ref_store *ref_store);

int repack_without_refs(struct ref_store *ref_store,
			struct string_list *refnames, struct strbuf *err);

#endif /* REFS_PACKED_BACKEND_H */
