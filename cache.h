#ifndef CACHE_H
#define CACHE_H

#include "git-compat-util.h"
#include "strbuf.h"
#include "hashmap.h"
#include "list.h"
#include "advice.h"
#include "gettext.h"
#include "convert.h"
#include "trace.h"
#include "trace2.h"
#include "string-list.h"
#include "pack-revindex.h"
#include "hash.h"
#include "path.h"
#include "oid-array.h"
#include "repository.h"
#include "mem-pool.h"

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
	unsigned int mem_pool_allocated;
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
#define CE_FSMONITOR_VALID   (1 << 21)
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

#define S_ISSPARSEDIR(m) ((m) == S_IFDIR)

/* Forward structure decls */
struct pathspec;
struct child_process;
struct tree;

/*
 * Copy the sha1 and stat state of a cache entry from one to
 * another. But we never change the name, or the hash state!
 */
static inline void copy_cache_entry(struct cache_entry *dst,
				    const struct cache_entry *src)
{
	unsigned int state = dst->ce_flags & CE_HASHED;
	int mem_pool_allocated = dst->mem_pool_allocated;

	/* Don't copy hash chain and name */
	memcpy(&dst->ce_stat_data, &src->ce_stat_data,
			offsetof(struct cache_entry, name) -
			offsetof(struct cache_entry, ce_stat_data));

	/* Restore the hash state */
	dst->ce_flags = (dst->ce_flags & ~CE_HASHED) | state;

	/* Restore the mem_pool_allocated flag */
	dst->mem_pool_allocated = mem_pool_allocated;
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
	if (S_ISSPARSEDIR(mode))
		return S_IFDIR;
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
#define FSMONITOR_CHANGED	(1 << 8)

struct split_index;
struct untracked_cache;
struct progress;
struct pattern_list;

struct index_state {
	struct cache_entry **cache;
	unsigned int version;
	unsigned int cache_nr, cache_alloc, cache_changed;
	struct string_list *resolve_undo;
	struct cache_tree *cache_tree;
	struct split_index *split_index;
	struct cache_time timestamp;
	unsigned name_hash_initialized : 1,
		 initialized : 1,
		 drop_cache_tree : 1,
		 updated_workdir : 1,
		 updated_skipworktree : 1,
		 fsmonitor_has_run_once : 1,

		 /*
		  * sparse_index == 1 when sparse-directory
		  * entries exist. Requires sparse-checkout
		  * in cone mode.
		  */
		 sparse_index : 1;
	struct hashmap name_hash;
	struct hashmap dir_hash;
	struct object_id oid;
	struct untracked_cache *untracked;
	char *fsmonitor_last_update;
	struct ewah_bitmap *fsmonitor_dirty;
	struct mem_pool *ce_mem_pool;
	struct progress *progress;
	struct repository *repo;
	struct pattern_list *sparse_checkout_patterns;
};

/* Name hashing */
int test_lazy_init_name_hash(struct index_state *istate, int try_threaded);
void add_name_hash(struct index_state *istate, struct cache_entry *ce);
void remove_name_hash(struct index_state *istate, struct cache_entry *ce);
void free_name_hash(struct index_state *istate);

/* Cache entry creation and cleanup */

/*
 * Create cache_entry intended for use in the specified index. Caller
 * is responsible for discarding the cache_entry with
 * `discard_cache_entry`.
 */
struct cache_entry *make_cache_entry(struct index_state *istate,
				     unsigned int mode,
				     const struct object_id *oid,
				     const char *path,
				     int stage,
				     unsigned int refresh_options);

struct cache_entry *make_empty_cache_entry(struct index_state *istate,
					   size_t name_len);

/*
 * Create a cache_entry that is not intended to be added to an index. If
 * `ce_mem_pool` is not NULL, the entry is allocated within the given memory
 * pool. Caller is responsible for discarding "loose" entries with
 * `discard_cache_entry()` and the memory pool with
 * `mem_pool_discard(ce_mem_pool, should_validate_cache_entries())`.
 */
struct cache_entry *make_transient_cache_entry(unsigned int mode,
					       const struct object_id *oid,
					       const char *path,
					       int stage,
					       struct mem_pool *ce_mem_pool);

struct cache_entry *make_empty_transient_cache_entry(size_t len,
						     struct mem_pool *ce_mem_pool);

/*
 * Discard cache entry.
 */
void discard_cache_entry(struct cache_entry *ce);

/*
 * Check configuration if we should perform extra validation on cache
 * entries.
 */
int should_validate_cache_entries(void);

/*
 * Duplicate a cache_entry. Allocate memory for the new entry from a
 * memory_pool. Takes into account cache_entry fields that are meant
 * for managing the underlying memory allocation of the cache_entry.
 */
struct cache_entry *dup_cache_entry(const struct cache_entry *ce, struct index_state *istate);

/*
 * Validate the cache entries in the index.  This is an internal
 * consistency check that the cache_entry structs are allocated from
 * the expected memory pool.
 */
void validate_cache_entries(const struct index_state *istate);

/*
 * Bulk prefetch all missing cache entries that are not GITLINKs and that match
 * the given predicate. This function should only be called if
 * has_promisor_remote() returns true.
 */
typedef int (*must_prefetch_predicate)(const struct cache_entry *);
void prefetch_cache_entries(const struct index_state *istate,
			    must_prefetch_predicate must_prefetch);

#ifdef USE_THE_INDEX_COMPATIBILITY_MACROS
extern struct index_state the_index;

#define active_cache (the_index.cache)
#define active_nr (the_index.cache_nr)
#define active_alloc (the_index.cache_alloc)
#define active_cache_changed (the_index.cache_changed)
#define active_cache_tree (the_index.cache_tree)

#define read_cache() repo_read_index(the_repository)
#define read_cache_from(path) read_index_from(&the_index, (path), (get_git_dir()))
#define read_cache_preload(pathspec) repo_read_index_preload(the_repository, (pathspec), 0)
#define is_cache_unborn() is_index_unborn(&the_index)
#define read_cache_unmerged() repo_read_index_unmerged(the_repository)
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
#define refresh_and_write_cache(refresh_flags, write_flags, gentle) repo_refresh_and_write_index(the_repository, (refresh_flags), (write_flags), (gentle), NULL, NULL, NULL)
#define ce_match_stat(ce, st, options) ie_match_stat(&the_index, (ce), (st), (options))
#define ce_modified(ce, st, options) ie_modified(&the_index, (ce), (st), (options))
#define cache_dir_exists(name, namelen) index_dir_exists(&the_index, (name), (namelen))
#define cache_file_exists(name, namelen, igncase) index_file_exists(&the_index, (name), (namelen), (igncase))
#define cache_name_is_other(name, namelen) index_name_is_other(&the_index, (name), (namelen))
#define resolve_undo_clear() resolve_undo_clear_index(&the_index)
#define unmerge_cache_entry_at(at) unmerge_index_entry_at(&the_index, at)
#define unmerge_cache(pathspec) unmerge_index(&the_index, pathspec)
#define read_blob_data_from_cache(path, sz) read_blob_data_from_index(&the_index, (path), (sz))
#define hold_locked_index(lock_file, flags) repo_hold_locked_index(the_repository, (lock_file), (flags))
#endif

#define TYPE_BITS 3

/*
 * Values in this enum (except those outside the 3 bit range) are part
 * of pack file format. See Documentation/technical/pack-format.txt
 * for more information.
 */
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
#define CONFIG_COUNT_ENVIRONMENT "GIT_CONFIG_COUNT"
#define EXEC_PATH_ENVIRONMENT "GIT_EXEC_PATH"
#define CEILING_DIRECTORIES_ENVIRONMENT "GIT_CEILING_DIRECTORIES"
#define NO_REPLACE_OBJECTS_ENVIRONMENT "GIT_NO_REPLACE_OBJECTS"
#define GIT_REPLACE_REF_BASE_ENVIRONMENT "GIT_REPLACE_REF_BASE"
#define GITATTRIBUTES_FILE ".gitattributes"
#define INFOATTRIBUTES_FILE "info/attributes"
#define ATTRIBUTE_MACRO_PREFIX "[attr]"
#define GITMODULES_FILE ".gitmodules"
#define GITMODULES_INDEX ":.gitmodules"
#define GITMODULES_HEAD "HEAD:.gitmodules"
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
#define GIT_OPTIONAL_LOCKS_ENVIRONMENT "GIT_OPTIONAL_LOCKS"
#define GIT_TEXT_DOMAIN_DIR_ENVIRONMENT "GIT_TEXTDOMAINDIR"

/*
 * Environment variable used in handshaking the wire protocol.
 * Contains a colon ':' separated list of keys with optional values
 * 'key[=value]'.  Presence of unknown keys and values must be
 * ignored.
 */
#define GIT_PROTOCOL_ENVIRONMENT "GIT_PROTOCOL"
/* HTTP header used to handshake the wire protocol */
#define GIT_PROTOCOL_HEADER "Git-Protocol"

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

void setup_git_env(const char *git_dir);

/*
 * Returns true iff we have a configured git repository (either via
 * setup_git_directory, or in the environment via $GIT_DIR).
 */
int have_git_dir(void);

extern int is_bare_repository_cfg;
int is_bare_repository(void);
int is_inside_git_dir(void);
extern char *git_work_tree_cfg;
int is_inside_work_tree(void);
const char *get_git_dir(void);
const char *get_git_common_dir(void);
char *get_object_directory(void);
char *get_index_file(void);
char *get_graft_file(struct repository *r);
void set_git_dir(const char *path, int make_realpath);
int get_common_dir_noenv(struct strbuf *sb, const char *gitdir);
int get_common_dir(struct strbuf *sb, const char *gitdir);
const char *get_git_namespace(void);
const char *strip_namespace(const char *namespaced_ref);
const char *get_super_prefix(void);
const char *get_git_work_tree(void);

/*
 * Return true if the given path is a git directory; note that this _just_
 * looks at the directory itself. If you want to know whether "foo/.git"
 * is a repository, you must feed that path, not just "foo".
 */
int is_git_directory(const char *path);

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
int is_nonbare_repository_dir(struct strbuf *path);

#define READ_GITFILE_ERR_STAT_FAILED 1
#define READ_GITFILE_ERR_NOT_A_FILE 2
#define READ_GITFILE_ERR_OPEN_FAILED 3
#define READ_GITFILE_ERR_READ_FAILED 4
#define READ_GITFILE_ERR_INVALID_FORMAT 5
#define READ_GITFILE_ERR_NO_PATH 6
#define READ_GITFILE_ERR_NOT_A_REPO 7
#define READ_GITFILE_ERR_TOO_LARGE 8
void read_gitfile_error_die(int error_code, const char *path, const char *dir);
const char *read_gitfile_gently(const char *path, int *return_error_code);
#define read_gitfile(path) read_gitfile_gently((path), NULL)
const char *resolve_gitdir_gently(const char *suspect, int *return_error_code);
#define resolve_gitdir(path) resolve_gitdir_gently((path), NULL)

void set_git_work_tree(const char *tree);

#define ALTERNATE_DB_ENVIRONMENT "GIT_ALTERNATE_OBJECT_DIRECTORIES"

void setup_work_tree(void);
/*
 * Find the commondir and gitdir of the repository that contains the current
 * working directory, without changing the working directory or other global
 * state. The result is appended to commondir and gitdir.  If the discovered
 * gitdir does not correspond to a worktree, then 'commondir' and 'gitdir' will
 * both have the same result appended to the buffer.  The return value is
 * either 0 upon success and non-zero if no repository was found.
 */
int discover_git_directory(struct strbuf *commondir,
			   struct strbuf *gitdir);
const char *setup_git_directory_gently(int *);
const char *setup_git_directory(void);
char *prefix_path(const char *prefix, int len, const char *path);
char *prefix_path_gently(const char *prefix, int len, int *remaining, const char *path);

/*
 * Concatenate "prefix" (if len is non-zero) and "path", with no
 * connecting characters (so "prefix" should end with a "/").
 * Unlike prefix_path, this should be used if the named file does
 * not have to interact with index entry; i.e. name of a random file
 * on the filesystem.
 *
 * The return value is always a newly allocated string (even if the
 * prefix was empty).
 */
char *prefix_filename(const char *prefix, const char *path);

int check_filename(const char *prefix, const char *name);
void verify_filename(const char *prefix,
		     const char *name,
		     int diagnose_misspelt_rev);
void verify_non_filename(const char *prefix, const char *name);
int path_inside_repo(const char *prefix, const char *path);

#define INIT_DB_QUIET 0x0001
#define INIT_DB_EXIST_OK 0x0002

int init_db(const char *git_dir, const char *real_git_dir,
	    const char *template_dir, int hash_algo,
	    const char *initial_branch, unsigned int flags);
void initialize_repository_version(int hash_algo, int reinit);

void sanitize_stdfds(void);
int daemonize(void);

#define alloc_nr(x) (((x)+16)*3/2)

/**
 * Dynamically growing an array using realloc() is error prone and boring.
 *
 * Define your array with:
 *
 * - a pointer (`item`) that points at the array, initialized to `NULL`
 *   (although please name the variable based on its contents, not on its
 *   type);
 *
 * - an integer variable (`alloc`) that keeps track of how big the current
 *   allocation is, initialized to `0`;
 *
 * - another integer variable (`nr`) to keep track of how many elements the
 *   array currently has, initialized to `0`.
 *
 * Then before adding `n`th element to the item, call `ALLOC_GROW(item, n,
 * alloc)`.  This ensures that the array can hold at least `n` elements by
 * calling `realloc(3)` and adjusting `alloc` variable.
 *
 * ------------
 * sometype *item;
 * size_t nr;
 * size_t alloc
 *
 * for (i = 0; i < nr; i++)
 * 	if (we like item[i] already)
 * 		return;
 *
 * // we did not like any existing one, so add one
 * ALLOC_GROW(item, nr + 1, alloc);
 * item[nr++] = value you like;
 * ------------
 *
 * You are responsible for updating the `nr` variable.
 *
 * If you need to specify the number of elements to allocate explicitly
 * then use the macro `REALLOC_ARRAY(item, alloc)` instead of `ALLOC_GROW`.
 *
 * Consider using ALLOC_GROW_BY instead of ALLOC_GROW as it has some
 * added niceties.
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

/*
 * Similar to ALLOC_GROW but handles updating of the nr value and
 * zeroing the bytes of the newly-grown array elements.
 *
 * DO NOT USE any expression with side-effect for any of the
 * arguments.
 */
#define ALLOC_GROW_BY(x, nr, increase, alloc) \
	do { \
		if (increase) { \
			size_t new_nr = nr + (increase); \
			if (new_nr < nr) \
				BUG("negative growth in ALLOC_GROW_BY"); \
			ALLOC_GROW(x, new_nr, alloc); \
			memset((x) + nr, 0, sizeof(*(x)) * (increase)); \
			nr = new_nr; \
		} \
	} while (0)

/* Initialize and use the cache information */
struct lock_file;
void preload_index(struct index_state *index,
		   const struct pathspec *pathspec,
		   unsigned int refresh_flags);
int do_read_index(struct index_state *istate, const char *path,
		  int must_exist); /* for testting only! */
int read_index_from(struct index_state *, const char *path,
		    const char *gitdir);
int is_index_unborn(struct index_state *);

void ensure_full_index(struct index_state *istate);

/* For use with `write_locked_index()`. */
#define COMMIT_LOCK		(1 << 0)
#define SKIP_IF_UNCHANGED	(1 << 1)

/*
 * Write the index while holding an already-taken lock. Close the lock,
 * and if `COMMIT_LOCK` is given, commit it.
 *
 * Unless a split index is in use, write the index into the lockfile.
 *
 * With a split index, write the shared index to a temporary file,
 * adjust its permissions and rename it into place, then write the
 * split index to the lockfile. If the temporary file for the shared
 * index cannot be created, fall back to the behavior described in
 * the previous paragraph.
 *
 * With `COMMIT_LOCK`, the lock is always committed or rolled back.
 * Without it, the lock is closed, but neither committed nor rolled
 * back.
 *
 * If `SKIP_IF_UNCHANGED` is given and the index is unchanged, nothing
 * is written (and the lock is rolled back if `COMMIT_LOCK` is given).
 */
int write_locked_index(struct index_state *, struct lock_file *lock, unsigned flags);

int discard_index(struct index_state *);
void move_index_extensions(struct index_state *dst, struct index_state *src);
int unmerged_index(const struct index_state *);

/**
 * Returns 1 if istate differs from tree, 0 otherwise.  If tree is NULL,
 * compares istate to HEAD.  If tree is NULL and on an unborn branch,
 * returns 1 if there are entries in istate, 0 otherwise.  If an strbuf is
 * provided, the space-separated list of files that differ will be appended
 * to it.
 */
int repo_index_has_changes(struct repository *repo,
			   struct tree *tree,
			   struct strbuf *sb);

int verify_path(const char *path, unsigned mode);
int strcmp_offset(const char *s1, const char *s2, size_t *first_change);
int index_dir_exists(struct index_state *istate, const char *name, int namelen);
void adjust_dirname_case(struct index_state *istate, char *name);
struct cache_entry *index_file_exists(struct index_state *istate, const char *name, int namelen, int igncase);

/*
 * Searches for an entry defined by name and namelen in the given index.
 * If the return value is positive (including 0) it is the position of an
 * exact match. If the return value is negative, the negated value minus 1
 * is the position where the entry would be inserted.
 * Example: The current index consists of these files and its stages:
 *
 *   b#0, d#0, f#1, f#3
 *
 * index_name_pos(&index, "a", 1) -> -1
 * index_name_pos(&index, "b", 1) ->  0
 * index_name_pos(&index, "c", 1) -> -2
 * index_name_pos(&index, "d", 1) ->  1
 * index_name_pos(&index, "e", 1) -> -3
 * index_name_pos(&index, "f", 1) -> -3
 * index_name_pos(&index, "g", 1) -> -5
 */
int index_name_pos(struct index_state *, const char *name, int namelen);

/*
 * Determines whether an entry with the given name exists within the
 * given index. The return value is 1 if an exact match is found, otherwise
 * it is 0. Note that, unlike index_name_pos, this function does not expand
 * the index if it is sparse. If an item exists within the full index but it
 * is contained within a sparse directory (and not in the sparse index), 0 is
 * returned.
 */
int index_entry_exists(struct index_state *, const char *name, int namelen);

/*
 * Some functions return the negative complement of an insert position when a
 * precise match was not found but a position was found where the entry would
 * need to be inserted. This helper protects that logic from any integer
 * underflow.
 */
static inline int index_pos_to_insert_pos(uintmax_t pos)
{
	if (pos > INT_MAX)
		die("overflow: -1 - %"PRIuMAX, pos);
	return -1 - (int)pos;
}

#define ADD_CACHE_OK_TO_ADD 1		/* Ok to add */
#define ADD_CACHE_OK_TO_REPLACE 2	/* Ok to replace file/directory */
#define ADD_CACHE_SKIP_DFCHECK 4	/* Ok to skip DF conflict checks */
#define ADD_CACHE_JUST_APPEND 8		/* Append only */
#define ADD_CACHE_NEW_ONLY 16		/* Do not replace existing ones */
#define ADD_CACHE_KEEP_CACHE_TREE 32	/* Do not invalidate cache-tree */
#define ADD_CACHE_RENORMALIZE 64        /* Pass along HASH_RENORMALIZE */
int add_index_entry(struct index_state *, struct cache_entry *ce, int option);
void rename_index_entry_at(struct index_state *, int pos, const char *new_name);

/* Remove entry, return true if there are more entries to go. */
int remove_index_entry_at(struct index_state *, int pos);

void remove_marked_cache_entries(struct index_state *istate, int invalidate);
int remove_file_from_index(struct index_state *, const char *path);
#define ADD_CACHE_VERBOSE 1
#define ADD_CACHE_PRETEND 2
#define ADD_CACHE_IGNORE_ERRORS	4
#define ADD_CACHE_IGNORE_REMOVAL 8
#define ADD_CACHE_INTENT 16
/*
 * These two are used to add the contents of the file at path
 * to the index, marking the working tree up-to-date by storing
 * the cached stat info in the resulting cache entry.  A caller
 * that has already run lstat(2) on the path can call
 * add_to_index(), and all others can call add_file_to_index();
 * the latter will do necessary lstat(2) internally before
 * calling the former.
 */
int add_to_index(struct index_state *, const char *path, struct stat *, int flags);
int add_file_to_index(struct index_state *, const char *path, int flags);

int chmod_index_entry(struct index_state *, struct cache_entry *ce, char flip);
int ce_same_name(const struct cache_entry *a, const struct cache_entry *b);
void set_object_name_for_intent_to_add_entry(struct cache_entry *ce);
int index_name_is_other(struct index_state *, const char *, int);
void *read_blob_data_from_index(struct index_state *, const char *, unsigned long *);

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
/* don't refresh_fsmonitor state or do stat comparison even if CE_FSMONITOR_VALID is true */
#define CE_MATCH_IGNORE_FSMONITOR 0X20
int is_racy_timestamp(const struct index_state *istate,
		      const struct cache_entry *ce);
int has_racy_timestamp(struct index_state *istate);
int ie_match_stat(struct index_state *, const struct cache_entry *, struct stat *, unsigned int);
int ie_modified(struct index_state *, const struct cache_entry *, struct stat *, unsigned int);

#define HASH_WRITE_OBJECT 1
#define HASH_FORMAT_CHECK 2
#define HASH_RENORMALIZE  4
#define HASH_SILENT 8
int index_fd(struct index_state *istate, struct object_id *oid, int fd, struct stat *st, enum object_type type, const char *path, unsigned flags);
int index_path(struct index_state *istate, struct object_id *oid, const char *path, struct stat *st, unsigned flags);

/*
 * Record to sd the data from st that we use to check whether a file
 * might have changed.
 */
void fill_stat_data(struct stat_data *sd, struct stat *st);

/*
 * Return 0 if st is consistent with a file not having been changed
 * since sd was filled.  If there are differences, return a
 * combination of MTIME_CHANGED, CTIME_CHANGED, OWNER_CHANGED,
 * INODE_CHANGED, and DATA_CHANGED.
 */
int match_stat_data(const struct stat_data *sd, struct stat *st);
int match_stat_data_racy(const struct index_state *istate,
			 const struct stat_data *sd, struct stat *st);

void fill_stat_cache_info(struct index_state *istate, struct cache_entry *ce, struct stat *st);

#define REFRESH_REALLY                   (1 << 0) /* ignore_valid */
#define REFRESH_UNMERGED                 (1 << 1) /* allow unmerged */
#define REFRESH_QUIET                    (1 << 2) /* be quiet about it */
#define REFRESH_IGNORE_MISSING           (1 << 3) /* ignore non-existent */
#define REFRESH_IGNORE_SUBMODULES        (1 << 4) /* ignore submodules */
#define REFRESH_IN_PORCELAIN             (1 << 5) /* user friendly output, not "needs update" */
#define REFRESH_PROGRESS                 (1 << 6) /* show progress bar if stderr is tty */
#define REFRESH_IGNORE_SKIP_WORKTREE     (1 << 7) /* ignore skip_worktree entries */
int refresh_index(struct index_state *, unsigned int flags, const struct pathspec *pathspec, char *seen, const char *header_msg);
/*
 * Refresh the index and write it to disk.
 *
 * 'refresh_flags' is passed directly to 'refresh_index()', while
 * 'COMMIT_LOCK | write_flags' is passed to 'write_locked_index()', so
 * the lockfile is always either committed or rolled back.
 *
 * If 'gentle' is passed, errors locking the index are ignored.
 *
 * Return 1 if refreshing the index returns an error, -1 if writing
 * the index to disk fails, 0 on success.
 *
 * Note that if refreshing the index returns an error, we still write
 * out the index (unless locking fails).
 */
int repo_refresh_and_write_index(struct repository*, unsigned int refresh_flags, unsigned int write_flags, int gentle, const struct pathspec *, char *seen, const char *header_msg);

struct cache_entry *refresh_cache_entry(struct index_state *, struct cache_entry *, unsigned int);

void set_alternate_index_output(const char *);

extern int verify_index_checksum;
extern int verify_ce_order;

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
extern int warn_ambiguous_refs;
extern int warn_on_object_refname_ambiguity;
extern char *apply_default_whitespace;
extern char *apply_default_ignorewhitespace;
extern const char *git_attributes_file;
extern const char *git_hooks_path;
extern int zlib_compression_level;
extern int pack_compression_level;
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
 * commands that do not want replace references to be active.
 */
extern int read_replace_refs;
extern char *git_replace_ref_base;

/*
 * These values are used to help identify parts of a repository to fsync.
 * FSYNC_COMPONENT_NONE identifies data that will not be a persistent part of the
 * repository and so shouldn't be fsynced.
 */
enum fsync_component {
	FSYNC_COMPONENT_NONE,
	FSYNC_COMPONENT_LOOSE_OBJECT		= 1 << 0,
	FSYNC_COMPONENT_PACK			= 1 << 1,
	FSYNC_COMPONENT_PACK_METADATA		= 1 << 2,
	FSYNC_COMPONENT_COMMIT_GRAPH		= 1 << 3,
	FSYNC_COMPONENT_INDEX			= 1 << 4,
	FSYNC_COMPONENT_REFERENCE		= 1 << 5,
};

#define FSYNC_COMPONENTS_OBJECTS (FSYNC_COMPONENT_LOOSE_OBJECT | \
				  FSYNC_COMPONENT_PACK)

