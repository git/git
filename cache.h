#ifndef CACHE_H
#define CACHE_H

#include "git-compat-util.h"
#include "strbuf.h"
#include "hashmap.h"
#include "list.h"
#include "gettext.h"
#include "string-list.h"
#include "hash.h"
#include "path.h"
#include "pathspec.h"
#include "object.h"
#include "repository.h"
#include "statinfo.h"

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

static inline int ce_path_match(struct index_state *istate,
				const struct cache_entry *ce,
				const struct pathspec *pathspec,
				char *seen)
{
	return match_pathspec(istate, pathspec, ce->name, ce_namelen(ce), 0, seen,
			      S_ISDIR(ce->ce_mode) || S_ISGITLINK(ce->ce_mode));
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

enum sparse_index_mode {
	/*
	 * There are no sparse directories in the index at all.
	 *
	 * Repositories that don't use cone-mode sparse-checkout will
	 * always have their indexes in this mode.
	 */
	INDEX_EXPANDED = 0,

	/*
	 * The index has already been collapsed to sparse directories
	 * whereever possible.
	 */
	INDEX_COLLAPSED,

	/*
	 * The sparse directories that exist are outside the
	 * sparse-checkout boundary, but it is possible that some file
	 * entries could collapse to sparse directory entries.
	 */
	INDEX_PARTIALLY_SPARSE,
};

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
		 fsmonitor_has_run_once : 1;
	enum sparse_index_mode sparse_index;
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

/**
 * A "struct index_state istate" must be initialized with
 * INDEX_STATE_INIT or the corresponding index_state_init().
 *
 * If the variable won't be used again, use release_index() to free()
 * its resources. If it needs to be used again use discard_index(),
 * which does the same thing, but will use use index_state_init() at
 * the end. The discard_index() will use its own "istate->repo" as the
 * "r" argument to index_state_init() in that case.
 */
#define INDEX_STATE_INIT(r) { \
	.repo = (r), \
}
void index_state_init(struct index_state *istate, struct repository *r);
void release_index(struct index_state *istate);

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
 * repo_has_promisor_remote() returns true.
 */
typedef int (*must_prefetch_predicate)(const struct cache_entry *);
void prefetch_cache_entries(const struct index_state *istate,
			    must_prefetch_predicate must_prefetch);

#ifdef USE_THE_INDEX_VARIABLE
extern struct index_state the_index;
#endif

#define INIT_DB_QUIET 0x0001
#define INIT_DB_EXIST_OK 0x0002

int init_db(const char *git_dir, const char *real_git_dir,
	    const char *template_dir, int hash_algo,
	    const char *initial_branch, unsigned int flags);
void initialize_repository_version(int hash_algo, int reinit);

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

void discard_index(struct index_state *);
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
 * Like index_name_pos, returns the position of an entry of the given name in
 * the index if one exists, otherwise returns a negative value where the negated
 * value minus 1 is the position where the index entry would be inserted. Unlike
 * index_name_pos, however, a sparse index is not expanded to find an entry
 * inside a sparse directory.
 */
int index_name_pos_sparse(struct index_state *, const char *name, int namelen);

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

extern int quote_path_fully;

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020
#define TYPE_CHANGED    0x0040

int base_name_compare(const char *name1, size_t len1, int mode1,
		      const char *name2, size_t len2, int mode2);
int df_name_compare(const char *name1, size_t len1, int mode1,
		    const char *name2, size_t len2, int mode2);
int name_compare(const char *name1, size_t len1, const char *name2, size_t len2);
int cache_name_stage_compare(const char *name1, int len1, int stage1, const char *name2, int len2, int stage2);

const char *git_editor(void);
const char *git_sequence_editor(void);
const char *git_pager(int stdout_is_tty);
int is_terminal_dumb(void);

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

/* Dumb servers support */
int update_server_info(int);

extern const char *git_mailmap_file;
extern const char *git_mailmap_blob;

#define COPY_READ_ERROR (-2)
#define COPY_WRITE_ERROR (-3)
int copy_fd(int ifd, int ofd);
int copy_file(const char *dst, const char *src, int mode);
int copy_file_with_time(const char *dst, const char *src, int mode);

/* pager.c */
void setup_pager(void);
int pager_in_use(void);
extern int pager_use_color;
int term_columns(void);
void term_clear_line(void);
int decimal_width(uintmax_t);
int check_pager_config(const char *cmd);
void prepare_pager_args(struct child_process *, const char *pager);

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
int ws_blank_line(const char *line, int len);
#define ws_tab_width(rule)     ((rule) & WS_TAB_WIDTH_MASK)

/* ls-files */
void overlay_tree_on_index(struct index_state *istate,
			   const char *tree_name, const char *prefix);

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

#endif /* CACHE_H */
