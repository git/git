#ifndef PACKFILE_H
#define PACKFILE_H

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

/* A hook to report invalid files in pack directory */
#define PACKDIR_FILE_PACK 1
#define PACKDIR_FILE_IDX 2
#define PACKDIR_FILE_GARBAGE 4
extern void (*report_garbage)(unsigned seen_bits, const char *path);

extern void prepare_packed_git(void);
extern void reprepare_packed_git(void);
extern void install_packed_git(struct packed_git *pack);

/*
 * Give a rough count of objects in the repository. This sacrifices accuracy
 * for speed.
 */
unsigned long approximate_object_count(void);

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

extern unsigned char *use_pack(struct packed_git *, struct pack_window **, off_t, unsigned long *);
extern void close_pack_windows(struct packed_git *);
extern void close_all_packs(void);
extern void unuse_pack(struct pack_window **);
extern struct packed_git *add_packed_git(const char *path, size_t path_len, int local);

extern unsigned long unpack_object_header_buffer(const unsigned char *buf, unsigned long len, enum object_type *type, unsigned long *sizep);

extern void release_pack_memory(size_t);

extern int open_packed_git(struct packed_git *p);

#endif
