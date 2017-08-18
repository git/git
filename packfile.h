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

extern unsigned int pack_used_ctr;
extern unsigned int pack_mmap_calls;
extern unsigned int peak_pack_open_windows;
extern unsigned int pack_open_windows;
extern unsigned int pack_open_fds;
extern unsigned int pack_max_fds;
extern size_t peak_pack_mapped;
extern size_t pack_mapped;

#endif
