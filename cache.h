#ifndef CACHE_H
#define CACHE_H

#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <netinet/in.h>

#include SHA1_HEADER
#include <zlib.h>

#if ZLIB_VERNUM < 0x1200
#define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
#endif

#ifdef DT_UNKNOWN
#define DTYPE(de)	((de)->d_type)
#else
#define DT_UNKNOWN	0
#define DT_DIR		1
#define DT_REG		2
#define DT_LNK		3
#define DTYPE(de)	DT_UNKNOWN
#endif

/*
 * Environment variables transition.
 * We accept older names for now but warn.
 */
extern char *gitenv_bc(const char *);
#define gitenv(e) (getenv(e) ? : gitenv_bc(e))

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

#define CE_NAMEMASK  (0x0fff)
#define CE_STAGEMASK (0x3000)
#define CE_STAGESHIFT 12

#define create_ce_flags(len, stage) htons((len) | ((stage) << CE_STAGESHIFT))
#define ce_namelen(ce) (CE_NAMEMASK & ntohs((ce)->ce_flags))
#define ce_size(ce) cache_entry_size(ce_namelen(ce))
#define ce_stage(ce) ((CE_STAGEMASK & ntohs((ce)->ce_flags)) >> CE_STAGESHIFT)

#define ce_permissions(mode) (((mode) & 0100) ? 0755 : 0644)
static inline unsigned int create_ce_mode(unsigned int mode)
{
	if (S_ISLNK(mode))
		return htonl(S_IFLNK);
	return htonl(S_IFREG | ce_permissions(mode));
}

#define cache_entry_size(len) ((offsetof(struct cache_entry,name) + (len) + 8) & ~7)

extern struct cache_entry **active_cache;
extern unsigned int active_nr, active_alloc, active_cache_changed;

#define GIT_DIR_ENVIRONMENT "GIT_DIR"
#define DEFAULT_GIT_DIR_ENVIRONMENT ".git"
#define DB_ENVIRONMENT "GIT_OBJECT_DIRECTORY"
#define INDEX_ENVIRONMENT "GIT_INDEX_FILE"

extern char *get_object_directory(void);
extern char *get_index_file(void);

#define ALTERNATE_DB_ENVIRONMENT "GIT_ALTERNATE_OBJECT_DIRECTORIES"

#define alloc_nr(x) (((x)+16)*3/2)

/* Initialize and use the cache information */
extern int read_cache(void);
extern int write_cache(int newfd, struct cache_entry **cache, int entries);
extern int cache_name_pos(const char *name, int namelen);
#define ADD_CACHE_OK_TO_ADD 1		/* Ok to add */
#define ADD_CACHE_OK_TO_REPLACE 2	/* Ok to replace file/directory */
extern int add_cache_entry(struct cache_entry *ce, int option);
extern int remove_entry_at(int pos);
extern int remove_file_from_cache(char *path);
extern int same_name(struct cache_entry *a, struct cache_entry *b);
extern int cache_match_stat(struct cache_entry *ce, struct stat *st);
extern int index_fd(unsigned char *sha1, int fd, struct stat *st);

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020
#define TYPE_CHANGED    0x0040

/* Return a statically allocated filename matching the sha1 signature */
extern char *sha1_file_name(const unsigned char *sha1);

/* Read and unpack a sha1 file into memory, write memory to a sha1 file */
extern void * map_sha1_file(const unsigned char *sha1, unsigned long *size);
extern void * unpack_sha1_file(void *map, unsigned long mapsize, char *type, unsigned long *size);
extern void * read_sha1_file(const unsigned char *sha1, char *type, unsigned long *size);
extern int write_sha1_file(char *buf, unsigned long len, const char *type, unsigned char *return_sha1);

extern int check_sha1_signature(unsigned char *sha1, void *buf, unsigned long size, const char *type);

/* Read a tree into the cache */
extern int read_tree(void *buffer, unsigned long size, int stage);

extern int write_sha1_from_fd(const unsigned char *sha1, int fd);

extern int has_sha1_file(const unsigned char *sha1);

/* Convert to/from hex/sha1 representation */
extern int get_sha1(const char *str, unsigned char *sha1);
extern int get_sha1_hex(const char *hex, unsigned char *sha1);
extern char *sha1_to_hex(const unsigned char *sha1);	/* static buffer result! */

/* General helper functions */
extern void usage(const char *err);
extern void die(const char *err, ...);
extern int error(const char *err, ...);

extern int cache_name_compare(const char *name1, int len1, const char *name2, int len2);

extern void *read_object_with_reference(const unsigned char *sha1,
					const unsigned char *required_type,
					unsigned long *size,
					unsigned char *sha1_ret);

const char *show_date(unsigned long time, int timezone);
void parse_date(char *date, char *buf, int bufsize);
void datestamp(char *buf, int bufsize);

static inline void *xmalloc(int size)
{
	void *ret = malloc(size);
	if (!ret)
		die("Out of memory, malloc failed");
	return ret;
}

static inline void *xrealloc(void *ptr, int size)
{
	void *ret = realloc(ptr, size);
	if (!ret)
		die("Out of memory, realloc failed");
	return ret;
}

#endif /* CACHE_H */