#define FSYNC_COMPONENTS_DERIVED_METADATA (FSYNC_COMPONENT_PACK_METADATA | \
					   FSYNC_COMPONENT_COMMIT_GRAPH)

#define FSYNC_COMPONENTS_DEFAULT ((FSYNC_COMPONENTS_OBJECTS | \
				   FSYNC_COMPONENTS_DERIVED_METADATA) & \
				  ~FSYNC_COMPONENT_LOOSE_OBJECT)

#define FSYNC_COMPONENTS_COMMITTED (FSYNC_COMPONENTS_OBJECTS | \
				    FSYNC_COMPONENT_REFERENCE)

#define FSYNC_COMPONENTS_ADDED (FSYNC_COMPONENTS_COMMITTED | \
				FSYNC_COMPONENT_INDEX)

#define FSYNC_COMPONENTS_ALL (FSYNC_COMPONENT_LOOSE_OBJECT | \
			      FSYNC_COMPONENT_PACK | \
			      FSYNC_COMPONENT_PACK_METADATA | \
			      FSYNC_COMPONENT_COMMIT_GRAPH | \
			      FSYNC_COMPONENT_INDEX | \
			      FSYNC_COMPONENT_REFERENCE)

/*
 * A bitmask indicating which components of the repo should be fsynced.
 */
