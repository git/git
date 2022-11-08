#ifndef DIR_H
#define DIR_H

#include "cache.h"
#include "hashmap.h"
#include "strbuf.h"

/**
 * The directory listing API is used to enumerate paths in the work tree,
 * optionally taking `.git/info/exclude` and `.gitignore` files per directory
 * into account.
 */

/**
 * Calling sequence
 * ----------------
 *
 * Note: The index may be checked for .gitignore files that are
 * CE_SKIP_WORKTREE marked. If you want to exclude files, make sure you have
 * loaded the index first.
 *
 * - Prepare `struct dir_struct dir` using `dir_init()` function.
 *
 * - To add single exclude pattern, call `add_pattern_list()` and then
 *   `add_pattern()`.
 *
 * - To add patterns from a file (e.g. `.git/info/exclude`), call
 *   `add_patterns_from_file()` , and/or set `dir.exclude_per_dir`.
 *
 * - A short-hand function `setup_standard_excludes()` can be used to set
 *   up the standard set of exclude settings, instead of manually calling
 *   the add_pattern*() family of functions.
 *
 * - Call `fill_directory()`.
 *
 * - Use `dir.entries[]` and `dir.ignored[]`.
 *
 * - Call `dir_clear()` when the contained elements are no longer in use.
 *
 */

struct dir_entry {
	unsigned int len;
	char name[FLEX_ARRAY]; /* more */
};

#define PATTERN_FLAG_NODIR 1
#define PATTERN_FLAG_ENDSWITH 4
#define PATTERN_FLAG_MUSTBEDIR 8
#define PATTERN_FLAG_NEGATIVE 16

struct path_pattern {
	/*
	 * This allows callers of last_matching_pattern() etc.
	 * to determine the origin of the matching pattern.
	 */
	struct pattern_list *pl;

	const char *pattern;
	int patternlen;
	int nowildcardlen;
	const char *base;
	int baselen;
	unsigned flags;		/* PATTERN_FLAG_* */

	/*
	 * Counting starts from 1 for line numbers in ignore files,
	 * and from -1 decrementing for patterns from CLI args.
	 */
	int srcpos;
};

/* used for hashmaps for cone patterns */
struct pattern_entry {
	struct hashmap_entry ent;
	char *pattern;
	size_t patternlen;
};

/*
 * Each excludes file will be parsed into a fresh exclude_list which
 * is appended to the relevant exclude_list_group (either EXC_DIRS or
 * EXC_FILE).  An exclude_list within the EXC_CMDL exclude_list_group
 * can also be used to represent the list of --exclude values passed
 * via CLI args.
 */
struct pattern_list {
	int nr;
	int alloc;

	/* remember pointer to exclude file contents so we can free() */
	char *filebuf;

	/* origin of list, e.g. path to filename, or descriptive string */
	const char *src;

	struct path_pattern **patterns;

	/*
	 * While scanning the excludes, we attempt to match the patterns
	 * with a more restricted set that allows us to use hashsets for
	 * matching logic, which is faster than the linear lookup in the
	 * excludes array above. If non-zero, that check succeeded.
	 */
	unsigned use_cone_patterns;
	unsigned full_cone;

	/*
	 * Stores paths where everything starting with those paths
	 * is included.
	 */
	struct hashmap recursive_hashmap;

	/*
	 * Used to check single-level parents of blobs.
	 */
	struct hashmap parent_hashmap;
};

/*
 * The contents of the per-directory exclude files are lazily read on
 * demand and then cached in memory, one per exclude_stack struct, in
 * order to avoid opening and parsing each one every time that
 * directory is traversed.
 */
struct exclude_stack {
	struct exclude_stack *prev; /* the struct exclude_stack for the parent directory */
	int baselen;
	int exclude_ix; /* index of exclude_list within EXC_DIRS exclude_list_group */
	struct untracked_cache_dir *ucd;
};

struct exclude_list_group {
	int nr, alloc;
	struct pattern_list *pl;
};

struct oid_stat {
	struct stat_data stat;
	struct object_id oid;
	int valid;
};

