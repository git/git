#ifndef OBJECT_STORE_LL_H
#define OBJECT_STORE_LL_H

#include "hashmap.h"
#include "object.h"
#include "list.h"
#include "thread-utils.h"
#include "oidset.h"

struct oidmap;
struct oidtree;
struct strbuf;

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
	uint32_t loose_objects_subdir_seen[8]; /* 256 bits */
	struct oidtree *loose_objects_cache;

	/* Map between object IDs for loose objects. */
	struct loose_object_map *loose_map;

	/*
	 * This is a temporary object store created by the tmp_objdir
	 * facility. Disable ref updates since the objects in the store
	 * might be discarded on rollback.
	 */
	int disable_ref_updates;

	/*
	 * This object store is ephemeral, so there is no need to fsync.
	 */
	int will_destroy;

	/*
	 * Path to the alternative object store. If this is a relative path,
	 * it is relative to the current working directory.
	 */
	char *path;
};

struct input_stream {
	const void *(*read)(struct input_stream *, unsigned long *len);
	void *data;
	int is_finished;
};

void prepare_alt_odb(struct repository *r);
int has_alt_odb(struct repository *r);
char *compute_alternate_path(const char *path, struct strbuf *err);
struct object_directory *find_odb(struct repository *r, const char *obj_dir);
typedef int alt_odb_fn(struct object_directory *, void *);
int foreach_alt_odb(alt_odb_fn, void*);
typedef void alternate_ref_fn(const struct object_id *oid, void *);
void for_each_alternate_ref(alternate_ref_fn, void *);

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
 * Replace the current writable object directory with the specified temporary
 * object directory; returns the former primary object directory.
 */
struct object_directory *set_temporary_primary_odb(const char *dir, int will_destroy);

/*
 * Restore a previous ODB replaced by set_temporary_main_odb.
 */
void restore_primary_odb(struct object_directory *restore_odb, const char *old_path);

/*
 * Populate and return the loose object cache array corresponding to the
 * given object ID.
 */
struct oidtree *odb_loose_cache(struct object_directory *odb,
				  const struct object_id *oid);

/* Empty the loose object cache for the specified object directory. */
void odb_clear_loose_cache(struct object_directory *odb);

/* Clear and free the specified object directory */
void free_object_directory(struct object_directory *odb);

struct packed_git {
	struct hashmap_entry packmap_ent;
	struct packed_git *next;
	struct list_head mru;
	struct pack_window *windows;
	off_t pack_size;
	const void *index_data;
	size_t index_size;
	uint32_t num_objects;
	size_t crc_offset;
	struct oidset bad_objects;
	int index_version;
	time_t mtime;
	int pack_fd;
	int index;              /* for builtin/pack-objects.c */
	unsigned pack_local:1,
		 pack_keep:1,
		 pack_keep_in_core:1,
		 freshened:1,
		 do_not_close:1,
		 pack_promisor:1,
		 multi_pack_index:1,
		 is_cruft:1;
	unsigned char hash[GIT_MAX_RAWSZ];
	struct revindex_entry *revindex;
	const uint32_t *revindex_data;
	const uint32_t *revindex_map;
	size_t revindex_size;
	/*
	 * mtimes_map points at the beginning of the memory mapped region of
	 * this pack's corresponding .mtimes file, and mtimes_size is the size
	 * of that .mtimes file
	 */
	const uint32_t *mtimes_map;
	size_t mtimes_size;
	/* something like ".git/objects/pack/xxxxx.pack" */
	char pack_name[FLEX_ARRAY]; /* more */
};

struct multi_pack_index;

static inline int pack_map_entry_cmp(const void *cmp_data UNUSED,
				     const struct hashmap_entry *entry,
				     const struct hashmap_entry *entry2,
				     const void *keydata)
{
	const char *key = keydata;
	const struct packed_git *pg1, *pg2;

	pg1 = container_of(entry, const struct packed_git, packmap_ent);
	pg2 = container_of(entry2, const struct packed_git, packmap_ent);

	return strcmp(pg1->pack_name, key ? key : pg2->pack_name);
}