extern enum fsync_component fsync_components;
extern int fsync_object_files;
extern int use_fsync;

enum fsync_method {
	FSYNC_METHOD_FSYNC,
	FSYNC_METHOD_WRITEOUT_ONLY
};

extern enum fsync_method fsync_method;
extern int core_preload_index;
extern int precomposed_unicode;
extern int protect_hfs;
extern int protect_ntfs;

extern int core_apply_sparse_checkout;
extern int core_sparse_checkout_cone;
extern int sparse_expect_files_outside_of_patterns;

/*
 * Returns the boolean value of $GIT_OPTIONAL_LOCKS (or the default value).
 */
int use_optional_locks(void);

/*
 * The character that begins a commented line in user-editable file
 * that is subject to stripspace.
 */
extern char comment_line_char;
extern int auto_comment_line_char;

enum log_refs_config {
	LOG_REFS_UNSET = -1,
	LOG_REFS_NONE = 0,
	LOG_REFS_NORMAL,
	LOG_REFS_ALWAYS
};
extern enum log_refs_config log_all_ref_updates;

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
extern int repository_format_worktree_config;

/*
 * You _have_ to initialize a `struct repository_format` using
 * `= REPOSITORY_FORMAT_INIT` before calling `read_repository_format()`.
 */
struct repository_format {
	int version;
	int precious_objects;
	char *partial_clone; /* value of extensions.partialclone */
	int worktree_config;
	int is_bare;
	int hash_algo;
	int sparse_index;
	char *work_tree;
	struct string_list unknown_extensions;
	struct string_list v1_only_extensions;
};

