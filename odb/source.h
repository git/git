#ifndef ODB_SOURCE_H
#define ODB_SOURCE_H

#include "object.h"

enum odb_source_type {
	/*
	 * The "unknown" type, which should never be in use. This is type
	 * mostly exists to catch cases where the type field remains zeroed
	 * out.
	 */
	ODB_SOURCE_UNKNOWN,

	/* The "files" backend that uses loose objects and packfiles. */
	ODB_SOURCE_FILES,
};

/* Flags that can be passed to `odb_read_object_info_extended()`. */
enum object_info_flags {
	/* Invoke lookup_replace_object() on the given hash. */
	OBJECT_INFO_LOOKUP_REPLACE = (1 << 0),

	/* Do not reprepare object sources when the first lookup has failed. */
	OBJECT_INFO_QUICK = (1 << 1),

	/*
	 * Do not attempt to fetch the object if missing (even if fetch_is_missing is
	 * nonzero).
	 */
	OBJECT_INFO_SKIP_FETCH_OBJECT = (1 << 2),

	/* Die if object corruption (not just an object being missing) was detected. */
	OBJECT_INFO_DIE_IF_CORRUPT = (1 << 3),

	/*
	 * We have already tried reading the object, but it couldn't be found
	 * via any of the attached sources, and are now doing a second read.
	 * This second read asks the individual sources to also evaluate
	 * whether any on-disk state may have changed that may have caused the
	 * object to appear.
	 *
	 * This flag is for internal use, only. The second read only occurs
	 * when `OBJECT_INFO_QUICK` was not passed.
	 */
	OBJECT_INFO_SECOND_READ = (1 << 4),

	/*
	 * This is meant for bulk prefetching of missing blobs in a partial
	 * clone. Implies OBJECT_INFO_SKIP_FETCH_OBJECT and OBJECT_INFO_QUICK.
	 */
	OBJECT_INFO_FOR_PREFETCH = (OBJECT_INFO_SKIP_FETCH_OBJECT | OBJECT_INFO_QUICK),
};

struct object_id;
struct object_info;
struct odb_read_stream;
struct odb_write_stream;

/*
 * A callback function that can be used to iterate through objects. If given,
 * the optional `oi` parameter will be populated the same as if you would call
 * `odb_read_object_info()`.
 *
 * Returning a non-zero error code will cause iteration to abort. The error
 * code will be propagated.
 */
typedef int (*odb_for_each_object_cb)(const struct object_id *oid,
				      struct object_info *oi,
				      void *cb_data);

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

	/* The type used by this source. */
	enum odb_source_type type;

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

	/*
	 * This callback is expected to free the underlying object database source and
	 * all associated resources. The function will never be called with a NULL pointer.
	 */
	void (*free)(struct odb_source *source);

	/*
	 * This callback is expected to close any open resources, like for
	 * example file descriptors or connections. The source is expected to
	 * still be usable after it has been closed. Closed resources may need
	 * to be reopened in that case.
	 */
	void (*close)(struct odb_source *source);

	/*
	 * This callback is expected to clear underlying caches of the object
	 * database source. The function is called when the repository has for
	 * example just been repacked so that new objects will become visible.
	 */
	void (*reprepare)(struct odb_source *source);

	/*
	 * This callback is expected to read object information from the object
	 * database source. The object info will be partially populated with
	 * pointers for each bit of information that was requested by the
	 * caller.
	 *
	 * The flags field is a combination of `OBJECT_INFO` flags. Only the
	 * following fields need to be handled by the backend:
	 *
	 *   - `OBJECT_INFO_QUICK` indicates it is fine to use caches without
	 *     re-verifying the data.
	 *
	 *   - `OBJECT_INFO_SECOND_READ` indicates that the initial object
	 *     lookup has failed and that the object sources should check
	 *     whether any of its on-disk state has changed that may have
	 *     caused the object to appear. Sources are free to ignore the
	 *     second read in case they know that the first read would have
	 *     already surfaced the object without reloading any on-disk state.
	 *
	 * The callback is expected to return a negative error code in case
	 * reading the object has failed, 0 otherwise.
	 */
	int (*read_object_info)(struct odb_source *source,
				const struct object_id *oid,
				struct object_info *oi,
				enum object_info_flags flags);

	/*
	 * This callback is expected to create a new read stream that can be
	 * used to stream the object identified by the given ID.
	 *
	 * The callback is expected to return a negative error code in case
	 * creating the object stream has failed, 0 otherwise.
	 */
	int (*read_object_stream)(struct odb_read_stream **out,
				  struct odb_source *source,
				  const struct object_id *oid);

	/*
	 * This callback is expected to iterate over all objects stored in this
	 * source and invoke the callback function for each of them. It is
	 * valid to yield the same object multiple time. A non-zero exit code
	 * from the object callback shall abort iteration.
	 *
	 * The optional `oi` structure shall be populated similar to how an individual
	 * call to `odb_source_read_object_info()` would have behaved. If the caller
	 * passes a `NULL` pointer then the object itself shall not be read.
	 *
	 * The callback is expected to return a negative error code in case the
	 * iteration has failed to read all objects, 0 otherwise. When the
	 * callback function returns a non-zero error code then that error code
	 * should be returned.
	 */
	int (*for_each_object)(struct odb_source *source,
			       const struct object_info *request,
			       odb_for_each_object_cb cb,
			       void *cb_data,
			       unsigned flags);

	/*
	 * This callback is expected to freshen the given object so that its
	 * last access time is set to the current time. This is used to ensure
	 * that objects that are recent will not get garbage collected even if
	 * they were unreachable.
	 *
	 * Returns 0 in case the object does not exist, 1 in case the object
	 * has been freshened.
	 */
	int (*freshen_object)(struct odb_source *source,
			      const struct object_id *oid);

	/*
	 * This callback is expected to persist the given object into the
	 * object source. In case the object already exists it shall be
	 * freshened.
	 *
	 * The flags field is a combination of `WRITE_OBJECT` flags.
	 *
	 * The resulting object ID (and optionally the compatibility object ID)
	 * shall be written into the out pointers. The callback is expected to
	 * return 0 on success, a negative error code otherwise.
	 */
	int (*write_object)(struct odb_source *source,
			    const void *buf, unsigned long len,
			    enum object_type type,
			    struct object_id *oid,
			    struct object_id *compat_oid,
			    unsigned flags);

	/*
	 * This callback is expected to persist the given object stream into
	 * the object source.
	 *
	 * The resulting object ID shall be written into the out pointer. The
	 * callback is expected to return 0 on success, a negative error code
	 * otherwise.
	 */
	int (*write_object_stream)(struct odb_source *source,
				   struct odb_write_stream *stream, size_t len,
				   struct object_id *oid);
};

