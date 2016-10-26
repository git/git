#ifndef CACHE_H
#define CACHE_H

#include "git-compat-util.h"
#include "strbuf.h"
#include "hashmap.h"
#include "advice.h"
#include "gettext.h"
#include "convert.h"
#include "trace.h"
#include "string-list.h"
#include "pack-revindex.h"

#include SHA1_HEADER
#ifndef platform_SHA_CTX
/*
 * platform's underlying implementation of SHA-1; could be OpenSSL,
 * blk_SHA, Apple CommonCrypto, etc...  Note that including
 * SHA1_HEADER may have already defined platform_SHA_CTX for our
 * own implementations like block-sha1 and ppc-sha1, so we list
 * the default for OpenSSL compatible SHA-1 implementations here.
 */
#define platform_SHA_CTX	SHA_CTX
#define platform_SHA1_Init	SHA1_Init
#define platform_SHA1_Update	SHA1_Update
#define platform_SHA1_Final    	SHA1_Final
#endif

#define git_SHA_CTX		platform_SHA_CTX
#define git_SHA1_Init		platform_SHA1_Init
#define git_SHA1_Update		platform_SHA1_Update
#define git_SHA1_Final		platform_SHA1_Final

#ifdef SHA1_MAX_BLOCK_SIZE
#include "compat/sha1-chunked.h"
#undef git_SHA1_Update
#define git_SHA1_Update		git_SHA1_Update_Chunked
#endif

#include <zlib.h>
typedef struct git_zstream {
	z_stream z;
	unsigned long avail_in;
	unsigned long avail_out;
	unsigned long total_in;
	unsigned long total_out;
	unsigned char *next_in;
	unsigned char *next_out;
} git_zstream;

void git_inflate_init(git_zstream *);
void git_inflate_init_gzip_only(git_zstream *);
void git_inflate_end(git_zstream *);
int git_inflate(git_zstream *, int flush);

void git_deflate_init(git_zstream *, int level);
void git_deflate_init_gzip(git_zstream *, int level);
void git_deflate_init_raw(git_zstream *, int level);
void git_deflate_end(git_zstream *);
int git_deflate_abort(git_zstream *);
int git_deflate_end_gently(git_zstream *);
int git_deflate(git_zstream *, int flush);
unsigned long git_deflate_bound(git_zstream *, unsigned long);

/* The length in bytes and in hex digits of an object name (SHA-1 value). */
#define GIT_SHA1_RAWSZ 20
#define GIT_SHA1_HEXSZ (2 * GIT_SHA1_RAWSZ)

struct object_id {
	unsigned char hash[GIT_SHA1_RAWSZ];
};

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

/* unknown mode (impossible combination S_IFIFO|S_IFCHR) */
#define S_IFINVALID     0030000

/*
 * A "directory link" is a link to another git directory.
 *
 * The value 0160000 is not normally a valid mode, and
 * also just happens to be S_IFDIR + S_IFLNK
 */
#define S_IFGITLINK	0160000
#define S_ISGITLINK(m)	(((m) & S_IFMT) == S_IFGITLINK)

/*
 * Some mode bits are also used internally for computations.
 *
 * They *must* not overlap with any valid modes, and they *must* not be emitted
 * to outside world - i.e. appear on disk or network. In other words, it's just
 * temporary fields, which we internally use, but they have to stay in-house.
 *
 * ( such approach is valid, as standard S_IF* fits into 16 bits, and in Git
 *   codebase mode is `unsigned int` which is assumed to be at least 32 bits )
 */

/* used internally in tree-diff */
#define S_DIFFTREE_IFXMIN_NEQ	0x80000000


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
	uint32_t hdr_signature;
	uint32_t hdr_version;
	uint32_t hdr_entries;
};

#define INDEX_FORMAT_LB 2
#define INDEX_FORMAT_UB 4

/*
 * The "cache_time" is just the low 32 bits of the
 * time. It doesn't matter if it overflows - we only
 * check it for equality in the 32 bits we save.
 */
struct cache_time {
	uint32_t sec;
	uint32_t nsec;
};

struct stat_data {
	struct cache_time sd_ctime;
	struct cache_time sd_mtime;
	unsigned int sd_dev;
	unsigned int sd_ino;
	unsigned int sd_uid;
	unsigned int sd_gid;
	unsigned int sd_size;
};

struct cache_entry {
	struct hashmap_entry ent;
	struct stat_data ce_stat_data;
	unsigned int ce_mode;
	unsigned int ce_flags;
	unsigned int ce_namelen;
	unsigned int index;	/* for link extension */
	struct object_id oid;
	char name[FLEX_ARRAY]; /* more */
};

#define CE_STAGEMASK (0x3000)
#define CE_EXTENDED  (0x4000)
#define CE_VALID     (0x8000)
#define CE_STAGESHIFT 12

/*
 * Range 0xFFFF0FFF in ce_flags is divided into
 * two parts: in-memory flags and on-disk ones.
 * Flags in CE_EXTENDED_FLAGS will get saved on-disk
 * if you want to save a new flag, add it in
 * CE_EXTENDED_FLAGS
 *
 * In-memory only flags
 */
#define CE_UPDATE            (1 << 16)
#define CE_REMOVE            (1 << 17)
#define CE_UPTODATE          (1 << 18)
#define CE_ADDED             (1 << 19)

#define CE_HASHED            (1 << 20)
#define CE_WT_REMOVE         (1 << 22) /* remove in work directory */
#define CE_CONFLICTED        (1 << 23)

#define CE_UNPACKED          (1 << 24)
#define CE_NEW_SKIP_WORKTREE (1 << 25)

/* used to temporarily mark paths matched by pathspecs */
#define CE_MATCHED           (1 << 26)

#define CE_UPDATE_IN_BASE    (1 << 27)
#define CE_STRIP_NAME        (1 << 28)

/*
 * Extended on-disk flags
 */
#define CE_INTENT_TO_ADD     (1 << 29)
#define CE_SKIP_WORKTREE     (1 << 30)
/* CE_EXTENDED2 is for future extension */
#define CE_EXTENDED2         (1U << 31)

#define CE_EXTENDED_FLAGS (CE_INTENT_TO_ADD | CE_SKIP_WORKTREE)

/*
 * Safeguard to avoid saving wrong flags:
 *  - CE_EXTENDED2 won't get saved until its semantic is known
 *  - Bits in 0x0000FFFF have been saved in ce_flags already
 *  - Bits in 0x003F0000 are currently in-memory flags
 */
#if CE_EXTENDED_FLAGS & 0x803FFFFF
#error "CE_EXTENDED_FLAGS out of range"
#endif

/* Forward structure decls */
struct pathspec;
struct child_process;

/*
 * Copy the sha1 and stat state of a cache entry from one to
 * another. But we never change the name, or the hash state!
 */
static inline void copy_cache_entry(struct cache_entry *dst,
				    const struct cache_entry *src)
{
	unsigned int state = dst->ce_flags & CE_HASHED;

	/* Don't copy hash chain and name */
	memcpy(&dst->ce_stat_data, &src->ce_stat_data,
			offsetof(struct cache_entry, name) -
			offsetof(struct cache_entry, ce_stat_data));

	/* Restore the hash state */
	dst->ce_flags = (dst->ce_flags & ~CE_HASHED) | state;
}

static inline unsigned create_ce_flags(unsigned stage)
{
	return (stage << CE_STAGESHIFT);
}

#define ce_namelen(ce) ((ce)->ce_namelen)
#define ce_size(ce) cache_entry_size(ce_namelen(ce))
#define ce_stage(ce) ((CE_STAGEMASK & (ce)->ce_flags) >> CE_STAGESHIFT)
#define ce_uptodate(ce) ((ce)->ce_flags & CE_UPTODATE)
#define ce_skip_worktree(ce) ((ce)->ce_flags & CE_SKIP_WORKTREE)
#define ce_mark_uptodate(ce) ((ce)->ce_flags |= CE_UPTODATE)
#define ce_intent_to_add(ce) ((ce)->ce_flags & CE_INTENT_TO_ADD)

#define ce_permissions(mode) (((mode) & 0100) ? 0755 : 0644)
static inline unsigned int create_ce_mode(unsigned int mode)
{
	if (S_ISLNK(mode))
		return S_IFLNK;
	if (S_ISDIR(mode) || S_ISGITLINK(mode))
		return S_IFGITLINK;
	return S_IFREG | ce_permissions(mode);
}
static inline unsigned int ce_mode_from_stat(const struct cache_entry *ce,
					     unsigned int mode)
{
	extern int trust_executable_bit, has_symlinks;
	if (!has_symlinks && S_ISREG(mode) &&
	    ce && S_ISLNK(ce->ce_mode))
		return ce->ce_mode;
	if (!trust_executable_bit && S_ISREG(mode)) {
		if (ce && S_ISREG(ce->ce_mode))
			return ce->ce_mode;
		return create_ce_mode(0666);
	}
	return create_ce_mode(mode);
}
static inline int ce_to_dtype(const struct cache_entry *ce)
{
	unsigned ce_mode = ntohl(ce->ce_mode);
	if (S_ISREG(ce_mode))
		return DT_REG;
	else if (S_ISDIR(ce_mode) || S_ISGITLINK(ce_mode))
		return DT_DIR;
	else if (S_ISLNK(ce_mode))
		return DT_LNK;
	else
		return DT_UNKNOWN;
}
static inline unsigned int canon_mode(unsigned int mode)
{
	if (S_ISREG(mode))
		return S_IFREG | ce_permissions(mode);
	if (S_ISLNK(mode))
		return S_IFLNK;
	if (S_ISDIR(mode))
		return S_IFDIR;
	return S_IFGITLINK;
}

