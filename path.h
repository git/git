#ifndef PATH_H
#define PATH_H

struct repository;
struct strbuf;
struct string_list;
struct worktree;

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
 * The `repo_common_path` family of functions will construct a path into a
 * repository's common git directory, which is shared by all worktrees.
 */
char *repo_common_path(const struct repository *repo,
		       const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
const char *repo_common_path_append(const struct repository *repo,
				    struct strbuf *sb,
				    const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
const char *repo_common_path_replace(const struct repository *repo,
				     struct strbuf *sb,
				     const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * The `repo_git_path` family of functions will construct a path into a repository's
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
char *repo_git_path(struct repository *repo,
		    const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
const char *repo_git_path_append(struct repository *repo,
				 struct strbuf *sb,
				 const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
const char *repo_git_path_replace(struct repository *repo,
				  struct strbuf *sb,
				  const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * Similar to repo_git_path() but can produce paths for a specified
 * worktree instead of current one. When no worktree is given, then the path is
 * computed relative to main worktree of the given repository.
 */
const char *worktree_git_path(struct repository *r,
			      const struct worktree *wt,
			      const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * The `repo_worktree_path` family of functions will construct a path into a
 * repository's worktree.
 *
 * Returns a `NULL` pointer in case the repository has no worktree.
 */
char *repo_worktree_path(const struct repository *repo,
				const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));
const char *repo_worktree_path_append(const struct repository *repo,
				      struct strbuf *sb,
				      const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
const char *repo_worktree_path_replace(const struct repository *repo,
				       struct strbuf *sb,
				       const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));

/*
 * The `repo_submodule_path` family of functions will construct a path into a
 * submodule's git directory located at `path`. `path` must be a submodule path
 * as found in the index and must be part of the given repository.
 *
 * Returns a `NULL` pointer in case the submodule cannot be found.
 */
char *repo_submodule_path(struct repository *repo,
			  const char *path,
			  const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
const char *repo_submodule_path_append(struct repository *repo,
				       struct strbuf *sb,
				       const char *path,
				       const char *fmt, ...)
	__attribute__((format (printf, 4, 5)));
const char *repo_submodule_path_replace(struct repository *repo,
					struct strbuf *sb,
					const char *path,
					const char *fmt, ...)
	__attribute__((format (printf, 4, 5)));

void report_linked_checkout_garbage(struct repository *r);

/*
 * You can define a static memoized git path like:
 *
 *    static REPO_GIT_PATH_FUNC(git_path_foo, "FOO")
 *
 * or use one of the global ones below.
 */
#define REPO_GIT_PATH_FUNC(var, filename) \
	const char *git_path_##var(struct repository *r) \
	{ \
		if (!r->cached_paths.var) \
			r->cached_paths.var = repo_git_path(r, filename); \
		return r->cached_paths.var; \
	}

const char *git_path_squash_msg(struct repository *r);
const char *git_path_merge_msg(struct repository *r);
const char *git_path_merge_rr(struct repository *r);
const char *git_path_merge_mode(struct repository *r);
const char *git_path_merge_head(struct repository *r);
const char *git_path_fetch_head(struct repository *r);
const char *git_path_shallow(struct repository *r);

int ends_with_path_components(const char *path, const char *components);

int calc_shared_perm(struct repository *repo, int mode);
int adjust_shared_perm(struct repository *repo, const char *path);

char *interpolate_path(const char *path, int real_home);

/* The bits are as follows:
 *
 * - ENTER_REPO_STRICT: callers that require exact paths (as opposed
 *   to allowing known suffixes like ".git", ".git/.git" to be
 *   omitted) can set this bit.
 *
 * - ENTER_REPO_ANY_OWNER_OK: callers that are willing to run without
 *   ownership check can set this bit.
 */
enum {
	ENTER_REPO_STRICT = (1<<0),
	ENTER_REPO_ANY_OWNER_OK = (1<<1),
};

const char *enter_repo(const char *path, unsigned flags);
const char *remove_leading_path(const char *in, const char *prefix);
const char *relative_path(const char *in, const char *prefix, struct strbuf *sb);
int normalize_path_copy_len(char *dst, const char *src, int *prefix_len);
int normalize_path_copy(char *dst, const char *src);
/**
 * Normalize in-place the path contained in the strbuf. If an error occurs,
 * the contents of "sb" are left untouched, and -1 is returned.
 */
int strbuf_normalize_path(struct strbuf *src);
int longest_ancestor_length(const char *path, struct string_list *prefixes);
char *strip_path_suffix(const char *path, const char *suffix);
int daemon_avoid_alias(const char *path);

/*
 * These functions match their is_hfs_dotgit() counterparts; see utf8.h for
 * details.
 */
int is_ntfs_dotgit(const char *name);
int is_ntfs_dotgitmodules(const char *name);
int is_ntfs_dotgitignore(const char *name);
int is_ntfs_dotgitattributes(const char *name);
int is_ntfs_dotmailmap(const char *name);

/*
 * Returns true iff "str" could be confused as a command-line option when
 * passed to a sub-program like "ssh". Note that this has nothing to do with
 * shell-quoting, which should be handled separately; we're assuming here that
 * the string makes it verbatim to the sub-program.
 */
int looks_like_command_line_option(const char *str);

/**
 * Return a newly allocated string with the evaluation of
 * "$XDG_CONFIG_HOME/$subdir/$filename" if $XDG_CONFIG_HOME is non-empty, otherwise
 * "$HOME/.config/$subdir/$filename". Return NULL upon error.
 */
char *xdg_config_home_for(const char *subdir, const char *filename);

/**
 * Return a newly allocated string with the evaluation of
 * "$XDG_CONFIG_HOME/git/$filename" if $XDG_CONFIG_HOME is non-empty, otherwise
 * "$HOME/.config/git/$filename". Return NULL upon error.
 */
char *xdg_config_home(const char *filename);

/**
 * Return a newly allocated string with the evaluation of
 * "$XDG_CACHE_HOME/git/$filename" if $XDG_CACHE_HOME is non-empty, otherwise
 * "$HOME/.cache/git/$filename". Return NULL upon error.
 */
char *xdg_cache_home(const char *filename);

/*
 * Create a directory and (if share is nonzero) adjust its permissions
 * according to the shared_repository setting. Only use this for
 * directories under $GIT_DIR.  Don't use it for working tree
 * directories.
 */
void safe_create_dir(struct repository *repo, const char *dir, int share);

/*
 * Similar to `safe_create_dir()`, but with two differences:
 *
 *   - It knows to resolve gitlink files for symlinked worktrees.
 *
 *   - It always adjusts shared permissions.
 *
 * Returns a negative erorr code on error, 0 on success.
 */
int safe_create_dir_in_gitdir(struct repository *repo, const char *path);

/*
 * Create the directory containing the named path, using care to be
 * somewhat safe against races. Return one of the scld_error values to
 * indicate success/failure. On error, set errno to describe the
 * problem.
 *
 * SCLD_VANISHED indicates that one of the ancestor directories of the
 * path existed at one point during the function call and then
 * suddenly vanished, probably because another process pruned the
 * directory while we were working.  To be robust against this kind of
 * race, callers might want to try invoking the function again when it
 * returns SCLD_VANISHED.
 *
 * safe_create_leading_directories() temporarily changes path while it
 * is working but restores it before returning.
 * safe_create_leading_directories_const() doesn't modify path, even
 * temporarily. Both these variants adjust the permissions of the
 * created directories to honor core.sharedRepository, so they are best
 * suited for files inside the git dir. For working tree files, use
 * safe_create_leading_directories_no_share() instead, as it ignores
 * the core.sharedRepository setting.
 */
enum scld_error {
	SCLD_OK = 0,
	SCLD_FAILED = -1,
	SCLD_PERMS = -2,
	SCLD_EXISTS = -3,
	SCLD_VANISHED = -4
};
enum scld_error safe_create_leading_directories(struct repository *repo, char *path);
enum scld_error safe_create_leading_directories_const(struct repository *repo,
						      const char *path);
enum scld_error safe_create_leading_directories_no_share(char *path);

/*
 * Create a file, potentially creating its leading directories in case they
 * don't exist. Returns the return value of the open(3p) call.
 */
int safe_create_file_with_leading_directories(struct repository *repo,
					      const char *path);

# ifdef USE_THE_REPOSITORY_VARIABLE
#  include "strbuf.h"
#  include "repository.h"

#define GIT_PATH_FUNC(func, filename) \
	const char *func(void) \
	{ \
		static char *ret; \
		if (!ret) \
			ret = repo_git_path(the_repository, filename); \
		return ret; \
	}

# endif /* USE_THE_REPOSITORY_VARIABLE */

#endif /* PATH_H */
