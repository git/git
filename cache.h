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
#include <limits.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <dirent.h>

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

#ifdef __GNUC__
#define NORETURN __attribute__((__noreturn__))
#else
#define NORETURN
#endif

/*
 * Intensive research over the course of many years has shown that
 * port 9418 is totally unused by anything else. Or
 *
 *	Your search - "port 9418" - did not match any documents.
 *
 * as www.google.com puts it.
 */
#define DEFAULT_GIT_PORT 9418

/*
 * Environment variables transition.
 * We accept older names for now but warn.
 */
extern char *gitenv_bc(const char *);
#define gitenv(e) (getenv(e) ? : gitenv_bc(e))

/*
 * Basic data structures for the directory cache
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
#define CE_UPDATE    (0x4000)
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
#define GRAFT_ENVIRONMENT "GIT_GRAFT_FILE"

extern char *get_object_directory(void);
extern char *get_refs_directory(void);
extern char *get_index_file(void);
extern char *get_graft_file(void);

#define ALTERNATE_DB_ENVIRONMENT "GIT_ALTERNATE_OBJECT_DIRECTORIES"

#define alloc_nr(x) (((x)+16)*3/2)

/* Initialize and use the cache information */
extern int read_cache(void);
extern int write_cache(int newfd, struct cache_entry **cache, int entries);
extern int cache_name_pos(const char *name, int namelen);
#define ADD_CACHE_OK_TO_ADD 1		/* Ok to add */
#define ADD_CACHE_OK_TO_REPLACE 2	/* Ok to replace file/directory */
#define ADD_CACHE_SKIP_DFCHECK 4	/* Ok to skip DF conflict checks */
extern int add_cache_entry(struct cache_entry *ce, int option);
extern int remove_cache_entry_at(int pos);
extern int remove_file_from_cache(char *path);
extern int ce_same_name(struct cache_entry *a, struct cache_entry *b);
extern int ce_match_stat(struct cache_entry *ce, struct stat *st);
extern int ce_path_match(const struct cache_entry *ce, const char **pathspec);
extern int index_fd(unsigned char *sha1, int fd, struct stat *st, int write_object, const char *type);
extern void fill_stat_cache_info(struct cache_entry *ce, struct stat *st);

struct cache_file {
	struct cache_file *next;
	char lockfile[PATH_MAX];
};
extern int hold_index_file_for_update(struct cache_file *, const char *path);
extern int commit_index_file(struct cache_file *);
extern void rollback_index_file(struct cache_file *);

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020
#define TYPE_CHANGED    0x0040

/* Return a statically allocated filename matching the sha1 signature */
extern char *mkpath(const char *fmt, ...);
extern char *git_path(const char *fmt, ...);
extern char *sha1_file_name(const unsigned char *sha1);
extern char *sha1_pack_name(const unsigned char *sha1);
extern char *sha1_pack_index_name(const unsigned char *sha1);

int git_mkstemp(char *path, size_t n, const char *template);

int safe_create_leading_directories(char *path);
char *safe_strncpy(char *, const char *, size_t);

/* Read and unpack a sha1 file into memory, write memory to a sha1 file */
extern int unpack_sha1_header(z_stream *stream, void *map, unsigned long mapsize, void *buffer, unsigned long size);
extern int parse_sha1_header(char *hdr, char *type, unsigned long *sizep);
extern int sha1_object_info(const unsigned char *, char *, unsigned long *);
extern void * unpack_sha1_file(void *map, unsigned long mapsize, char *type, unsigned long *size);
extern void * read_sha1_file(const unsigned char *sha1, char *type, unsigned long *size);
extern int write_sha1_file(void *buf, unsigned long len, const char *type, unsigned char *return_sha1);
extern char *write_sha1_file_prepare(void *buf,
				     unsigned long len,
				     const char *type,
				     unsigned char *sha1,
				     unsigned char *hdr,
				     int *hdrlen);

extern int check_sha1_signature(const unsigned char *sha1, void *buf, unsigned long size, const char *type);

/* Read a tree into the cache */
extern int read_tree(void *buffer, unsigned long size, int stage, const char **paths);

extern int write_sha1_from_fd(const unsigned char *sha1, int fd, char *buffer,
			      size_t bufsize, size_t *bufposn);