/*
 *  Untracked cache
 *
 *  The following inputs are sufficient to determine what files in a
 *  directory are excluded:
 *
 *   - The list of files and directories of the directory in question
 *   - The $GIT_DIR/index
 *   - dir_struct flags
 *   - The content of $GIT_DIR/info/exclude
 *   - The content of core.excludesfile
 *   - The content (or the lack) of .gitignore of all parent directories
 *     from $GIT_WORK_TREE
 *   - The check_only flag in read_directory_recursive (for
 *     DIR_HIDE_EMPTY_DIRECTORIES)
 *
 *  The first input can be checked using directory mtime. In many
 *  filesystems, directory mtime (stat_data field) is updated when its
 *  files or direct subdirs are added or removed.
 *
 *  The second one can be hooked from cache_tree_invalidate_path().
 *  Whenever a file (or a submodule) is added or removed from a
 *  directory, we invalidate that directory.
 *
 *  The remaining inputs are easy, their SHA-1 could be used to verify
 *  their contents (exclude_sha1[], info_exclude_sha1[] and
 *  excludes_file_sha1[])
 */
struct untracked_cache_dir {
	struct untracked_cache_dir **dirs;
	char **untracked;
	struct stat_data stat_data;
	unsigned int untracked_alloc, dirs_nr, dirs_alloc;
	unsigned int untracked_nr;
	unsigned int check_only : 1;
	/* all data except 'dirs' in this struct are good */
	unsigned int valid : 1;
	unsigned int recurse : 1;
	/* null object ID means this directory does not have .gitignore */
	struct object_id exclude_oid;
	char name[FLEX_ARRAY];
};

struct untracked_cache {
	struct oid_stat ss_info_exclude;
	struct oid_stat ss_excludes_file;
	const char *exclude_per_dir;
	char *exclude_per_dir_to_free;
	struct strbuf ident;
	/*
	 * dir_struct#flags must match dir_flags or the untracked
	 * cache is ignored.
	 */
	unsigned dir_flags;
	struct untracked_cache_dir *root;
	/* Statistics */
	int dir_created;
	int gitignore_invalidated;
	int dir_invalidated;
	int dir_opened;
	/* fsmonitor invalidation data */
	unsigned int use_fsmonitor : 1;
};

/**
 * structure is used to pass directory traversal options to the library and to
 * record the paths discovered. A single `struct dir_struct` is used regardless
 * of whether or not the traversal recursively descends into subdirectories.
 */
struct dir_struct {

	/* The number of members in `entries[]` array. */
	int nr;

	/* Internal use; keeps track of allocation of `entries[]` array.*/
	int alloc;

	/* The number of members in `ignored[]` array. */
	int ignored_nr;

	int ignored_alloc;

	/* bit-field of options */
	enum {

		/**
		 * Return just ignored files in `entries[]`, not untracked files.
		 * This flag is mutually exclusive with `DIR_SHOW_IGNORED_TOO`.
		 */
		DIR_SHOW_IGNORED = 1<<0,

		/* Include a directory that is not tracked. */
		DIR_SHOW_OTHER_DIRECTORIES = 1<<1,

		/* Do not include a directory that is not tracked and is empty. */
		DIR_HIDE_EMPTY_DIRECTORIES = 1<<2,

		/**
		 * If set, recurse into a directory that looks like a Git directory.
		 * Otherwise it is shown as a directory.
		 */
		DIR_NO_GITLINKS = 1<<3,

		/**
		 * Special mode for git-add. Return ignored files in `ignored[]` and
		 * untracked files in `entries[]`. Only returns ignored files that match
		 * pathspec exactly (no wildcards). Does not recurse into ignored
		 * directories.
		 */
		DIR_COLLECT_IGNORED = 1<<4,

		/**
		 * Similar to `DIR_SHOW_IGNORED`, but return ignored files in
		 * `ignored[]` in addition to untracked files in `entries[]`.
		 * This flag is mutually exclusive with `DIR_SHOW_IGNORED`.
		 */
		DIR_SHOW_IGNORED_TOO = 1<<5,

		DIR_COLLECT_KILLED_ONLY = 1<<6,

		/**
		 * Only has meaning if `DIR_SHOW_IGNORED_TOO` is also set; if this is
		 * set, the untracked contents of untracked directories are also
		 * returned in `entries[]`.
		 */
		DIR_KEEP_UNTRACKED_CONTENTS = 1<<7,

		/**
		 * Only has meaning if `DIR_SHOW_IGNORED_TOO` is also set; if this is
		 * set, returns ignored files and directories that match an exclude
		 * pattern. If a directory matches an exclude pattern, then the
		 * directory is returned and the contained paths are not. A directory
		 * that does not match an exclude pattern will not be returned even if
		 * all of its contents are ignored. In this case, the contents are
		 * returned as individual entries.
		 *
		 * If this is set, files and directories that explicitly match an ignore
		 * pattern are reported. Implicitly ignored directories (directories that
		 * do not match an ignore pattern, but whose contents are all ignored)
		 * are not reported, instead all of the contents are reported.
		 */
		DIR_SHOW_IGNORED_TOO_MODE_MATCHING = 1<<8,

