#ifndef DIR_H
#define DIR_H

/* See Documentation/technical/api-directory-listing.txt */

#include "cache.h"
#include "strbuf.h"

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

struct dir_struct {
	int nr, alloc;
	int ignored_nr, ignored_alloc;
	enum {
		DIR_SHOW_IGNORED = 1<<0,
		DIR_SHOW_OTHER_DIRECTORIES = 1<<1,
		DIR_HIDE_EMPTY_DIRECTORIES = 1<<2,
		DIR_NO_GITLINKS = 1<<3,
		DIR_COLLECT_IGNORED = 1<<4,
		DIR_SHOW_IGNORED_TOO = 1<<5,
		DIR_COLLECT_KILLED_ONLY = 1<<6,
		DIR_KEEP_UNTRACKED_CONTENTS = 1<<7,
		DIR_SHOW_IGNORED_TOO_MODE_MATCHING = 1<<8,
		DIR_SKIP_NESTED_GIT = 1<<9
	} flags;
	struct dir_entry **entries;
	struct dir_entry **ignored;

	/* Exclude info */
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
};

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
int match_pathspec(const struct index_state *istate,
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
		   const char *, int, int, unsigned);

struct path_pattern *last_matching_pattern(struct dir_struct *dir,
					   struct index_state *istate,
					   const char *name, int *dtype);

int is_excluded(struct dir_struct *dir,
		struct index_state *istate,
		const char *name, int *dtype);

struct pattern_list *add_pattern_list(struct dir_struct *dir,
				      int group_type, const char *src);
int add_patterns_from_file_to_list(const char *fname, const char *base, int baselen,
				   struct pattern_list *pl, struct  index_state *istate);
void add_patterns_from_file(struct dir_struct *, const char *fname);
int add_patterns_from_blob_to_list(struct object_id *oid,
				   const char *base, int baselen,
				   struct pattern_list *pl);
void parse_path_pattern(const char **string, int *patternlen, unsigned *flags, int *nowildcardlen);
void add_pattern(const char *string, const char *base,
		 int baselen, struct pattern_list *pl, int srcpos);
void clear_pattern_list(struct pattern_list *pl);
void clear_directory(struct dir_struct *dir);

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

void setup_standard_excludes(struct dir_struct *dir);


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

/*
 * Remove path and its contents, recursively. flags is a combination
 * of the above REMOVE_DIR_* constants. Return 0 on success.
 *
 * This function uses path as temporary scratch space, but restores it
 * before returning.
 */
int remove_dir_recursively(struct strbuf *path, int flag);

/* tries to remove the path with empty directories along it, ignores ENOENT */
int remove_path(const char *path);

int fspathcmp(const char *a, const char *b);
int fspathncmp(const char *a, const char *b, size_t count);

/*
 * The prefix part of pattern must not contains wildcards.
 */
struct pathspec_item;
int git_fnmatch(const struct pathspec_item *item,
		const char *pattern, const char *string,
		int prefix);

int submodule_path_match(const struct index_state *istate,
			 const struct pathspec *ps,
			 const char *submodule_name,
			 char *seen);

static inline int ce_path_match(const struct index_state *istate,
				const struct cache_entry *ce,
				const struct pathspec *pathspec,
				char *seen)
{
	return match_pathspec(istate, pathspec, ce->name, ce_namelen(ce), 0, seen,
			      S_ISDIR(ce->ce_mode) || S_ISGITLINK(ce->ce_mode));
}

static inline int dir_path_match(const struct index_state *istate,
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
#endif
