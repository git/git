#ifndef CACHE_H
#define CACHE_H

#include "git-compat-util.h"
#include "strbuf.h"
#include "hash.h"
#include "advice.h"
#include "gettext.h"
#include "convert.h"

#include SHA1_HEADER
#ifndef git_SHA_CTX
#define git_SHA_CTX	SHA_CTX
#define git_SHA1_Init	SHA1_Init
#define git_SHA1_Update	SHA1_Update
#define git_SHA1_Final	SHA1_Final
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
void git_deflate_end(git_zstream *);
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
 *
 * NOTE! We *really* shouldn't depend on the S_IFxxx macros
 * always having the same values everywhere. We should use
 * our internal git values for these things, and then we can
 * translate that to the OS-specific value. It just so
 * happens that everybody shares the same bit representation
 * in the UNIX world (and apparently wider too..)
 */
#define S_IFGITLINK	0160000
#define S_ISGITLINK(m)	(((m) & S_IFMT) == S_IFGITLINK)

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
struct ondisk_cache_entry {
	struct cache_time ctime;
	struct cache_time mtime;
	unsigned int dev;
	unsigned int ino;
	unsigned int mode;
	unsigned int uid;
	unsigned int gid;
	unsigned int size;
	unsigned char sha1[20];
	unsigned short flags;
	char name[FLEX_ARRAY]; /* more */
};

/*
 * This struct is used when CE_EXTENDED bit is 1
 * The struct must match ondisk_cache_entry exactly from
 * ctime till flags
 */
struct ondisk_cache_entry_extended {
	struct cache_time ctime;
	struct cache_time mtime;
	unsigned int dev;
	unsigned int ino;
	unsigned int mode;
	unsigned int uid;
	unsigned int gid;
	unsigned int size;
	unsigned char sha1[20];
	unsigned short flags;
	unsigned short flags2;
	char name[FLEX_ARRAY]; /* more */
};

struct cache_entry {
	struct cache_time ce_ctime;
	struct cache_time ce_mtime;
	unsigned int ce_dev;
	unsigned int ce_ino;
	unsigned int ce_mode;
	unsigned int ce_uid;
	unsigned int ce_gid;
	unsigned int ce_size;
	unsigned int ce_flags;
	unsigned char sha1[20];
	struct cache_entry *next;
	char name[FLEX_ARRAY]; /* more */
};

#define CE_NAMEMASK  (0x0fff)
#define CE_STAGEMASK (0x3000)
#define CE_EXTENDED  (0x4000)
#define CE_VALID     (0x8000)
#define CE_STAGESHIFT 12

/*
 * Range 0xFFFF0000 in ce_flags is divided into
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
#define CE_UNHASHED          (1 << 21)
#define CE_WT_REMOVE         (1 << 22) /* remove in work directory */
#define CE_CONFLICTED        (1 << 23)

#define CE_UNPACKED          (1 << 24)
#define CE_NEW_SKIP_WORKTREE (1 << 25)

/*
 * Extended on-disk flags
 */
#define CE_INTENT_TO_ADD     (1 << 29)
#define CE_SKIP_WORKTREE     (1 << 30)
/* CE_EXTENDED2 is for future extension */
#define CE_EXTENDED2         (1 << 31)

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

/*
 * Copy the sha1 and stat state of a cache entry from one to
 * another. But we never change the name, or the hash state!
 */
#define CE_STATE_MASK (CE_HASHED | CE_UNHASHED)
static inline void copy_cache_entry(struct cache_entry *dst, struct cache_entry *src)
{
	unsigned int state = dst->ce_flags & CE_STATE_MASK;

	/* Don't copy hash chain and name */
	memcpy(dst, src, offsetof(struct cache_entry, next));

	/* Restore the hash state */
	dst->ce_flags = (dst->ce_flags & ~CE_STATE_MASK) | state;
}

static inline unsigned create_ce_flags(size_t len, unsigned stage)
{
	if (len >= CE_NAMEMASK)
		len = CE_NAMEMASK;
	return (len | (stage << CE_STAGESHIFT));
}

static inline size_t ce_namelen(const struct cache_entry *ce)
{
	size_t len = ce->ce_flags & CE_NAMEMASK;
	if (len < CE_NAMEMASK)
		return len;
	return strlen(ce->name + CE_NAMEMASK) + CE_NAMEMASK;
}

#define ce_size(ce) cache_entry_size(ce_namelen(ce))
#define ondisk_ce_size(ce) (((ce)->ce_flags & CE_EXTENDED) ? \
			    ondisk_cache_entry_extended_size(ce_namelen(ce)) : \
			    ondisk_cache_entry_size(ce_namelen(ce)))
