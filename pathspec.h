#ifndef PATHSPEC_H
#define PATHSPEC_H

/* Pathspec magic */
#define PATHSPEC_FROMTOP	(1<<0)
#define PATHSPEC_MAXDEPTH	(1<<1)
#define PATHSPEC_LITERAL	(1<<2)
#define PATHSPEC_GLOB		(1<<3)
#define PATHSPEC_ICASE		(1<<4)
#define PATHSPEC_EXCLUDE	(1<<5)
#define PATHSPEC_ALL_MAGIC	  \
	(PATHSPEC_FROMTOP	| \
	 PATHSPEC_MAXDEPTH	| \
	 PATHSPEC_LITERAL	| \
	 PATHSPEC_GLOB		| \
	 PATHSPEC_ICASE		| \
	 PATHSPEC_EXCLUDE)

#define PATHSPEC_ONESTAR 1	/* the pathspec pattern satisfies GFNM_ONESTAR */

struct pathspec {
	const char **_raw; /* get_pathspec() result, not freed by clear_pathspec() */
	int nr;
	unsigned int has_wildcard:1;
	unsigned int recursive:1;
	unsigned magic;
	int max_depth;
	struct pathspec_item {
		const char *match;
		const char *original;
		unsigned magic;
		int len, prefix;
		int nowildcard_len;
		int flags;
	} *items;
};

#define GUARD_PATHSPEC(ps, mask) \
	do { \
		if ((ps)->magic & ~(mask))	       \
			die("BUG:%s:%d: unsupported magic %x",	\
			    __FILE__, __LINE__, (ps)->magic & ~(mask)); \
	} while (0)

/* parse_pathspec flags */
#define PATHSPEC_PREFER_CWD (1<<0) /* No args means match cwd */
#define PATHSPEC_PREFER_FULL (1<<1) /* No args means match everything */
#define PATHSPEC_MAXDEPTH_VALID (1<<2) /* max_depth field is valid */
/* strip the trailing slash if the given path is a gitlink */
#define PATHSPEC_STRIP_SUBMODULE_SLASH_CHEAP (1<<3)
/* die if a symlink is part of the given path's directory */
#define PATHSPEC_SYMLINK_LEADING_PATH (1<<4)
/*
 * This is like a combination of ..LEADING_PATH and .._SLASH_CHEAP
 * (but not the same): it strips the trailing slash if the given path
 * is a gitlink but also checks and dies if gitlink is part of the
 * leading path (i.e. the given path goes beyond a submodule). It's
 * safer than _SLASH_CHEAP and also more expensive.
 */
#define PATHSPEC_STRIP_SUBMODULE_SLASH_EXPENSIVE (1<<5)
#define PATHSPEC_PREFIX_ORIGIN (1<<6)
#define PATHSPEC_KEEP_ORDER (1<<7)
/*
 * For the callers that just need pure paths from somewhere else, not
 * from command line. Global --*-pathspecs options are ignored. No
 * magic is parsed in each pathspec either. If PATHSPEC_LITERAL is
 * allowed, then it will automatically set for every pathspec.
 */
#define PATHSPEC_LITERAL_PATH (1<<8)

extern void parse_pathspec(struct pathspec *pathspec,
			   unsigned magic_mask,
			   unsigned flags,
			   const char *prefix,
			   const char **args);
extern void copy_pathspec(struct pathspec *dst, const struct pathspec *src);
extern void clear_pathspec(struct pathspec *);

static inline int ps_strncmp(const struct pathspec_item *item,
			     const char *s1, const char *s2, size_t n)
{
	if (item->magic & PATHSPEC_ICASE)
		return strncasecmp(s1, s2, n);
	else
		return strncmp(s1, s2, n);
}

static inline int ps_strcmp(const struct pathspec_item *item,
			    const char *s1, const char *s2)
{
	if (item->magic & PATHSPEC_ICASE)
		return strcasecmp(s1, s2);
	else
		return strcmp(s1, s2);
}

extern char *find_pathspecs_matching_against_index(const struct pathspec *pathspec);
extern void add_pathspec_matches_against_index(const struct pathspec *pathspec, char *seen);

#endif /* PATHSPEC_H */
