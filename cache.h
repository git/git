#ifndef CACHE_H
#define CACHE_H

#include "git-compat-util.h"

#include SHA1_HEADER
#include <zlib.h>

#if ZLIB_VERNUM < 0x1200
#define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
#endif

#if defined(DT_UNKNOWN) && !defined(NO_D_TYPE_IN_DIRENT)
#define DTYPE(de)	((de)->d_type)
#else
#undef DT_UNKNOWN
#undef DT_DIR
#undef DT_REG
#undef DT_LNK
#define DT_UNKNOWN	0
#define DT_DIR		1
#define DT_REG		2
#define DT_LNK		3
#define DTYPE(de)	DT_UNKNOWN
#endif

/*
 * Intensive research over the course of many years has shown that
 * port 9418 is totally unused by anything else. Or
 *
 *	Your search - "port 9418" - did not match any documents.
 *
 * as www.google.com puts it.
 *
 * This port has been properly assigned for git use by IANA:
 * git (Assigned-9418) [I06-050728-0001].
 *
 *	git  9418/tcp   git pack transfer service
 *	git  9418/udp   git pack transfer service
 *
 * with Linus Torvalds <torvalds@osdl.org> as the point of
 * contact. September 2005.
 *
 * See http://www.iana.org/assignments/port-numbers
 */
#define DEFAULT_GIT_PORT 9418

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
	char name[FLEX_ARRAY]; /* more */
};

#define CE_NAMEMASK  (0x0fff)
#define CE_STAGEMASK (0x3000)
#define CE_UPDATE    (0x4000)
#define CE_VALID     (0x8000)
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
#define canon_mode(mode) \
	(S_ISREG(mode) ? (S_IFREG | ce_permissions(mode)) : \
	S_ISLNK(mode) ? S_IFLNK : S_IFDIR)

#define cache_entry_size(len) ((offsetof(struct cache_entry,name) + (len) + 8) & ~7)

extern struct cache_entry **active_cache;
extern unsigned int active_nr, active_alloc, active_cache_changed;
extern struct cache_tree *active_cache_tree;
extern int cache_errno;

#define GIT_DIR_ENVIRONMENT "GIT_DIR"
#define DEFAULT_GIT_DIR_ENVIRONMENT ".git"
#define DB_ENVIRONMENT "GIT_OBJECT_DIRECTORY"
#define INDEX_ENVIRONMENT "GIT_INDEX_FILE"
#define GRAFT_ENVIRONMENT "GIT_GRAFT_FILE"
#define TEMPLATE_DIR_ENVIRONMENT "GIT_TEMPLATE_DIR"
#define CONFIG_ENVIRONMENT "GIT_CONFIG"
#define CONFIG_LOCAL_ENVIRONMENT "GIT_CONFIG_LOCAL"
#define EXEC_PATH_ENVIRONMENT "GIT_EXEC_PATH"

extern int is_bare_git_dir(const char *dir);
extern const char *get_git_dir(void);
extern char *get_object_directory(void);
extern char *get_refs_directory(void);
extern char *get_index_file(void);
extern char *get_graft_file(void);

#define ALTERNATE_DB_ENVIRONMENT "GIT_ALTERNATE_OBJECT_DIRECTORIES"

extern const char **get_pathspec(const char *prefix, const char **pathspec);
extern const char *setup_git_directory_gently(int *);
extern const char *setup_git_directory(void);
extern const char *prefix_path(const char *prefix, int len, const char *path);
extern const char *prefix_filename(const char *prefix, int len, const char *path);
extern void verify_filename(const char *prefix, const char *name);
extern void verify_non_filename(const char *prefix, const char *name);

#define alloc_nr(x) (((x)+16)*3/2)

