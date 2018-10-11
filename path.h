#ifndef PATH_H
#define PATH_H

struct repository;
struct strbuf;
struct string_list;

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
int validate_headref(const char *ref);

int adjust_shared_perm(const char *path);

char *interpolate_path(const char *path, int real_home);
const char *enter_repo(const char *path, int strict);
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
int is_mount_point_via_stat(struct strbuf *path);
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
void safe_create_dir(const char *dir, int share);

#endif /* PATH_H */