#define ce_stage(ce) ((CE_STAGEMASK & (ce)->ce_flags) >> CE_STAGESHIFT)
#define ce_uptodate(ce) ((ce)->ce_flags & CE_UPTODATE)
#define ce_skip_worktree(ce) ((ce)->ce_flags & CE_SKIP_WORKTREE)
#define ce_mark_uptodate(ce) ((ce)->ce_flags |= CE_UPTODATE)

#define ce_permissions(mode) (((mode) & 0100) ? 0755 : 0644)
static inline unsigned int create_ce_mode(unsigned int mode)
{
	if (S_ISLNK(mode))
		return S_IFLNK;
	if (S_ISDIR(mode) || S_ISGITLINK(mode))
		return S_IFGITLINK;
	return S_IFREG | ce_permissions(mode);
}
static inline unsigned int ce_mode_from_stat(struct cache_entry *ce, unsigned int mode)
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

#define flexible_size(STRUCT,len) ((offsetof(struct STRUCT,name) + (len) + 8) & ~7)
#define cache_entry_size(len) flexible_size(cache_entry,len)
#define ondisk_cache_entry_size(len) flexible_size(ondisk_cache_entry,len)
#define ondisk_cache_entry_extended_size(len) flexible_size(ondisk_cache_entry_extended,len)

struct index_state {
	struct cache_entry **cache;
	unsigned int cache_nr, cache_alloc, cache_changed;
	struct string_list *resolve_undo;
	struct cache_tree *cache_tree;
	struct cache_time timestamp;
	void *alloc;
	unsigned name_hash_initialized : 1,
		 initialized : 1;
	struct hash_table name_hash;
};

extern struct index_state the_index;

/* Name hashing */
extern void add_name_hash(struct index_state *istate, struct cache_entry *ce);
/*
 * We don't actually *remove* it, we can just mark it invalid so that
 * we won't find it in lookups.
 *
 * Not only would we have to search the lists (simple enough), but
 * we'd also have to rehash other hash buckets in case this makes the
 * hash bucket empty (common). So it's much better to just mark
 * it.
 */
static inline void remove_name_hash(struct cache_entry *ce)
{
	ce->ce_flags |= CE_UNHASHED;
}


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
#define write_cache(newfd, cache, entries) write_index(&the_index, (newfd))
#define discard_cache() discard_index(&the_index)
#define unmerged_cache() unmerged_index(&the_index)
#define cache_name_pos(name, namelen) index_name_pos(&the_index,(name),(namelen))
#define add_cache_entry(ce, option) add_index_entry(&the_index, (ce), (option))
#define rename_cache_entry_at(pos, new_name) rename_index_entry_at(&the_index, (pos), (new_name))
#define remove_cache_entry_at(pos) remove_index_entry_at(&the_index, (pos))
#define remove_file_from_cache(path) remove_file_from_index(&the_index, (path))
#define add_to_cache(path, st, flags) add_to_index(&the_index, (path), (st), (flags))
#define add_file_to_cache(path, flags) add_file_to_index(&the_index, (path), (flags))
#define refresh_cache(flags) refresh_index(&the_index, (flags), NULL, NULL, NULL)
#define ce_match_stat(ce, st, options) ie_match_stat(&the_index, (ce), (st), (options))
#define ce_modified(ce, st, options) ie_modified(&the_index, (ce), (st), (options))
#define cache_name_exists(name, namelen, igncase) index_name_exists(&the_index, (name), (namelen), (igncase))
#define cache_name_is_other(name, namelen) index_name_is_other(&the_index, (name), (namelen))
#define resolve_undo_clear() resolve_undo_clear_index(&the_index)
#define unmerge_cache_entry_at(at) unmerge_index_entry_at(&the_index, at)
#define unmerge_cache(pathspec) unmerge_index(&the_index, pathspec)
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

