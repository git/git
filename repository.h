#ifndef REPOSITORY_H
#define REPOSITORY_H

struct config_set;
struct fsmonitor_settings;
struct git_hash_algo;
struct index_state;
struct lock_file;
struct pathspec;
struct raw_object_store;
struct submodule_cache;
struct promisor_remote_config;
struct remote_state;

enum untracked_cache_setting {
	UNTRACKED_CACHE_KEEP,
	UNTRACKED_CACHE_REMOVE,
	UNTRACKED_CACHE_WRITE,
};

enum fetch_negotiation_setting {
	FETCH_NEGOTIATION_CONSECUTIVE,
	FETCH_NEGOTIATION_SKIPPING,
	FETCH_NEGOTIATION_NOOP,
};

#define REF_STORAGE_FORMAT_UNKNOWN  0
#define REF_STORAGE_FORMAT_FILES    1
#define REF_STORAGE_FORMAT_REFTABLE 2

struct repo_settings {
	int initialized;

	int core_commit_graph;
	int commit_graph_generation_version;
	int commit_graph_read_changed_paths;
	int gc_write_commit_graph;
	int fetch_write_commit_graph;
	int command_requires_full_index;
	int sparse_index;
	int pack_read_reverse_index;
	int pack_use_bitmap_boundary_traversal;
	int pack_use_multi_pack_reuse;

	/*
	 * Does this repository have core.useReplaceRefs=true (on by
	 * default)? This provides a repository-scoped version of this
	 * config, though it could be disabled process-wide via some Git
	 * builtins or the --no-replace-objects option. See
	 * replace_refs_enabled() for more details.
	 */
	int read_replace_refs;

	struct fsmonitor_settings *fsmonitor; /* lazily loaded */

	int index_version;
	int index_skip_hash;
	enum untracked_cache_setting core_untracked_cache;

	int pack_use_sparse;
	enum fetch_negotiation_setting fetch_negotiation_algorithm;

	int core_multi_pack_index;
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
	struct raw_object_store *objects;

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

	/* Repository's reference storage format, as serialized on disk. */
	unsigned int ref_storage_format;

	/* A unique-id for tracing purposes. */
	int trace2_repo_id;

	/* True if commit-graph has been disabled within this process. */
	int commit_graph_disabled;

	/* Configurations related to promisor remotes. */
	char *repository_format_partial_clone;
	struct promisor_remote_config *promisor_remote_config;

	/* Configurations */
	int repository_format_worktree_config;

	/* Indicate if a repository has a different 'commondir' from 'gitdir' */
	unsigned different_commondir:1;
};

extern struct repository *the_repository;
#ifdef USE_THE_INDEX_VARIABLE
extern struct index_state the_index;
#endif

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
void repo_set_ref_storage_format(struct repository *repo, unsigned int format);
void initialize_the_repository(void);
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

void prepare_repo_settings(struct repository *r);

/*
 * Return 1 if upgrade repository format to target_version succeeded,
 * 0 if no upgrade is necessary, and -1 when upgrade is not possible.
 */
int upgrade_repository_format(int target_version);

#endif /* REPOSITORY_H */
