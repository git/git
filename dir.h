#ifndef DIR_H
#define DIR_H

struct dir_entry {
	unsigned int len;
	char name[FLEX_ARRAY]; /* more */
};

#define EXC_FLAG_NODIR 1
#define EXC_FLAG_NOWILDCARD 2
#define EXC_FLAG_ENDSWITH 4
#define EXC_FLAG_MUSTBEDIR 8

struct exclude_list {
	int nr;
	int alloc;
	struct exclude {
		const char *pattern;
		int patternlen;
		const char *base;
		int baselen;
		int to_exclude;
		int flags;
	} **excludes;
};

struct exclude_stack {
	struct exclude_stack *prev;
	char *filebuf;
	int baselen;
	int exclude_ix;
};

struct dir_struct {
	int nr, alloc;
	int ignored_nr, ignored_alloc;
	enum {
		DIR_SHOW_IGNORED = 1<<0,
		DIR_SHOW_OTHER_DIRECTORIES = 1<<1,
		DIR_HIDE_EMPTY_DIRECTORIES = 1<<2,
		DIR_NO_GITLINKS = 1<<3,
		DIR_COLLECT_IGNORED = 1<<4
	} flags;
	struct dir_entry **entries;
	struct dir_entry **ignored;

	/* Exclude info */
	const char *exclude_per_dir;
	struct exclude_list exclude_list[3];
	/*
	 * We maintain three exclude pattern lists:
	 * EXC_CMDL lists patterns explicitly given on the command line.
	 * EXC_DIRS lists patterns obtained from per-directory ignore files.
	 * EXC_FILE lists patterns from fallback ignore files.
	 */
#define EXC_CMDL 0
#define EXC_DIRS 1
#define EXC_FILE 2

	struct exclude_stack *exclude_stack;
	char basebuf[PATH_MAX];
};

extern int common_prefix(const char **pathspec);

#define MATCHED_RECURSIVELY 1
#define MATCHED_FNMATCH 2
#define MATCHED_EXACTLY 3
extern int match_pathspec(const char **pathspec, const char *name, int namelen, int prefix, char *seen);

extern int read_directory(struct dir_struct *, const char *path, const char *base, int baselen, const char **pathspec);

extern int excluded(struct dir_struct *, const char *, int *);
extern void add_excludes_from_file(struct dir_struct *, const char *fname);
extern void add_exclude(const char *string, const char *base,
			int baselen, struct exclude_list *which);
extern int file_exists(const char *);

extern char *get_relative_cwd(char *buffer, int size, const char *dir);
extern int is_inside_dir(const char *dir);

static inline int is_dot_or_dotdot(const char *name)
{
	return (name[0] == '.' &&
		(name[1] == '\0' ||
		 (name[1] == '.' && name[2] == '\0')));
}

extern int is_empty_dir(const char *dir);

extern void setup_standard_excludes(struct dir_struct *dir);
extern int remove_dir_recursively(struct strbuf *path, int only_empty);

/* tries to remove the path with empty directories along it, ignores ENOENT */
extern int remove_path(const char *path);

#endif
