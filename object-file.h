#ifndef OBJECT_FILE_H
#define OBJECT_FILE_H

#include "git-zlib.h"
#include "object.h"
#include "odb.h"

struct index_state;

enum {
	INDEX_WRITE_OBJECT = (1 << 0),
	INDEX_FORMAT_CHECK = (1 << 1),
	INDEX_RENORMALIZE  = (1 << 2),
};

int index_fd(struct index_state *istate, struct object_id *oid, int fd, struct stat *st, enum object_type type, const char *path, unsigned flags);
int index_path(struct index_state *istate, struct object_id *oid, const char *path, struct stat *st, unsigned flags);

struct object_info;
struct odb_read_stream;
struct odb_source;

struct odb_source_loose {
	struct odb_source *source;

	/*
	 * Used to store the results of readdir(3) calls when we are OK
	 * sacrificing accuracy due to races for speed. That includes
	 * object existence with OBJECT_INFO_QUICK, as well as
	 * our search for unique abbreviated hashes. Don't use it for tasks
	 * requiring greater accuracy!
	 *
	 * Be sure to call odb_load_loose_cache() before using.
	 */
	uint32_t subdir_seen[8]; /* 256 bits */
	struct oidtree *cache;

	/* Map between object IDs for loose objects. */
	struct loose_object_map *map;
};

struct odb_source_loose *odb_source_loose_new(struct odb_source *source);
void odb_source_loose_free(struct odb_source_loose *loose);

/* Reprepare the loose source by emptying the loose object cache. */
void odb_source_loose_reprepare(struct odb_source *source);

int odb_source_loose_read_object_info(struct odb_source *source,
				      const struct object_id *oid,
				      struct object_info *oi,
				      unsigned flags);

int odb_source_loose_read_object_stream(struct odb_read_stream **out,
					struct odb_source *source,
					const struct object_id *oid);

/*
 * Return true iff an object database source has a loose object
 * with the specified name.  This function does not respect replace
 * references.
 */
int odb_source_loose_has_object(struct odb_source *source,
				const struct object_id *oid);

int odb_source_loose_freshen_object(struct odb_source *source,
				    const struct object_id *oid);

int odb_source_loose_write_object(struct odb_source *source,
				  const void *buf, unsigned long len,
				  enum object_type type, struct object_id *oid,
				  struct object_id *compat_oid_in, unsigned flags);

int odb_source_loose_write_stream(struct odb_source *source,
				  struct odb_write_stream *stream, size_t len,
				  struct object_id *oid);

/*
 * Populate and return the loose object cache array corresponding to the
 * given object ID.
 */
struct oidtree *odb_source_loose_cache(struct odb_source *source,
				       const struct object_id *oid);

/*
 * Put in `buf` the name of the file in the local object database that
 * would be used to store a loose object with the specified oid.
 */
const char *odb_loose_path(struct odb_source *source,
			   struct strbuf *buf,
			   const struct object_id *oid);

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
int for_each_loose_file_in_source(struct odb_source *source,
				  each_loose_object_fn obj_cb,
				  each_loose_cruft_fn cruft_cb,
				  each_loose_subdir_fn subdir_cb,
				  void *data);

/*
 * Iterate through all loose objects in the given object database source and
 * invoke the callback function for each of them. If an object info request is
 * given, then the object info will be read for every individual object and
 * passed to the callback as if `odb_source_loose_read_object_info()` was
 * called for the object.
 */
int odb_source_loose_for_each_object(struct odb_source *source,
				     const struct object_info *request,
				     odb_for_each_object_cb cb,
				     void *cb_data,
				     unsigned flags);

/**
 * format_object_header() is a thin wrapper around s xsnprintf() that
 * writes the initial "<type> <obj-len>" part of the loose object
 * header. It returns the size that snprintf() returns + 1.
 */
int format_object_header(char *str, size_t size, enum object_type type,
			 size_t objsize);

int force_object_loose(struct odb_source *source,
		       const struct object_id *oid, time_t mtime);

/**
 * With in-core object data in "buf", rehash it to make sure the
 * object name actually matches "oid" to detect object corruption.
 *
 * A negative value indicates an error, usually that the OID is not
 * what we expected, but it might also indicate another error.
 */
int check_object_signature(struct repository *r, const struct object_id *oid,
			   void *map, unsigned long size,
			   enum object_type type);

/**
 * A streaming version of check_object_signature().
 * Try reading the object named with "oid" using
 * the streaming interface and rehash it to do the same.
 */
int stream_object_signature(struct repository *r, const struct object_id *oid);

enum finalize_object_file_flags {
	FOF_SKIP_COLLISION_CHECK = 1,
};

int finalize_object_file(struct repository *repo,
			 const char *tmpfile, const char *filename);
int finalize_object_file_flags(struct repository *repo,
			       const char *tmpfile, const char *filename,
			       enum finalize_object_file_flags flags);

void hash_object_file(const struct git_hash_algo *algo, const void *buf,
		      unsigned long len, enum object_type type,
		      struct object_id *oid);

/* Helper to check and "touch" a file */
int check_and_freshen_file(const char *fn, int freshen);

/*
 * Open the loose object at path, check its hash, and return the contents,
 * use the "oi" argument to assert things about the object, or e.g. populate its
 * type, and size. If the object is a blob, then "contents" may return NULL,
 * to allow streaming of large blobs.
 *
 * Returns 0 on success, negative on error (details may be written to stderr).
 */
int read_loose_object(struct repository *repo,
		      const char *path,
		      const struct object_id *expected_oid,
		      struct object_id *real_oid,
		      void **contents,
		      struct object_info *oi);

struct odb_transaction;

/*
 * Tell the object database to optimize for adding
 * multiple objects. odb_transaction_files_commit must be called
 * to make new objects visible. If a transaction is already
 * pending, NULL is returned.
 */
struct odb_transaction *odb_transaction_files_begin(struct odb_source *source);

#endif /* OBJECT_FILE_H */
