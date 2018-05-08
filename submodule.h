#ifndef SUBMODULE_H
#define SUBMODULE_H

struct repository;
struct diff_options;
struct argv_array;
struct oid_array;
struct remote;

enum {
	RECURSE_SUBMODULES_ONLY = -5,
	RECURSE_SUBMODULES_CHECK = -4,
	RECURSE_SUBMODULES_ERROR = -3,
	RECURSE_SUBMODULES_NONE = -2,
	RECURSE_SUBMODULES_ON_DEMAND = -1,
	RECURSE_SUBMODULES_OFF = 0,
	RECURSE_SUBMODULES_DEFAULT = 1,
	RECURSE_SUBMODULES_ON = 2
};

enum submodule_update_type {
	SM_UPDATE_UNSPECIFIED = 0,
	SM_UPDATE_CHECKOUT,
	SM_UPDATE_REBASE,
	SM_UPDATE_MERGE,
	SM_UPDATE_NONE,
	SM_UPDATE_COMMAND
};

struct submodule_update_strategy {
	enum submodule_update_type type;
	const char *command;
};
#define SUBMODULE_UPDATE_STRATEGY_INIT {SM_UPDATE_UNSPECIFIED, NULL}

extern int is_gitmodules_unmerged(const struct index_state *istate);
extern int is_staging_gitmodules_ok(struct index_state *istate);
extern int update_path_in_gitmodules(const char *oldpath, const char *newpath);
extern int remove_path_from_gitmodules(const char *path);
extern void stage_updated_gitmodules(struct index_state *istate);
extern void set_diffopt_flags_from_submodule_config(struct diff_options *,
		const char *path);
extern int git_default_submodule_config(const char *var, const char *value, void *cb);

struct option;
int option_parse_recurse_submodules_worktree_updater(const struct option *opt,
						     const char *arg, int unset);
extern int is_submodule_active(struct repository *repo, const char *path);
/*
 * Determine if a submodule has been populated at a given 'path' by checking if
 * the <path>/.git resolves to a valid git repository.
 * If return_error_code is NULL, die on error.
 * Otherwise the return error code is the same as of resolve_gitdir_gently.
 */
extern int is_submodule_populated_gently(const char *path, int *return_error_code);
extern void die_in_unpopulated_submodule(const struct index_state *istate,
					 const char *prefix);
extern void die_path_inside_submodule(const struct index_state *istate,
				      const struct pathspec *ps);
extern enum submodule_update_type parse_submodule_update_type(const char *value);
extern int parse_submodule_update_strategy(const char *value,
		struct submodule_update_strategy *dst);
extern const char *submodule_strategy_to_string(const struct submodule_update_strategy *s);
extern void handle_ignore_submodules_arg(struct diff_options *, const char *);
extern void show_submodule_summary(struct diff_options *o, const char *path,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule);
extern void show_submodule_inline_diff(struct diff_options *o, const char *path,
		struct object_id *one, struct object_id *two,
		unsigned dirty_submodule);
/* Check if we want to update any submodule.*/
extern int should_update_submodules(void);
/*
 * Returns the submodule struct if the given ce entry is a submodule
 * and it should be updated. Returns NULL otherwise.
 */
extern const struct submodule *submodule_from_ce(const struct cache_entry *ce);
extern void check_for_new_submodule_commits(struct object_id *oid);
extern int fetch_populated_submodules(struct repository *r,
				      const struct argv_array *options,
				      const char *prefix,
				      int command_line_option,
				      int default_option,
				      int quiet, int max_parallel_jobs);
extern unsigned is_submodule_modified(const char *path, int ignore_untracked);
extern int submodule_uses_gitfile(const char *path);

#define SUBMODULE_REMOVAL_DIE_ON_ERROR (1<<0)
#define SUBMODULE_REMOVAL_IGNORE_UNTRACKED (1<<1)
#define SUBMODULE_REMOVAL_IGNORE_IGNORED_UNTRACKED (1<<2)
extern int bad_to_remove_submodule(const char *path, unsigned flags);
extern int merge_submodule(struct object_id *result, const char *path,
			   const struct object_id *base,
			   const struct object_id *a,
			   const struct object_id *b, int search);

/* Checks if there are submodule changes in a..b. */
extern int submodule_touches_in_range(struct object_id *a,
				      struct object_id *b);
extern int find_unpushed_submodules(struct oid_array *commits,
				    const char *remotes_name,
				    struct string_list *needs_pushing);
extern int push_unpushed_submodules(struct oid_array *commits,
				    const struct remote *remote,
				    const char **refspec, int refspec_nr,
				    const struct string_list *push_options,
				    int dry_run);
/*
 * Given a submodule path (as in the index), return the repository
 * path of that submodule in 'buf'. Return -1 on error or when the
 * submodule is not initialized.
 */
int submodule_to_gitdir(struct strbuf *buf, const char *submodule);

#define SUBMODULE_MOVE_HEAD_DRY_RUN (1<<0)
#define SUBMODULE_MOVE_HEAD_FORCE   (1<<1)
extern int submodule_move_head(const char *path,
			       const char *old,
			       const char *new_head,
			       unsigned flags);

/*
 * Prepare the "env_array" parameter of a "struct child_process" for executing
 * a submodule by clearing any repo-specific environment variables, but
 * retaining any config in the environment.
 */
extern void prepare_submodule_repo_env(struct argv_array *out);

#define ABSORB_GITDIR_RECURSE_SUBMODULES (1<<0)
extern void absorb_git_dir_into_superproject(const char *prefix,
					     const char *path,
					     unsigned flags);

/*
 * Return the absolute path of the working tree of the superproject, which this
 * project is a submodule of. If this repository is not a submodule of
 * another repository, return NULL.
 */
extern const char *get_superproject_working_tree(void);

#endif