		DIR_SKIP_NESTED_GIT = 1<<9
	} flags;

	/* An array of `struct dir_entry`, each element of which describes a path. */
	struct dir_entry **entries;

	/**
	 * used for ignored paths with the `DIR_SHOW_IGNORED_TOO` and
	 * `DIR_COLLECT_IGNORED` flags.
	 */
	struct dir_entry **ignored;

	/**
	 * The name of the file to be read in each directory for excluded files
	 * (typically `.gitignore`).
	 */
	const char *exclude_per_dir;

	/*
	 * We maintain three groups of exclude pattern lists:
	 *
	 * EXC_CMDL lists patterns explicitly given on the command line.
	 * EXC_DIRS lists patterns obtained from per-directory ignore files.
	 * EXC_FILE lists patterns from fallback ignore files, e.g.
	 *   - .git/info/exclude
	 *   - core.excludesfile
	 *
	 * Each group contains multiple exclude lists, a single list
	 * per source.
	 */
#define EXC_CMDL 0
#define EXC_DIRS 1
#define EXC_FILE 2
	struct exclude_list_group exclude_list_group[3];

	/*
	 * Temporary variables which are used during loading of the
	 * per-directory exclude lists.
	 *
	 * exclude_stack points to the top of the exclude_stack, and
	 * basebuf contains the full path to the current
	 * (sub)directory in the traversal. Exclude points to the
	 * matching exclude struct if the directory is excluded.
	 */
	struct exclude_stack *exclude_stack;
	struct path_pattern *pattern;
	struct strbuf basebuf;

	/* Enable untracked file cache if set */
	struct untracked_cache *untracked;
	struct oid_stat ss_info_exclude;
	struct oid_stat ss_excludes_file;
	unsigned unmanaged_exclude_files;

	/* Stats about the traversal */
	unsigned visited_paths;
	unsigned visited_directories;
};

#define DIR_INIT { 0 }

struct dirent *readdir_skip_dot_and_dotdot(DIR *dirp);

/*Count the number of slashes for string s*/
int count_slashes(const char *s);

/*
 * The ordering of these constants is significant, with
 * higher-numbered match types signifying "closer" (i.e. more
 * specific) matches which will override lower-numbered match types
 * when populating the seen[] array.
 */
#define MATCHED_RECURSIVELY 1
#define MATCHED_RECURSIVELY_LEADING_PATHSPEC 2
#define MATCHED_FNMATCH 3
#define MATCHED_EXACTLY 4
int simple_length(const char *match);
int no_wildcard(const char *string);
char *common_prefix(const struct pathspec *pathspec);
int match_pathspec(struct index_state *istate,
		   const struct pathspec *pathspec,
		   const char *name, int namelen,
		   int prefix, char *seen, int is_dir);
int report_path_error(const char *ps_matched, const struct pathspec *pathspec);
int within_depth(const char *name, int namelen, int depth, int max_depth);

int fill_directory(struct dir_struct *dir,
		   struct index_state *istate,
		   const struct pathspec *pathspec);
int read_directory(struct dir_struct *, struct index_state *istate,
		   const char *path, int len,
		   const struct pathspec *pathspec);

enum pattern_match_result {
	UNDECIDED = -1,
	NOT_MATCHED = 0,
	MATCHED = 1,
	MATCHED_RECURSIVE = 2,
};

/*
 * Scan the list of patterns to determine if the ordered list
 * of patterns matches on 'pathname'.
 *
 * Return 1 for a match, 0 for not matched and -1 for undecided.
 */
enum pattern_match_result path_matches_pattern_list(const char *pathname,
				int pathlen,
				const char *basename, int *dtype,
				struct pattern_list *pl,
				struct index_state *istate);

int init_sparse_checkout_patterns(struct index_state *state);

int path_in_sparse_checkout(const char *path,
			    struct index_state *istate);
int path_in_cone_mode_sparse_checkout(const char *path,
				      struct index_state *istate);

struct dir_entry *dir_add_ignored(struct dir_struct *dir,
				  struct index_state *istate,
				  const char *pathname, int len);

/*
 * these implement the matching logic for dir.c:excluded_from_list and
 * attr.c:path_matches()
 */