/*
 * Always use this to initialize a `struct repository_format`
 * to a well-defined, default state before calling
 * `read_repository()`.
 */
#define REPOSITORY_FORMAT_INIT \
{ \
	.version = -1, \
	.is_bare = -1, \
	.hash_algo = GIT_HASH_SHA1, \
	.unknown_extensions = STRING_LIST_INIT_DUP, \
	.v1_only_extensions = STRING_LIST_INIT_DUP, \
}

/*
 * Read the repository format characteristics from the config file "path" into
 * "format" struct. Returns the numeric version. On error, or if no version is
 * found in the configuration, -1 is returned, format->version is set to -1,
 * and all other fields in the struct are set to the default configuration
 * (REPOSITORY_FORMAT_INIT). Always initialize the struct using
 * REPOSITORY_FORMAT_INIT before calling this function.
 */
int read_repository_format(struct repository_format *format, const char *path);

/*
 * Free the memory held onto by `format`, but not the struct itself.
 * (No need to use this after `read_repository_format()` fails.)
 */
void clear_repository_format(struct repository_format *format);

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
 *
 * If successful and fmt is not NULL, fill fmt with data.
 */
void check_repository_format(struct repository_format *fmt);

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020
#define TYPE_CHANGED    0x0040

/*
 * Return an abbreviated sha1 unique within this repository's object database.
 * The result will be at least `len` characters long, and will be NUL
 * terminated.
 *
 * The non-`_r` version returns a static buffer which remains valid until 4
 * more calls to find_unique_abbrev are made.
 *
 * The `_r` variant writes to a buffer supplied by the caller, which must be at
 * least `GIT_MAX_HEXSZ + 1` bytes. The return value is the number of bytes
 * written (excluding the NUL terminator).
 *
 * Note that while this version avoids the static buffer, it is not fully
 * reentrant, as it calls into other non-reentrant git code.
 */