struct raw_object_store {
	/*
	 * Set of all object directories; the main directory is first (and
	 * cannot be NULL after initialization). Subsequent directories are
	 * alternates.
	 */
	struct object_directory *odb;
	struct object_directory **odb_tail;
	struct kh_odb_path_map *odb_by_path;

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
	unsigned replace_map_initialized : 1;
	pthread_mutex_t replace_mutex; /* protect object replace functions */

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

	struct {
		struct packed_git **packs;
		unsigned flags;
	} kept_pack_cache;

	/*
	 * A map of packfiles to packed_git structs for tracking which
	 * packs have been loaded already.
	 */
	struct hashmap pack_map;

	/*
	 * A fast, rough count of the number of objects in the repository.
	 * These two fields are not meant for direct access. Use
	 * repo_approximate_object_count() instead.
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
 * Create a temporary file rooted in the object database directory, or
 * die on failure. The filename is taken from "pattern", which should have the
 * usual "XXXXXX" trailer, and the resulting filename is written into the
 * "template" buffer. Returns the open descriptor.
 */
int odb_mkstemp(struct strbuf *temp_filename, const char *pattern);

/*
 * Create a pack .keep file named "name" (which should generally be the output
 * of odb_pack_name). Returns a file descriptor opened for writing, or -1 on
 * error.
 */
int odb_pack_keep(const char *name);

/*
 * Put in `buf` the name of the file in the local object database that
 * would be used to store a loose object with the specified oid.
 */
const char *loose_object_path(struct repository *r, struct strbuf *buf,
			      const struct object_id *oid);

void *map_loose_object(struct repository *r, const struct object_id *oid,
		       unsigned long *size);

void *repo_read_object_file(struct repository *r,
			    const struct object_id *oid,
			    enum object_type *type,
			    unsigned long *size);

/* Read and unpack an object file into memory, write memory to an object file */
int oid_object_info(struct repository *r, const struct object_id *, unsigned long *);

void hash_object_file(const struct git_hash_algo *algo, const void *buf,
		      unsigned long len, enum object_type type,
		      struct object_id *oid);

int write_object_file_flags(const void *buf, unsigned long len,
			    enum object_type type, struct object_id *oid,
			    struct object_id *comapt_oid_in, unsigned flags);
static inline int write_object_file(const void *buf, unsigned long len,
				    enum object_type type, struct object_id *oid)
{
	return write_object_file_flags(buf, len, type, oid, NULL, 0);
}

int write_object_file_literally(const void *buf, unsigned long len,
				const char *type, struct object_id *oid,
				unsigned flags);
int stream_loose_object(struct input_stream *in_stream, size_t len,
			struct object_id *oid);

/*
 * Add an object file to the in-memory object store, without writing it
 * to disk.
 *
 * Callers are responsible for calling write_object_file to record the
 * object in persistent storage before writing any other new objects
 * that reference it.
 */
int pretend_object_file(void *, unsigned long, enum object_type,
			struct object_id *oid);

int force_object_loose(const struct object_id *oid, time_t mtime);

struct object_info {
	/* Request */
	enum object_type *typep;
	unsigned long *sizep;
	off_t *disk_sizep;
	struct object_id *delta_base_oid;
	struct strbuf *type_name;
	void **contentp;
	size_t content_limit;

	/* Response */
	enum {
		OI_CACHED,
		OI_LOOSE,
		OI_PACKED,
		OI_DBCACHED
	} whence;