int match_basename(const char *, int,
		   const char *, int, int, unsigned);
int match_pathname(const char *, int,
		   const char *, int,
		   const char *, int, int);

struct path_pattern *last_matching_pattern(struct dir_struct *dir,
					   struct index_state *istate,
					   const char *name, int *dtype);

int is_excluded(struct dir_struct *dir,
		struct index_state *istate,
		const char *name, int *dtype);

int pl_hashmap_cmp(const void *unused_cmp_data,
		   const struct hashmap_entry *a,
		   const struct hashmap_entry *b,
		   const void *key);
int hashmap_contains_parent(struct hashmap *map,
			    const char *path,
			    struct strbuf *buffer);
struct pattern_list *add_pattern_list(struct dir_struct *dir,
				      int group_type, const char *src);
int add_patterns_from_file_to_list(const char *fname, const char *base, int baselen,
				   struct pattern_list *pl, struct index_state *istate,
				   unsigned flags);
void add_patterns_from_file(struct dir_struct *, const char *fname);
int add_patterns_from_blob_to_list(struct object_id *oid,
				   const char *base, int baselen,
				   struct pattern_list *pl);
void parse_path_pattern(const char **string, int *patternlen, unsigned *flags, int *nowildcardlen);
void add_pattern(const char *string, const char *base,
		 int baselen, struct pattern_list *pl, int srcpos);
void clear_pattern_list(struct pattern_list *pl);
void dir_clear(struct dir_struct *dir);

int repo_file_exists(struct repository *repo, const char *path);
int file_exists(const char *);

int is_inside_dir(const char *dir);
int dir_inside_of(const char *subdir, const char *dir);

static inline int is_dot_or_dotdot(const char *name)
{
	return (name[0] == '.' &&
		(name[1] == '\0' ||
		 (name[1] == '.' && name[2] == '\0')));
}

int is_empty_dir(const char *dir);

/*
 * Retrieve the "humanish" basename of the given Git URL.
 *
 * For example:
 * 	/path/to/repo.git => "repo"
 * 	host.xz:foo/.git => "foo"
 * 	http://example.com/user/bar.baz => "bar.baz"
 */
char *git_url_basename(const char *repo, int is_bundle, int is_bare);
void strip_dir_trailing_slashes(char *dir);

void setup_standard_excludes(struct dir_struct *dir);

char *get_sparse_checkout_filename(void);
int get_sparse_checkout_patterns(struct pattern_list *pl);

/* Constants for remove_dir_recursively: */

/*
 * If a non-directory is found within path, stop and return an error.
 * (In this case some empty directories might already have been
 * removed.)
 */
#define REMOVE_DIR_EMPTY_ONLY 01

/*
 * If any Git work trees are found within path, skip them without
 * considering it an error.
 */
#define REMOVE_DIR_KEEP_NESTED_GIT 02

/* Remove the contents of path, but leave path itself. */
#define REMOVE_DIR_KEEP_TOPLEVEL 04

/* Remove the_original_cwd too */
#define REMOVE_DIR_PURGE_ORIGINAL_CWD 0x08

/*
 * Remove path and its contents, recursively. flags is a combination
 * of the above REMOVE_DIR_* constants. Return 0 on success.
 *
 * This function uses path as temporary scratch space, but restores it
 * before returning.
 */
int remove_dir_recursively(struct strbuf *path, int flag);

/*
 * Tries to remove the path, along with leading empty directories so long as
 * those empty directories are not startup_info->original_cwd.  Ignores
 * ENOENT.
 */
int remove_path(const char *path);

int fspathcmp(const char *a, const char *b);
int fspatheq(const char *a, const char *b);
int fspathncmp(const char *a, const char *b, size_t count);
unsigned int fspathhash(const char *str);

/*
 * The prefix part of pattern must not contains wildcards.
 */
struct pathspec_item;
int git_fnmatch(const struct pathspec_item *item,
		const char *pattern, const char *string,
		int prefix);

int submodule_path_match(struct index_state *istate,
			 const struct pathspec *ps,
			 const char *submodule_name,
			 char *seen);

static inline int ce_path_match(struct index_state *istate,
				const struct cache_entry *ce,
				const struct pathspec *pathspec,
				char *seen)
{
	return match_pathspec(istate, pathspec, ce->name, ce_namelen(ce), 0, seen,
			      S_ISDIR(ce->ce_mode) || S_ISGITLINK(ce->ce_mode));
}