#define GIT_DIR_ENVIRONMENT "GIT_DIR"
#define GIT_WORK_TREE_ENVIRONMENT "GIT_WORK_TREE"
#define DEFAULT_GIT_DIR_ENVIRONMENT ".git"
#define DB_ENVIRONMENT "GIT_OBJECT_DIRECTORY"
#define INDEX_ENVIRONMENT "GIT_INDEX_FILE"
#define GRAFT_ENVIRONMENT "GIT_GRAFT_FILE"
#define TEMPLATE_DIR_ENVIRONMENT "GIT_TEMPLATE_DIR"
#define CONFIG_ENVIRONMENT "GIT_CONFIG"
#define CONFIG_DATA_ENVIRONMENT "GIT_CONFIG_PARAMETERS"
#define EXEC_PATH_ENVIRONMENT "GIT_EXEC_PATH"
#define CEILING_DIRECTORIES_ENVIRONMENT "GIT_CEILING_DIRECTORIES"
#define NO_REPLACE_OBJECTS_ENVIRONMENT "GIT_NO_REPLACE_OBJECTS"
#define GITATTRIBUTES_FILE ".gitattributes"
#define INFOATTRIBUTES_FILE "info/attributes"
#define ATTRIBUTE_MACRO_PREFIX "[attr]"
#define GIT_NOTES_REF_ENVIRONMENT "GIT_NOTES_REF"
#define GIT_NOTES_DEFAULT_REF "refs/notes/commits"
#define GIT_NOTES_DISPLAY_REF_ENVIRONMENT "GIT_NOTES_DISPLAY_REF"
#define GIT_NOTES_REWRITE_REF_ENVIRONMENT "GIT_NOTES_REWRITE_REF"
#define GIT_NOTES_REWRITE_MODE_ENVIRONMENT "GIT_NOTES_REWRITE_MODE"

/*
 * Repository-local GIT_* environment variables
 * The array is NULL-terminated to simplify its usage in contexts such
 * environment creation or simple walk of the list.
 * The number of non-NULL entries is available as a macro.
 */
#define LOCAL_REPO_ENV_SIZE 9
extern const char *const local_repo_env[LOCAL_REPO_ENV_SIZE + 1];

extern int is_bare_repository_cfg;
extern int is_bare_repository(void);
extern int is_inside_git_dir(void);
extern char *git_work_tree_cfg;
extern int is_inside_work_tree(void);
extern int have_git_dir(void);
extern const char *get_git_dir(void);
extern char *get_object_directory(void);
extern char *get_index_file(void);
extern char *get_graft_file(void);
extern int set_git_dir(const char *path);
extern const char *get_git_work_tree(void);
extern const char *read_gitfile_gently(const char *path);
extern void set_git_work_tree(const char *tree);

#define ALTERNATE_DB_ENVIRONMENT "GIT_ALTERNATE_OBJECT_DIRECTORIES"

extern const char **get_pathspec(const char *prefix, const char **pathspec);
extern void setup_work_tree(void);
extern const char *setup_git_directory_gently(int *);
extern const char *setup_git_directory(void);
extern char *prefix_path(const char *prefix, int len, const char *path);
extern const char *prefix_filename(const char *prefix, int len, const char *path);
extern int check_filename(const char *prefix, const char *name);
extern void verify_filename(const char *prefix, const char *name);
extern void verify_non_filename(const char *prefix, const char *name);

#define INIT_DB_QUIET 0x0001

extern int set_git_dir_init(const char *git_dir, const char *real_git_dir, int);
extern int init_db(const char *template_dir, unsigned int flags);

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
			x = xrealloc((x), alloc * sizeof(*(x))); \
		} \
	} while (0)

/* Initialize and use the cache information */
extern int read_index(struct index_state *);
extern int read_index_preload(struct index_state *, const char **pathspec);
extern int read_index_from(struct index_state *, const char *path);
extern int is_index_unborn(struct index_state *);
extern int read_index_unmerged(struct index_state *);
extern int write_index(struct index_state *, int newfd);
extern int discard_index(struct index_state *);
extern int unmerged_index(const struct index_state *);
extern int verify_path(const char *path);
extern struct cache_entry *index_name_exists(struct index_state *istate, const char *name, int namelen, int igncase);
extern int index_name_pos(const struct index_state *, const char *name, int namelen);
#define ADD_CACHE_OK_TO_ADD 1		/* Ok to add */
#define ADD_CACHE_OK_TO_REPLACE 2	/* Ok to replace file/directory */
#define ADD_CACHE_SKIP_DFCHECK 4	/* Ok to skip DF conflict checks */
#define ADD_CACHE_JUST_APPEND 8		/* Append only; tree.c::read_tree() */
#define ADD_CACHE_NEW_ONLY 16		/* Do not replace existing ones */
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
extern struct cache_entry *make_cache_entry(unsigned int mode, const unsigned char *sha1, const char *path, int stage, int refresh);
extern int ce_same_name(struct cache_entry *a, struct cache_entry *b);
extern int index_name_is_other(const struct index_state *, const char *, int);