extern int write_sha1_to_fd(int fd, const unsigned char *sha1);

extern int has_sha1_pack(const unsigned char *sha1);
extern int has_sha1_file(const unsigned char *sha1);

extern int has_pack_file(const unsigned char *sha1);
extern int has_pack_index(const unsigned char *sha1);

/* Convert to/from hex/sha1 representation */
extern int get_sha1(const char *str, unsigned char *sha1);
extern int get_sha1_hex(const char *hex, unsigned char *sha1);
extern char *sha1_to_hex(const unsigned char *sha1);	/* static buffer result! */

/* General helper functions */
extern void usage(const char *err) NORETURN;
extern void die(const char *err, ...) NORETURN;
extern int error(const char *err, ...);

extern int base_name_compare(const char *name1, int len1, int mode1, const char *name2, int len2, int mode2);
extern int cache_name_compare(const char *name1, int len1, const char *name2, int len2);

extern void *read_object_with_reference(const unsigned char *sha1,
					const char *required_type,
					unsigned long *size,
					unsigned char *sha1_ret);

const char *show_date(unsigned long time, int timezone);
void parse_date(const char *date, char *buf, int bufsize);
void datestamp(char *buf, int bufsize);

extern int setup_ident(void);
extern char *get_ident(const char *name, const char *email, const char *date_str);
extern char *git_author_info(void);
extern char *git_committer_info(void);

static inline void *xmalloc(size_t size)
{
	void *ret = malloc(size);
	if (!ret)
		die("Out of memory, malloc failed");
	return ret;
}

static inline void *xrealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (!ret)
		die("Out of memory, realloc failed");
	return ret;
}

static inline void *xcalloc(size_t nmemb, size_t size)
{
	void *ret = calloc(nmemb, size);
	if (!ret)
		die("Out of memory, calloc failed");
	return ret;
}

struct checkout {
	const char *base_dir;
	int base_dir_len;
	unsigned force:1,
		 quiet:1,
		 not_new:1,
		 refresh_cache:1;
};

extern int checkout_entry(struct cache_entry *ce, struct checkout *state);

extern struct alternate_object_database {
	char *base;
	char *name;
} *alt_odb;
extern void prepare_alt_odb(void);

extern struct packed_git {
	struct packed_git *next;
	unsigned long index_size;
	unsigned long pack_size;
	unsigned int *index_base;
	void *pack_base;
	unsigned int pack_last_used;
	unsigned int pack_use_cnt;
	unsigned char sha1[20];
	char pack_name[0]; /* something like ".git/objects/pack/xxxxx.pack" */
} *packed_git;

struct pack_entry {
	unsigned int offset;
	unsigned char sha1[20];
	struct packed_git *p;
};

struct ref {
	struct ref *next;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	struct ref *peer_ref; /* when renaming */
	char name[0];
};

extern int git_connect(int fd[2], char *url, const char *prog);
extern int finish_connect(pid_t pid);
extern int path_match(const char *path, int nr, char **match);
extern int match_refs(struct ref *src, struct ref *dst, struct ref ***dst_tail,
		      int nr_refspec, char **refspec, int all);
extern int get_ack(int fd, unsigned char *result_sha1);
extern struct ref **get_remote_heads(int in, struct ref **list, int nr_match, char **match);

extern struct packed_git *parse_pack_index(unsigned char *sha1);

extern void prepare_packed_git(void);
extern void install_packed_git(struct packed_git *pack);

extern struct packed_git *find_sha1_pack(const unsigned char *sha1, 
					 struct packed_git *packs);

extern int use_packed_git(struct packed_git *);
extern void unuse_packed_git(struct packed_git *);
extern struct packed_git *add_packed_git(char *, int);
extern int num_packed_objects(const struct packed_git *p);
extern int nth_packed_object_sha1(const struct packed_git *, int, unsigned char*);
extern int find_pack_entry_one(const unsigned char *, struct pack_entry *, struct packed_git *);
extern void *unpack_entry_gently(struct pack_entry *, char *, unsigned long *);
extern void packed_object_info_detail(struct pack_entry *, char *, unsigned long *, unsigned long *, int *, unsigned char *);

/* Dumb servers support */
extern int update_server_info(int);

#endif /* CACHE_H */
