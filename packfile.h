#ifndef PACKFILE_H
#define PACKFILE_H

#include "cache.h"
#include "oidset.h"

/* in object-store.h */
struct packed_git;
struct object_info;

/*
 * Generate the filename to be used for a pack file with checksum "sha1" and
 * extension "ext". The result is written into the strbuf "buf", overwriting
 * any existing contents. A pointer to buf->buf is returned as a convenience.
 *
 * Example: odb_pack_name(out, sha1, "idx") => ".git/objects/pack/pack-1234..idx"
 */
extern char *odb_pack_name(struct strbuf *buf, const unsigned char *sha1, const char *ext);

/*
 * Return the name of the (local) packfile with the specified sha1 in
 * its name.  The return value is a pointer to memory that is
 * overwritten each time this function is called.
 */
extern char *sha1_pack_name(const unsigned char *sha1);

/*
 * Return the name of the (local) pack index file with the specified
 * sha1 in its name.  The return value is a pointer to memory that is
 * overwritten each time this function is called.
 */
extern char *sha1_pack_index_name(const unsigned char *sha1);

extern struct packed_git *parse_pack_index(unsigned char *sha1, const char *idx_path);

typedef void each_file_in_pack_dir_fn(const char *full_path, size_t full_path_len,
				      const char *file_pach, void *data);
void for_each_file_in_pack_dir(const char *objdir,
			       each_file_in_pack_dir_fn fn,
			       void *data);

/* A hook to report invalid files in pack directory */
#define PACKDIR_FILE_PACK 1
#define PACKDIR_FILE_IDX 2
#define PACKDIR_FILE_GARBAGE 4
extern void (*report_garbage)(unsigned seen_bits, const char *path);

extern void reprepare_packed_git(struct repository *r);
extern void install_packed_git(struct repository *r, struct packed_git *pack);

struct packed_git *get_packed_git(struct repository *r);
struct list_head *get_packed_git_mru(struct repository *r);
struct multi_pack_index *get_multi_pack_index(struct repository *r);
struct packed_git *get_all_packs(struct repository *r);

/*
 * Give a rough count of objects in the repository. This sacrifices accuracy
 * for speed.
 */
unsigned long approximate_object_count(void);

extern struct packed_git *find_sha1_pack(const unsigned char *sha1,
					 struct packed_git *packs);

extern void pack_report(void);

/*
 * mmap the index file for the specified packfile (if it is not
 * already mmapped).  Return 0 on success.
 */
extern int open_pack_index(struct packed_git *);

/*
 * munmap the index file for the specified packfile (if it is
 * currently mmapped).
 */
extern void close_pack_index(struct packed_git *);

extern uint32_t get_pack_fanout(struct packed_git *p, uint32_t value);

extern unsigned char *use_pack(struct packed_git *, struct pack_window **, off_t, unsigned long *);
extern void close_pack_windows(struct packed_git *);
extern void close_pack(struct packed_git *);
extern void close_all_packs(struct raw_object_store *o);
extern void unuse_pack(struct pack_window **);
extern void clear_delta_base_cache(void);
extern struct packed_git *add_packed_git(const char *path, size_t path_len, int local);

/*
 * Make sure that a pointer access into an mmap'd index file is within bounds,
 * and can provide at least 8 bytes of data.
 *
 * Note that this is only necessary for variable-length segments of the file
 * (like the 64-bit extended offset table), as we compare the size to the
 * fixed-length parts when we open the file.
 */
extern void check_pack_index_ptr(const struct packed_git *p, const void *ptr);

/*
 * Perform binary search on a pack-index for a given oid. Packfile is expected to
 * have a valid pack-index.
 *
 * See 'bsearch_hash' for more information.
 */
int bsearch_pack(const struct object_id *oid, const struct packed_git *p, uint32_t *result);

/*
 * Return the SHA-1 of the nth object within the specified packfile.
 * Open the index if it is not already open.  The return value points
 * at the SHA-1 within the mmapped index.  Return NULL if there is an
 * error.
 */
extern const unsigned char *nth_packed_object_sha1(struct packed_git *, uint32_t n);
/*
 * Like nth_packed_object_sha1, but write the data into the object specified by
 * the the first argument.  Returns the first argument on success, and NULL on
 * error.
 */
extern const struct object_id *nth_packed_object_oid(struct object_id *, struct packed_git *, uint32_t n);

/*
 * Return the offset of the nth object within the specified packfile.
 * The index must already be opened.
 */
extern off_t nth_packed_object_offset(const struct packed_git *, uint32_t n);

/*
 * If the object named sha1 is present in the specified packfile,
 * return its offset within the packfile; otherwise, return 0.
 */
extern off_t find_pack_entry_one(const unsigned char *sha1, struct packed_git *);

extern int is_pack_valid(struct packed_git *);
extern void *unpack_entry(struct repository *r, struct packed_git *, off_t, enum object_type *, unsigned long *);
extern unsigned long unpack_object_header_buffer(const unsigned char *buf, unsigned long len, enum object_type *type, unsigned long *sizep);
extern unsigned long get_size_from_delta(struct packed_git *, struct pack_window **, off_t);
extern int unpack_object_header(struct packed_git *, struct pack_window **, off_t *, unsigned long *);

extern void release_pack_memory(size_t);

/* global flag to enable extra checks when accessing packed objects */
extern int do_check_packed_object_crc;

extern int packed_object_info(struct repository *r,
			      struct packed_git *pack,
			      off_t offset, struct object_info *);

extern void mark_bad_packed_object(struct packed_git *p, const unsigned char *sha1);
extern const struct packed_git *has_packed_and_bad(const unsigned char *sha1);

/*
 * Iff a pack file in the given repository contains the object named by sha1,
 * return true and store its location to e.
 */
extern int find_pack_entry(struct repository *r, const struct object_id *oid, struct pack_entry *e);

extern int has_object_pack(const struct object_id *oid);

extern int has_pack_index(const unsigned char *sha1);

/*
 * Return 1 if an object in a promisor packfile is or refers to the given
 * object, 0 otherwise.
 */
extern int is_promisor_object(const struct object_id *oid);

#endif
