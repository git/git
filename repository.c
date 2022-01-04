/*
 * not really _using_ the compat macros, just make sure the_index
 * declaration matches the definition in this file.
 */
#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "cache.h"
#include "repository.h"
#include "object-store.h"
#include "config.h"
#include "object.h"
#include "lockfile.h"
#include "remote.h"
#include "submodule-config.h"
#include "sparse-index.h"
#include "promisor-remote.h"

/* The main repository */
static struct repository the_repo;
struct repository *the_repository;
struct index_state the_index;

void initialize_the_repository(void)
{
	the_repository = &the_repo;

	the_repo.index = &the_index;
	the_repo.objects = raw_object_store_new();
	the_repo.remote_state = remote_state_new();
	the_repo.parsed_objects = parsed_object_pool_new();

	repo_set_hash_algo(&the_repo, GIT_HASH_SHA1);
}

static void expand_base_dir(char **out, const char *in,
			    const char *base_dir, const char *def_in)
{
	free(*out);
	if (in)
		*out = xstrdup(in);
	else
		*out = xstrfmt("%s/%s", base_dir, def_in);
}

static void repo_set_commondir(struct repository *repo,
			       const char *commondir)
{
	struct strbuf sb = STRBUF_INIT;

	free(repo->commondir);

	if (commondir) {
		repo->different_commondir = 1;
		repo->commondir = xstrdup(commondir);
		return;
	}

	repo->different_commondir = get_common_dir_noenv(&sb, repo->gitdir);
	repo->commondir = strbuf_detach(&sb, NULL);
}

void repo_set_gitdir(struct repository *repo,
		     const char *root,
		     const struct set_gitdir_args *o)
{
	const char *gitfile = read_gitfile(root);
	/*
	 * repo->gitdir is saved because the caller could pass "root"
	 * that also points to repo->gitdir. We want to keep it alive
	 * until after xstrdup(root). Then we can free it.
	 */
	char *old_gitdir = repo->gitdir;

	repo->gitdir = xstrdup(gitfile ? gitfile : root);
	free(old_gitdir);

	repo_set_commondir(repo, o->commondir);

	if (!repo->objects->odb) {
		CALLOC_ARRAY(repo->objects->odb, 1);
		repo->objects->odb_tail = &repo->objects->odb->next;
	}
	expand_base_dir(&repo->objects->odb->path, o->object_dir,
			repo->commondir, "objects");

	repo->objects->odb->disable_ref_updates = o->disable_ref_updates;

	free(repo->objects->alternate_db);
	repo->objects->alternate_db = xstrdup_or_null(o->alternate_db);
	expand_base_dir(&repo->graft_file, o->graft_file,
			repo->commondir, "info/grafts");
	expand_base_dir(&repo->index_file, o->index_file,
			repo->gitdir, "index");
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
	struct set_gitdir_args args = { NULL };

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

	repo_set_gitdir(repo, resolved_gitdir, &args);

out:
	free(abspath);
	return ret;
}

void repo_set_worktree(struct repository *repo, const char *path)
{
	repo->worktree = real_pathdup(path, 1);

	trace2_def_repo(repo);
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
int repo_init(struct repository *repo,
	      const char *gitdir,
	      const char *worktree)
{
	struct repository_format format = REPOSITORY_FORMAT_INIT;
	memset(repo, 0, sizeof(*repo));

	repo->objects = raw_object_store_new();
	repo->parsed_objects = parsed_object_pool_new();
	repo->remote_state = remote_state_new();

	if (repo_init_gitdir(repo, gitdir))
		goto error;

	if (read_and_verify_repository_format(&format, repo->commondir))
		goto error;

	repo_set_hash_algo(repo, format.hash_algo);

	/* take ownership of format.partial_clone */
	repo->repository_format_partial_clone = format.partial_clone;
	format.partial_clone = NULL;

	if (worktree)
		repo_set_worktree(repo, worktree);

	clear_repository_format(&format);
	return 0;

error:
	repo_clear(repo);
	return -1;
}

int repo_submodule_init(struct repository *subrepo,
			struct repository *superproject,
			const char *path,
			const struct object_id *treeish_name)
{
	struct strbuf gitdir = STRBUF_INIT;
	struct strbuf worktree = STRBUF_INIT;
	int ret = 0;

	strbuf_repo_worktree_path(&gitdir, superproject, "%s/.git", path);
	strbuf_repo_worktree_path(&worktree, superproject, "%s", path);

	if (repo_init(subrepo, gitdir.buf, worktree.buf)) {
		/*
		 * If initialization fails then it may be due to the submodule
		 * not being populated in the superproject's worktree.  Instead
		 * we can try to initialize the submodule by finding it's gitdir
		 * in the superproject's 'modules' directory.  In this case the
		 * submodule would not have a worktree.
		 */
		const struct submodule *sub =
			submodule_from_path(superproject, treeish_name, path);
		if (!sub) {
			ret = -1;
			goto out;
		}

		strbuf_reset(&gitdir);
		submodule_name_to_gitdir(&gitdir, superproject, sub->name);

		if (repo_init(subrepo, gitdir.buf, NULL)) {
			ret = -1;
			goto out;
		}
	}

	subrepo->submodule_prefix = xstrfmt("%s%s/",
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
	FREE_AND_NULL(repo->graft_file);
	FREE_AND_NULL(repo->index_file);
	FREE_AND_NULL(repo->worktree);
	FREE_AND_NULL(repo->submodule_prefix);

	raw_object_store_clear(repo->objects);
	FREE_AND_NULL(repo->objects);

	parsed_object_pool_clear(repo->parsed_objects);
	FREE_AND_NULL(repo->parsed_objects);

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
		if (repo->index != &the_index)
			FREE_AND_NULL(repo->index);
	}

	if (repo->promisor_remote_config) {
		promisor_remote_clear(repo->promisor_remote_config);
		FREE_AND_NULL(repo->promisor_remote_config);
	}

	if (repo->remote_state) {
		remote_state_clear(repo->remote_state);
		FREE_AND_NULL(repo->remote_state);
	}
}

int repo_read_index(struct repository *repo)
{
	int res;

	if (!repo->index)
		CALLOC_ARRAY(repo->index, 1);

	/* Complete the double-reference */
	if (!repo->index->repo)
		repo->index->repo = repo;
	else if (repo->index->repo != repo)
		BUG("repo's index should point back at itself");

	res = read_index_from(repo->index, repo->index_file, repo->gitdir);

	prepare_repo_settings(repo);
	if (repo->settings.command_requires_full_index)
		ensure_full_index(repo->index);

	return res;
}

int repo_hold_locked_index(struct repository *repo,
			   struct lock_file *lf,
			   int flags)
{
	if (!repo->index_file)
		BUG("the repo hasn't been setup");
	return hold_lock_file_for_update(lf, repo->index_file, flags);
}
