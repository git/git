#ifndef REPOSITORY_H
#define REPOSITORY_H

#include "strmap.h"
#include "repo-settings.h"

struct config_set;
struct git_hash_algo;
struct index_state;
struct lock_file;
struct pathspec;
struct object_database;
struct submodule_cache;
struct promisor_remote_config;
struct remote_state;

enum ref_storage_format {
	REF_STORAGE_FORMAT_UNKNOWN,
	REF_STORAGE_FORMAT_FILES,
	REF_STORAGE_FORMAT_REFTABLE,
};

struct repo_path_cache {
	char *squash_msg;
	char *merge_msg;
	char *merge_rr;
	char *merge_mode;
	char *merge_head;
	char *fetch_head;
	char *shallow;
};

struct repository {
	/* Environment */
	/*
	 * Path to the git directory.
	 * Cannot be NULL after initialization.
	 */
	char *gitdir;

	/*
	 * Path to the common git directory.
	 * Cannot be NULL after initialization.
	 */
	char *commondir;

	/*
	 * Holds any information related to accessing the raw object content.
	 */
	struct object_database *objects;

	/*
	 * All objects in this repository that have been parsed. This structure
	 * owns all objects it references, so users of "struct object *"
	 * generally do not need to free them; instead, when a repository is no
	 * longer used, call parsed_object_pool_clear() on this structure, which
	 * is called by the repositories repo_clear on its desconstruction.
	 */
	struct parsed_object_pool *parsed_objects;

	/*
	 * The store in which the refs are held. This should generally only be
	 * accessed via get_main_ref_store(), as that will lazily initialize
	 * the ref object.
	 */
	struct ref_store *refs_private;

	/*
	 * A strmap of ref_stores, stored by submodule name, accessible via
	 * `repo_get_submodule_ref_store()`.
	 */
	struct strmap submodule_ref_stores;

	/*
	 * A strmap of ref_stores, stored by worktree id, accessible via
	 * `get_worktree_ref_store()`.
	 */
	struct strmap worktree_ref_stores;

	/*
	 * Contains path to often used file names.
	 */
	struct repo_path_cache cached_paths;

	/*
	 * Path to the repository's graft file.
	 * Cannot be NULL after initialization.
	 */
	char *graft_file;

	/*
	 * Path to the current worktree's index file.
	 * Cannot be NULL after initialization.
	 */
	char *index_file;

	/*
	 * Path to the working directory.
	 * A NULL value indicates that there is no working directory.
	 */
	char *worktree;

	/*
	 * Path from the root of the top-level superproject down to this
	 * repository.  This is only non-NULL if the repository is initialized
	 * as a submodule of another repository.
	 */
	char *submodule_prefix;

	struct repo_settings settings;

	/* Subsystems */
	/*
	 * Repository's config which contains key-value pairs from the usual
	 * set of config files (i.e. repo specific .git/config, user wide
	 * ~/.gitconfig, XDG config file and the global /etc/gitconfig)
	 */
	struct config_set *config;

	/* Repository's submodule config as defined by '.gitmodules' */
	struct submodule_cache *submodule_cache;

	/*
	 * Repository's in-memory index.
	 * 'repo_read_index()' can be used to populate 'index'.
	 */
	struct index_state *index;

	/* Repository's remotes and associated structures. */
	struct remote_state *remote_state;

	/* Repository's current hash algorithm, as serialized on disk. */
	const struct git_hash_algo *hash_algo;

	/* Repository's compatibility hash algorithm. */
	const struct git_hash_algo *compat_hash_algo;

	/* Repository's reference storage format, as serialized on disk. */
	enum ref_storage_format ref_storage_format;

	/* A unique-id for tracing purposes. */
	int trace2_repo_id;

	/* True if commit-graph has been disabled within this process. */
	int commit_graph_disabled;

	/* Configurations related to promisor remotes. */
	char *repository_format_partial_clone;
	struct promisor_remote_config *promisor_remote_config;

	/* Configurations */
	int repository_format_worktree_config;
	int repository_format_relative_worktrees;

	/* Indicate if a repository has a different 'commondir' from 'gitdir' */
	unsigned different_commondir:1;
};

#ifdef USE_THE_REPOSITORY_VARIABLE
extern struct repository *the_repository;
#endif

const char *repo_get_git_dir(struct repository *repo);
const char *repo_get_common_dir(struct repository *repo);
const char *repo_get_object_directory(struct repository *repo);
const char *repo_get_index_file(struct repository *repo);
const char *repo_get_graft_file(struct repository *repo);
const char *repo_get_work_tree(struct repository *repo);

/*
 * Define a custom repository layout. Any field can be NULL, which
 * will default back to the path according to the default layout.
 */
struct set_gitdir_args {
	const char *commondir;
	const char *object_dir;
	const char *graft_file;
	const char *index_file;
	const char *alternate_db;
	int disable_ref_updates;
};

void repo_set_gitdir(struct repository *repo, const char *root,
		     const struct set_gitdir_args *extra_args);
void repo_set_worktree(struct repository *repo, const char *path);
void repo_set_hash_algo(struct repository *repo, int algo);
void repo_set_compat_hash_algo(struct repository *repo, int compat_algo);
void repo_set_ref_storage_format(struct repository *repo,
				 enum ref_storage_format format);
void initialize_repository(struct repository *repo);
RESULT_MUST_BE_USED
int repo_init(struct repository *r, const char *gitdir, const char *worktree);

/*
 * Initialize the repository 'subrepo' as the submodule at the given path. If
 * the submodule's gitdir cannot be found at <path>/.git, this function calls
 * submodule_from_path() to try to find it. treeish_name is only used if
 * submodule_from_path() needs to be called; see its documentation for more
 * information.
 * Return 0 upon success and a non-zero value upon failure.
 */
struct object_id;
RESULT_MUST_BE_USED
int repo_submodule_init(struct repository *subrepo,
			struct repository *superproject,
			const char *path,
			const struct object_id *treeish_name);
void repo_clear(struct repository *repo);

/*
 * Populates the repository's index from its index_file, an index struct will
 * be allocated if needed.
 *
 * Return the number of index entries in the populated index or a value less
 * than zero if an error occurred.  If the repository's index has already been
 * populated then the number of entries will simply be returned.
 */
int repo_read_index(struct repository *repo);
int repo_hold_locked_index(struct repository *repo,
			   struct lock_file *lf,
			   int flags);

int repo_read_index_unmerged(struct repository *);
/*
 * Opportunistically update the index but do not complain if we can't.
 * The lockfile is always committed or rolled back.
 */
void repo_update_index_if_able(struct repository *, struct lock_file *);

/*
 * Return 1 if upgrade repository format to target_version succeeded,
 * 0 if no upgrade is necessary, and -1 when upgrade is not possible.
 */
int upgrade_repository_format(int target_version);

#endif /* REPOSITORY_H */
