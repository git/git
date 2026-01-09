#ifndef ODB_H
#define ODB_H

#include "hashmap.h"
#include "object.h"
#include "oidset.h"
#include "oidmap.h"
#include "string-list.h"
#include "thread-utils.h"

struct oidmap;
struct oidtree;
struct strbuf;
struct repository;
struct multi_pack_index;

/*
 * Set this to 0 to prevent odb_read_object_info_extended() from fetching missing
 * blobs. This has a difference only if extensions.partialClone is set.
 *
 * Its default value is 1.
 */
extern int fetch_if_missing;

/*
 * Compute the exact path an alternate is at and returns it. In case of
 * error NULL is returned and the human readable error is added to `err`
 * `path` may be relative and should point to $GIT_DIR.
 * `err` must not be null.
 */
char *compute_alternate_path(const char *path, struct strbuf *err);

/*
 * The source is the part of the object database that stores the actual
 * objects. It thus encapsulates the logic to read and write the specific
 * on-disk format. An object database can have multiple sources:
 *
 *   - The primary source, which is typically located in "$GIT_DIR/objects".
 *     This is where new objects are usually written to.
 *
 *   - Alternate sources, which are configured via "objects/info/alternates" or
 *     via the GIT_ALTERNATE_OBJECT_DIRECTORIES environment variable. These
 *     alternate sources are only used to read objects.
 */
struct odb_source {
	struct odb_source *next;

	/* Object database that owns this object source. */
	struct object_database *odb;

	/* Private state for loose objects. */
	struct odb_source_loose *loose;

	/* Should only be accessed directly by packfile.c and midx.c. */
	struct packfile_store *packfiles;

	/*
	 * Figure out whether this is the local source of the owning
	 * repository, which would typically be its ".git/objects" directory.
	 * This local object directory is usually where objects would be
	 * written to.
	 */
	bool local;

	/*
	 * This object store is ephemeral, so there is no need to fsync.
	 */
	int will_destroy;

	/*
	 * Path to the source. If this is a relative path, it is relative to
	 * the current working directory.
	 */
	char *path;
};

struct packed_git;
struct packfile_store;
struct cached_object_entry;
struct odb_transaction;

/*
 * The object database encapsulates access to objects in a repository. It
 * manages one or more sources that store the actual objects which are
 * configured via alternates.
 */
struct object_database {
	/* Repository that owns this database. */
	struct repository *repo;

	/*
	 * State of current current object database transaction. Only one
	 * transaction may be pending at a time. Is NULL when no transaction is
	 * configured.
	 */
	struct odb_transaction *transaction;

	/*
	 * Set of all object directories; the main directory is first (and
	 * cannot be NULL after initialization). Subsequent directories are
	 * alternates.
	 */
	struct odb_source *sources;
	struct odb_source **sources_tail;
	struct kh_odb_path_map *source_by_path;

	int loaded_alternates;

	/*
	 * A list of alternate object directories loaded from the environment;
	 * this should not generally need to be accessed directly, but will
	 * populate the "sources" list when odb_prepare_alternates() is run.
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
	 * This is meant to hold a *small* number of objects that you would
	 * want odb_read_object() to be able to return, but yet you do not want
	 * to write them into the object store (e.g. a browse-only
	 * application).
	 */
	struct cached_object_entry *cached_objects;
	size_t cached_object_nr, cached_object_alloc;

	/*
	 * A fast, rough count of the number of objects in the repository.
	 * These two fields are not meant for direct access. Use
	 * repo_approximate_object_count() instead.
	 */
	unsigned long approximate_object_count;
	unsigned approximate_object_count_valid : 1;

	/*
	 * Submodule source paths that will be added as additional sources to
	 * allow lookup of submodule objects via the main object database.
	 */
	struct string_list submodule_source_paths;
};

