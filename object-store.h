#ifndef OBJECT_STORE_H
#define OBJECT_STORE_H

#include "cache.h"
#include "oidmap.h"
#include "list.h"
#include "sha1-array.h"
#include "strbuf.h"

struct object_directory {
	struct object_directory *next;

	/*
	 * Used to store the results of readdir(3) calls when we are OK
	 * sacrificing accuracy due to races for speed. That includes
	 * object existence with OBJECT_INFO_QUICK, as well as
	 * our search for unique abbreviated hashes. Don't use it for tasks
	 * requiring greater accuracy!
	 *
	 * Be sure to call odb_load_loose_cache() before using.
	 */
	char loose_objects_subdir_seen[256];
	struct oid_array loose_objects_cache[256];

	/*
	 * Path to the alternative object store. If this is a relative path,
	 * it is relative to the current working directory.
	 */
	char *path;
};

void prepare_alt_odb(struct repository *r);
char *compute_alternate_path(const char *path, struct strbuf *err);
typedef int alt_odb_fn(struct object_directory *, void *);
int foreach_alt_odb(alt_odb_fn, void*);

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
 * Populate and return the loose object cache array corresponding to the
 * given object ID.
 */
struct oid_array *odb_loose_cache(struct object_directory *odb,
				  const struct object_id *oid);

/* Empty the loose object cache for the specified object directory. */
void odb_clear_loose_cache(struct object_directory *odb);

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
	int index;              /* for builtin/pack-objects.c */
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

struct multi_pack_index;

struct raw_object_store {
	/*
	 * Set of all object directories; the main directory is first (and
	 * cannot be NULL after initialization). Subsequent directories are
	 * alternates.
	 */
	struct object_directory *odb;
	struct object_directory **odb_tail;
	int loaded_alternates;

	/*
	 * A list of alternate object directories loaded from the environment;
	 * this should not generally need to be accessed directly, but will
	 * populate the "odb" list when prepare_alt_odb() is run.
	 */
	char *alternate_db;

	/*
	 * Objects that should be substituted by other objects
	 * (see git-replace(1)).
	 */
	struct oidmap *replace_map;

	struct commit_graph *commit_graph;
	unsigned commit_graph_attempted : 1; /* if loading has been attempted */

	/*
	 * private data
	 *
	 * should only be accessed directly by packfile.c and midx.c
	 */
	struct multi_pack_index *multi_pack_index;

	/*
	 * private data
	 *
	 * should only be accessed directly by packfile.c
	 */

	struct packed_git *packed_git;
	/* A most-recently-used ordered version of the packed_git list. */
	struct list_head packed_git_mru;

	/*
	 * A linked list containing all packfiles, starting with those
	 * contained in the multi_pack_index.
	 */
	struct packed_git *all_packs;

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
const char *loose_object_path(struct repository *r, struct strbuf *buf, const unsigned char *sha1);

void *map_sha1_file(struct repository *r, const unsigned char *sha1, unsigned long *size);

extern void *read_object_file_extended(const struct object_id *oid,
				       enum object_type *type,
				       unsigned long *size, int lookup_replace);
static inline void *read_object_file(const struct object_id *oid, enum object_type *type, unsigned long *size)
{
	return read_object_file_extended(oid, type, size, 1);
}

/* Read and unpack an object file into memory, write memory to an object file */
int oid_object_info(struct repository *r, const struct object_id *, unsigned long *);

extern int hash_object_file(const void *buf, unsigned long len,
			    const char *type, struct object_id *oid);

extern int write_object_file(const void *buf, unsigned long len,
			     const char *type, struct object_id *oid);

extern int hash_object_file_literally(const void *buf, unsigned long len,
				      const char *type, struct object_id *oid,
				      unsigned flags);

extern int pretend_object_file(void *, unsigned long, enum object_type,
			       struct object_id *oid);

extern int force_object_loose(const struct object_id *oid, time_t mtime);

/*
 * Open the loose object at path, check its hash, and return the contents,
 * type, and size. If the object is a blob, then "contents" may return NULL,
 * to allow streaming of large blobs.
 *
 * Returns 0 on success, negative on error (details may be written to stderr).
 */
int read_loose_object(const char *path,
		      const struct object_id *expected_oid,
		      enum object_type *type,
		      unsigned long *size,
		      void **contents);

/*
 * Convenience for sha1_object_info_extended() with a NULL struct
 * object_info. OBJECT_INFO_SKIP_CACHED is automatically set; pass
 * nonzero flags to also set other flags.
 */
extern int has_sha1_file_with_flags(const unsigned char *sha1, int flags);
static inline int has_sha1_file(const unsigned char *sha1)
{
	return has_sha1_file_with_flags(sha1, 0);
}

/* Same as the above, except for struct object_id. */
extern int has_object_file(const struct object_id *oid);
extern int has_object_file_with_flags(const struct object_id *oid, int flags);

/*
 * Return true iff an alternate object database has a loose object
 * with the specified name.  This function does not respect replace
 * references.
 */
extern int has_loose_object_nonlocal(const struct object_id *);

extern void assert_oid_type(const struct object_id *oid, enum object_type expect);

struct object_info {
	/* Request */
	enum object_type *typep;
	unsigned long *sizep;
	off_t *disk_sizep;
	unsigned char *delta_base_sha1;
	struct strbuf *type_name;
	void **contentp;

