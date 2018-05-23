#ifndef OBJECT_STORE_H
#define OBJECT_STORE_H

#include "oidmap.h"

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

	/*
	 * Path to the alternative object store. If this is a relative path,
	 * it is relative to the current working directory.
	 */
	char path[FLEX_ARRAY];
};
void prepare_alt_odb(struct repository *r);
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

struct packed_git {
	struct packed_git *next;
	struct list_head mru;
	struct pack_window *windows;
	off_t pack_size;
	const void *index_data;
	size_t index_size;
	uint32_t num_objects;
	uint32_t num_bad_objects;
	unsigned char *bad_object_sha1;
	int index_version;
	time_t mtime;
	int pack_fd;
	unsigned pack_local:1,
		 pack_keep:1,
		 pack_keep_in_core:1,
		 freshened:1,
		 do_not_close:1,
		 pack_promisor:1;
	unsigned char sha1[20];
	struct revindex_entry *revindex;
	/* something like ".git/objects/pack/xxxxx.pack" */
	char pack_name[FLEX_ARRAY]; /* more */
};

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

	/*
	 * Objects that should be substituted by other objects
	 * (see git-replace(1)).
	 */
	struct oidmap *replace_map;

	/*
	 * private data
	 *
	 * should only be accessed directly by packfile.c
	 */

	struct packed_git *packed_git;
	/* A most-recently-used ordered version of the packed_git list. */
	struct list_head packed_git_mru;

	/*
	 * A fast, rough count of the number of objects in the repository.
	 * These two fields are not meant for direct access. Use
	 * approximate_object_count() instead.
	 */
	unsigned long approximate_object_count;
	unsigned approximate_object_count_valid : 1;

	/*
	 * Whether packed_git has already been populated with this repository's
	 * packs.
	 */
	unsigned packed_git_initialized : 1;
};

struct raw_object_store *raw_object_store_new(void);
void raw_object_store_clear(struct raw_object_store *o);

/*
 * Put in `buf` the name of the file in the local object database that
 * would be used to store a loose object with the specified sha1.
 */
void sha1_file_name(struct repository *r, struct strbuf *buf, const unsigned char *sha1);

void *map_sha1_file(struct repository *r, const unsigned char *sha1, unsigned long *size);

#endif /* OBJECT_STORE_H */
