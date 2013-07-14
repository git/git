#ifndef PATHSPEC_H
#define PATHSPEC_H

/* Pathspec magic */
#define PATHSPEC_FROMTOP	(1<<0)
#define PATHSPEC_ALL_MAGIC PATHSPEC_FROMTOP

#define PATHSPEC_ONESTAR 1	/* the pathspec pattern sastisfies GFNM_ONESTAR */

struct pathspec {
	const char **raw; /* get_pathspec() result, not freed by free_pathspec() */
	int nr;
	unsigned int has_wildcard:1;
	unsigned int recursive:1;
	unsigned magic;
	int max_depth;
	struct pathspec_item {
		const char *match;
		const char *original;
		unsigned magic;
		int len;
		int nowildcard_len;
		int flags;
	} *items;
};

/* parse_pathspec flags */
#define PATHSPEC_PREFER_CWD (1<<0) /* No args means match cwd */
#define PATHSPEC_PREFER_FULL (1<<1) /* No args means match everything */

extern int init_pathspec(struct pathspec *, const char **);
extern void parse_pathspec(struct pathspec *pathspec,
			   unsigned magic_mask,
			   unsigned flags,
			   const char *prefix,
			   const char **args);
extern void copy_pathspec(struct pathspec *dst, const struct pathspec *src);
extern void free_pathspec(struct pathspec *);

extern int limit_pathspec_to_literal(void);

extern char *find_pathspecs_matching_against_index(const char **pathspec);
extern void add_pathspec_matches_against_index(const char **pathspec, char *seen, int specs);
extern const char *check_path_for_gitlink(const char *path);
extern void die_if_path_beyond_symlink(const char *path, const char *prefix);

#endif /* PATHSPEC_H */
