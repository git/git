#ifndef PATH_H
#define PATH_H

struct repository;

/*
 * Return a statically allocated filename, either generically (mkpath), in
 * the repository directory (git_path), or in a submodule's repository
 * directory (git_path_submodule). In all cases, note that the result
 * may be overwritten by another call to _any_ of the functions. Consider
 * using the safer "dup" or "strbuf" formats below (in some cases, the
 * unsafe versions have already been removed).
 */
extern const char *mkpath(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern const char *git_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern const char *git_common_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));

extern char *mksnpath(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
extern void strbuf_git_path(struct strbuf *sb, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
extern void strbuf_git_common_path(struct strbuf *sb,
				   const struct repository *repo,
				   const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
extern char *git_path_buf(struct strbuf *buf, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
extern int strbuf_git_path_submodule(struct strbuf *sb, const char *path,
				     const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
extern char *git_pathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));
extern char *mkpathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));
extern char *git_pathdup_submodule(const char *path, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

extern char *repo_git_path(const struct repository *repo,
			   const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
extern void strbuf_repo_git_path(struct strbuf *sb,
				 const struct repository *repo,
				 const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

extern char *repo_worktree_path(const struct repository *repo,
				const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
extern void strbuf_repo_worktree_path(struct strbuf *sb,
				      const struct repository *repo,
				      const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

extern void report_linked_checkout_garbage(void);

/*
 * You can define a static memoized git path like:
 *
 *    static GIT_PATH_FUNC(git_path_foo, "FOO");
 *
 * or use one of the global ones below.
 */
#define GIT_PATH_FUNC(func, filename) \
	const char *func(void) \
	{ \
		static char *ret; \
		if (!ret) \
			ret = git_pathdup(filename); \
		return ret; \
	}

const char *git_path_cherry_pick_head(void);
const char *git_path_revert_head(void);
const char *git_path_squash_msg(void);
const char *git_path_merge_msg(void);
const char *git_path_merge_rr(void);
const char *git_path_merge_mode(void);
const char *git_path_merge_head(void);
const char *git_path_fetch_head(void);
const char *git_path_shallow(void);

#endif /* PATH_H */