static inline int dir_path_match(struct index_state *istate,
				 const struct dir_entry *ent,
				 const struct pathspec *pathspec,
				 int prefix, char *seen)
{
	int has_trailing_dir = ent->len && ent->name[ent->len - 1] == '/';
	int len = has_trailing_dir ? ent->len - 1 : ent->len;
	return match_pathspec(istate, pathspec, ent->name, len, prefix, seen,
			      has_trailing_dir);
}

int cmp_dir_entry(const void *p1, const void *p2);
int check_dir_entry_contains(const struct dir_entry *out, const struct dir_entry *in);

void untracked_cache_invalidate_path(struct index_state *, const char *, int safe_path);
void untracked_cache_remove_from_index(struct index_state *, const char *);
void untracked_cache_add_to_index(struct index_state *, const char *);

void free_untracked_cache(struct untracked_cache *);
struct untracked_cache *read_untracked_extension(const void *data, unsigned long sz);
void write_untracked_extension(struct strbuf *out, struct untracked_cache *untracked);
void add_untracked_cache(struct index_state *istate);
void remove_untracked_cache(struct index_state *istate);

/*
 * Connect a worktree to a git directory by creating (or overwriting) a
 * '.git' file containing the location of the git directory. In the git
 * directory set the core.worktree setting to indicate where the worktree is.
 * When `recurse_into_nested` is set, recurse into any nested submodules,
 * connecting them as well.
 */
void connect_work_tree_and_git_dir(const char *work_tree,
				   const char *git_dir,
				   int recurse_into_nested);
void relocate_gitdir(const char *path,
		     const char *old_git_dir,
		     const char *new_git_dir);

/**
 * The "enum path_matches_kind" determines how path_match_flags() will
 * behave. The flags come in sets, and one (and only one) must be
 * provided out of each "set":
 *
 * PATH_MATCH_NATIVE:
 *	Path separator is is_dir_sep()
 * PATH_MATCH_XPLATFORM:
 *	Path separator is is_xplatform_dir_sep()
 *
 * Do we use is_dir_sep() to check for a directory separator
 * (*_NATIVE), or do we always check for '/' or '\' (*_XPLATFORM). The
 * "*_NATIVE" version on Windows is the same as "*_XPLATFORM",
 * everywhere else "*_NATIVE" means "only /".
 *
 * PATH_MATCH_STARTS_WITH_DOT_SLASH:
 *	Match a path starting with "./"
 * PATH_MATCH_STARTS_WITH_DOT_DOT_SLASH:
 *	Match a path starting with "../"
 *
 * The "/" in the above is adjusted based on the "*_NATIVE" and
 * "*_XPLATFORM" flags.
 */
enum path_match_flags {
	PATH_MATCH_NATIVE = 1 << 0,
	PATH_MATCH_XPLATFORM = 1 << 1,
	PATH_MATCH_STARTS_WITH_DOT_SLASH = 1 << 2,
	PATH_MATCH_STARTS_WITH_DOT_DOT_SLASH = 1 << 3,
};
#define PATH_MATCH_KINDS_MASK (PATH_MATCH_STARTS_WITH_DOT_SLASH | \
	PATH_MATCH_STARTS_WITH_DOT_DOT_SLASH)
#define PATH_MATCH_PLATFORM_MASK (PATH_MATCH_NATIVE | PATH_MATCH_XPLATFORM)

/**
 * path_match_flags() checks if a given "path" matches a given "enum
 * path_match_flags" criteria.
 */
int path_match_flags(const char *const path, const enum path_match_flags f);

/**
 * starts_with_dot_slash_native(): convenience wrapper for
 * path_match_flags() with PATH_MATCH_STARTS_WITH_DOT_SLASH and
 * PATH_MATCH_NATIVE.
 */
static inline int starts_with_dot_slash_native(const char *const path)
{
	const enum path_match_flags what = PATH_MATCH_STARTS_WITH_DOT_SLASH;

	return path_match_flags(path, what | PATH_MATCH_NATIVE);
}

/**
 * starts_with_dot_slash_native(): convenience wrapper for
 * path_match_flags() with PATH_MATCH_STARTS_WITH_DOT_DOT_SLASH and
 * PATH_MATCH_NATIVE.
 */
static inline int starts_with_dot_dot_slash_native(const char *const path)
{
	const enum path_match_flags what = PATH_MATCH_STARTS_WITH_DOT_DOT_SLASH;

	return path_match_flags(path, what | PATH_MATCH_NATIVE);
}
#endif
