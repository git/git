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

#define HASH_WRITE_OBJECT 1
#define HASH_FORMAT_CHECK 2
#define HASH_RENORMALIZE  4
#define HASH_SILENT 8
int index_fd(struct index_state *istate, struct object_id *oid, int fd, struct stat *st, enum object_type type, const char *path, unsigned flags);
int index_path(struct index_state *istate, struct object_id *oid, const char *path, struct stat *st, unsigned flags);

/*
 * Create the directory containing the named path, using care to be
 * somewhat safe against races. Return one of the scld_error values to
 * indicate success/failure. On error, set errno to describe the
 * problem.
 *
 * SCLD_VANISHED indicates that one of the ancestor directories of the
 * path existed at one point during the function call and then
 * suddenly vanished, probably because another process pruned the
 * directory while we were working.  To be robust against this kind of
 * race, callers might want to try invoking the function again when it
 * returns SCLD_VANISHED.
 *
 * safe_create_leading_directories() temporarily changes path while it
 * is working but restores it before returning.
 * safe_create_leading_directories_const() doesn't modify path, even
 * temporarily. Both these variants adjust the permissions of the
 * created directories to honor core.sharedRepository, so they are best
 * suited for files inside the git dir. For working tree files, use
 * safe_create_leading_directories_no_share() instead, as it ignores
 * the core.sharedRepository setting.
 */
enum scld_error {
	SCLD_OK = 0,
	SCLD_FAILED = -1,
	SCLD_PERMS = -2,
	SCLD_EXISTS = -3,
	SCLD_VANISHED = -4
};
enum scld_error safe_create_leading_directories(char *path);
enum scld_error safe_create_leading_directories_const(const char *path);
enum scld_error safe_create_leading_directories_no_share(char *path);

int mkdir_in_gitdir(const char *path);

int git_open_cloexec(const char *name, int flags);
#define git_open(name) git_open_cloexec(name, O_RDONLY)

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

int finalize_object_file(const char *tmpfile, const char *filename);

/* Helper to check and "touch" a file */
int check_and_freshen_file(const char *fn, int freshen);

void *read_object_with_reference(struct repository *r,
				 const struct object_id *oid,
				 enum object_type required_type,
				 unsigned long *size,
				 struct object_id *oid_ret);

#endif /* OBJECT_FILE_H */