/*
 * Create a new object database for the given repository.
 *
 * If the primary source parameter is set it will override the usual primary
 * object directory derived from the repository's common directory. The
 * alternate sources are expected to be a PATH_SEP-separated list of secondary
 * sources. Note that these alternate sources will be added in addition to, not
 * instead of, the alternates identified by the primary source.
 *
 * Returns the newly created object database.
 */
struct object_database *odb_new(struct repository *repo,
				const char *primary_source,
				const char *alternate_sources);

/* Free the object database and release all resources. */
void odb_free(struct object_database *o);

/*
 * Close the object database and all of its sources so that any held resources
 * will be released. The database can still be used after closing it, in which
 * case these resources may be reallocated.
 */
void odb_close(struct object_database *o);

/*
 * Clear caches, reload alternates and then reload object sources so that new
 * objects may become accessible.
 */
void odb_reprepare(struct object_database *o);

/*
 * Starts an ODB transaction. Subsequent objects are written to the transaction
 * and not committed until odb_transaction_commit() is invoked on the
 * transaction. If the ODB already has a pending transaction, NULL is returned.
 */
struct odb_transaction *odb_transaction_begin(struct object_database *odb);

/*
 * Commits an ODB transaction making the written objects visible. If the
 * specified transaction is NULL, the function is a no-op.
 */
void odb_transaction_commit(struct odb_transaction *transaction);

/*
 * Find source by its object directory path. Returns a `NULL` pointer in case
 * the source could not be found.
 */
struct odb_source *odb_find_source(struct object_database *odb, const char *obj_dir);

/* Same as `odb_find_source()`, but dies in case the source doesn't exist. */
struct odb_source *odb_find_source_or_die(struct object_database *odb, const char *obj_dir);

/*
 * Replace the current writable object directory with the specified temporary
 * object directory; returns the former primary source.
 */
struct odb_source *odb_set_temporary_primary_source(struct object_database *odb,
						    const char *dir, int will_destroy);

/*
 * Restore the primary source that was previously replaced by
 * `odb_set_temporary_primary_source()`.
 */
void odb_restore_primary_source(struct object_database *odb,
				struct odb_source *restore_source,
				const char *old_path);

/*
 * Call odb_add_submodule_source_by_path() to add the submodule at the given
 * path to a list. The object stores of all submodules in that list will be
 * added as additional sources in the object store when looking up objects.
 */
void odb_add_submodule_source_by_path(struct object_database *odb,
				      const char *path);

/*
 * Iterate through all alternates of the database and execute the provided
 * callback function for each of them. Stop iterating once the callback
 * function returns a non-zero value, in which case the value is bubbled up
 * from the callback.
 */
typedef int odb_for_each_alternate_fn(struct odb_source *, void *);
int odb_for_each_alternate(struct object_database *odb,
			   odb_for_each_alternate_fn cb, void *payload);

/*
 * Iterate through all alternates of the database and yield their respective
 * references.
 */
typedef void odb_for_each_alternate_ref_fn(const struct object_id *oid, void *);
void odb_for_each_alternate_ref(struct object_database *odb,
				odb_for_each_alternate_ref_fn cb, void *payload);

/*
 * Create a temporary file rooted in the primary alternate's directory, or die
 * on failure. The filename is taken from "pattern", which should have the
 * usual "XXXXXX" trailer, and the resulting filename is written into the
 * "template" buffer. Returns the open descriptor.
 */
int odb_mkstemp(struct object_database *odb,
		struct strbuf *temp_filename, const char *pattern);

/*
 * Prepare alternate object sources for the given database by reading
 * "objects/info/alternates" and opening the respective sources.
 */
void odb_prepare_alternates(struct object_database *odb);

/*
 * Check whether the object database has any alternates. The primary object
 * source does not count as alternate.
 */
int odb_has_alternates(struct object_database *odb);

/*
 * Add the directory to the on-disk alternates file; the new entry will also
 * take effect in the current process.
 */
void odb_add_to_alternates_file(struct object_database *odb,
				const char *dir);

/*
 * Add the directory to the in-memory list of alternate sources (along with any
 * recursive alternates it points to), but do not modify the on-disk alternates
 * file.
 */
