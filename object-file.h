#ifndef OBJECT_FILE_H
#define OBJECT_FILE_H

#include "git-zlib.h"
#include "object.h"

struct index_state;

/*
 * Set this to 0 to prevent oid_object_info_extended() from fetching missing
 * blobs. This has a difference only if extensions.partialClone is set.
 *
 * Its default value is 1.
 */
extern int fetch_if_missing;

enum {
	INDEX_WRITE_OBJECT = (1 << 0),
	INDEX_FORMAT_CHECK = (1 << 1),
	INDEX_RENORMALIZE  = (1 << 2),
};

int index_fd(struct index_state *istate, struct object_id *oid, int fd, struct stat *st, enum object_type type, const char *path, unsigned flags);
int index_path(struct index_state *istate, struct object_id *oid, const char *path, struct stat *st, unsigned flags);

struct object_directory;

const char *odb_loose_path(struct object_directory *odb,
			   struct strbuf *buf,
			   const struct object_id *oid);

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

/**
 * unpack_loose_header() initializes the data stream needed to unpack
 * a loose object header.
 *
 * Returns:
 *
 * - ULHR_OK on success
 * - ULHR_BAD on error
 * - ULHR_TOO_LONG if the header was too long
 *
 * It will only parse up to MAX_HEADER_LEN bytes unless an optional
 * "hdrbuf" argument is non-NULL. This is intended for use with
 * OBJECT_INFO_ALLOW_UNKNOWN_TYPE to extract the bad type for (error)
 * reporting. The full header will be extracted to "hdrbuf" for use
 * with parse_loose_header(), ULHR_TOO_LONG will still be returned
 * from this function to indicate that the header was too long.
 */
enum unpack_loose_header_result {
	ULHR_OK,
	ULHR_BAD,
	ULHR_TOO_LONG,
};
enum unpack_loose_header_result unpack_loose_header(git_zstream *stream,
						    unsigned char *map,
						    unsigned long mapsize,
						    void *buffer,
						    unsigned long bufsiz,
						    struct strbuf *hdrbuf);

/**
 * parse_loose_header() parses the starting "<type> <len>\0" of an
 * object. If it doesn't follow that format -1 is returned. To check
 * the validity of the <type> populate the "typep" in the "struct
 * object_info". It will be OBJ_BAD if the object type is unknown. The
 * parsed <len> can be retrieved via "oi->sizep", and from there
 * passed to unpack_loose_rest().
 */
struct object_info;
int parse_loose_header(const char *hdr, struct object_info *oi);

enum {
	/*
	 * By default, `write_object_file_literally()` does not actually write
	 * anything into the object store, but only computes the object ID.
	 * This flag changes that so that the object will be written as a loose
	 * object and persisted.
	 */
	WRITE_OBJECT_FILE_PERSIST = (1 << 0),

	/*
	 * Do not print an error in case something gose wrong.
	 */
	WRITE_OBJECT_FILE_SILENT = (1 << 1),
};

int write_object_file_flags(const void *buf, unsigned long len,
			    enum object_type type, struct object_id *oid,
			    struct object_id *comapt_oid_in, unsigned flags);
static inline int write_object_file(const void *buf, unsigned long len,
				    enum object_type type, struct object_id *oid)
{
	return write_object_file_flags(buf, len, type, oid, NULL, 0);
}

struct input_stream {
	const void *(*read)(struct input_stream *, unsigned long *len);
	void *data;
	int is_finished;
};

int write_object_file_literally(const void *buf, unsigned long len,
				const char *type, struct object_id *oid,
				unsigned flags);
int stream_loose_object(struct input_stream *in_stream, size_t len,
			struct object_id *oid);

int force_object_loose(const struct object_id *oid, time_t mtime);

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

int loose_object_info(struct repository *r,
		      const struct object_id *oid,
		      struct object_info *oi, int flags);

enum finalize_object_file_flags {
	FOF_SKIP_COLLISION_CHECK = 1,
};

int finalize_object_file(const char *tmpfile, const char *filename);
int finalize_object_file_flags(const char *tmpfile, const char *filename,
			       enum finalize_object_file_flags flags);

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
int read_loose_object(const char *path,
		      const struct object_id *expected_oid,
		      struct object_id *real_oid,
		      void **contents,
		      struct object_info *oi);

#endif /* OBJECT_FILE_H */