const char *repo_find_unique_abbrev(struct repository *r, const struct object_id *oid, int len);
#define find_unique_abbrev(oid, len) repo_find_unique_abbrev(the_repository, oid, len)
int repo_find_unique_abbrev_r(struct repository *r, char *hex, const struct object_id *oid, int len);
#define find_unique_abbrev_r(hex, oid, len) repo_find_unique_abbrev_r(the_repository, hex, oid, len)

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
char *interpolate_path(const char *path, int real_home);
/* NEEDSWORK: remove this synonym once in-flight topics have migrated */
#define expand_user_path interpolate_path
const char *enter_repo(const char *path, int strict);
static inline int is_absolute_path(const char *path)
{
	return is_dir_sep(path[0]) || has_dos_drive_prefix(path);
}
int is_directory(const char *);
char *strbuf_realpath(struct strbuf *resolved, const char *path,
		      int die_on_error);
char *strbuf_realpath_forgiving(struct strbuf *resolved, const char *path,
				int die_on_error);
char *real_pathdup(const char *path, int die_on_error);
const char *absolute_path(const char *path);
char *absolute_pathdup(const char *path);
const char *remove_leading_path(const char *in, const char *prefix);
const char *relative_path(const char *in, const char *prefix, struct strbuf *sb);
int normalize_path_copy_len(char *dst, const char *src, int *prefix_len);
int normalize_path_copy(char *dst, const char *src);
int longest_ancestor_length(const char *path, struct string_list *prefixes);
char *strip_path_suffix(const char *path, const char *suffix);
int is_mount_point_via_stat(struct strbuf *path);
int daemon_avoid_alias(const char *path);