/* do stat comparison even if CE_VALID is true */
#define CE_MATCH_IGNORE_VALID		01
/* do not check the contents but report dirty on racily-clean entries */
#define CE_MATCH_RACY_IS_DIRTY		02
/* do stat comparison even if CE_SKIP_WORKTREE is true */
#define CE_MATCH_IGNORE_SKIP_WORKTREE	04
extern int ie_match_stat(const struct index_state *, struct cache_entry *, struct stat *, unsigned int);
extern int ie_modified(const struct index_state *, struct cache_entry *, struct stat *, unsigned int);

struct pathspec {
	const char **raw; /* get_pathspec() result, not freed by free_pathspec() */
	int nr;
	unsigned int has_wildcard:1;
	unsigned int recursive:1;
	int max_depth;
	struct pathspec_item {
		const char *match;
		int len;
		unsigned int use_wildcard:1;
	} *items;
};

extern int init_pathspec(struct pathspec *, const char **);
extern void free_pathspec(struct pathspec *);
extern int ce_path_match(const struct cache_entry *ce, const struct pathspec *pathspec);

#define HASH_WRITE_OBJECT 1
#define HASH_FORMAT_CHECK 2
extern int index_fd(unsigned char *sha1, int fd, struct stat *st, enum object_type type, const char *path, unsigned flags);
extern int index_path(unsigned char *sha1, const char *path, struct stat *st, unsigned flags);
extern void fill_stat_cache_info(struct cache_entry *ce, struct stat *st);

#define REFRESH_REALLY		0x0001	/* ignore_valid */
#define REFRESH_UNMERGED	0x0002	/* allow unmerged */
#define REFRESH_QUIET		0x0004	/* be quiet about it */
#define REFRESH_IGNORE_MISSING	0x0008	/* ignore non-existent */
#define REFRESH_IGNORE_SUBMODULES	0x0010	/* ignore submodules */
#define REFRESH_IN_PORCELAIN	0x0020	/* user friendly output, not "needs update" */
extern int refresh_index(struct index_state *, unsigned int flags, const char **pathspec, char *seen, const char *header_msg);

struct lock_file {
	struct lock_file *next;
	int fd;
	pid_t owner;
	char on_list;
	char filename[PATH_MAX];
};
#define LOCK_DIE_ON_ERROR 1
#define LOCK_NODEREF 2
extern int unable_to_lock_error(const char *path, int err);
extern NORETURN void unable_to_lock_index_die(const char *path, int err);
extern int hold_lock_file_for_update(struct lock_file *, const char *path, int);
extern int hold_lock_file_for_append(struct lock_file *, const char *path, int);
extern int commit_lock_file(struct lock_file *);
extern void update_index_if_able(struct index_state *, struct lock_file *);

extern int hold_locked_index(struct lock_file *, int);
extern int commit_locked_index(struct lock_file *);
extern void set_alternate_index_output(const char *);
extern int close_lock_file(struct lock_file *);
extern void rollback_lock_file(struct lock_file *);
extern int delete_ref(const char *, const unsigned char *sha1, int delopt);

/* Environment bits from configuration mechanism */
extern int trust_executable_bit;
extern int trust_ctime;
extern int quote_path_fully;
extern int has_symlinks;
extern int minimum_abbrev, default_abbrev;
extern int ignore_case;
extern int assume_unchanged;
extern int prefer_symlink_refs;
extern int log_all_ref_updates;
extern int warn_ambiguous_refs;
extern int shared_repository;
extern const char *apply_default_whitespace;
extern const char *apply_default_ignorewhitespace;
extern int zlib_compression_level;
extern int core_compression_level;
extern int core_compression_seen;
extern size_t packed_git_window_size;
extern size_t packed_git_limit;
extern size_t delta_base_cache_limit;
extern unsigned long big_file_threshold;
extern int read_replace_refs;
extern int fsync_object_files;
extern int core_preload_index;
extern int core_apply_sparse_checkout;

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
	PUSH_DEFAULT_UPSTREAM,
	PUSH_DEFAULT_CURRENT
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

