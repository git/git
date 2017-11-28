#include "cache.h"
#include "repository.h"
#include "config.h"
#include "submodule-config.h"

/* The main repository */
static struct repository the_repo = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &the_index, NULL, 0, 0
};
struct repository *the_repository = &the_repo;

static char *git_path_from_env(const char *envvar, const char *git_dir,
			       const char *path, int fromenv)
{
	if (fromenv) {
		const char *value = getenv(envvar);
		if (value)
			return xstrdup(value);
	}

	return xstrfmt("%s/%s", git_dir, path);
}

static int find_common_dir(struct strbuf *sb, const char *gitdir, int fromenv)
{
	if (fromenv) {
		const char *value = getenv(GIT_COMMON_DIR_ENVIRONMENT);
		if (value) {
			strbuf_addstr(sb, value);
			return 1;
		}
	}

	return get_common_dir_noenv(sb, gitdir);
}

static void repo_setup_env(struct repository *repo)
{
	struct strbuf sb = STRBUF_INIT;

	repo->different_commondir = find_common_dir(&sb, repo->gitdir,
						    !repo->ignore_env);
	free(repo->commondir);
	repo->commondir = strbuf_detach(&sb, NULL);
	free(repo->objectdir);
	repo->objectdir = git_path_from_env(DB_ENVIRONMENT, repo->commondir,
					    "objects", !repo->ignore_env);
	free(repo->graft_file);
	repo->graft_file = git_path_from_env(GRAFT_ENVIRONMENT, repo->commondir,
					     "info/grafts", !repo->ignore_env);
	free(repo->index_file);
	repo->index_file = git_path_from_env(INDEX_ENVIRONMENT, repo->gitdir,
					     "index", !repo->ignore_env);
}

void repo_set_gitdir(struct repository *repo, const char *path)
{
	const char *gitfile = read_gitfile(path);
	char *old_gitdir = repo->gitdir;

	repo->gitdir = xstrdup(gitfile ? gitfile : path);
	repo_setup_env(repo);

	free(old_gitdir);
}

void repo_set_hash_algo(struct repository *repo, int hash_algo)
{
	repo->hash_algo = &hash_algos[hash_algo];
}

/*
 * Attempt to resolve and set the provided 'gitdir' for repository 'repo'.
 * Return 0 upon success and a non-zero value upon failure.
 */
static int repo_init_gitdir(struct repository *repo, const char *gitdir)
{
	int ret = 0;
	int error = 0;
	char *abspath = NULL;
	const char *resolved_gitdir;

	abspath = real_pathdup(gitdir, 0);
	if (!abspath) {
		ret = -1;
		goto out;
	}

	/* 'gitdir' must reference the gitdir directly */
	resolved_gitdir = resolve_gitdir_gently(abspath, &error);
	if (!resolved_gitdir) {
		ret = -1;
		goto out;
	}

	repo_set_gitdir(repo, resolved_gitdir);

out:
	free(abspath);
	return ret;
}

void repo_set_worktree(struct repository *repo, const char *path)
{
	repo->worktree = real_pathdup(path, 1);
}

static int read_and_verify_repository_format(struct repository_format *format,
					     const char *commondir)
{
	int ret = 0;
	struct strbuf sb = STRBUF_INIT;

	strbuf_addf(&sb, "%s/config", commondir);
	read_repository_format(format, sb.buf);
	strbuf_reset(&sb);

	if (verify_repository_format(format, &sb) < 0) {
		warning("%s", sb.buf);
		ret = -1;
	}

	strbuf_release(&sb);
	return ret;
}

/*
 * Initialize 'repo' based on the provided 'gitdir'.
 * Return 0 upon success and a non-zero value upon failure.
 */
int repo_init(struct repository *repo, const char *gitdir, const char *worktree)
{
	struct repository_format format;
	memset(repo, 0, sizeof(*repo));

	repo->ignore_env = 1;

	if (repo_init_gitdir(repo, gitdir))
		goto error;

	if (read_and_verify_repository_format(&format, repo->commondir))
		goto error;

	repo_set_hash_algo(repo, format.hash_algo);

	if (worktree)
		repo_set_worktree(repo, worktree);

	return 0;

error:
	repo_clear(repo);
	return -1;
}

/*
 * Initialize 'submodule' as the submodule given by 'path' in parent repository
 * 'superproject'.
 * Return 0 upon success and a non-zero value upon failure.
 */
int repo_submodule_init(struct repository *submodule,
			struct repository *superproject,
			const char *path)
{
	const struct submodule *sub;
	struct strbuf gitdir = STRBUF_INIT;
	struct strbuf worktree = STRBUF_INIT;
	int ret = 0;

	sub = submodule_from_cache(superproject, &null_oid, path);
	if (!sub) {
		ret = -1;
		goto out;
	}

	strbuf_repo_worktree_path(&gitdir, superproject, "%s/.git", path);
	strbuf_repo_worktree_path(&worktree, superproject, "%s", path);

	if (repo_init(submodule, gitdir.buf, worktree.buf)) {
		/*
		 * If initilization fails then it may be due to the submodule
		 * not being populated in the superproject's worktree.  Instead
		 * we can try to initilize the submodule by finding it's gitdir
		 * in the superproject's 'modules' directory.  In this case the
		 * submodule would not have a worktree.
		 */
		strbuf_reset(&gitdir);
		strbuf_repo_git_path(&gitdir, superproject,
				     "modules/%s", sub->name);

		if (repo_init(submodule, gitdir.buf, NULL)) {
			ret = -1;
			goto out;
		}
	}

	submodule->submodule_prefix = xstrfmt("%s%s/",
					      superproject->submodule_prefix ?
					      superproject->submodule_prefix :
					      "", path);

out:
	strbuf_release(&gitdir);
	strbuf_release(&worktree);
	return ret;
}

void repo_clear(struct repository *repo)
{
	FREE_AND_NULL(repo->gitdir);
	FREE_AND_NULL(repo->commondir);
	FREE_AND_NULL(repo->objectdir);
	FREE_AND_NULL(repo->graft_file);
	FREE_AND_NULL(repo->index_file);
	FREE_AND_NULL(repo->worktree);
	FREE_AND_NULL(repo->submodule_prefix);

	if (repo->config) {
		git_configset_clear(repo->config);
		FREE_AND_NULL(repo->config);
	}

	if (repo->submodule_cache) {
		submodule_cache_free(repo->submodule_cache);
		repo->submodule_cache = NULL;
	}

	if (repo->index) {
		discard_index(repo->index);
		FREE_AND_NULL(repo->index);
	}
}

int repo_read_index(struct repository *repo)
{
	if (!repo->index)
		repo->index = xcalloc(1, sizeof(*repo->index));

	return read_index_from(repo->index, repo->index_file);
}
