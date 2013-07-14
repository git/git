#ifndef PATHSPEC_H
#define PATHSPEC_H

#define PATHSPEC_ONESTAR 1	/* the pathspec pattern sastisfies GFNM_ONESTAR */

struct pathspec {
	const char **raw; /* get_pathspec() result, not freed by free_pathspec() */
	int nr;
	unsigned int has_wildcard:1;
	unsigned int recursive:1;
	int max_depth;
	struct pathspec_item {
		const char *match;
		int len;
		int nowildcard_len;
		int flags;
	} *items;
};

extern int init_pathspec(struct pathspec *, const char **);
extern void copy_pathspec(struct pathspec *dst, const struct pathspec *src);
extern void free_pathspec(struct pathspec *);

extern int limit_pathspec_to_literal(void);

extern char *find_pathspecs_matching_against_index(const char **pathspec);
extern void add_pathspec_matches_against_index(const char **pathspec, char *seen, int specs);
extern const char *check_path_for_gitlink(const char *path);
extern void die_if_path_beyond_symlink(const char *path, const char *prefix);

#endif /* PATHSPEC_H */