#define cache_entry_size(len) (offsetof(struct cache_entry,name) + (len) + 1)

#define SOMETHING_CHANGED	(1 << 0) /* unclassified changes go here */
#define CE_ENTRY_CHANGED	(1 << 1)
#define CE_ENTRY_REMOVED	(1 << 2)
#define CE_ENTRY_ADDED		(1 << 3)
#define RESOLVE_UNDO_CHANGED	(1 << 4)
#define CACHE_TREE_CHANGED	(1 << 5)
#define SPLIT_INDEX_ORDERED	(1 << 6)
#define UNTRACKED_CHANGED	(1 << 7)

struct split_index;
struct untracked_cache;

struct index_state {
	struct cache_entry **cache;
	unsigned int version;
	unsigned int cache_nr, cache_alloc, cache_changed;
	struct string_list *resolve_undo;
	struct cache_tree *cache_tree;
	struct split_index *split_index;
	struct cache_time timestamp;
	unsigned name_hash_initialized : 1,
		 initialized : 1;
	struct hashmap name_hash;
	struct hashmap dir_hash;
	unsigned char sha1[20];
	struct untracked_cache *untracked;
};

extern struct index_state the_index;

/* Name hashing */
extern void add_name_hash(struct index_state *istate, struct cache_entry *ce);
extern void remove_name_hash(struct index_state *istate, struct cache_entry *ce);
extern void free_name_hash(struct index_state *istate);


#ifndef NO_THE_INDEX_COMPATIBILITY_MACROS
#define active_cache (the_index.cache)
#define active_nr (the_index.cache_nr)
#define active_alloc (the_index.cache_alloc)
#define active_cache_changed (the_index.cache_changed)
#define active_cache_tree (the_index.cache_tree)

#define read_cache() read_index(&the_index)
#define read_cache_from(path) read_index_from(&the_index, (path))
#define read_cache_preload(pathspec) read_index_preload(&the_index, (pathspec))
#define is_cache_unborn() is_index_unborn(&the_index)
#define read_cache_unmerged() read_index_unmerged(&the_index)
#define discard_cache() discard_index(&the_index)
#define unmerged_cache() unmerged_index(&the_index)
#define cache_name_pos(name, namelen) index_name_pos(&the_index,(name),(namelen))
#define add_cache_entry(ce, option) add_index_entry(&the_index, (ce), (option))
#define rename_cache_entry_at(pos, new_name) rename_index_entry_at(&the_index, (pos), (new_name))
#define remove_cache_entry_at(pos) remove_index_entry_at(&the_index, (pos))
#define remove_file_from_cache(path) remove_file_from_index(&the_index, (path))
#define add_to_cache(path, st, flags) add_to_index(&the_index, (path), (st), (flags))
#define add_file_to_cache(path, flags) add_file_to_index(&the_index, (path), (flags))
#define chmod_cache_entry(ce, flip) chmod_index_entry(&the_index, (ce), (flip))
#define refresh_cache(flags) refresh_index(&the_index, (flags), NULL, NULL, NULL)
#define ce_match_stat(ce, st, options) ie_match_stat(&the_index, (ce), (st), (options))
#define ce_modified(ce, st, options) ie_modified(&the_index, (ce), (st), (options))
#define cache_dir_exists(name, namelen) index_dir_exists(&the_index, (name), (namelen))
#define cache_file_exists(name, namelen, igncase) index_file_exists(&the_index, (name), (namelen), (igncase))
#define cache_name_is_other(name, namelen) index_name_is_other(&the_index, (name), (namelen))
#define resolve_undo_clear() resolve_undo_clear_index(&the_index)
#define unmerge_cache_entry_at(at) unmerge_index_entry_at(&the_index, at)
#define unmerge_cache(pathspec) unmerge_index(&the_index, pathspec)
#define read_blob_data_from_cache(path, sz) read_blob_data_from_index(&the_index, (path), (sz))
#endif

enum object_type {
	OBJ_BAD = -1,
	OBJ_NONE = 0,
	OBJ_COMMIT = 1,
	OBJ_TREE = 2,
	OBJ_BLOB = 3,
	OBJ_TAG = 4,
	/* 5 for future expansion */
	OBJ_OFS_DELTA = 6,
	OBJ_REF_DELTA = 7,
	OBJ_ANY,
	OBJ_MAX
};

static inline enum object_type object_type(unsigned int mode)
{
	return S_ISDIR(mode) ? OBJ_TREE :
		S_ISGITLINK(mode) ? OBJ_COMMIT :
		OBJ_BLOB;
}

/* Double-check local_repo_env below if you add to this list. */
#define GIT_DIR_ENVIRONMENT "GIT_DIR"
#define GIT_COMMON_DIR_ENVIRONMENT "GIT_COMMON_DIR"
#define GIT_NAMESPACE_ENVIRONMENT "GIT_NAMESPACE"
#define GIT_WORK_TREE_ENVIRONMENT "GIT_WORK_TREE"
#define GIT_PREFIX_ENVIRONMENT "GIT_PREFIX"
#define GIT_SUPER_PREFIX_ENVIRONMENT "GIT_INTERNAL_SUPER_PREFIX"
#define DEFAULT_GIT_DIR_ENVIRONMENT ".git"
#define DB_ENVIRONMENT "GIT_OBJECT_DIRECTORY"
#define INDEX_ENVIRONMENT "GIT_INDEX_FILE"
#define GRAFT_ENVIRONMENT "GIT_GRAFT_FILE"
#define GIT_SHALLOW_FILE_ENVIRONMENT "GIT_SHALLOW_FILE"
#define TEMPLATE_DIR_ENVIRONMENT "GIT_TEMPLATE_DIR"
#define CONFIG_ENVIRONMENT "GIT_CONFIG"
#define CONFIG_DATA_ENVIRONMENT "GIT_CONFIG_PARAMETERS"
#define EXEC_PATH_ENVIRONMENT "GIT_EXEC_PATH"
#define CEILING_DIRECTORIES_ENVIRONMENT "GIT_CEILING_DIRECTORIES"
#define NO_REPLACE_OBJECTS_ENVIRONMENT "GIT_NO_REPLACE_OBJECTS"
#define GIT_REPLACE_REF_BASE_ENVIRONMENT "GIT_REPLACE_REF_BASE"
#define GITATTRIBUTES_FILE ".gitattributes"
#define INFOATTRIBUTES_FILE "info/attributes"
#define ATTRIBUTE_MACRO_PREFIX "[attr]"
#define GIT_NOTES_REF_ENVIRONMENT "GIT_NOTES_REF"
#define GIT_NOTES_DEFAULT_REF "refs/notes/commits"
#define GIT_NOTES_DISPLAY_REF_ENVIRONMENT "GIT_NOTES_DISPLAY_REF"
#define GIT_NOTES_REWRITE_REF_ENVIRONMENT "GIT_NOTES_REWRITE_REF"
#define GIT_NOTES_REWRITE_MODE_ENVIRONMENT "GIT_NOTES_REWRITE_MODE"
#define GIT_LITERAL_PATHSPECS_ENVIRONMENT "GIT_LITERAL_PATHSPECS"
#define GIT_GLOB_PATHSPECS_ENVIRONMENT "GIT_GLOB_PATHSPECS"
#define GIT_NOGLOB_PATHSPECS_ENVIRONMENT "GIT_NOGLOB_PATHSPECS"
#define GIT_ICASE_PATHSPECS_ENVIRONMENT "GIT_ICASE_PATHSPECS"
#define GIT_QUARANTINE_ENVIRONMENT "GIT_QUARANTINE_PATH"

/*
 * This environment variable is expected to contain a boolean indicating
 * whether we should or should not treat:
 *
 *   GIT_DIR=foo.git git ...
 *
 * as if GIT_WORK_TREE=. was given. It's not expected that users will make use
 * of this, but we use it internally to communicate to sub-processes that we
 * are in a bare repo. If not set, defaults to true.
 */
#define GIT_IMPLICIT_WORK_TREE_ENVIRONMENT "GIT_IMPLICIT_WORK_TREE"

/*
 * Repository-local GIT_* environment variables; these will be cleared
 * when git spawns a sub-process that runs inside another repository.
 * The array is NULL-terminated, which makes it easy to pass in the "env"
 * parameter of a run-command invocation, or to do a simple walk.
 */
extern const char * const local_repo_env[];

