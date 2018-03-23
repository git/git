#ifndef OBJECT_STORE_H
#define OBJECT_STORE_H

struct alternate_object_database {
	struct alternate_object_database *next;

	/* see alt_scratch_buf() */
	struct strbuf scratch;
	size_t base_len;

	/*
	 * Used to store the results of readdir(3) calls when searching
	 * for unique abbreviated hashes.  This cache is never
	 * invalidated, thus it's racy and not necessarily accurate.
	 * That's fine for its purpose; don't use it for tasks requiring
	 * greater accuracy!
	 */
	char loose_objects_subdir_seen[256];
	struct oid_array loose_objects_cache;

	char path[FLEX_ARRAY];
};
void prepare_alt_odb(void);
char *compute_alternate_path(const char *path, struct strbuf *err);
typedef int alt_odb_fn(struct alternate_object_database *, void *);
int foreach_alt_odb(alt_odb_fn, void*);

/*
 * Allocate a "struct alternate_object_database" but do _not_ actually
 * add it to the list of alternates.
 */
struct alternate_object_database *alloc_alt_odb(const char *dir);

/*
 * Add the directory to the on-disk alternates file; the new entry will also
 * take effect in the current process.
 */
void add_to_alternates_file(const char *dir);

/*
 * Add the directory to the in-memory list of alternates (along with any
 * recursive alternates it points to), but do not modify the on-disk alternates
 * file.
 */
void add_to_alternates_memory(const char *dir);

/*
 * Returns a scratch strbuf pre-filled with the alternate object directory,
 * including a trailing slash, which can be used to access paths in the
 * alternate. Always use this over direct access to alt->scratch, as it
 * cleans up any previous use of the scratch buffer.
 */
struct strbuf *alt_scratch_buf(struct alternate_object_database *alt);

struct raw_object_store {
	/*
	 * Path to the repository's object store.
	 * Cannot be NULL after initialization.
	 */
	char *objectdir;

	/* Path to extra alternate object database if not NULL */
	char *alternate_db;

	struct alternate_object_database *alt_odb_list;
	struct alternate_object_database **alt_odb_tail;
};

struct raw_object_store *raw_object_store_new(void);
void raw_object_store_clear(struct raw_object_store *o);

#endif /* OBJECT_STORE_H */