/*
 * Allocate and initialize a new source for the given object database located
 * at `path`. `local` indicates whether or not the source is the local and thus
 * primary object source of the object database.
 */
struct odb_source *odb_source_new(struct object_database *odb,
				  const char *path,
				  bool local);

/*
 * Initialize the source for the given object database located at `path`.
 * `local` indicates whether or not the source is the local and thus primary
 * object source of the object database.
 *
 * This function is only supposed to be called by specific object source
 * implementations.
 */
void odb_source_init(struct odb_source *source,
		     struct object_database *odb,
		     enum odb_source_type type,
		     const char *path,
		     bool local);

/*
 * Free the object database source, releasing all associated resources and
 * freeing the structure itself.
 */
void odb_source_free(struct odb_source *source);

/*
 * Release the object database source, releasing all associated resources.
 *
 * This function is only supposed to be called by specific object source
 * implementations.
 */
void odb_source_release(struct odb_source *source);

/*
 * Close the object database source without releasing he underlying data. The
 * source can still be used going forward, but it first needs to be reopened.
 * This can be useful to reduce resource usage.
 */
static inline void odb_source_close(struct odb_source *source)
{
	source->close(source);
}

/*
 * Reprepare the object database source and clear any caches. Depending on the
 * backend used this may have the effect that concurrently-written objects
 * become visible.
 */
static inline void odb_source_reprepare(struct odb_source *source)
{
	source->reprepare(source);
}

/*
 * Read an object from the object database source identified by its object ID.
 * Returns 0 on success, a negative error code otherwise.
 */
static inline int odb_source_read_object_info(struct odb_source *source,
					      const struct object_id *oid,
					      struct object_info *oi,
					      enum object_info_flags flags)
{
	return source->read_object_info(source, oid, oi, flags);
}

/*
 * Create a new read stream for the given object ID. Returns 0 on success, a
 * negative error code otherwise.
 */
static inline int odb_source_read_object_stream(struct odb_read_stream **out,
						struct odb_source *source,
						const struct object_id *oid)
{
	return source->read_object_stream(out, source, oid);
}

/*
 * Iterate through all objects contained in the given source and invoke the
 * callback function for each of them. Returning a non-zero code from the
 * callback function aborts iteration. There is no guarantee that objects
 * are only iterated over once.
 *
 * The optional `oi` structure shall be populated similar to how an individual
 * call to `odb_source_read_object_info()` would have behaved. If the caller
 * passes a `NULL` pointer then the object itself shall not be read.
 *
 * The flags is a bitfield of `ODB_FOR_EACH_OBJECT_*` flags. Not all flags may
 * apply to a specific backend, so whether or not they are honored is defined
 * by the implementation.
 *
 * Returns 0 when all objects have been iterated over, a negative error code in
 * case iteration has failed, or a non-zero value returned from the callback.
 */
static inline int odb_source_for_each_object(struct odb_source *source,
					     const struct object_info *request,
					     odb_for_each_object_cb cb,
					     void *cb_data,
					     unsigned flags)
{
	return source->for_each_object(source, request, cb, cb_data, flags);
}

/*
 * Freshen an object in the object database by updating its timestamp.
 * Returns 1 in case the object has been freshened, 0 in case the object does
 * not exist.
 */
static inline int odb_source_freshen_object(struct odb_source *source,
					    const struct object_id *oid)
{
	return source->freshen_object(source, oid);
}

/*
 * Write an object into the object database source. Returns 0 on success, a
 * negative error code otherwise. Populates the given out pointers for the
 * object ID and the compatibility object ID, if non-NULL.
 */
static inline int odb_source_write_object(struct odb_source *source,
					  const void *buf, unsigned long len,
					  enum object_type type,
					  struct object_id *oid,
					  struct object_id *compat_oid,
					  unsigned flags)
{
	return source->write_object(source, buf, len, type, oid,
				    compat_oid, flags);
}

/*
 * Write an object into the object database source via a stream. The overall
 * length of the object must be known in advance.
 *
 * Return 0 on success, a negative error code otherwise. Populates the given
 * out pointer for the object ID.
 */
static inline int odb_source_write_object_stream(struct odb_source *source,
						 struct odb_write_stream *stream,
						 size_t len,
						 struct object_id *oid)
{
	return source->write_object_stream(source, stream, len, oid);
}

#endif