/*
 * Returns true iff we have a configured git repository (either via
 * setup_git_directory, or in the environment via $GIT_DIR).
 */
int have_git_dir(void);

extern int is_bare_repository_cfg;
extern int is_bare_repository(void);
extern int is_inside_git_dir(void);
extern char *git_work_tree_cfg;
extern int is_inside_work_tree(void);
extern const char *get_git_dir(void);
extern const char *get_git_common_dir(void);
extern char *get_object_directory(void);
extern char *get_index_file(void);
extern char *get_graft_file(void);
extern int set_git_dir(const char *path);
extern int get_common_dir_noenv(struct strbuf *sb, const char *gitdir);
extern int get_common_dir(struct strbuf *sb, const char *gitdir);
extern const char *get_git_namespace(void);
extern const char *strip_namespace(const char *namespaced_ref);
extern const char *get_super_prefix(void);
extern const char *get_git_work_tree(void);

/*
 * Return true if the given path is a git directory; note that this _just_
 * looks at the directory itself. If you want to know whether "foo/.git"
 * is a repository, you must feed that path, not just "foo".
 */
extern int is_git_directory(const char *path);

/*
 * Return 1 if the given path is the root of a git repository or
 * submodule, else 0. Will not return 1 for bare repositories with the
 * exception of creating a bare repository in "foo/.git" and calling
 * is_git_repository("foo").
 *
 * If we run into read errors, we err on the side of saying "yes, it is",
 * as we usually consider sub-repos precious, and would prefer to err on the
 * side of not disrupting or deleting them.
 */
extern int is_nonbare_repository_dir(struct strbuf *path);

#define READ_GITFILE_ERR_STAT_FAILED 1
#define READ_GITFILE_ERR_NOT_A_FILE 2
#define READ_GITFILE_ERR_OPEN_FAILED 3
#define READ_GITFILE_ERR_READ_FAILED 4
#define READ_GITFILE_ERR_INVALID_FORMAT 5
#define READ_GITFILE_ERR_NO_PATH 6
#define READ_GITFILE_ERR_NOT_A_REPO 7
#define READ_GITFILE_ERR_TOO_LARGE 8
extern const char *read_gitfile_gently(const char *path, int *return_error_code);
#define read_gitfile(path) read_gitfile_gently((path), NULL)
extern const char *resolve_gitdir(const char *suspect);
extern void set_git_work_tree(const char *tree);

#define ALTERNATE_DB_ENVIRONMENT "GIT_ALTERNATE_OBJECT_DIRECTORIES"

extern const char **get_pathspec(const char *prefix, const char **pathspec);
extern void setup_work_tree(void);
extern const char *setup_git_directory_gently(int *);
extern const char *setup_git_directory(void);
extern char *prefix_path(const char *prefix, int len, const char *path);
extern char *prefix_path_gently(const char *prefix, int len, int *remaining, const char *path);
extern const char *prefix_filename(const char *prefix, int len, const char *path);
extern int check_filename(const char *prefix, const char *name);
extern void verify_filename(const char *prefix,
			    const char *name,
			    int diagnose_misspelt_rev);
extern void verify_non_filename(const char *prefix, const char *name);
extern int path_inside_repo(const char *prefix, const char *path);

#define INIT_DB_QUIET 0x0001
#define INIT_DB_EXIST_OK 0x0002

extern int init_db(const char *git_dir, const char *real_git_dir,
		   const char *template_dir, unsigned int flags);

extern void sanitize_stdfds(void);
extern int daemonize(void);

#define alloc_nr(x) (((x)+16)*3/2)

/*
 * Realloc the buffer pointed at by variable 'x' so that it can hold
 * at least 'nr' entries; the number of entries currently allocated
 * is 'alloc', using the standard growing factor alloc_nr() macro.
 *
 * DO NOT USE any expression with side-effect for 'x', 'nr', or 'alloc'.
 */
#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			REALLOC_ARRAY(x, alloc); \
		} \
	} while (0)

/* Initialize and use the cache information */
struct lock_file;
extern int read_index(struct index_state *);
extern int read_index_preload(struct index_state *, const struct pathspec *pathspec);
extern int do_read_index(struct index_state *istate, const char *path,
			 int must_exist); /* for testting only! */
extern int read_index_from(struct index_state *, const char *path);
extern int is_index_unborn(struct index_state *);
extern int read_index_unmerged(struct index_state *);
#define COMMIT_LOCK		(1 << 0)
#define CLOSE_LOCK		(1 << 1)
extern int write_locked_index(struct index_state *, struct lock_file *lock, unsigned flags);
extern int discard_index(struct index_state *);
extern int unmerged_index(const struct index_state *);
extern int verify_path(const char *path);
extern int index_dir_exists(struct index_state *istate, const char *name, int namelen);
extern void adjust_dirname_case(struct index_state *istate, char *name);
extern struct cache_entry *index_file_exists(struct index_state *istate, const char *name, int namelen, int igncase);
extern int index_name_pos(const struct index_state *, const char *name, int namelen);
#define ADD_CACHE_OK_TO_ADD 1		/* Ok to add */
#define ADD_CACHE_OK_TO_REPLACE 2	/* Ok to replace file/directory */
#define ADD_CACHE_SKIP_DFCHECK 4	/* Ok to skip DF conflict checks */
#define ADD_CACHE_JUST_APPEND 8		/* Append only; tree.c::read_tree() */
#define ADD_CACHE_NEW_ONLY 16		/* Do not replace existing ones */
#define ADD_CACHE_KEEP_CACHE_TREE 32	/* Do not invalidate cache-tree */
extern int add_index_entry(struct index_state *, struct cache_entry *ce, int option);
extern void rename_index_entry_at(struct index_state *, int pos, const char *new_name);
extern int remove_index_entry_at(struct index_state *, int pos);
extern void remove_marked_cache_entries(struct index_state *istate);
extern int remove_file_from_index(struct index_state *, const char *path);
#define ADD_CACHE_VERBOSE 1
#define ADD_CACHE_PRETEND 2
#define ADD_CACHE_IGNORE_ERRORS	4
#define ADD_CACHE_IGNORE_REMOVAL 8
#define ADD_CACHE_INTENT 16
extern int add_to_index(struct index_state *, const char *path, struct stat *, int flags);
extern int add_file_to_index(struct index_state *, const char *path, int flags);
extern struct cache_entry *make_cache_entry(unsigned int mode, const unsigned char *sha1, const char *path, int stage, unsigned int refresh_options);
extern int chmod_index_entry(struct index_state *, struct cache_entry *ce, char flip);
extern int ce_same_name(const struct cache_entry *a, const struct cache_entry *b);
extern void set_object_name_for_intent_to_add_entry(struct cache_entry *ce);
extern int index_name_is_other(const struct index_state *, const char *, int);
extern void *read_blob_data_from_index(struct index_state *, const char *, unsigned long *);

/* do stat comparison even if CE_VALID is true */
#define CE_MATCH_IGNORE_VALID		01
/* do not check the contents but report dirty on racily-clean entries */
#define CE_MATCH_RACY_IS_DIRTY		02
/* do stat comparison even if CE_SKIP_WORKTREE is true */
#define CE_MATCH_IGNORE_SKIP_WORKTREE	04
/* ignore non-existent files during stat update  */
#define CE_MATCH_IGNORE_MISSING		0x08
/* enable stat refresh */
#define CE_MATCH_REFRESH		0x10
extern int ie_match_stat(const struct index_state *, const struct cache_entry *, struct stat *, unsigned int);
extern int ie_modified(const struct index_state *, const struct cache_entry *, struct stat *, unsigned int);

#define HASH_WRITE_OBJECT 1
#define HASH_FORMAT_CHECK 2
extern int index_fd(unsigned char *sha1, int fd, struct stat *st, enum object_type type, const char *path, unsigned flags);
extern int index_path(unsigned char *sha1, const char *path, struct stat *st, unsigned flags);

/*
 * Record to sd the data from st that we use to check whether a file
 * might have changed.
 */
extern void fill_stat_data(struct stat_data *sd, struct stat *st);

/*
 * Return 0 if st is consistent with a file not having been changed
 * since sd was filled.  If there are differences, return a
 * combination of MTIME_CHANGED, CTIME_CHANGED, OWNER_CHANGED,
 * INODE_CHANGED, and DATA_CHANGED.
 */
extern int match_stat_data(const struct stat_data *sd, struct stat *st);
extern int match_stat_data_racy(const struct index_state *istate,
				const struct stat_data *sd, struct stat *st);

extern void fill_stat_cache_info(struct cache_entry *ce, struct stat *st);

#define REFRESH_REALLY		0x0001	/* ignore_valid */
#define REFRESH_UNMERGED	0x0002	/* allow unmerged */
#define REFRESH_QUIET		0x0004	/* be quiet about it */
#define REFRESH_IGNORE_MISSING	0x0008	/* ignore non-existent */
#define REFRESH_IGNORE_SUBMODULES	0x0010	/* ignore submodules */
#define REFRESH_IN_PORCELAIN	0x0020	/* user friendly output, not "needs update" */
extern int refresh_index(struct index_state *, unsigned int flags, const struct pathspec *pathspec, char *seen, const char *header_msg);
extern struct cache_entry *refresh_cache_entry(struct cache_entry *, unsigned int);