/* Initialize and use the cache information */
extern int read_cache(void);
extern int read_cache_from(const char *path);
extern int write_cache(int newfd, struct cache_entry **cache, int entries);
extern int discard_cache(void);
extern int verify_path(const char *path);
extern int cache_name_pos(const char *name, int namelen);
#define ADD_CACHE_OK_TO_ADD 1		/* Ok to add */
#define ADD_CACHE_OK_TO_REPLACE 2	/* Ok to replace file/directory */
#define ADD_CACHE_SKIP_DFCHECK 4	/* Ok to skip DF conflict checks */
extern int add_cache_entry(struct cache_entry *ce, int option);
extern struct cache_entry *refresh_cache_entry(struct cache_entry *ce, int really);
extern int remove_cache_entry_at(int pos);
extern int remove_file_from_cache(const char *path);
extern int add_file_to_index(const char *path, int verbose);
extern int ce_same_name(struct cache_entry *a, struct cache_entry *b);
extern int ce_match_stat(struct cache_entry *ce, struct stat *st, int);
extern int ce_modified(struct cache_entry *ce, struct stat *st, int);
extern int ce_path_match(const struct cache_entry *ce, const char **pathspec);
extern int index_fd(unsigned char *sha1, int fd, struct stat *st, int write_object, const char *type);
extern int read_pipe(int fd, char** return_buf, unsigned long* return_size);
extern int index_pipe(unsigned char *sha1, int fd, const char *type, int write_object);
extern int index_path(unsigned char *sha1, const char *path, struct stat *st, int write_object);
extern void fill_stat_cache_info(struct cache_entry *ce, struct stat *st);

#define REFRESH_REALLY		0x0001	/* ignore_valid */
#define REFRESH_UNMERGED	0x0002	/* allow unmerged */
#define REFRESH_QUIET		0x0004	/* be quiet about it */
#define REFRESH_IGNORE_MISSING	0x0008	/* ignore non-existent */
extern int refresh_cache(unsigned int flags);

struct lock_file {
	struct lock_file *next;
	char filename[PATH_MAX];
};
extern int hold_lock_file_for_update(struct lock_file *, const char *path, int);
extern int commit_lock_file(struct lock_file *);
extern void rollback_lock_file(struct lock_file *);
extern int delete_ref(const char *, unsigned char *sha1);

/* Environment bits from configuration mechanism */
extern int use_legacy_headers;
extern int trust_executable_bit;
extern int assume_unchanged;
extern int prefer_symlink_refs;
extern int log_all_ref_updates;
extern int warn_ambiguous_refs;
extern int shared_repository;
extern const char *apply_default_whitespace;
extern int zlib_compression_level;
extern size_t packed_git_window_size;
extern size_t packed_git_limit;

#define GIT_REPO_VERSION 0
extern int repository_format_version;
extern int check_repository_format(void);

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020
#define TYPE_CHANGED    0x0040

