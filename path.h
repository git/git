#ifndef PATH_H
#define PATH_H

struct repository;
struct strbuf;

/*
 * The result to all functions which return statically allocated memory may be
 * overwritten by another call to _any_ one of these functions. Consider using
 * the safer variants which operate on strbufs or return allocated memory.
 */

/*
 * Return a statically allocated path.
 */
const char *mkpath(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

/*
 * Return a path.
 */
char *mkpathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

/*
 * Construct a path and place the result in the provided buffer `buf`.
 */
char *mksnpath(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * The `but_common_path` family of functions will construct a path into a
 * repository's common but directory, which is shared by all worktrees.
 */

/*
 * Constructs a path into the common but directory of repository `repo` and
 * append it in the provided buffer `sb`.
 */
void strbuf_but_common_path(struct strbuf *sb,
			    const struct repository *repo,
			    const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * Return a statically allocated path into the main repository's
 * (the_repository) common but directory.
 */
const char *but_common_path(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));


/*
 * The `but_path` family of functions will construct a path into a repository's
 * but directory.
 *
 * These functions will perform adjustments to the resultant path to account
 * for special paths which are either considered common among worktrees (e.g.
 * paths into the object directory) or have been explicitly set via an
 * environment variable or config (e.g. path to the index file).
 *
 * For an exhaustive list of the adjustments made look at `common_list` and
 * `adjust_but_path` in path.c.
 */

/*
 * Return a path into the but directory of repository `repo`.
 */
char *repo_but_path(const struct repository *repo,
		    const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

/*
 * Construct a path into the but directory of repository `repo` and append it
 * to the provided buffer `sb`.
 */
void strbuf_repo_but_path(struct strbuf *sb,
			  const struct repository *repo,
			  const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * Return a statically allocated path into the main repository's
 * (the_repository) but directory.
 */
const char *but_path(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

/*
 * Return a path into the main repository's (the_repository) but directory.
 */
char *but_pathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

/*
 * Construct a path into the main repository's (the_repository) but directory
 * and place it in the provided buffer `buf`, the contents of the buffer will
 * be overridden.
 */
char *but_path_buf(struct strbuf *buf, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

/*
 * Construct a path into the main repository's (the_repository) but directory
 * and append it to the provided buffer `sb`.
 */
void strbuf_but_path(struct strbuf *sb, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

/*
 * Return a path into the worktree of repository `repo`.
 *
 * If the repository doesn't have a worktree NULL is returned.
 */
char *repo_worktree_path(const struct repository *repo,
				const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

/*
 * Construct a path into the worktree of repository `repo` and append it
 * to the provided buffer `sb`.
 *
 * If the repository doesn't have a worktree nothing will be appended to `sb`.
 */
void strbuf_repo_worktree_path(struct strbuf *sb,
				      const struct repository *repo,
				      const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * Return a path into a submodule's but directory located at `path`.  `path`
 * must only reference a submodule of the main repository (the_repository).
 */
char *but_pathdup_submodule(const char *path, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

/*
 * Construct a path into a submodule's but directory located at `path` and
 * append it to the provided buffer `sb`.  `path` must only reference a
 * submodule of the main repository (the_repository).
 */
int strbuf_but_path_submodule(struct strbuf *sb, const char *path,
				     const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

void report_linked_checkout_garbage(void);

/*
 * You can define a static memoized but path like:
 *
 *    static BUT_PATH_FUNC(but_path_foo, "FOO")
 *
 * or use one of the global ones below.
 */
#define BUT_PATH_FUNC(func, filename) \
	const char *func(void) \
	{ \
		static char *ret; \
		if (!ret) \
			ret = but_pathdup(filename); \
		return ret; \
	}

#define REPO_BUT_PATH_FUNC(var, filename) \
	const char *but_path_##var(struct repository *r) \
	{ \
		if (!r->cached_paths.var) \
			r->cached_paths.var = repo_but_path(r, filename); \
		return r->cached_paths.var; \
	}

const char *but_path_squash_msg(struct repository *r);
const char *but_path_merge_msg(struct repository *r);
const char *but_path_merge_rr(struct repository *r);
const char *but_path_merge_mode(struct repository *r);
const char *but_path_merge_head(struct repository *r);
const char *but_path_merge_autostash(struct repository *r);
const char *but_path_auto_merge(struct repository *r);
const char *but_path_fetch_head(struct repository *r);
const char *but_path_shallow(struct repository *r);


int ends_with_path_components(const char *path, const char *components);

#endif /* PATH_H */