extern void update_index_if_able(struct index_state *, struct lock_file *);

extern int hold_locked_index(struct lock_file *, int);
extern void set_alternate_index_output(const char *);

/* Environment bits from configuration mechanism */
extern int trust_executable_bit;
extern int trust_ctime;
extern int check_stat;
extern int quote_path_fully;
extern int has_symlinks;
extern int minimum_abbrev, default_abbrev;
extern int ignore_case;
extern int assume_unchanged;
extern int prefer_symlink_refs;
extern int log_all_ref_updates;
extern int warn_ambiguous_refs;
extern int warn_on_object_refname_ambiguity;
extern const char *apply_default_whitespace;
extern const char *apply_default_ignorewhitespace;
extern const char *git_attributes_file;
extern const char *git_hooks_path;
extern int zlib_compression_level;
extern int core_compression_level;
extern int core_compression_seen;
extern size_t packed_git_window_size;
extern size_t packed_git_limit;
extern size_t delta_base_cache_limit;
extern unsigned long big_file_threshold;
extern unsigned long pack_size_limit_cfg;

/*
 * Accessors for the core.sharedrepository config which lazy-load the value
 * from the config (if not already set). The "reset" function can be
 * used to unset "set" or cached value, meaning that the value will be loaded
 * fresh from the config file on the next call to get_shared_repository().
 */
void set_shared_repository(int value);
int get_shared_repository(void);
void reset_shared_repository(void);

/*
 * Do replace refs need to be checked this run?  This variable is
 * initialized to true unless --no-replace-object is used or
 * $GIT_NO_REPLACE_OBJECTS is set, but is set to false by some
 * commands that do not want replace references to be active.  As an
 * optimization it is also set to false if replace references have
 * been sought but there were none.
 */
extern int check_replace_refs;
extern char *git_replace_ref_base;

extern int fsync_object_files;
extern int core_preload_index;
extern int core_apply_sparse_checkout;
extern int precomposed_unicode;
extern int protect_hfs;
extern int protect_ntfs;
extern int git_db_env, git_index_env, git_graft_env, git_common_dir_env;

/*
 * Include broken refs in all ref iterations, which will
 * generally choke dangerous operations rather than letting
 * them silently proceed without taking the broken ref into
 * account.
 */
extern int ref_paranoia;

/*
 * The character that begins a commented line in user-editable file
 * that is subject to stripspace.
 */
extern char comment_line_char;
extern int auto_comment_line_char;

/* Windows only */
enum hide_dotfiles_type {
	HIDE_DOTFILES_FALSE = 0,
	HIDE_DOTFILES_TRUE,
	HIDE_DOTFILES_DOTGITONLY
};
extern enum hide_dotfiles_type hide_dotfiles;

enum branch_track {
	BRANCH_TRACK_UNSPECIFIED = -1,
	BRANCH_TRACK_NEVER = 0,
	BRANCH_TRACK_REMOTE,
	BRANCH_TRACK_ALWAYS,
	BRANCH_TRACK_EXPLICIT,
	BRANCH_TRACK_OVERRIDE
};

enum rebase_setup_type {
	AUTOREBASE_NEVER = 0,
	AUTOREBASE_LOCAL,
	AUTOREBASE_REMOTE,
	AUTOREBASE_ALWAYS
};

enum push_default_type {
	PUSH_DEFAULT_NOTHING = 0,
	PUSH_DEFAULT_MATCHING,
	PUSH_DEFAULT_SIMPLE,
	PUSH_DEFAULT_UPSTREAM,
	PUSH_DEFAULT_CURRENT,
	PUSH_DEFAULT_UNSPECIFIED
};

extern enum branch_track git_branch_track;
extern enum rebase_setup_type autorebase;
extern enum push_default_type push_default;

enum object_creation_mode {
	OBJECT_CREATION_USES_HARDLINKS = 0,
	OBJECT_CREATION_USES_RENAMES = 1
};

extern enum object_creation_mode object_creation_mode;

extern char *notes_ref_name;

extern int grafts_replace_parents;

/*
 * GIT_REPO_VERSION is the version we write by default. The
 * _READ variant is the highest number we know how to
 * handle.
 */
#define GIT_REPO_VERSION 0
#define GIT_REPO_VERSION_READ 1
extern int repository_format_precious_objects;

struct repository_format {
	int version;
	int precious_objects;
	int is_bare;
	char *work_tree;
	struct string_list unknown_extensions;
};

/*
 * Read the repository format characteristics from the config file "path" into
 * "format" struct. Returns the numeric version. On error, -1 is returned,
 * format->version is set to -1, and all other fields in the struct are
 * undefined.
 */
int read_repository_format(struct repository_format *format, const char *path);

/*
 * Verify that the repository described by repository_format is something we
 * can read. If it is, return 0. Otherwise, return -1, and "err" will describe
 * any errors encountered.
 */
int verify_repository_format(const struct repository_format *format,
			     struct strbuf *err);

/*
 * Check the repository format version in the path found in get_git_dir(),
 * and die if it is a version we don't understand. Generally one would
 * set_git_dir() before calling this, and use it only for "are we in a valid
 * repo?".
 */
extern void check_repository_format(void);

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020
#define TYPE_CHANGED    0x0040

/*
 * Return a statically allocated filename, either generically (mkpath), in
 * the repository directory (git_path), or in a submodule's repository
 * directory (git_path_submodule). In all cases, note that the result
 * may be overwritten by another call to _any_ of the functions. Consider
 * using the safer "dup" or "strbuf" formats below (in some cases, the
 * unsafe versions have already been removed).
 */
extern const char *mkpath(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern const char *git_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern const char *git_common_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));

