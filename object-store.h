#ifndef OBJECT_STORE_H
#define OBJECT_STORE_H

#include "hashmap.h"
#include "object.h"
#include "list.h"
#include "oidset.h"
#include "oidmap.h"
#include "thread-utils.h"

struct oidmap;
struct oidtree;
struct strbuf;
struct repository;

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

struct packed_git;
struct multi_pack_index;
struct cached_object_entry;

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
	struct oidmap replace_map;
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
	 * This is meant to hold a *small* number of objects that you would
	 * want repo_read_object_file() to be able to return, but yet you do not want
	 * to write them into the object store (e.g. a browse-only
	 * application).
	 */
	struct cached_object_entry *cached_objects;
	size_t cached_object_nr, cached_object_alloc;

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

void *repo_read_object_file(struct repository *r,
			    const struct object_id *oid,
			    enum object_type *type,
			    unsigned long *size);

/* Read and unpack an object file into memory, write memory to an object file */
int oid_object_info(struct repository *r, const struct object_id *, unsigned long *);

/*
 * Add an object file to the in-memory object store, without writing it
 * to disk.
 *
 * Callers are responsible for calling write_object_file to record the
 * object in persistent storage before writing any other new objects
 * that reference it.
 */
int pretend_object_file(struct repository *repo,
			void *buf, unsigned long len, enum object_type type,
			struct object_id *oid);

struct object_info {
	/* Request */
	enum object_type *typep;
	unsigned long *sizep;
	off_t *disk_sizep;
	struct object_id *delta_base_oid;
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
#define OBJECT_INFO_INIT { 0 }

/* Invoke lookup_replace_object() on the given hash */
#define OBJECT_INFO_LOOKUP_REPLACE 1
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

enum {
	/* Retry packed storage after checking packed and loose storage */
	HAS_OBJECT_RECHECK_PACKED = (1 << 0),
	/* Allow fetching the object in case the repository has a promisor remote. */
	HAS_OBJECT_FETCH_PROMISOR = (1 << 1),
};

/*
 * Returns 1 if the object exists. This function will not lazily fetch objects
 * in a partial clone by default.
 */
int has_object(struct repository *r, const struct object_id *oid,
	       unsigned flags);

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
/* Flags for for_each_*_object(). */
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


void *read_object_with_reference(struct repository *r,
				 const struct object_id *oid,
				 enum object_type required_type,
				 unsigned long *size,
				 struct object_id *oid_ret);

#endif /* OBJECT_STORE_H */
