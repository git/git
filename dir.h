#ifndef DIR_H
#define DIR_H

/* See Documentation/technical/api-directory-listing.txt */

#include "strbuf.h"

struct dir_entry {
	unsigned int len;
	char name[FLEX_ARRAY]; /* more */
};

#define EXC_FLAG_NODIR 1
#define EXC_FLAG_ENDSWITH 4
#define EXC_FLAG_MUSTBEDIR 8
#define EXC_FLAG_NEGATIVE 16

struct exclude {
	/*
	 * This allows callers of last_exclude_matching() etc.
	 * to determine the origin of the matching pattern.
	 */
	struct exclude_list *el;

	const char *pattern;
	int patternlen;
	int nowildcardlen;
	const char *base;
	int baselen;
	int flags;

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
struct exclude_list {
	int nr;
	int alloc;

	/* remember pointer to exclude file contents so we can free() */
	char *filebuf;

	/* origin of list, e.g. path to filename, or descriptive string */
	const char *src;

	struct exclude **excludes;
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
};

struct exclude_list_group {
	int nr, alloc;
	struct exclude_list *el;
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
		DIR_COLLECT_KILLED_ONLY = 1<<6
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
	struct exclude *exclude;
	struct strbuf basebuf;
};

/*
 * The ordering of these constants is significant, with
 * higher-numbered match types signifying "closer" (i.e. more
 * specific) matches which will override lower-numbered match types
 * when populating the seen[] array.
 */
#define MATCHED_RECURSIVELY 1
#define MATCHED_FNMATCH 2
#define MATCHED_EXACTLY 3
extern int simple_length(const char *match);
extern int no_wildcard(const char *string);
extern char *common_prefix(const struct pathspec *pathspec);
extern int match_pathspec(const struct pathspec *pathspec,
			  const char *name, int namelen,
			  int prefix, char *seen, int is_dir);
extern int within_depth(const char *name, int namelen, int depth, int max_depth);

extern int fill_directory(struct dir_struct *dir, const struct pathspec *pathspec);
extern int read_directory(struct dir_struct *, const char *path, int len, const struct pathspec *pathspec);

extern int is_excluded_from_list(const char *pathname, int pathlen, const char *basename,
				 int *dtype, struct exclude_list *el);
struct dir_entry *dir_add_ignored(struct dir_struct *dir, const char *pathname, int len);

/*
 * these implement the matching logic for dir.c:excluded_from_list and
 * attr.c:path_matches()
 */
extern int match_basename(const char *, int,
			  const char *, int, int, int);
extern int match_pathname(const char *, int,
			  const char *, int,
			  const char *, int, int, int);

extern struct exclude *last_exclude_matching(struct dir_struct *dir,
					     const char *name, int *dtype);

extern int is_excluded(struct dir_struct *dir, const char *name, int *dtype);

extern struct exclude_list *add_exclude_list(struct dir_struct *dir,
					     int group_type, const char *src);
extern int add_excludes_from_file_to_list(const char *fname, const char *base, int baselen,
					  struct exclude_list *el, int check_index);
extern void add_excludes_from_file(struct dir_struct *, const char *fname);
extern void parse_exclude_pattern(const char **string, int *patternlen, int *flags, int *nowildcardlen);
extern void add_exclude(const char *string, const char *base,
			int baselen, struct exclude_list *el, int srcpos);
extern void clear_exclude_list(struct exclude_list *el);
extern void clear_directory(struct dir_struct *dir);
extern int file_exists(const char *);

extern int is_inside_dir(const char *dir);
extern int dir_inside_of(const char *subdir, const char *dir);

static inline int is_dot_or_dotdot(const char *name)
{
	return (name[0] == '.' &&
		(name[1] == '\0' ||
		 (name[1] == '.' && name[2] == '\0')));
}

extern int is_empty_dir(const char *dir);

extern void setup_standard_excludes(struct dir_struct *dir);

#define REMOVE_DIR_EMPTY_ONLY 01
#define REMOVE_DIR_KEEP_NESTED_GIT 02
#define REMOVE_DIR_KEEP_TOPLEVEL 04
extern int remove_dir_recursively(struct strbuf *path, int flag);

/* tries to remove the path with empty directories along it, ignores ENOENT */
extern int remove_path(const char *path);

extern int strcmp_icase(const char *a, const char *b);
extern int strncmp_icase(const char *a, const char *b, size_t count);
extern int fnmatch_icase(const char *pattern, const char *string, int flags);

/*
 * The prefix part of pattern must not contains wildcards.
 */
struct pathspec_item;
extern int git_fnmatch(const struct pathspec_item *item,
		       const char *pattern, const char *string,
		       int prefix);

static inline int ce_path_match(const struct cache_entry *ce,
				const struct pathspec *pathspec,
				char *seen)
{
	return match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, seen,
			      S_ISDIR(ce->ce_mode) || S_ISGITLINK(ce->ce_mode));
}

static inline int dir_path_match(const struct dir_entry *ent,
				 const struct pathspec *pathspec,
				 int prefix, char *seen)
{
	int has_trailing_dir = ent->len && ent->name[ent->len - 1] == '/';
	int len = has_trailing_dir ? ent->len - 1 : ent->len;
	return match_pathspec(pathspec, ent->name, len, prefix, seen,
			      has_trailing_dir);
}

#endif