struct odb_source *odb_add_to_alternates_memory(struct object_database *odb,
						const char *dir);

/*
 * Read an object from the database. Returns the object data and assigns object
 * type and size to the `type` and `size` pointers, if these pointers are
 * non-NULL. Returns a `NULL` pointer in case the object does not exist.
 *
 * This function dies on corrupt objects; the callers who want to deal with
 * them should arrange to call odb_read_object_info_extended() and give error
 * messages themselves.
 */
void *odb_read_object(struct object_database *odb,
		      const struct object_id *oid,
		      enum object_type *type,
		      unsigned long *size);

void *odb_read_object_peeled(struct object_database *odb,
			     const struct object_id *oid,
			     enum object_type required_type,
			     unsigned long *size,
			     struct object_id *oid_ret);

/*
 * Add an object file to the in-memory object store, without writing it
 * to disk.
 *
 * Callers are responsible for calling write_object_file to record the
 * object in persistent storage before writing any other new objects
 * that reference it.
 */
int odb_pretend_object(struct object_database *odb,
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

/*
 * Read object info from the object database and populate the `object_info`
 * structure. Returns 0 on success, a negative error code otherwise.
 */
int odb_read_object_info_extended(struct object_database *odb,
				  const struct object_id *oid,
				  struct object_info *oi,
				  unsigned flags);

/*
 * Read a subset of object info for the given object ID. Returns an `enum
 * object_type` on success, a negative error code otherwise. If successful and
 * `sizep` is non-NULL, then the size of the object will be written to the
 * pointer.
 */
int odb_read_object_info(struct object_database *odb,
			 const struct object_id *oid,
			 unsigned long *sizep);

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
int odb_has_object(struct object_database *odb,
		   const struct object_id *oid,
		   unsigned flags);

int odb_freshen_object(struct object_database *odb,
		       const struct object_id *oid);

void odb_assert_oid_type(struct object_database *odb,
			 const struct object_id *oid, enum object_type expect);

/*
 * Enabling the object read lock allows multiple threads to safely call the
 * following functions in parallel: odb_read_object(),
 * odb_read_object_peeled(), odb_read_object_info() and odb().
 *
 * obj_read_lock() and obj_read_unlock() may also be used to protect other
 * section which cannot execute in parallel with object reading. Since the used
 * lock is a recursive mutex, these sections can even contain calls to object
 * reading functions. However, beware that in these cases zlib inflation won't
 * be performed in parallel, losing performance.
 *
 * TODO: odb_read_object_info_extended()'s call stack has a recursive behavior. If
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

enum {
	/*
	 * By default, `odb_write_object()` does not actually write anything
	 * into the object store, but only computes the object ID. This flag
	 * changes that so that the object will be written as a loose object
	 * and persisted.
	 */
	WRITE_OBJECT_PERSIST = (1 << 0),

	/*
	 * Do not print an error in case something goes wrong.
	 */
	WRITE_OBJECT_SILENT = (1 << 1),
};

/*
 * Write an object into the object database. The object is being written into
 * the local alternate of the repository. If provided, the converted object ID
 * as well as the compatibility object ID are written to the respective
 * pointers.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int odb_write_object_ext(struct object_database *odb,
			 const void *buf, unsigned long len,
			 enum object_type type,
			 struct object_id *oid,
			 struct object_id *compat_oid,
			 unsigned flags);

static inline int odb_write_object(struct object_database *odb,
				   const void *buf, unsigned long len,
				   enum object_type type,
				   struct object_id *oid)
{
	return odb_write_object_ext(odb, buf, len, type, oid, NULL, 0);
}

struct odb_write_stream {
	const void *(*read)(struct odb_write_stream *, unsigned long *len);
	void *data;
	int is_finished;
};

int odb_write_object_stream(struct object_database *odb,
			    struct odb_write_stream *stream, size_t len,
			    struct object_id *oid);

#endif /* ODB_H */