	/*
	 * Set if caller is able to use OI_DBCACHED entries without copying.
	 * This only applies to OI_DBCACHED entries at the moment,
	 * not OI_CACHED or any other type of entry.
	 */
	unsigned direct_cache:1;

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
#define OBJECT_INFO_INIT { 0 }

/* Invoke lookup_replace_object() on the given hash */
#define OBJECT_INFO_LOOKUP_REPLACE 1
/* Allow reading from a loose object file of unknown/bogus type */
#define OBJECT_INFO_ALLOW_UNKNOWN_TYPE 2
/* Do not retry packed storage after checking packed and loose storage */
#define OBJECT_INFO_QUICK 8
/*
 * Do not attempt to fetch the object if missing (even if fetch_is_missing is
 * nonzero).
 */
#define OBJECT_INFO_SKIP_FETCH_OBJECT 16
/*
 * This is meant for bulk prefetching of missing blobs in a partial
 * clone. Implies OBJECT_INFO_SKIP_FETCH_OBJECT and OBJECT_INFO_QUICK
 */
#define OBJECT_INFO_FOR_PREFETCH (OBJECT_INFO_SKIP_FETCH_OBJECT | OBJECT_INFO_QUICK)

/* Die if object corruption (not just an object being missing) was detected. */
#define OBJECT_INFO_DIE_IF_CORRUPT 32

int oid_object_info_extended(struct repository *r,
			     const struct object_id *,
			     struct object_info *, unsigned flags);

/*
 * Open the loose object at path, check its hash, and return the contents,
 * use the "oi" argument to assert things about the object, or e.g. populate its
 * type, and size. If the object is a blob, then "contents" may return NULL,
 * to allow streaming of large blobs.
 *
 * Returns 0 on success, negative on error (details may be written to stderr).
 */
int read_loose_object(const char *path,
		      const struct object_id *expected_oid,
		      struct object_id *real_oid,
		      void **contents,
		      struct object_info *oi);

/* Retry packed storage after checking packed and loose storage */
#define HAS_OBJECT_RECHECK_PACKED 1

/*
 * Returns 1 if the object exists. This function will not lazily fetch objects
 * in a partial clone.
 */
int has_object(struct repository *r, const struct object_id *oid,
	       unsigned flags);

/*
 * These macros and functions are deprecated. If checking existence for an
 * object that is likely to be missing and/or whose absence is relatively
 * inconsequential (or is consequential but the caller is prepared to handle
 * it), use has_object(), which has better defaults (no lazy fetch in a partial
 * clone and no rechecking of packed storage). In the unlikely event that a
 * caller needs to assert existence of an object that it fully expects to
 * exist, and wants to trigger a lazy fetch in a partial clone, use
 * oid_object_info_extended() with a NULL struct object_info.
 *
 * These functions can be removed once all callers have migrated to
 * has_object() and/or oid_object_info_extended().
 */
int repo_has_object_file(struct repository *r, const struct object_id *oid);
int repo_has_object_file_with_flags(struct repository *r,
				    const struct object_id *oid, int flags);

/*
 * Return true iff an alternate object database has a loose object
 * with the specified name.  This function does not respect replace
 * references.
 */
int has_loose_object_nonlocal(const struct object_id *);

int has_loose_object(const struct object_id *);

/**
 * format_object_header() is a thin wrapper around s xsnprintf() that
 * writes the initial "<type> <obj-len>" part of the loose object
 * header. It returns the size that snprintf() returns + 1.
 */
int format_object_header(char *str, size_t size, enum object_type type,
			 size_t objsize);

void assert_oid_type(const struct object_id *oid, enum object_type expect);

/*
 * Enabling the object read lock allows multiple threads to safely call the
 * following functions in parallel: repo_read_object_file(),
 * read_object_with_reference(), oid_object_info() and oid_object_info_extended().
 *
 * obj_read_lock() and obj_read_unlock() may also be used to protect other
 * section which cannot execute in parallel with object reading. Since the used
 * lock is a recursive mutex, these sections can even contain calls to object
 * reading functions. However, beware that in these cases zlib inflation won't
 * be performed in parallel, losing performance.
 *
 * TODO: oid_object_info_extended()'s call stack has a recursive behavior. If
 * any of its callees end up calling it, this recursive call won't benefit from
 * parallel inflation.
 */
void enable_obj_read_lock(void);
void disable_obj_read_lock(void);

extern int obj_read_use_lock;
extern pthread_mutex_t obj_read_mutex;

static inline void obj_read_lock(void)
{
	if(obj_read_use_lock)
		pthread_mutex_lock(&obj_read_mutex);
}

static inline void obj_read_unlock(void)
{
	if(obj_read_use_lock)
		pthread_mutex_unlock(&obj_read_mutex);
}

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

	/* Only iterate over packs that are not marked as kept in-core. */
	FOR_EACH_OBJECT_SKIP_IN_CORE_KEPT_PACKS = (1<<3),

	/* Only iterate over packs that do not have .keep files. */
	FOR_EACH_OBJECT_SKIP_ON_DISK_KEPT_PACKS = (1<<4),
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

/* Free pointers inside of object_info, but not object_info itself */
void free_object_info_contents(struct object_info *object_info);

#endif /* OBJECT_STORE_LL_H */