/*
 * These functions match their is_hfs_dotgit() counterparts; see utf8.h for
 * details.
 */
int is_ntfs_dotgit(const char *name);
int is_ntfs_dotgitmodules(const char *name);
int is_ntfs_dotgitignore(const char *name);
int is_ntfs_dotgitattributes(const char *name);
int is_ntfs_dotmailmap(const char *name);

/*
 * Returns true iff "str" could be confused as a command-line option when
 * passed to a sub-program like "ssh". Note that this has nothing to do with
 * shell-quoting, which should be handled separately; we're assuming here that
 * the string makes it verbatim to the sub-program.
 */
int looks_like_command_line_option(const char *str);

/**
 * Return a newly allocated string with the evaluation of
 * "$XDG_CONFIG_HOME/$subdir/$filename" if $XDG_CONFIG_HOME is non-empty, otherwise
 * "$HOME/.config/$subdir/$filename". Return NULL upon error.
 */
char *xdg_config_home_for(const char *subdir, const char *filename);

/**
 * Return a newly allocated string with the evaluation of
 * "$XDG_CONFIG_HOME/git/$filename" if $XDG_CONFIG_HOME is non-empty, otherwise
 * "$HOME/.config/git/$filename". Return NULL upon error.
 */
char *xdg_config_home(const char *filename);

/**
 * Return a newly allocated string with the evaluation of
 * "$XDG_CACHE_HOME/git/$filename" if $XDG_CACHE_HOME is non-empty, otherwise
 * "$HOME/.cache/git/$filename". Return NULL upon error.
 */
char *xdg_cache_home(const char *filename);

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
	unsigned int val = hexval(s[0]);
	return (val & ~0xf) ? val : (val << 4) | hexval(s[1]);
}

/* Convert to/from hex/sha1 representation */
#define MINIMUM_ABBREV minimum_abbrev
#define DEFAULT_ABBREV default_abbrev

/* used when the code does not know or care what the default abbrev is */
#define FALLBACK_DEFAULT_ABBREV 7

struct object_context {
	unsigned short mode;
	/*
	 * symlink_path is only used by get_tree_entry_follow_symlinks,
	 * and only for symlinks that point outside the repository.
	 */
	struct strbuf symlink_path;
	/*
	 * If GET_OID_RECORD_PATH is set, this will record path (if any)
	 * found when resolving the name. The caller is responsible for
	 * releasing the memory.
	 */
	char *path;
};

#define GET_OID_QUIETLY           01
#define GET_OID_COMMIT            02
#define GET_OID_COMMITTISH        04
#define GET_OID_TREE             010
#define GET_OID_TREEISH          020
#define GET_OID_BLOB             040
#define GET_OID_FOLLOW_SYMLINKS 0100
#define GET_OID_RECORD_PATH     0200
#define GET_OID_ONLY_TO_DIE    04000
#define GET_OID_REQUIRE_PATH  010000

#define GET_OID_DISAMBIGUATORS \
	(GET_OID_COMMIT | GET_OID_COMMITTISH | \
	GET_OID_TREE | GET_OID_TREEISH | \
	GET_OID_BLOB)

enum get_oid_result {
	FOUND = 0,
	MISSING_OBJECT = -1, /* The requested object is missing */
	SHORT_NAME_AMBIGUOUS = -2,
	/* The following only apply when symlinks are followed */
	DANGLING_SYMLINK = -4, /*
				* The initial symlink is there, but
				* (transitively) points to a missing
				* in-tree file
				*/
	SYMLINK_LOOP = -5,
	NOT_DIR = -6, /*
		       * Somewhere along the symlink chain, a path is
		       * requested which contains a file as a
		       * non-final element.
		       */
};