	/* Response */
	enum {
		OI_CACHED,
		OI_LOOSE,
		OI_PACKED,
		OI_DBCACHED
	} whence;
	union {
		/*
		 * struct {
		 * 	... Nothing to expose in this case
		 * } cached;
		 * struct {
		 * 	... Nothing to expose in this case
		 * } loose;
		 */
		struct {
			struct packed_git *pack;
			off_t offset;
			unsigned int is_delta;
		} packed;
	} u;
};

/*
 * Initializer for a "struct object_info" that wants no items. You may
 * also memset() the memory to all-zeroes.
 */
#define OBJECT_INFO_INIT {NULL}

/* Invoke lookup_replace_object() on the given hash */
#define OBJECT_INFO_LOOKUP_REPLACE 1
/* Allow reading from a loose object file of unknown/bogus type */
#define OBJECT_INFO_ALLOW_UNKNOWN_TYPE 2
/* Do not check cached storage */
#define OBJECT_INFO_SKIP_CACHED 4
/* Do not retry packed storage after checking packed and loose storage */
#define OBJECT_INFO_QUICK 8
/* Do not check loose object */
#define OBJECT_INFO_IGNORE_LOOSE 16

int oid_object_info_extended(struct repository *r,
			     const struct object_id *,
			     struct object_info *, unsigned flags);

/*
 * Iterate over the files in the loose-object parts of the object
 * directory "path", triggering the following callbacks:
 *
 *  - loose_object is called for each loose object we find.
 *
 *  - loose_cruft is called for any files that do not appear to be
 *    loose objects. Note that we only look in the loose object
 *    directories "objects/[0-9a-f]{2}/", so we will not report
 *    "objects/foobar" as cruft.
 *
 *  - loose_subdir is called for each top-level hashed subdirectory
 *    of the object directory (e.g., "$OBJDIR/f0"). It is called
 *    after the objects in the directory are processed.
 *
 * Any callback that is NULL will be ignored. Callbacks returning non-zero
 * will end the iteration.
 *
 * In the "buf" variant, "path" is a strbuf which will also be used as a
 * scratch buffer, but restored to its original contents before
 * the function returns.
 */
typedef int each_loose_object_fn(const struct object_id *oid,
				 const char *path,
				 void *data);
typedef int each_loose_cruft_fn(const char *basename,
				const char *path,
				void *data);
typedef int each_loose_subdir_fn(unsigned int nr,
				 const char *path,
				 void *data);
int for_each_file_in_obj_subdir(unsigned int subdir_nr,
				struct strbuf *path,
				each_loose_object_fn obj_cb,
				each_loose_cruft_fn cruft_cb,
				each_loose_subdir_fn subdir_cb,
				void *data);
int for_each_loose_file_in_objdir(const char *path,
				  each_loose_object_fn obj_cb,
				  each_loose_cruft_fn cruft_cb,
				  each_loose_subdir_fn subdir_cb,
				  void *data);
int for_each_loose_file_in_objdir_buf(struct strbuf *path,
				      each_loose_object_fn obj_cb,
				      each_loose_cruft_fn cruft_cb,
				      each_loose_subdir_fn subdir_cb,
				      void *data);

/* Flags for for_each_*_object() below. */
enum for_each_object_flags {
	/* Iterate only over local objects, not alternates. */
	FOR_EACH_OBJECT_LOCAL_ONLY = (1<<0),

	/* Only iterate over packs obtained from the promisor remote. */
	FOR_EACH_OBJECT_PROMISOR_ONLY = (1<<1),

	/*
	 * Visit objects within a pack in packfile order rather than .idx order
	 */
	FOR_EACH_OBJECT_PACK_ORDER = (1<<2),
};

/*
 * Iterate over all accessible loose objects without respect to
 * reachability. By default, this includes both local and alternate objects.
 * The order in which objects are visited is unspecified.
 *
 * Any flags specific to packs are ignored.
 */
int for_each_loose_object(each_loose_object_fn, void *,
			  enum for_each_object_flags flags);

/*
 * Iterate over all accessible packed objects without respect to reachability.
 * By default, this includes both local and alternate packs.
 *
 * Note that some objects may appear twice if they are found in multiple packs.
 * Each pack is visited in an unspecified order. By default, objects within a
 * pack are visited in pack-idx order (i.e., sorted by oid).
 */
typedef int each_packed_object_fn(const struct object_id *oid,
				  struct packed_git *pack,
				  uint32_t pos,
				  void *data);
int for_each_object_in_pack(struct packed_git *p,
			    each_packed_object_fn, void *data,
			    enum for_each_object_flags flags);
int for_each_packed_object(each_packed_object_fn, void *,
			   enum for_each_object_flags flags);

#endif /* OBJECT_STORE_H */