/* Return a statically allocated filename matching the sha1 signature */
extern char *mkpath(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern char *git_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern char *sha1_file_name(const unsigned char *sha1);
extern char *sha1_pack_name(const unsigned char *sha1);
extern char *sha1_pack_index_name(const unsigned char *sha1);
extern const char *find_unique_abbrev(const unsigned char *sha1, int);
extern const unsigned char null_sha1[20];
static inline int is_null_sha1(const unsigned char *sha1)
{
	return !memcmp(sha1, null_sha1, 20);
}
static inline int hashcmp(const unsigned char *sha1, const unsigned char *sha2)
{
	return memcmp(sha1, sha2, 20);
}
static inline void hashcpy(unsigned char *sha_dst, const unsigned char *sha_src)
{
	memcpy(sha_dst, sha_src, 20);
}
static inline void hashclr(unsigned char *hash)
{
	memset(hash, 0, 20);
}

int git_mkstemp(char *path, size_t n, const char *template);

enum sharedrepo {
	PERM_UMASK = 0,
	PERM_GROUP,
	PERM_EVERYBODY
};
int git_config_perm(const char *var, const char *value);
int adjust_shared_perm(const char *path);
int safe_create_leading_directories(char *path);
char *enter_repo(char *path, int strict);

/* Read and unpack a sha1 file into memory, write memory to a sha1 file */
extern int sha1_object_info(const unsigned char *, char *, unsigned long *);
extern void * unpack_sha1_file(void *map, unsigned long mapsize, char *type, unsigned long *size);
extern void * read_sha1_file(const unsigned char *sha1, char *type, unsigned long *size);
extern int hash_sha1_file(void *buf, unsigned long len, const char *type, unsigned char *sha1);
extern int write_sha1_file(void *buf, unsigned long len, const char *type, unsigned char *return_sha1);

extern int check_sha1_signature(const unsigned char *sha1, void *buf, unsigned long size, const char *type);

extern int write_sha1_from_fd(const unsigned char *sha1, int fd, char *buffer,
			      size_t bufsize, size_t *bufposn);
extern int write_sha1_to_fd(int fd, const unsigned char *sha1);
extern int move_temp_to_file(const char *tmpfile, const char *filename);

extern int has_sha1_pack(const unsigned char *sha1, const char **ignore);
extern int has_sha1_file(const unsigned char *sha1);
extern void *map_sha1_file(const unsigned char *sha1, unsigned long *);
extern int legacy_loose_object(unsigned char *);

extern int has_pack_file(const unsigned char *sha1);
extern int has_pack_index(const unsigned char *sha1);

enum object_type {
	OBJ_NONE = 0,
	OBJ_COMMIT = 1,
	OBJ_TREE = 2,
	OBJ_BLOB = 3,
	OBJ_TAG = 4,
	/* 5 for future expansion */
	OBJ_OFS_DELTA = 6,
	OBJ_REF_DELTA = 7,
	OBJ_BAD,
};

extern signed char hexval_table[256];
static inline unsigned int hexval(unsigned int c)
{
	return hexval_table[c];
}

/* Convert to/from hex/sha1 representation */
#define MINIMUM_ABBREV 4
#define DEFAULT_ABBREV 7

extern int get_sha1(const char *str, unsigned char *sha1);
extern int get_sha1_hex(const char *hex, unsigned char *sha1);
extern char *sha1_to_hex(const unsigned char *sha1);	/* static buffer result! */
extern int read_ref(const char *filename, unsigned char *sha1);
extern const char *resolve_ref(const char *path, unsigned char *sha1, int, int *);
extern int create_symref(const char *ref, const char *refs_heads_master);
extern int validate_symref(const char *ref);

extern int base_name_compare(const char *name1, int len1, int mode1, const char *name2, int len2, int mode2);
extern int cache_name_compare(const char *name1, int len1, const char *name2, int len2);

extern void *read_object_with_reference(const unsigned char *sha1,
					const char *required_type,
					unsigned long *size,
					unsigned char *sha1_ret);

const char *show_date(unsigned long time, int timezone, int relative);
const char *show_rfc2822_date(unsigned long time, int timezone);
int parse_date(const char *date, char *buf, int bufsize);
void datestamp(char *buf, int bufsize);
unsigned long approxidate(const char *);

extern int setup_ident(void);
extern void ignore_missing_committer_name();
extern const char *git_author_info(int);
extern const char *git_committer_info(int);

struct checkout {
	const char *base_dir;
	int base_dir_len;
	unsigned force:1,
		 quiet:1,
		 not_new:1,
		 refresh_cache:1;
};

extern int checkout_entry(struct cache_entry *ce, struct checkout *state, char *topath);

extern struct alternate_object_database {
	struct alternate_object_database *next;
	char *name;
	char base[FLEX_ARRAY]; /* more */
} *alt_odb_list;
extern void prepare_alt_odb(void);

struct pack_window {
	struct pack_window *next;
	unsigned char *base;
	off_t offset;
	size_t len;
	unsigned int last_used;
	unsigned int inuse_cnt;
};

extern struct packed_git {
	struct packed_git *next;
	struct pack_window *windows;
	unsigned int *index_base;
	off_t index_size;
	off_t pack_size;
	int pack_fd;
	int pack_local;
	unsigned char sha1[20];
	/* something like ".git/objects/pack/xxxxx.pack" */
	char pack_name[FLEX_ARRAY]; /* more */
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
	unsigned char force;
	struct ref *peer_ref; /* when renaming */
	char name[FLEX_ARRAY]; /* more */
};

#define REF_NORMAL	(1u << 0)
#define REF_HEADS	(1u << 1)
#define REF_TAGS	(1u << 2)

extern pid_t git_connect(int fd[2], char *url, const char *prog);
extern int finish_connect(pid_t pid);
extern int path_match(const char *path, int nr, char **match);
extern int match_refs(struct ref *src, struct ref *dst, struct ref ***dst_tail,
		      int nr_refspec, char **refspec, int all);
extern int get_ack(int fd, unsigned char *result_sha1);
extern struct ref **get_remote_heads(int in, struct ref **list, int nr_match, char **match, unsigned int flags);
extern int server_supports(const char *feature);

extern struct packed_git *parse_pack_index(unsigned char *sha1);
extern struct packed_git *parse_pack_index_file(const unsigned char *sha1,
						char *idx_path);

extern void prepare_packed_git(void);
extern void reprepare_packed_git(void);
extern void install_packed_git(struct packed_git *pack);

extern struct packed_git *find_sha1_pack(const unsigned char *sha1, 
					 struct packed_git *packs);

extern void pack_report();
extern unsigned char* use_pack(struct packed_git *, struct pack_window **, unsigned long, unsigned int *);
extern void unuse_pack(struct pack_window **);
extern struct packed_git *add_packed_git(char *, int, int);
extern int num_packed_objects(const struct packed_git *p);
extern int nth_packed_object_sha1(const struct packed_git *, int, unsigned char*);
extern unsigned long find_pack_entry_one(const unsigned char *, struct packed_git *);
extern void *unpack_entry(struct packed_git *, unsigned long, char *, unsigned long *);
extern unsigned long unpack_object_header_gently(const unsigned char *buf, unsigned long len, enum object_type *type, unsigned long *sizep);
extern void packed_object_info_detail(struct packed_git *, unsigned long, char *, unsigned long *, unsigned long *, unsigned int *, unsigned char *);

/* Dumb servers support */
extern int update_server_info(int);

typedef int (*config_fn_t)(const char *, const char *);
extern int git_default_config(const char *, const char *);
extern int git_config_from_file(config_fn_t fn, const char *);
extern int git_config(config_fn_t fn);
extern int git_config_int(const char *, const char *);
extern int git_config_bool(const char *, const char *);
extern int git_config_set(const char *, const char *);
extern int git_config_set_multivar(const char *, const char *, const char *, int);
extern int git_config_rename_section(const char *, const char *);
extern int check_repository_format_version(const char *var, const char *value);

#define MAX_GITNAME (1000)
extern char git_default_email[MAX_GITNAME];
extern char git_default_name[MAX_GITNAME];

extern char *git_commit_encoding;
extern char *git_log_output_encoding;

extern int copy_fd(int ifd, int ofd);
extern void read_or_die(int fd, void *buf, size_t count);
extern void write_or_die(int fd, const void *buf, size_t count);
extern int write_or_whine(int fd, const void *buf, size_t count, const char *msg);

/* pager.c */
extern void setup_pager(void);
extern int pager_in_use;
extern int pager_use_color;

/* base85 */
int decode_85(char *dst, char *line, int linelen);
void encode_85(char *buf, unsigned char *data, int bytes);

/* alloc.c */
struct blob;
struct tree;
struct commit;
struct tag;
extern struct blob *alloc_blob_node(void);
extern struct tree *alloc_tree_node(void);
extern struct commit *alloc_commit_node(void);
extern struct tag *alloc_tag_node(void);
extern void alloc_report(void);

/* trace.c */
extern int nfvasprintf(char **str, const char *fmt, va_list va);
extern void trace_printf(const char *format, ...);
extern void trace_argv_printf(const char **argv, int count, const char *format, ...);

#endif /* CACHE_H */