int repo_get_oid(struct repository *r, const char *str, struct object_id *oid);
__attribute__((format (printf, 2, 3)))
int get_oidf(struct object_id *oid, const char *fmt, ...);
int repo_get_oid_commit(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_committish(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_tree(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_treeish(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_blob(struct repository *r, const char *str, struct object_id *oid);
int repo_get_oid_mb(struct repository *r, const char *str, struct object_id *oid);
void maybe_die_on_misspelt_object_name(struct repository *repo,
				       const char *name,
				       const char *prefix);
enum get_oid_result get_oid_with_context(struct repository *repo, const char *str,
					 unsigned flags, struct object_id *oid,
					 struct object_context *oc);

#define get_oid(str, oid)		repo_get_oid(the_repository, str, oid)
#define get_oid_commit(str, oid)	repo_get_oid_commit(the_repository, str, oid)
#define get_oid_committish(str, oid)	repo_get_oid_committish(the_repository, str, oid)
#define get_oid_tree(str, oid)		repo_get_oid_tree(the_repository, str, oid)
#define get_oid_treeish(str, oid)	repo_get_oid_treeish(the_repository, str, oid)
#define get_oid_blob(str, oid)		repo_get_oid_blob(the_repository, str, oid)
#define get_oid_mb(str, oid) 		repo_get_oid_mb(the_repository, str, oid)

typedef int each_abbrev_fn(const struct object_id *oid, void *);
int repo_for_each_abbrev(struct repository *r, const char *prefix, each_abbrev_fn, void *);
#define for_each_abbrev(prefix, fn, data) repo_for_each_abbrev(the_repository, prefix, fn, data)

int set_disambiguate_hint_config(const char *var, const char *value);

/*
 * Try to read a SHA1 in hexadecimal format from the 40 characters
 * starting at hex.  Write the 20-byte result to sha1 in binary form.
 * Return 0 on success.  Reading stops if a NUL is encountered in the
 * input, so it is safe to pass this function an arbitrary
 * null-terminated string.
 */
int get_sha1_hex(const char *hex, unsigned char *sha1);
int get_oid_hex(const char *hex, struct object_id *sha1);

/* Like get_oid_hex, but for an arbitrary hash algorithm. */
int get_oid_hex_algop(const char *hex, struct object_id *oid, const struct git_hash_algo *algop);

/*
 * Read `len` pairs of hexadecimal digits from `hex` and write the
 * values to `binary` as `len` bytes. Return 0 on success, or -1 if
 * the input does not consist of hex digits).
 */
int hex_to_bytes(unsigned char *binary, const char *hex, size_t len);

/*
 * Convert a binary hash in "unsigned char []" or an object name in
 * "struct object_id *" to its hex equivalent. The `_r` variant is reentrant,
 * and writes the NUL-terminated output to the buffer `out`, which must be at
 * least `GIT_MAX_HEXSZ + 1` bytes, and returns a pointer to out for
 * convenience.
 *
 * The non-`_r` variant returns a static buffer, but uses a ring of 4
 * buffers, making it safe to make multiple calls for a single statement, like:
 *
 *   printf("%s -> %s", hash_to_hex(one), hash_to_hex(two));
 *   printf("%s -> %s", oid_to_hex(one), oid_to_hex(two));
 */
char *hash_to_hex_algop_r(char *buffer, const unsigned char *hash, const struct git_hash_algo *);
char *oid_to_hex_r(char *out, const struct object_id *oid);
char *hash_to_hex_algop(const unsigned char *hash, const struct git_hash_algo *);	/* static buffer result! */
char *hash_to_hex(const unsigned char *hash);						/* same static buffer */
char *oid_to_hex(const struct object_id *oid);						/* same static buffer */

/*
 * Parse a 40-character hexadecimal object ID starting from hex, updating the
 * pointer specified by end when parsing stops.  The resulting object ID is
 * stored in oid.  Returns 0 on success.  Parsing will stop on the first NUL or
 * other invalid character.  end is only updated on success; otherwise, it is
 * unmodified.
 */
int parse_oid_hex(const char *hex, struct object_id *oid, const char **end);

/* Like parse_oid_hex, but for an arbitrary hash algorithm. */
int parse_oid_hex_algop(const char *hex, struct object_id *oid, const char **end,
			const struct git_hash_algo *algo);


/*
 * These functions work like get_oid_hex and parse_oid_hex, but they will parse
 * a hex value for any algorithm. The algorithm is detected based on the length
 * and the algorithm in use is returned. If this is not a hex object ID in any
 * algorithm, returns GIT_HASH_UNKNOWN.
 */
int get_oid_hex_any(const char *hex, struct object_id *oid);
int parse_oid_hex_any(const char *hex, struct object_id *oid, const char **end);

/*
 * This reads short-hand syntax that not only evaluates to a commit
 * object name, but also can act as if the end user spelled the name
 * of the branch from the command line.
 *
 * - "@{-N}" finds the name of the Nth previous branch we were on, and
 *   places the name of the branch in the given buf and returns the
 *   number of characters parsed if successful.
 *
 * - "<branch>@{upstream}" finds the name of the other ref that
 *   <branch> is configured to merge with (missing <branch> defaults
 *   to the current branch), and places the name of the branch in the
 *   given buf and returns the number of characters parsed if
 *   successful.
 *
 * If the input is not of the accepted format, it returns a negative
 * number to signal an error.
 *
 * If the input was ok but there are not N branch switches in the
 * reflog, it returns 0.
 */
#define INTERPRET_BRANCH_LOCAL (1<<0)
#define INTERPRET_BRANCH_REMOTE (1<<1)
#define INTERPRET_BRANCH_HEAD (1<<2)
struct interpret_branch_name_options {
	/*
	 * If "allowed" is non-zero, it is a treated as a bitfield of allowable
	 * expansions: local branches ("refs/heads/"), remote branches
	 * ("refs/remotes/"), or "HEAD". If no "allowed" bits are set, any expansion is
	 * allowed, even ones to refs outside of those namespaces.
	 */
	unsigned allowed;

	/*
	 * If ^{upstream} or ^{push} (or equivalent) is requested, and the
	 * branch in question does not have such a reference, return -1 instead
	 * of die()-ing.
	 */
	unsigned nonfatal_dangling_mark : 1;
};
int repo_interpret_branch_name(struct repository *r,
			       const char *str, int len,
			       struct strbuf *buf,
			       const struct interpret_branch_name_options *options);
#define interpret_branch_name(str, len, buf, options) \
	repo_interpret_branch_name(the_repository, str, len, buf, options)

int validate_headref(const char *ref);

int base_name_compare(const char *name1, int len1, int mode1, const char *name2, int len2, int mode2);
int df_name_compare(const char *name1, int len1, int mode1, const char *name2, int len2, int mode2);
int name_compare(const char *name1, size_t len1, const char *name2, size_t len2);
int cache_name_stage_compare(const char *name1, int len1, int stage1, const char *name2, int len2, int stage2);

void *read_object_with_reference(struct repository *r,
				 const struct object_id *oid,
				 enum object_type required_type,
				 unsigned long *size,
				 struct object_id *oid_ret);

struct object *repo_peel_to_type(struct repository *r,
				 const char *name, int namelen,
				 struct object *o, enum object_type);
#define peel_to_type(name, namelen, obj, type) \
	repo_peel_to_type(the_repository, name, namelen, obj, type)

#define IDENT_STRICT	       1
#define IDENT_NO_DATE	       2
#define IDENT_NO_NAME	       4

enum want_ident {
	WANT_BLANK_IDENT,
	WANT_AUTHOR_IDENT,
	WANT_COMMITTER_IDENT
};

const char *git_author_info(int);
const char *git_committer_info(int);
const char *fmt_ident(const char *name, const char *email,
		      enum want_ident whose_ident,
		      const char *date_str, int);
const char *fmt_name(enum want_ident);
const char *ident_default_name(void);
const char *ident_default_email(void);
const char *git_editor(void);
const char *git_sequence_editor(void);
const char *git_pager(int stdout_is_tty);
int is_terminal_dumb(void);
int git_ident_config(const char *, const char *, void *);
/*
 * Prepare an ident to fall back on if the user didn't configure it.
 */
void prepare_fallback_ident(const char *name, const char *email);
void reset_ident_date(void);

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
int split_ident_line(struct ident_split *, const char *, int);

/*
 * Compare split idents for equality or strict ordering. Note that we
 * compare only the ident part of the line, ignoring any timestamp.
 *
 * Because there are two fields, we must choose one as the primary key; we
 * currently arbitrarily pick the email.
 */
int ident_cmp(const struct ident_split *, const struct ident_split *);

struct cache_def {
	struct strbuf path;
	int flags;
	int track_flags;
	int prefix_len_stat_func;
};
#define CACHE_DEF_INIT { \
	.path = STRBUF_INIT, \
}
static inline void cache_def_clear(struct cache_def *cache)
{
	strbuf_release(&cache->path);
}

int has_symlink_leading_path(const char *name, int len);
int threaded_has_symlink_leading_path(struct cache_def *, const char *, int);
int check_leading_path(const char *name, int len, int warn_on_lstat_err);
int has_dirs_only_path(const char *name, int len, int prefix_len);
void invalidate_lstat_cache(void);
void schedule_dir_for_removal(const char *name, int len);
void remove_scheduled_dirs(void);

struct pack_window {
	struct pack_window *next;
	unsigned char *base;
	off_t offset;
	size_t len;
	unsigned int last_used;
	unsigned int inuse_cnt;
};

struct pack_entry {
	off_t offset;
	struct packed_git *p;
};

/*
 * Create a temporary file rooted in the object database directory, or
 * die on failure. The filename is taken from "pattern", which should have the
 * usual "XXXXXX" trailer, and the resulting filename is written into the
 * "template" buffer. Returns the open descriptor.
 */
int odb_mkstemp(struct strbuf *temp_filename, const char *pattern);

/*
 * Create a pack .keep file named "name" (which should generally be the output
 * of odb_pack_name). Returns a file descriptor opened for writing, or -1 on
 * error.
 */
int odb_pack_keep(const char *name);

/*
 * Set this to 0 to prevent oid_object_info_extended() from fetching missing
 * blobs. This has a difference only if extensions.partialClone is set.
 *
 * Its default value is 1.
 */
extern int fetch_if_missing;

/* Dumb servers support */
int update_server_info(int);

const char *get_log_output_encoding(void);
const char *get_commit_output_encoding(void);

int committer_ident_sufficiently_given(void);
int author_ident_sufficiently_given(void);

extern const char *git_commit_encoding;
extern const char *git_log_output_encoding;
extern const char *git_mailmap_file;
extern const char *git_mailmap_blob;

/* IO helper functions */
void maybe_flush_or_die(FILE *, const char *);
__attribute__((format (printf, 2, 3)))
void fprintf_or_die(FILE *, const char *fmt, ...);
void fwrite_or_die(FILE *f, const void *buf, size_t count);
void fflush_or_die(FILE *f);

#define COPY_READ_ERROR (-2)
#define COPY_WRITE_ERROR (-3)
int copy_fd(int ifd, int ofd);
int copy_file(const char *dst, const char *src, int mode);
int copy_file_with_time(const char *dst, const char *src, int mode);

void write_or_die(int fd, const void *buf, size_t count);
void fsync_or_die(int fd, const char *);
int fsync_component(enum fsync_component component, int fd);
void fsync_component_or_die(enum fsync_component component, int fd, const char *msg);

ssize_t read_in_full(int fd, void *buf, size_t count);
ssize_t write_in_full(int fd, const void *buf, size_t count);
ssize_t pread_in_full(int fd, void *buf, size_t count, off_t offset);

static inline ssize_t write_str_in_full(int fd, const char *str)
{
	return write_in_full(fd, str, strlen(str));
}

/**
 * Open (and truncate) the file at path, write the contents of buf to it,
 * and close it. Dies if any errors are encountered.
 */
void write_file_buf(const char *path, const char *buf, size_t len);

/**
 * Like write_file_buf(), but format the contents into a buffer first.
 * Additionally, write_file() will append a newline if one is not already
 * present, making it convenient to write text files:
 *
 *   write_file(path, "counter: %d", ctr);
 */
__attribute__((format (printf, 2, 3)))
void write_file(const char *path, const char *fmt, ...);

/* pager.c */
void setup_pager(void);
int pager_in_use(void);
extern int pager_use_color;
int term_columns(void);
void term_clear_line(void);
int decimal_width(uintmax_t);
int check_pager_config(const char *cmd);
void prepare_pager_args(struct child_process *, const char *pager);

extern const char *editor_program;
extern const char *askpass_program;
extern const char *excludes_file;

/* base85 */
int decode_85(char *dst, const char *line, int linelen);
void encode_85(char *buf, const unsigned char *data, int bytes);

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
void shift_tree(struct repository *, const struct object_id *, const struct object_id *, struct object_id *, int);
void shift_tree_by(struct repository *, const struct object_id *, const struct object_id *, struct object_id *, const char *);

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
/* All WS_* -- when extended, adapt diff.c emit_symbol */
#define WS_RULE_MASK           07777
extern unsigned whitespace_rule_cfg;
unsigned whitespace_rule(struct index_state *, const char *);
unsigned parse_whitespace_rule(const char *);
unsigned ws_check(const char *line, int len, unsigned ws_rule);
void ws_check_emit(const char *line, int len, unsigned ws_rule, FILE *stream, const char *set, const char *reset, const char *ws);
char *whitespace_error_string(unsigned ws);
void ws_fix_copy(struct strbuf *, const char *, int, unsigned, int *);
int ws_blank_line(const char *line, int len, unsigned ws_rule);
#define ws_tab_width(rule)     ((rule) & WS_TAB_WIDTH_MASK)

/* ls-files */
void overlay_tree_on_index(struct index_state *istate,
			   const char *tree_name, const char *prefix);

/* setup.c */
struct startup_info {
	int have_repository;
	const char *prefix;
	const char *original_cwd;
};
extern struct startup_info *startup_info;
extern const char *tmp_original_cwd;

/* merge.c */
struct commit_list;
int try_merge_command(struct repository *r,
		const char *strategy, size_t xopts_nr,
		const char **xopts, struct commit_list *common,
		const char *head_arg, struct commit_list *remotes);
int checkout_fast_forward(struct repository *r,
			  const struct object_id *from,
			  const struct object_id *to,
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

/*
 * Create a directory and (if share is nonzero) adjust its permissions
 * according to the shared_repository setting. Only use this for
 * directories under $GIT_DIR.  Don't use it for working tree
 * directories.
 */
void safe_create_dir(const char *dir, int share);

/*
 * Should we print an ellipsis after an abbreviated SHA-1 value
 * when doing diff-raw output or indicating a detached HEAD?
 */
int print_sha1_ellipsis(void);

/* Return 1 if the file is empty or does not exists, 0 otherwise. */
int is_empty_or_missing_file(const char *filename);

#endif /* CACHE_H */
