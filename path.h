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
 * The `git_common_path` family of functions will construct a path into a
 * repository's common git directory, which is shared by all worktrees.
 */

/*
 * Constructs a path into the common git directory of repository `repo` and
 * append it in the provided buffer `sb`.
 */
void strbuf_git_common_path(struct strbuf *sb,
			    const struct repository *repo,
			    const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * Return a statically allocated path into the main repository's
 * (the_repository) common git directory.
 */
const char *git_common_path(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));


/*
 * The `git_path` family of functions will construct a path into a repository's
 * git directory.
 *
 * These functions will perform adjustments to the resultant path to account
 * for special paths which are either considered common among worktrees (e.g.
 * paths into the object directory) or have been explicitly set via an
 * environment variable or config (e.g. path to the index file).
 *
 * For an exhaustive list of the adjustments made look at `common_list` and
 * `adjust_git_path` in path.c.
 */

/*
 * Return a path into the git directory of repository `repo`.
 */
char *repo_git_path(const struct repository *repo,
		    const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

/*
 * Construct a path into the git directory of repository `repo` and append it
 * to the provided buffer `sb`.
 */
void strbuf_repo_git_path(struct strbuf *sb,
			  const struct repository *repo,
			  const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * Return a statically allocated path into the main repository's
 * (the_repository) git directory.
 */
const char *git_path(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

/*
 * Return a path into the main repository's (the_repository) git directory.
 */
char *git_pathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

/*
 * Construct a path into the main repository's (the_repository) git directory
 * and place it in the provided buffer `buf`, the contents of the buffer will
 * be overridden.
 */
char *git_path_buf(struct strbuf *buf, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

/*
 * Construct a path into the main repository's (the_repository) git directory
 * and append it to the provided buffer `sb`.
 */
void strbuf_git_path(struct strbuf *sb, const char *fmt, ...)
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
 * Return a path into a submodule's git directory located at `path`.  `path`
 * must only reference a submodule of the main repository (the_repository).
 */
char *git_pathdup_submodule(const char *path, const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

/*
 * Construct a path into a submodule's git directory located at `path` and
 * append it to the provided buffer `sb`.  `path` must only reference a
 * submodule of the main repository (the_repository).
 */
int strbuf_git_path_submodule(struct strbuf *sb, const char *path,
				     const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

void report_linked_checkout_garbage(void);

/*
 * You can define a static memoized git path like:
 *
 *    static GIT_PATH_FUNC(git_path_foo, "FOO")
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

#define REPO_GIT_PATH_FUNC(var, filename) \
	const char *git_path_##var(struct repository *r) \
	{ \
		if (!r->cached_paths.var) \
			r->cached_paths.var = repo_git_path(r, filename); \
		return r->cached_paths.var; \
	}

struct path_cache {
	const char *squash_msg;
	const char *merge_msg;
	const char *merge_rr;
	const char *merge_mode;
	const char *merge_head;
	const char *merge_autostash;
	const char *auto_merge;
	const char *fetch_head;
	const char *shallow;
};

#define PATH_CACHE_INIT                                        \
	{                                                      \
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL \
	}

const char *git_path_squash_msg(struct repository *r);
const char *git_path_merge_msg(struct repository *r);
const char *git_path_merge_rr(struct repository *r);
const char *git_path_merge_mode(struct repository *r);
const char *git_path_merge_head(struct repository *r);
const char *git_path_merge_autostash(struct repository *r);
const char *git_path_auto_merge(struct repository *r);
const char *git_path_fetch_head(struct repository *r);
const char *git_path_shallow(struct repository *r);


int ends_with_path_components(const char *path, const char *components);

#endif /* PATH_H */
