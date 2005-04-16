#ifndef CACHE_H
#define CACHE_H

#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <netinet/in.h>

#include <openssl/sha.h>
#include <zlib.h>

/*
 * Basic data structures for the directory cache
 *
 * NOTE NOTE NOTE! This is all in the native CPU byte format. It's
 * not even trying to be portable. It's trying to be efficient. It's
 * just a cache, after all.
 */

#define CACHE_SIGNATURE 0x44495243	/* "DIRC" */
struct cache_header {
	unsigned int hdr_signature;
	unsigned int hdr_version;
	unsigned int hdr_entries;
	unsigned char sha1[20];
};

/*
 * The "cache_time" is just the low 32 bits of the
 * time. It doesn't matter if it overflows - we only
 * check it for equality in the 32 bits we save.
 */
struct cache_time {
	unsigned int sec;
	unsigned int nsec;
};

/*
 * dev/ino/uid/gid/size are also just tracked to the low 32 bits
 * Again - this is just a (very strong in practice) heuristic that
 * the inode hasn't changed.
 *
 * We save the fields in big-endian order to allow using the
 * index file over NFS transparently.
 */
struct cache_entry {
	struct cache_time ce_ctime;
	struct cache_time ce_mtime;
	unsigned int ce_dev;
	unsigned int ce_ino;
	unsigned int ce_mode;
	unsigned int ce_uid;
	unsigned int ce_gid;
	unsigned int ce_size;
	unsigned char sha1[20];
	unsigned short ce_flags;
	char name[0];
};

#define CE_NAMEMASK (0x0fff)
#define CE_STAGE1   (0x1000)
#define CE_STAGE2   (0x2000)

const char *sha1_file_directory;
struct cache_entry **active_cache;
unsigned int active_nr, active_alloc;

#define DB_ENVIRONMENT "SHA1_FILE_DIRECTORY"
#define DEFAULT_DB_ENVIRONMENT ".git/objects"

#define cache_entry_size(len) ((offsetof(struct cache_entry,name) + (len) + 8) & ~7)
#define ce_namelen(ce) (CE_NAMEMASK & ntohs((ce)->ce_flags))
#define ce_size(ce) cache_entry_size(ce_namelen(ce))

#define alloc_nr(x) (((x)+16)*3/2)

/* Initialize and use the cache information */
extern int read_cache(void);
extern int write_cache(int newfd, struct cache_entry **cache, int entries);
extern int cache_name_pos(const char *name, int namelen);
extern int add_cache_entry(struct cache_entry *ce, int ok_to_add);
extern int remove_file_from_cache(char *path);
extern int cache_match_stat(struct cache_entry *ce, struct stat *st);

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020

/* Return a statically allocated filename matching the sha1 signature */
extern char *sha1_file_name(const unsigned char *sha1);

/* Write a memory buffer out to the sha file */
extern int write_sha1_buffer(const unsigned char *sha1, void *buf, unsigned int size);

/* Read and unpack a sha1 file into memory, write memory to a sha1 file */
extern void * map_sha1_file(const unsigned char *sha1, unsigned long *size);
extern void * unpack_sha1_file(void *map, unsigned long mapsize, char *type, unsigned long *size);
extern void * read_sha1_file(const unsigned char *sha1, char *type, unsigned long *size);
extern int write_sha1_file(char *buf, unsigned len, unsigned char *return_sha1);
extern int check_sha1_signature(unsigned char *sha1, void *buf, unsigned long size);

/* Convert to/from hex/sha1 representation */
extern int get_sha1_hex(const char *hex, unsigned char *sha1);
extern char *sha1_to_hex(const unsigned char *sha1);	/* static buffer result! */

/* General helper functions */
extern void usage(const char *err);
extern void die(const char *err, ...);
extern int error(const char *err, ...);

extern int cache_name_compare(const char *name1, int len1, const char *name2, int len2);

#endif /* CACHE_H */
