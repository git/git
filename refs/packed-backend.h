#ifndef REFS_PACKED_BACKEND_H
#define REFS_PACKED_BACKEND_H

struct packed_ref_store *packed_ref_store_create(
		const char *path, unsigned int store_flags);

int packed_read_raw_ref(struct packed_ref_store *refs,
			const char *refname, unsigned char *sha1,
			struct strbuf *referent, unsigned int *type);

int packed_peel_ref(struct packed_ref_store *refs,
		    const char *refname, unsigned char *sha1);

struct ref_iterator *packed_ref_iterator_begin(
		struct packed_ref_store *refs,
		const char *prefix, unsigned int flags);

/*
 * Lock the packed-refs file for writing. Flags is passed to
 * hold_lock_file_for_update(). Return 0 on success. On errors, set
 * errno appropriately and return a nonzero value.
 */
int lock_packed_refs(struct packed_ref_store *refs, int flags);

void add_packed_ref(struct packed_ref_store *refs,
		    const char *refname, const struct object_id *oid);

int commit_packed_refs(struct packed_ref_store *refs);

int repack_without_refs(struct packed_ref_store *refs,
			struct string_list *refnames, struct strbuf *err);

#endif /* REFS_PACKED_BACKEND_H */