extern char *mksnpath(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
extern void strbuf_git_path(struct strbuf *sb, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
extern void strbuf_git_common_path(struct strbuf *sb, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
extern char *git_path_buf(struct strbuf *buf, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
extern int strbuf_git_path_submodule(struct strbuf *sb, const char *path,
				     const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
extern char *git_pathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));
extern char *mkpathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));
extern char *git_pathdup_submodule(const char *path, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

extern void report_linked_checkout_garbage(void);

/*
 * You can define a static memoized git path like:
 *
 *    static GIT_PATH_FUNC(git_path_foo, "FOO");
 *
 * or use one of the global ones below.
 */
#define GIT_PATH_FUNC(func, filename) \
	const char *func(void) \
	{ \
		static char *ret; \
		if (!ret) \
			ret = git_pathdup(filename); \
		return ret; \
	}

const char *git_path_cherry_pick_head(void);
const char *git_path_revert_head(void);
const char *git_path_squash_msg(void);
const char *git_path_merge_msg(void);
const char *git_path_merge_rr(void);
const char *git_path_merge_mode(void);
const char *git_path_merge_head(void);
const char *git_path_fetch_head(void);
const char *git_path_shallow(void);

/*
 * Return the name of the file in the local object database that would
 * be used to store a loose object with the specified sha1.  The
 * return value is a pointer to a statically allocated buffer that is
 * overwritten each time the function is called.
 */
extern const char *sha1_file_name(const unsigned char *sha1);

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

/*
 * Return an abbreviated sha1 unique within this repository's object database.
 * The result will be at least `len` characters long, and will be NUL
 * terminated.
 *
 * The non-`_r` version returns a static buffer which remains valid until 4
 * more calls to find_unique_abbrev are made.
 *
 * The `_r` variant writes to a buffer supplied by the caller, which must be at
 * least `GIT_SHA1_HEXSZ + 1` bytes. The return value is the number of bytes
 * written (excluding the NUL terminator).
 *
 * Note that while this version avoids the static buffer, it is not fully
 * reentrant, as it calls into other non-reentrant git code.
 */
extern const char *find_unique_abbrev(const unsigned char *sha1, int len);
extern int find_unique_abbrev_r(char *hex, const unsigned char *sha1, int len);

extern const unsigned char null_sha1[GIT_SHA1_RAWSZ];
extern const struct object_id null_oid;

static inline int hashcmp(const unsigned char *sha1, const unsigned char *sha2)
{
	int i;

	for (i = 0; i < GIT_SHA1_RAWSZ; i++, sha1++, sha2++) {
		if (*sha1 != *sha2)
			return *sha1 - *sha2;
	}

	return 0;
}

static inline int oidcmp(const struct object_id *oid1, const struct object_id *oid2)
{
	return hashcmp(oid1->hash, oid2->hash);
}

static inline int is_null_sha1(const unsigned char *sha1)
{
	return !hashcmp(sha1, null_sha1);
}

static inline int is_null_oid(const struct object_id *oid)
{
	return !hashcmp(oid->hash, null_sha1);
}

static inline void hashcpy(unsigned char *sha_dst, const unsigned char *sha_src)
{
	memcpy(sha_dst, sha_src, GIT_SHA1_RAWSZ);
}

static inline void oidcpy(struct object_id *dst, const struct object_id *src)
{
	hashcpy(dst->hash, src->hash);
}

static inline void hashclr(unsigned char *hash)
{
	memset(hash, 0, GIT_SHA1_RAWSZ);
}

static inline void oidclr(struct object_id *oid)
{
	hashclr(oid->hash);
}


#define EMPTY_TREE_SHA1_HEX \
	"4b825dc642cb6eb9a060e54bf8d69288fbee4904"
#define EMPTY_TREE_SHA1_BIN_LITERAL \
	 "\x4b\x82\x5d\xc6\x42\xcb\x6e\xb9\xa0\x60" \
	 "\xe5\x4b\xf8\xd6\x92\x88\xfb\xee\x49\x04"
extern const struct object_id empty_tree_oid;
#define EMPTY_TREE_SHA1_BIN (empty_tree_oid.hash)

#define EMPTY_BLOB_SHA1_HEX \
	"e69de29bb2d1d6434b8b29ae775ad8c2e48c5391"
#define EMPTY_BLOB_SHA1_BIN_LITERAL \
	"\xe6\x9d\xe2\x9b\xb2\xd1\xd6\x43\x4b\x8b" \
	"\x29\xae\x77\x5a\xd8\xc2\xe4\x8c\x53\x91"
extern const struct object_id empty_blob_oid;
#define EMPTY_BLOB_SHA1_BIN (empty_blob_oid.hash)


static inline int is_empty_blob_sha1(const unsigned char *sha1)
{
	return !hashcmp(sha1, EMPTY_BLOB_SHA1_BIN);
}

static inline int is_empty_blob_oid(const struct object_id *oid)
{
	return !hashcmp(oid->hash, EMPTY_BLOB_SHA1_BIN);
}

static inline int is_empty_tree_sha1(const unsigned char *sha1)
{
	return !hashcmp(sha1, EMPTY_TREE_SHA1_BIN);
}

static inline int is_empty_tree_oid(const struct object_id *oid)
{
	return !hashcmp(oid->hash, EMPTY_TREE_SHA1_BIN);
}


int git_mkstemp(char *path, size_t n, const char *template);

/* set default permissions by passing mode arguments to open(2) */
int git_mkstemps_mode(char *pattern, int suffix_len, int mode);
int git_mkstemp_mode(char *pattern, int mode);

/*
 * NOTE NOTE NOTE!!
 *
 * PERM_UMASK, OLD_PERM_GROUP and OLD_PERM_EVERYBODY enumerations must
 * not be changed. Old repositories have core.sharedrepository written in
 * numeric format, and therefore these values are preserved for compatibility
 * reasons.
 */
enum sharedrepo {
	PERM_UMASK          = 0,
	OLD_PERM_GROUP      = 1,
	OLD_PERM_EVERYBODY  = 2,
	PERM_GROUP          = 0660,
	PERM_EVERYBODY      = 0664
};
int git_config_perm(const char *var, const char *value);
int adjust_shared_perm(const char *path);

/*
 * Create the directory containing the named path, using care to be
 * somewhat safe against races.  Return one of the scld_error values
 * to indicate success/failure.
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
 * temporarily.
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

int mkdir_in_gitdir(const char *path);
extern char *expand_user_path(const char *path);
const char *enter_repo(const char *path, int strict);
static inline int is_absolute_path(const char *path)
{
	return is_dir_sep(path[0]) || has_dos_drive_prefix(path);
}
int is_directory(const char *);
const char *real_path(const char *path);
const char *real_path_if_valid(const char *path);
const char *absolute_path(const char *path);
const char *remove_leading_path(const char *in, const char *prefix);
const char *relative_path(const char *in, const char *prefix, struct strbuf *sb);
int normalize_path_copy_len(char *dst, const char *src, int *prefix_len);
int normalize_path_copy(char *dst, const char *src);
int longest_ancestor_length(const char *path, struct string_list *prefixes);
char *strip_path_suffix(const char *path, const char *suffix);
int daemon_avoid_alias(const char *path);
extern int is_ntfs_dotgit(const char *name);

/**
 * Return a newly allocated string with the evaluation of
 * "$XDG_CONFIG_HOME/git/$filename" if $XDG_CONFIG_HOME is non-empty, otherwise
 * "$HOME/.config/git/$filename". Return NULL upon error.
 */
extern char *xdg_config_home(const char *filename);

/* object replacement */
#define LOOKUP_REPLACE_OBJECT 1
#define LOOKUP_UNKNOWN_OBJECT 2
extern void *read_sha1_file_extended(const unsigned char *sha1, enum object_type *type, unsigned long *size, unsigned flag);
static inline void *read_sha1_file(const unsigned char *sha1, enum object_type *type, unsigned long *size)
{
	return read_sha1_file_extended(sha1, type, size, LOOKUP_REPLACE_OBJECT);
}

/*
 * This internal function is only declared here for the benefit of
 * lookup_replace_object().  Please do not call it directly.
 */
extern const unsigned char *do_lookup_replace_object(const unsigned char *sha1);

/*
 * If object sha1 should be replaced, return the replacement object's
 * name (replaced recursively, if necessary).  The return value is
 * either sha1 or a pointer to a permanently-allocated value.  When
 * object replacement is suppressed, always return sha1.
 */
static inline const unsigned char *lookup_replace_object(const unsigned char *sha1)
{
	if (!check_replace_refs)
		return sha1;
	return do_lookup_replace_object(sha1);
}

static inline const unsigned char *lookup_replace_object_extended(const unsigned char *sha1, unsigned flag)
{
	if (!(flag & LOOKUP_REPLACE_OBJECT))
		return sha1;
	return lookup_replace_object(sha1);
}

/* Read and unpack a sha1 file into memory, write memory to a sha1 file */
extern int sha1_object_info(const unsigned char *, unsigned long *);
extern int hash_sha1_file(const void *buf, unsigned long len, const char *type, unsigned char *sha1);
extern int write_sha1_file(const void *buf, unsigned long len, const char *type, unsigned char *return_sha1);
extern int hash_sha1_file_literally(const void *buf, unsigned long len, const char *type, unsigned char *sha1, unsigned flags);
extern int pretend_sha1_file(void *, unsigned long, enum object_type, unsigned char *);
extern int force_object_loose(const unsigned char *sha1, time_t mtime);
extern int git_open(const char *name);
extern void *map_sha1_file(const unsigned char *sha1, unsigned long *size);
extern int unpack_sha1_header(git_zstream *stream, unsigned char *map, unsigned long mapsize, void *buffer, unsigned long bufsiz);
extern int parse_sha1_header(const char *hdr, unsigned long *sizep);

/* global flag to enable extra checks when accessing packed objects */
extern int do_check_packed_object_crc;

extern int check_sha1_signature(const unsigned char *sha1, void *buf, unsigned long size, const char *type);

extern int finalize_object_file(const char *tmpfile, const char *filename);

extern int has_sha1_pack(const unsigned char *sha1);

/*
 * Return true iff we have an object named sha1, whether local or in
 * an alternate object database, and whether packed or loose.  This
 * function does not respect replace references.
 *
 * If the QUICK flag is set, do not re-check the pack directory
 * when we cannot find the object (this means we may give a false
 * negative answer if another process is simultaneously repacking).
 */
#define HAS_SHA1_QUICK 0x1
extern int has_sha1_file_with_flags(const unsigned char *sha1, int flags);
static inline int has_sha1_file(const unsigned char *sha1)
{
	return has_sha1_file_with_flags(sha1, 0);
}

/* Same as the above, except for struct object_id. */
extern int has_object_file(const struct object_id *oid);
extern int has_object_file_with_flags(const struct object_id *oid, int flags);

/*
 * Return true iff an alternate object database has a loose object
 * with the specified name.  This function does not respect replace
 * references.
 */
extern int has_loose_object_nonlocal(const unsigned char *sha1);

extern int has_pack_index(const unsigned char *sha1);

extern void assert_sha1_type(const unsigned char *sha1, enum object_type expect);

extern const signed char hexval_table[256];
static inline unsigned int hexval(unsigned char c)
{
	return hexval_table[c];
}

/*
 * Convert two consecutive hexadecimal digits into a char.  Return a
 * negative value on error.  Don't run over the end of short strings.
 */
static inline int hex2chr(const char *s)
{
	int val = hexval(s[0]);
	return (val < 0) ? val : (val << 4) | hexval(s[1]);
}

/* Convert to/from hex/sha1 representation */
#define MINIMUM_ABBREV minimum_abbrev
#define DEFAULT_ABBREV default_abbrev

/* used when the code does not know or care what the default abbrev is */
#define FALLBACK_DEFAULT_ABBREV 7

struct object_context {
	unsigned char tree[20];
	char path[PATH_MAX];
	unsigned mode;
	/*
	 * symlink_path is only used by get_tree_entry_follow_symlinks,
	 * and only for symlinks that point outside the repository.
	 */
	struct strbuf symlink_path;
};

#define GET_SHA1_QUIETLY           01
#define GET_SHA1_COMMIT            02
#define GET_SHA1_COMMITTISH        04
#define GET_SHA1_TREE             010
#define GET_SHA1_TREEISH          020
#define GET_SHA1_BLOB             040
#define GET_SHA1_FOLLOW_SYMLINKS 0100
#define GET_SHA1_ONLY_TO_DIE    04000

#define GET_SHA1_DISAMBIGUATORS \
	(GET_SHA1_COMMIT | GET_SHA1_COMMITTISH | \
	GET_SHA1_TREE | GET_SHA1_TREEISH | \
	GET_SHA1_BLOB)

extern int get_sha1(const char *str, unsigned char *sha1);
extern int get_sha1_commit(const char *str, unsigned char *sha1);
extern int get_sha1_committish(const char *str, unsigned char *sha1);
extern int get_sha1_tree(const char *str, unsigned char *sha1);
extern int get_sha1_treeish(const char *str, unsigned char *sha1);
extern int get_sha1_blob(const char *str, unsigned char *sha1);
extern void maybe_die_on_misspelt_object_name(const char *name, const char *prefix);
extern int get_sha1_with_context(const char *str, unsigned flags, unsigned char *sha1, struct object_context *orc);

extern int get_oid(const char *str, struct object_id *oid);

typedef int each_abbrev_fn(const unsigned char *sha1, void *);
extern int for_each_abbrev(const char *prefix, each_abbrev_fn, void *);

extern int set_disambiguate_hint_config(const char *var, const char *value);

/*
 * Try to read a SHA1 in hexadecimal format from the 40 characters
 * starting at hex.  Write the 20-byte result to sha1 in binary form.
 * Return 0 on success.  Reading stops if a NUL is encountered in the
 * input, so it is safe to pass this function an arbitrary
 * null-terminated string.
 */
extern int get_sha1_hex(const char *hex, unsigned char *sha1);
extern int get_oid_hex(const char *hex, struct object_id *sha1);

/*
 * Convert a binary sha1 to its hex equivalent. The `_r` variant is reentrant,
 * and writes the NUL-terminated output to the buffer `out`, which must be at
 * least `GIT_SHA1_HEXSZ + 1` bytes, and returns a pointer to out for
 * convenience.
 *
 * The non-`_r` variant returns a static buffer, but uses a ring of 4
 * buffers, making it safe to make multiple calls for a single statement, like:
 *
 *   printf("%s -> %s", sha1_to_hex(one), sha1_to_hex(two));
 */
extern char *sha1_to_hex_r(char *out, const unsigned char *sha1);
extern char *oid_to_hex_r(char *out, const struct object_id *oid);
extern char *sha1_to_hex(const unsigned char *sha1);	/* static buffer result! */
extern char *oid_to_hex(const struct object_id *oid);	/* same static buffer as sha1_to_hex */

extern int interpret_branch_name(const char *str, int len, struct strbuf *);
extern int get_oid_mb(const char *str, struct object_id *oid);

extern int validate_headref(const char *ref);

extern int base_name_compare(const char *name1, int len1, int mode1, const char *name2, int len2, int mode2);
extern int df_name_compare(const char *name1, int len1, int mode1, const char *name2, int len2, int mode2);
extern int name_compare(const char *name1, size_t len1, const char *name2, size_t len2);
extern int cache_name_stage_compare(const char *name1, int len1, int stage1, const char *name2, int len2, int stage2);

extern void *read_object_with_reference(const unsigned char *sha1,
					const char *required_type,
					unsigned long *size,
					unsigned char *sha1_ret);

extern struct object *peel_to_type(const char *name, int namelen,
				   struct object *o, enum object_type);

struct date_mode {
	enum date_mode_type {
		DATE_NORMAL = 0,
		DATE_RELATIVE,
		DATE_SHORT,
		DATE_ISO8601,
		DATE_ISO8601_STRICT,
		DATE_RFC2822,
		DATE_STRFTIME,
		DATE_RAW,
		DATE_UNIX
	} type;
	const char *strftime_fmt;
	int local;
};

/*
 * Convenience helper for passing a constant type, like:
 *
 *   show_date(t, tz, DATE_MODE(NORMAL));
 */
#define DATE_MODE(t) date_mode_from_type(DATE_##t)
struct date_mode *date_mode_from_type(enum date_mode_type type);

const char *show_date(unsigned long time, int timezone, const struct date_mode *mode);
void show_date_relative(unsigned long time, int tz, const struct timeval *now,
			struct strbuf *timebuf);
int parse_date(const char *date, struct strbuf *out);
int parse_date_basic(const char *date, unsigned long *timestamp, int *offset);
int parse_expiry_date(const char *date, unsigned long *timestamp);
void datestamp(struct strbuf *out);
#define approxidate(s) approxidate_careful((s), NULL)
unsigned long approxidate_careful(const char *, int *);
unsigned long approxidate_relative(const char *date, const struct timeval *now);
void parse_date_format(const char *format, struct date_mode *mode);
int date_overflows(unsigned long date);

#define IDENT_STRICT	       1
#define IDENT_NO_DATE	       2
#define IDENT_NO_NAME	       4
extern const char *git_author_info(int);
extern const char *git_committer_info(int);
extern const char *fmt_ident(const char *name, const char *email, const char *date_str, int);
extern const char *fmt_name(const char *name, const char *email);
extern const char *ident_default_name(void);
extern const char *ident_default_email(void);
extern const char *git_editor(void);
extern const char *git_pager(int stdout_is_tty);
extern int git_ident_config(const char *, const char *, void *);
extern void reset_ident_date(void);

struct ident_split {
	const char *name_begin;
	const char *name_end;
	const char *mail_begin;
	const char *mail_end;
	const char *date_begin;
	const char *date_end;
	const char *tz_begin;
	const char *tz_end;
};
/*
 * Signals an success with 0, but time part of the result may be NULL
 * if the input lacks timestamp and zone
 */
extern int split_ident_line(struct ident_split *, const char *, int);

/*
 * Like show_date, but pull the timestamp and tz parameters from
 * the ident_split. It will also sanity-check the values and produce
 * a well-known sentinel date if they appear bogus.
 */
const char *show_ident_date(const struct ident_split *id,
			    const struct date_mode *mode);

/*
 * Compare split idents for equality or strict ordering. Note that we
 * compare only the ident part of the line, ignoring any timestamp.
 *
 * Because there are two fields, we must choose one as the primary key; we
 * currently arbitrarily pick the email.
 */
extern int ident_cmp(const struct ident_split *, const struct ident_split *);

struct checkout {
	struct index_state *istate;
	const char *base_dir;
	int base_dir_len;
	unsigned force:1,
		 quiet:1,
		 not_new:1,
		 refresh_cache:1;
};
#define CHECKOUT_INIT { NULL, "" }

#define TEMPORARY_FILENAME_LENGTH 25
extern int checkout_entry(struct cache_entry *ce, const struct checkout *state, char *topath);

struct cache_def {
	struct strbuf path;
	int flags;
	int track_flags;
	int prefix_len_stat_func;
};
#define CACHE_DEF_INIT { STRBUF_INIT, 0, 0, 0 }
static inline void cache_def_clear(struct cache_def *cache)
{
	strbuf_release(&cache->path);
}

extern int has_symlink_leading_path(const char *name, int len);
extern int threaded_has_symlink_leading_path(struct cache_def *, const char *, int);
extern int check_leading_path(const char *name, int len);
extern int has_dirs_only_path(const char *name, int len, int prefix_len);
extern void schedule_dir_for_removal(const char *name, int len);
extern void remove_scheduled_dirs(void);

extern struct alternate_object_database {
	struct alternate_object_database *next;

	/* see alt_scratch_buf() */
	struct strbuf scratch;
	size_t base_len;

	char path[FLEX_ARRAY];
} *alt_odb_list;
extern void prepare_alt_odb(void);
extern void read_info_alternates(const char * relative_base, int depth);
extern char *compute_alternate_path(const char *path, struct strbuf *err);
typedef int alt_odb_fn(struct alternate_object_database *, void *);
extern int foreach_alt_odb(alt_odb_fn, void*);

/*
 * Allocate a "struct alternate_object_database" but do _not_ actually
 * add it to the list of alternates.
 */
struct alternate_object_database *alloc_alt_odb(const char *dir);

/*
 * Add the directory to the on-disk alternates file; the new entry will also
 * take effect in the current process.
 */
extern void add_to_alternates_file(const char *dir);

/*
 * Add the directory to the in-memory list of alternates (along with any
 * recursive alternates it points to), but do not modify the on-disk alternates
 * file.
 */
extern void add_to_alternates_memory(const char *dir);

/*
 * Returns a scratch strbuf pre-filled with the alternate object directory,
 * including a trailing slash, which can be used to access paths in the
 * alternate. Always use this over direct access to alt->scratch, as it
 * cleans up any previous use of the scratch buffer.
 */
extern struct strbuf *alt_scratch_buf(struct alternate_object_database *alt);

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
	off_t pack_size;
	const void *index_data;
	size_t index_size;
	uint32_t num_objects;
	uint32_t num_bad_objects;
	unsigned char *bad_object_sha1;
	int index_version;
	time_t mtime;
	int pack_fd;
	unsigned pack_local:1,
		 pack_keep:1,
		 freshened:1,
		 do_not_close:1;
	unsigned char sha1[20];
	struct revindex_entry *revindex;
	/* something like ".git/objects/pack/xxxxx.pack" */
	char pack_name[FLEX_ARRAY]; /* more */
} *packed_git;

/*
 * A most-recently-used ordered version of the packed_git list, which can
 * be iterated instead of packed_git (and marked via mru_mark).
 */
struct mru;
extern struct mru *packed_git_mru;

struct pack_entry {
	off_t offset;
	unsigned char sha1[20];
	struct packed_git *p;
};

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

extern unsigned char *use_pack(struct packed_git *, struct pack_window **, off_t, unsigned long *);
extern void close_pack_windows(struct packed_git *);
extern void close_all_packs(void);
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
 * Return the SHA-1 of the nth object within the specified packfile.
 * Open the index if it is not already open.  The return value points
 * at the SHA-1 within the mmapped index.  Return NULL if there is an
 * error.
 */
extern const unsigned char *nth_packed_object_sha1(struct packed_git *, uint32_t n);

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
extern void *unpack_entry(struct packed_git *, off_t, enum object_type *, unsigned long *);
extern unsigned long unpack_object_header_buffer(const unsigned char *buf, unsigned long len, enum object_type *type, unsigned long *sizep);
extern unsigned long get_size_from_delta(struct packed_git *, struct pack_window **, off_t);
extern int unpack_object_header(struct packed_git *, struct pack_window **, off_t *, unsigned long *);

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
typedef int each_loose_object_fn(const unsigned char *sha1,
				 const char *path,
				 void *data);
typedef int each_loose_cruft_fn(const char *basename,
				const char *path,
				void *data);
typedef int each_loose_subdir_fn(int nr,
				 const char *path,
				 void *data);
int for_each_loose_file_in_objdir(const char *path,
				  each_loose_object_fn obj_cb,
				  each_loose_cruft_fn cruft_cb,
				  each_loose_subdir_fn subdir_cb,
				  void *data);
int for_each_loose_file_in_objdir_buf(struct strbuf *path,
				      each_loose_object_fn obj_cb,
				      each_loose_cruft_fn cruft_cb,
				      each_loose_subdir_fn subdir_cb,
				      void *data);

/*
 * Iterate over loose and packed objects in both the local
 * repository and any alternates repositories (unless the
 * LOCAL_ONLY flag is set).
 */
#define FOR_EACH_OBJECT_LOCAL_ONLY 0x1
typedef int each_packed_object_fn(const unsigned char *sha1,
				  struct packed_git *pack,
				  uint32_t pos,
				  void *data);
extern int for_each_loose_object(each_loose_object_fn, void *, unsigned flags);
extern int for_each_packed_object(each_packed_object_fn, void *, unsigned flags);

struct object_info {
	/* Request */
	enum object_type *typep;
	unsigned long *sizep;
	off_t *disk_sizep;
	unsigned char *delta_base_sha1;
	struct strbuf *typename;

	/* Response */
	enum {
		OI_CACHED,
		OI_LOOSE,
		OI_PACKED,
		OI_DBCACHED
	} whence;
	union {
		/*
		 * struct {
		 * 	... Nothing to expose in this case
		 * } cached;
		 * struct {
		 * 	... Nothing to expose in this case
		 * } loose;
		 */
		struct {
			struct packed_git *pack;
			off_t offset;
			unsigned int is_delta;
		} packed;
	} u;
};

/*
 * Initializer for a "struct object_info" that wants no items. You may
 * also memset() the memory to all-zeroes.
 */
#define OBJECT_INFO_INIT {NULL}

extern int sha1_object_info_extended(const unsigned char *, struct object_info *, unsigned flags);
extern int packed_object_info(struct packed_git *pack, off_t offset, struct object_info *);

/* Dumb servers support */
extern int update_server_info(int);

/* git_config_parse_key() returns these negated: */
#define CONFIG_INVALID_KEY 1
#define CONFIG_NO_SECTION_OR_NAME 2
/* git_config_set_gently(), git_config_set_multivar_gently() return the above or these: */
#define CONFIG_NO_LOCK -1
#define CONFIG_INVALID_FILE 3
#define CONFIG_NO_WRITE 4
#define CONFIG_NOTHING_SET 5
#define CONFIG_INVALID_PATTERN 6
#define CONFIG_GENERIC_ERROR 7

#define CONFIG_REGEX_NONE ((void *)1)

struct git_config_source {
	unsigned int use_stdin:1;
	const char *file;
	const char *blob;
};

enum config_origin_type {
	CONFIG_ORIGIN_BLOB,
	CONFIG_ORIGIN_FILE,
	CONFIG_ORIGIN_STDIN,
	CONFIG_ORIGIN_SUBMODULE_BLOB,
	CONFIG_ORIGIN_CMDLINE
};

typedef int (*config_fn_t)(const char *, const char *, void *);
extern int git_default_config(const char *, const char *, void *);
extern int git_config_from_file(config_fn_t fn, const char *, void *);
extern int git_config_from_mem(config_fn_t fn, const enum config_origin_type,
					const char *name, const char *buf, size_t len, void *data);
extern void git_config_push_parameter(const char *text);
extern int git_config_from_parameters(config_fn_t fn, void *data);
extern void git_config(config_fn_t fn, void *);
extern int git_config_with_options(config_fn_t fn, void *,
				   struct git_config_source *config_source,
				   int respect_includes);
extern int git_parse_ulong(const char *, unsigned long *);
extern int git_parse_maybe_bool(const char *);
extern int git_config_int(const char *, const char *);
extern int64_t git_config_int64(const char *, const char *);
extern unsigned long git_config_ulong(const char *, const char *);
extern int git_config_bool_or_int(const char *, const char *, int *);
extern int git_config_bool(const char *, const char *);
extern int git_config_maybe_bool(const char *, const char *);
extern int git_config_string(const char **, const char *, const char *);
extern int git_config_pathname(const char **, const char *, const char *);
extern int git_config_set_in_file_gently(const char *, const char *, const char *);
extern void git_config_set_in_file(const char *, const char *, const char *);
extern int git_config_set_gently(const char *, const char *);
extern void git_config_set(const char *, const char *);
extern int git_config_parse_key(const char *, char **, int *);
extern int git_config_key_is_valid(const char *key);
extern int git_config_set_multivar_gently(const char *, const char *, const char *, int);
extern void git_config_set_multivar(const char *, const char *, const char *, int);
extern int git_config_set_multivar_in_file_gently(const char *, const char *, const char *, const char *, int);
extern void git_config_set_multivar_in_file(const char *, const char *, const char *, const char *, int);
extern int git_config_rename_section(const char *, const char *);
extern int git_config_rename_section_in_file(const char *, const char *, const char *);
extern const char *git_etc_gitconfig(void);
extern int git_env_bool(const char *, int);
extern unsigned long git_env_ulong(const char *, unsigned long);
extern int git_config_system(void);
extern int config_error_nonbool(const char *);
#if defined(__GNUC__)
#define config_error_nonbool(s) (config_error_nonbool(s), const_error())
#endif
extern const char *get_log_output_encoding(void);
extern const char *get_commit_output_encoding(void);

extern int git_config_parse_parameter(const char *, config_fn_t fn, void *data);

enum config_scope {
	CONFIG_SCOPE_UNKNOWN = 0,
	CONFIG_SCOPE_SYSTEM,
	CONFIG_SCOPE_GLOBAL,
	CONFIG_SCOPE_REPO,
	CONFIG_SCOPE_CMDLINE,
};

extern enum config_scope current_config_scope(void);
extern const char *current_config_origin_type(void);
extern const char *current_config_name(void);

struct config_include_data {
	int depth;
	config_fn_t fn;
	void *data;
};
#define CONFIG_INCLUDE_INIT { 0 }
extern int git_config_include(const char *name, const char *value, void *data);

/*
 * Match and parse a config key of the form:
 *
 *   section.(subsection.)?key
 *
 * (i.e., what gets handed to a config_fn_t). The caller provides the section;
 * we return -1 if it does not match, 0 otherwise. The subsection and key
 * out-parameters are filled by the function (and subsection is NULL if it is
 * missing).
 */
extern int parse_config_key(const char *var,
			    const char *section,
			    const char **subsection, int *subsection_len,
			    const char **key);

struct config_set_element {
	struct hashmap_entry ent;
	char *key;
	struct string_list value_list;
};

struct configset_list_item {
	struct config_set_element *e;
	int value_index;
};

/*
 * the contents of the list are ordered according to their
 * position in the config files and order of parsing the files.
 * (i.e. key-value pair at the last position of .git/config will
 * be at the last item of the list)
 */
struct configset_list {
	struct configset_list_item *items;
	unsigned int nr, alloc;
};

struct config_set {
	struct hashmap config_hash;
	int hash_initialized;
	struct configset_list list;
};

extern void git_configset_init(struct config_set *cs);
extern int git_configset_add_file(struct config_set *cs, const char *filename);
extern int git_configset_get_value(struct config_set *cs, const char *key, const char **value);
extern const struct string_list *git_configset_get_value_multi(struct config_set *cs, const char *key);
extern void git_configset_clear(struct config_set *cs);
extern int git_configset_get_string_const(struct config_set *cs, const char *key, const char **dest);
extern int git_configset_get_string(struct config_set *cs, const char *key, char **dest);
extern int git_configset_get_int(struct config_set *cs, const char *key, int *dest);
extern int git_configset_get_ulong(struct config_set *cs, const char *key, unsigned long *dest);
extern int git_configset_get_bool(struct config_set *cs, const char *key, int *dest);
extern int git_configset_get_bool_or_int(struct config_set *cs, const char *key, int *is_bool, int *dest);
extern int git_configset_get_maybe_bool(struct config_set *cs, const char *key, int *dest);
extern int git_configset_get_pathname(struct config_set *cs, const char *key, const char **dest);

extern int git_config_get_value(const char *key, const char **value);
extern const struct string_list *git_config_get_value_multi(const char *key);
extern void git_config_clear(void);
extern void git_config_iter(config_fn_t fn, void *data);
extern int git_config_get_string_const(const char *key, const char **dest);
extern int git_config_get_string(const char *key, char **dest);
extern int git_config_get_int(const char *key, int *dest);
extern int git_config_get_ulong(const char *key, unsigned long *dest);
extern int git_config_get_bool(const char *key, int *dest);
extern int git_config_get_bool_or_int(const char *key, int *is_bool, int *dest);
extern int git_config_get_maybe_bool(const char *key, int *dest);
extern int git_config_get_pathname(const char *key, const char **dest);
extern int git_config_get_untracked_cache(void);

/*
 * This is a hack for test programs like test-dump-untracked-cache to
 * ensure that they do not modify the untracked cache when reading it.
 * Do not use it otherwise!
 */
extern int ignore_untracked_cache_config;

struct key_value_info {
	const char *filename;
	int linenr;
	enum config_origin_type origin_type;
	enum config_scope scope;
};

extern NORETURN void git_die_config(const char *key, const char *err, ...) __attribute__((format(printf, 2, 3)));
extern NORETURN void git_die_config_linenr(const char *key, const char *filename, int linenr);

extern int committer_ident_sufficiently_given(void);
extern int author_ident_sufficiently_given(void);

extern const char *git_commit_encoding;
extern const char *git_log_output_encoding;
extern const char *git_mailmap_file;
extern const char *git_mailmap_blob;

/* IO helper functions */
extern void maybe_flush_or_die(FILE *, const char *);
__attribute__((format (printf, 2, 3)))
extern void fprintf_or_die(FILE *, const char *fmt, ...);

#define COPY_READ_ERROR (-2)
#define COPY_WRITE_ERROR (-3)
extern int copy_fd(int ifd, int ofd);
extern int copy_file(const char *dst, const char *src, int mode);
extern int copy_file_with_time(const char *dst, const char *src, int mode);

extern void write_or_die(int fd, const void *buf, size_t count);
extern void fsync_or_die(int fd, const char *);

extern ssize_t read_in_full(int fd, void *buf, size_t count);
extern ssize_t write_in_full(int fd, const void *buf, size_t count);
extern ssize_t pread_in_full(int fd, void *buf, size_t count, off_t offset);

static inline ssize_t write_str_in_full(int fd, const char *str)
{
	return write_in_full(fd, str, strlen(str));
}

/**
 * Open (and truncate) the file at path, write the contents of buf to it,
 * and close it. Dies if any errors are encountered.
 */
extern void write_file_buf(const char *path, const char *buf, size_t len);

/**
 * Like write_file_buf(), but format the contents into a buffer first.
 * Additionally, write_file() will append a newline if one is not already
 * present, making it convenient to write text files:
 *
 *   write_file(path, "counter: %d", ctr);
 */
__attribute__((format (printf, 2, 3)))
extern void write_file(const char *path, const char *fmt, ...);

/* pager.c */
extern void setup_pager(void);
extern int pager_in_use(void);
extern int pager_use_color;
extern int term_columns(void);
extern int decimal_width(uintmax_t);
extern int check_pager_config(const char *cmd);
extern void prepare_pager_args(struct child_process *, const char *pager);

extern const char *editor_program;
extern const char *askpass_program;
extern const char *excludes_file;

/* base85 */
int decode_85(char *dst, const char *line, int linelen);
void encode_85(char *buf, const unsigned char *data, int bytes);

/* alloc.c */
extern void *alloc_blob_node(void);
extern void *alloc_tree_node(void);
extern void *alloc_commit_node(void);
extern void *alloc_tag_node(void);
extern void *alloc_object_node(void);
extern void alloc_report(void);
extern unsigned int alloc_commit_index(void);

/* pkt-line.c */
void packet_trace_identity(const char *prog);

/* add */
/*
 * return 0 if success, 1 - if addition of a file failed and
 * ADD_FILES_IGNORE_ERRORS was specified in flags
 */
int add_files_to_cache(const char *prefix, const struct pathspec *pathspec, int flags);

/* diff.c */
extern int diff_auto_refresh_index;

/* match-trees.c */
void shift_tree(const struct object_id *, const struct object_id *, struct object_id *, int);
void shift_tree_by(const struct object_id *, const struct object_id *, struct object_id *, const char *);

/*
 * whitespace rules.
 * used by both diff and apply
 * last two digits are tab width
 */
#define WS_BLANK_AT_EOL         0100
#define WS_SPACE_BEFORE_TAB     0200
#define WS_INDENT_WITH_NON_TAB  0400
#define WS_CR_AT_EOL           01000
#define WS_BLANK_AT_EOF        02000
#define WS_TAB_IN_INDENT       04000
#define WS_TRAILING_SPACE      (WS_BLANK_AT_EOL|WS_BLANK_AT_EOF)
#define WS_DEFAULT_RULE (WS_TRAILING_SPACE|WS_SPACE_BEFORE_TAB|8)
#define WS_TAB_WIDTH_MASK        077
extern unsigned whitespace_rule_cfg;
extern unsigned whitespace_rule(const char *);
extern unsigned parse_whitespace_rule(const char *);
extern unsigned ws_check(const char *line, int len, unsigned ws_rule);
extern void ws_check_emit(const char *line, int len, unsigned ws_rule, FILE *stream, const char *set, const char *reset, const char *ws);
extern char *whitespace_error_string(unsigned ws);
extern void ws_fix_copy(struct strbuf *, const char *, int, unsigned, int *);
extern int ws_blank_line(const char *line, int len, unsigned ws_rule);
#define ws_tab_width(rule)     ((rule) & WS_TAB_WIDTH_MASK)

/* ls-files */
void overlay_tree_on_cache(const char *tree_name, const char *prefix);

char *alias_lookup(const char *alias);
int split_cmdline(char *cmdline, const char ***argv);
/* Takes a negative value returned by split_cmdline */
const char *split_cmdline_strerror(int cmdline_errno);

/* setup.c */
struct startup_info {
	int have_repository;
	const char *prefix;
};
extern struct startup_info *startup_info;

/* merge.c */
struct commit_list;
int try_merge_command(const char *strategy, size_t xopts_nr,
		const char **xopts, struct commit_list *common,
		const char *head_arg, struct commit_list *remotes);
int checkout_fast_forward(const unsigned char *from,
			  const unsigned char *to,
			  int overwrite_ignore);


int sane_execvp(const char *file, char *const argv[]);

/*
 * A struct to encapsulate the concept of whether a file has changed
 * since we last checked it. This uses criteria similar to those used
 * for the index.
 */
struct stat_validity {
	struct stat_data *sd;
};

void stat_validity_clear(struct stat_validity *sv);

/*
 * Returns 1 if the path is a regular file (or a symlink to a regular
 * file) and matches the saved stat_validity, 0 otherwise.  A missing
 * or inaccessible file is considered a match if the struct was just
 * initialized, or if the previous update found an inaccessible file.
 */
int stat_validity_check(struct stat_validity *sv, const char *path);

/*
 * Update the stat_validity from a file opened at descriptor fd. If
 * the file is missing, inaccessible, or not a regular file, then
 * future calls to stat_validity_check will match iff one of those
 * conditions continues to be true.
 */
void stat_validity_update(struct stat_validity *sv, int fd);

int versioncmp(const char *s1, const char *s2);
void sleep_millisec(int millisec);

/*
 * Create a directory and (if share is nonzero) adjust its permissions
 * according to the shared_repository setting. Only use this for
 * directories under $GIT_DIR.  Don't use it for working tree
 * directories.
 */
void safe_create_dir(const char *dir, int share);

#endif /* CACHE_H */