extern char *mksnpath(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
extern char *git_snpath(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
extern char *git_pathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

/* Return a statically allocated filename matching the sha1 signature */
extern char *mkpath(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern char *git_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern char *git_path_submodule(const char *path, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

extern char *sha1_file_name(const unsigned char *sha1);
extern char *sha1_pack_name(const unsigned char *sha1);
extern char *sha1_pack_index_name(const unsigned char *sha1);
extern const char *find_unique_abbrev(const unsigned char *sha1, int);
extern const unsigned char null_sha1[20];

static inline int hashcmp(const unsigned char *sha1, const unsigned char *sha2)
{
	int i;

	for (i = 0; i < 20; i++, sha1++, sha2++) {
		if (*sha1 != *sha2)
			return *sha1 - *sha2;
	}

	return 0;
}

static inline int is_null_sha1(const unsigned char *sha1)
{
	return !hashcmp(sha1, null_sha1);
}

static inline void hashcpy(unsigned char *sha_dst, const unsigned char *sha_src)
{
	memcpy(sha_dst, sha_src, 20);
}
static inline void hashclr(unsigned char *hash)
{
	memset(hash, 0, 20);
}

#define EMPTY_TREE_SHA1_HEX \
	"4b825dc642cb6eb9a060e54bf8d69288fbee4904"
#define EMPTY_TREE_SHA1_BIN_LITERAL \
	 "\x4b\x82\x5d\xc6\x42\xcb\x6e\xb9\xa0\x60" \
	 "\xe5\x4b\xf8\xd6\x92\x88\xfb\xee\x49\x04"
#define EMPTY_TREE_SHA1_BIN \
	 ((const unsigned char *) EMPTY_TREE_SHA1_BIN_LITERAL)

int git_mkstemp(char *path, size_t n, const char *template);

int git_mkstemps(char *path, size_t n, const char *template, int suffix_len);

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
int set_shared_perm(const char *path, int mode);
#define adjust_shared_perm(path) set_shared_perm((path), 0)
int safe_create_leading_directories(char *path);
int safe_create_leading_directories_const(const char *path);
int mkdir_in_gitdir(const char *path);
extern char *expand_user_path(const char *path);
char *enter_repo(char *path, int strict);
static inline int is_absolute_path(const char *path)
{
	return is_dir_sep(path[0]) || has_dos_drive_prefix(path);
}
int is_directory(const char *);
const char *real_path(const char *path);
const char *absolute_path(const char *path);
const char *relative_path(const char *abs, const char *base);
int normalize_path_copy(char *dst, const char *src);
int longest_ancestor_length(const char *path, const char *prefix_list);
char *strip_path_suffix(const char *path, const char *suffix);
int daemon_avoid_alias(const char *path);
int offset_1st_component(const char *path);

/* object replacement */
#define READ_SHA1_FILE_REPLACE 1
extern void *read_sha1_file_extended(const unsigned char *sha1, enum object_type *type, unsigned long *size, unsigned flag);
static inline void *read_sha1_file(const unsigned char *sha1, enum object_type *type, unsigned long *size)
{
	return read_sha1_file_extended(sha1, type, size, READ_SHA1_FILE_REPLACE);
}
extern const unsigned char *do_lookup_replace_object(const unsigned char *sha1);
static inline const unsigned char *lookup_replace_object(const unsigned char *sha1)
{
	if (!read_replace_refs)
		return sha1;
	return do_lookup_replace_object(sha1);
}

/* Read and unpack a sha1 file into memory, write memory to a sha1 file */
extern int sha1_object_info(const unsigned char *, unsigned long *);
extern int hash_sha1_file(const void *buf, unsigned long len, const char *type, unsigned char *sha1);
extern int write_sha1_file(const void *buf, unsigned long len, const char *type, unsigned char *return_sha1);
extern int pretend_sha1_file(void *, unsigned long, enum object_type, unsigned char *);
extern int force_object_loose(const unsigned char *sha1, time_t mtime);
extern void *map_sha1_file(const unsigned char *sha1, unsigned long *size);
extern int unpack_sha1_header(git_zstream *stream, unsigned char *map, unsigned long mapsize, void *buffer, unsigned long bufsiz);
extern int parse_sha1_header(const char *hdr, unsigned long *sizep);

/* global flag to enable extra checks when accessing packed objects */
extern int do_check_packed_object_crc;

/* for development: log offset of pack access */
extern const char *log_pack_access;

extern int check_sha1_signature(const unsigned char *sha1, void *buf, unsigned long size, const char *type);

extern int move_temp_to_file(const char *tmpfile, const char *filename);

extern int has_sha1_pack(const unsigned char *sha1);
extern int has_sha1_file(const unsigned char *sha1);
extern int has_loose_object_nonlocal(const unsigned char *sha1);

extern int has_pack_index(const unsigned char *sha1);

extern void assert_sha1_type(const unsigned char *sha1, enum object_type expect);

extern const signed char hexval_table[256];
static inline unsigned int hexval(unsigned char c)
{
	return hexval_table[c];
}

/* Convert to/from hex/sha1 representation */
#define MINIMUM_ABBREV minimum_abbrev
#define DEFAULT_ABBREV default_abbrev

struct object_context {
	unsigned char tree[20];
	char path[PATH_MAX];
	unsigned mode;
};

extern int get_sha1(const char *str, unsigned char *sha1);
extern int get_sha1_with_mode_1(const char *str, unsigned char *sha1, unsigned *mode, int only_to_die, const char *prefix);
static inline int get_sha1_with_mode(const char *str, unsigned char *sha1, unsigned *mode)
{
	return get_sha1_with_mode_1(str, sha1, mode, 0, NULL);
}
extern int get_sha1_with_context_1(const char *name, unsigned char *sha1, struct object_context *orc, int only_to_die, const char *prefix);
static inline int get_sha1_with_context(const char *str, unsigned char *sha1, struct object_context *orc)
{
	return get_sha1_with_context_1(str, sha1, orc, 0, NULL);
}
extern int get_sha1_hex(const char *hex, unsigned char *sha1);
extern char *sha1_to_hex(const unsigned char *sha1);	/* static buffer result! */
extern int read_ref(const char *filename, unsigned char *sha1);
extern const char *resolve_ref(const char *path, unsigned char *sha1, int, int *);
extern int dwim_ref(const char *str, int len, unsigned char *sha1, char **ref);
extern int dwim_log(const char *str, int len, unsigned char *sha1, char **ref);
extern int interpret_branch_name(const char *str, struct strbuf *);
extern int get_sha1_mb(const char *str, unsigned char *sha1);

extern int refname_match(const char *abbrev_name, const char *full_name, const char **rules);
extern const char *ref_rev_parse_rules[];
extern const char *ref_fetch_rules[];

extern int create_symref(const char *ref, const char *refs_heads_master, const char *logmsg);
extern int validate_headref(const char *ref);

extern int base_name_compare(const char *name1, int len1, int mode1, const char *name2, int len2, int mode2);
extern int df_name_compare(const char *name1, int len1, int mode1, const char *name2, int len2, int mode2);
extern int cache_name_compare(const char *name1, int len1, const char *name2, int len2);

extern void *read_object_with_reference(const unsigned char *sha1,
					const char *required_type,
					unsigned long *size,
					unsigned char *sha1_ret);

extern struct object *peel_to_type(const char *name, int namelen,
				   struct object *o, enum object_type);

enum date_mode {
	DATE_NORMAL = 0,
	DATE_RELATIVE,
	DATE_SHORT,
	DATE_LOCAL,
	DATE_ISO8601,
	DATE_RFC2822,
	DATE_RAW
};

const char *show_date(unsigned long time, int timezone, enum date_mode mode);
const char *show_date_relative(unsigned long time, int tz,
			       const struct timeval *now,
			       char *timebuf,
			       size_t timebuf_size);
int parse_date(const char *date, char *buf, int bufsize);
int parse_date_basic(const char *date, unsigned long *timestamp, int *offset);
void datestamp(char *buf, int bufsize);
#define approxidate(s) approxidate_careful((s), NULL)
unsigned long approxidate_careful(const char *, int *);
unsigned long approxidate_relative(const char *date, const struct timeval *now);
enum date_mode parse_date_format(const char *format);

#define IDENT_WARN_ON_NO_NAME  1
#define IDENT_ERROR_ON_NO_NAME 2
#define IDENT_NO_DATE	       4
extern const char *git_author_info(int);
extern const char *git_committer_info(int);
extern const char *fmt_ident(const char *name, const char *email, const char *date_str, int);
extern const char *fmt_name(const char *name, const char *email);
extern const char *git_editor(void);
extern const char *git_pager(int stdout_is_tty);

struct checkout {
	const char *base_dir;
	int base_dir_len;
	unsigned force:1,
		 quiet:1,
		 not_new:1,
		 refresh_cache:1;
};

extern int checkout_entry(struct cache_entry *ce, const struct checkout *state, char *topath);

struct cache_def {
	char path[PATH_MAX + 1];
	int len;
	int flags;
	int track_flags;
	int prefix_len_stat_func;
};

extern int has_symlink_leading_path(const char *name, int len);
extern int threaded_has_symlink_leading_path(struct cache_def *, const char *, int);
extern int check_leading_path(const char *name, int len);
extern int has_dirs_only_path(const char *name, int len, int prefix_len);
extern void schedule_dir_for_removal(const char *name, int len);
extern void remove_scheduled_dirs(void);

extern struct alternate_object_database {
	struct alternate_object_database *next;
	char *name;
	char base[FLEX_ARRAY]; /* more */
} *alt_odb_list;
extern void prepare_alt_odb(void);
extern void add_to_alternates_file(const char *reference);
typedef int alt_odb_fn(struct alternate_object_database *, void *);
extern void foreach_alt_odb(alt_odb_fn, void*);

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
		 do_not_close:1;
	unsigned char sha1[20];
	/* something like ".git/objects/pack/xxxxx.pack" */
	char pack_name[FLEX_ARRAY]; /* more */
} *packed_git;

struct pack_entry {
	off_t offset;
	unsigned char sha1[20];
	struct packed_git *p;
};

struct ref {
	struct ref *next;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	char *symref;
	unsigned int force:1,
		merge:1,
		nonfastforward:1,
		deletion:1;
	enum {
		REF_STATUS_NONE = 0,
		REF_STATUS_OK,
		REF_STATUS_REJECT_NONFASTFORWARD,
		REF_STATUS_REJECT_NODELETE,
		REF_STATUS_UPTODATE,
		REF_STATUS_REMOTE_REJECT,
		REF_STATUS_EXPECTING_REPORT
	} status;
	char *remote_status;
	struct ref *peer_ref; /* when renaming */
	char name[FLEX_ARRAY]; /* more */
};

#define REF_NORMAL	(1u << 0)
#define REF_HEADS	(1u << 1)
#define REF_TAGS	(1u << 2)

extern struct ref *find_ref_by_name(const struct ref *list, const char *name);

#define CONNECT_VERBOSE       (1u << 0)
extern char *git_getpass(const char *prompt);
extern struct child_process *git_connect(int fd[2], const char *url, const char *prog, int flags);
extern int finish_connect(struct child_process *conn);
extern int git_connection_is_socket(struct child_process *conn);
extern int path_match(const char *path, int nr, char **match);
struct extra_have_objects {
	int nr, alloc;
	unsigned char (*array)[20];
};
extern struct ref **get_remote_heads(int in, struct ref **list, int nr_match, char **match, unsigned int flags, struct extra_have_objects *);
extern int server_supports(const char *feature);

extern struct packed_git *parse_pack_index(unsigned char *sha1, const char *idx_path);

extern void prepare_packed_git(void);
extern void reprepare_packed_git(void);
extern void install_packed_git(struct packed_git *pack);

extern struct packed_git *find_sha1_pack(const unsigned char *sha1,
					 struct packed_git *packs);

extern void pack_report(void);
extern int open_pack_index(struct packed_git *);
extern void close_pack_index(struct packed_git *);
extern unsigned char *use_pack(struct packed_git *, struct pack_window **, off_t, unsigned long *);
extern void close_pack_windows(struct packed_git *);
extern void unuse_pack(struct pack_window **);
extern void free_pack_by_name(const char *);
extern void clear_delta_base_cache(void);
extern struct packed_git *add_packed_git(const char *, int, int);
extern const unsigned char *nth_packed_object_sha1(struct packed_git *, uint32_t);
extern off_t nth_packed_object_offset(const struct packed_git *, uint32_t);
extern off_t find_pack_entry_one(const unsigned char *, struct packed_git *);
extern void *unpack_entry(struct packed_git *, off_t, enum object_type *, unsigned long *);
extern unsigned long unpack_object_header_buffer(const unsigned char *buf, unsigned long len, enum object_type *type, unsigned long *sizep);
extern unsigned long get_size_from_delta(struct packed_git *, struct pack_window **, off_t);
extern int unpack_object_header(struct packed_git *, struct pack_window **, off_t *, unsigned long *);

struct object_info {
	/* Request */
	unsigned long *sizep;

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
extern int sha1_object_info_extended(const unsigned char *, struct object_info *);

/* Dumb servers support */
extern int update_server_info(int);

/* git_config_parse_key() returns these negated: */
#define CONFIG_INVALID_KEY 1
#define CONFIG_NO_SECTION_OR_NAME 2
/* git_config_set(), git_config_set_multivar() return the above or these: */
#define CONFIG_NO_LOCK -1
#define CONFIG_INVALID_FILE 3
#define CONFIG_NO_WRITE 4
#define CONFIG_NOTHING_SET 5
#define CONFIG_INVALID_PATTERN 6

typedef int (*config_fn_t)(const char *, const char *, void *);
extern int git_default_config(const char *, const char *, void *);
extern int git_config_from_file(config_fn_t fn, const char *, void *);
extern void git_config_push_parameter(const char *text);
extern int git_config_from_parameters(config_fn_t fn, void *data);
extern int git_config(config_fn_t fn, void *);
extern int git_config_early(config_fn_t fn, void *, const char *repo_config);
extern int git_parse_ulong(const char *, unsigned long *);
extern int git_config_int(const char *, const char *);
extern unsigned long git_config_ulong(const char *, const char *);
extern int git_config_bool_or_int(const char *, const char *, int *);
extern int git_config_bool(const char *, const char *);
extern int git_config_maybe_bool(const char *, const char *);
extern int git_config_string(const char **, const char *, const char *);
extern int git_config_pathname(const char **, const char *, const char *);
extern int git_config_set(const char *, const char *);
extern int git_config_parse_key(const char *, char **, int *);
extern int git_config_set_multivar(const char *, const char *, const char *, int);
extern int git_config_rename_section(const char *, const char *);
extern const char *git_etc_gitconfig(void);
extern int check_repository_format_version(const char *var, const char *value, void *cb);
extern int git_env_bool(const char *, int);
extern int git_config_system(void);
extern int config_error_nonbool(const char *);
extern const char *get_log_output_encoding(void);
extern const char *get_commit_output_encoding(void);

extern int git_config_parse_parameter(const char *, config_fn_t fn, void *data);

extern const char *config_exclusive_filename;

#define MAX_GITNAME (1000)
extern char git_default_email[MAX_GITNAME];
extern char git_default_name[MAX_GITNAME];
#define IDENT_NAME_GIVEN 01
#define IDENT_MAIL_GIVEN 02
#define IDENT_ALL_GIVEN (IDENT_NAME_GIVEN|IDENT_MAIL_GIVEN)
extern int user_ident_explicitly_given;
extern int user_ident_sufficiently_given(void);

extern const char *git_commit_encoding;
extern const char *git_log_output_encoding;
extern const char *git_mailmap_file;

/* IO helper functions */
extern void maybe_flush_or_die(FILE *, const char *);
extern int copy_fd(int ifd, int ofd);
extern int copy_file(const char *dst, const char *src, int mode);
extern int copy_file_with_time(const char *dst, const char *src, int mode);
extern void write_or_die(int fd, const void *buf, size_t count);
extern int write_or_whine(int fd, const void *buf, size_t count, const char *msg);
extern int write_or_whine_pipe(int fd, const void *buf, size_t count, const char *msg);
extern void fsync_or_die(int fd, const char *);

extern ssize_t read_in_full(int fd, void *buf, size_t count);
extern ssize_t write_in_full(int fd, const void *buf, size_t count);
static inline ssize_t write_str_in_full(int fd, const char *str)
{
	return write_in_full(fd, str, strlen(str));
}

/* pager.c */
extern void setup_pager(void);
extern const char *pager_program;
extern int pager_in_use(void);
extern int pager_use_color;

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

/* trace.c */
__attribute__((format (printf, 1, 2)))
extern void trace_printf(const char *format, ...);
extern void trace_vprintf(const char *key, const char *format, va_list ap);
__attribute__((format (printf, 2, 3)))
extern void trace_argv_printf(const char **argv, const char *format, ...);
extern void trace_repo_setup(const char *prefix);
extern int trace_want(const char *key);
extern void trace_strbuf(const char *key, const struct strbuf *buf);

void packet_trace_identity(const char *prog);

/* add */
/*
 * return 0 if success, 1 - if addition of a file failed and
 * ADD_FILES_IGNORE_ERRORS was specified in flags
 */
int add_files_to_cache(const char *prefix, const char **pathspec, int flags);

/* diff.c */
extern int diff_auto_refresh_index;

/* match-trees.c */
void shift_tree(const unsigned char *, const unsigned char *, unsigned char *, int);
void shift_tree_by(const unsigned char *, const unsigned char *, unsigned char *, const char *);

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
int report_path_error(const char *ps_matched, const char **pathspec, int prefix_offset);
void overlay_tree_on_cache(const char *tree_name, const char *prefix);

char *alias_lookup(const char *alias);
int split_cmdline(char *cmdline, const char ***argv);
/* Takes a negative value returned by split_cmdline */
const char *split_cmdline_strerror(int cmdline_errno);

/* git.c */
struct startup_info {
	int have_repository;
	const char *prefix;
};
extern struct startup_info *startup_info;

/* builtin/merge.c */
int checkout_fast_forward(const unsigned char *from, const unsigned char *to);

#endif /* CACHE_H */
