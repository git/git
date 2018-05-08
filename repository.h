#ifndef REPOSITORY_H
#define REPOSITORY_H

struct config_set;
struct git_hash_algo;
struct index_state;
struct raw_object_store;
struct submodule_cache;

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

	/* The store in which the refs are held. */
	struct ref_store *refs;

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

	/* Repository's current hash algorithm, as serialized on disk. */
	const struct git_hash_algo *hash_algo;

	/* Configurations */

	/* Indicate if a repository has a different 'commondir' from 'gitdir' */
	unsigned different_commondir:1;
};

extern struct repository *the_repository;

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
};

extern void repo_set_gitdir(struct repository *repo,
			    const char *root,
			    const struct set_gitdir_args *extra_args);
extern void repo_set_worktree(struct repository *repo, const char *path);
extern void repo_set_hash_algo(struct repository *repo, int algo);
extern void initialize_the_repository(void);
extern int repo_init(struct repository *r,
		     const char *gitdir,
		     const char *worktree);
extern int repo_submodule_init(struct repository *submodule,
			       struct repository *superproject,
			       const char *path);
extern void repo_clear(struct repository *repo);

/*
 * Populates the repository's index from its index_file, an index struct will
 * be allocated if needed.
 *
 * Return the number of index entries in the populated index or a value less
 * than zero if an error occured.  If the repository's index has already been
 * populated then the number of entries will simply be returned.
 */
extern int repo_read_index(struct repository *repo);

#endif /* REPOSITORY_H */
