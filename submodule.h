#ifndef SUBMODULE_H
#define SUBMODULE_H

struct strvec;
struct cache_entry;
struct diff_options;
struct index_state;
struct object_id;
struct oid_array;
struct pathspec;
struct remote;
struct repository;
struct string_list;
struct strbuf;

enum submodule_recurse_mode {
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
#define SUBMODULE_UPDATE_STRATEGY_INIT { \
	.type = SM_UPDATE_UNSPECIFIED, \
}

int is_gitmodules_unmerged(struct index_state *istate);
int is_writing_gitmodules_ok(void);
int is_staging_gitmodules_ok(struct index_state *istate);
int update_path_in_gitmodules(const char *oldpath, const char *newpath);
int remove_path_from_gitmodules(const char *path);
void stage_updated_gitmodules(struct index_state *istate);
void set_diffopt_flags_from_submodule_config(struct diff_options *,
					     const char *path);
int git_default_submodule_config(const char *var, const char *value, void *cb);

struct option;
int option_parse_recurse_submodules_worktree_updater(const struct option *opt,
						     const char *arg, int unset);
int is_tree_submodule_active(struct repository *repo,
			     const struct object_id *treeish_name,
			     const char *path);
int is_submodule_active(struct repository *repo, const char *path);
/*
 * Determine if a submodule has been populated at a given 'path' by checking if
 * the <path>/.git resolves to a valid git repository.
 * If return_error_code is NULL, die on error.
 * Otherwise the return error code is the same as of resolve_gitdir_gently.
 */
int is_submodule_populated_gently(const char *path, int *return_error_code);
void die_in_unpopulated_submodule(struct index_state *istate,
				  const char *prefix);
void die_path_inside_submodule(struct index_state *istate,
			       const struct pathspec *ps);
enum submodule_update_type parse_submodule_update_type(const char *value);
int parse_submodule_update_strategy(const char *value,
				    struct submodule_update_strategy *dst);
const char *submodule_update_type_to_string(enum submodule_update_type type);
void handle_ignore_submodules_arg(struct diff_options *, const char *);
void show_submodule_diff_summary(struct diff_options *o, const char *path,
			    struct object_id *one, struct object_id *two,
			    unsigned dirty_submodule);
void show_submodule_inline_diff(struct diff_options *o, const char *path,
				struct object_id *one, struct object_id *two,
				unsigned dirty_submodule);
/* Check if we want to update any submodule.*/
int should_update_submodules(void);
/*
 * Returns the submodule struct if the given ce entry is a submodule
 * and it should be updated. Returns NULL otherwise.
 */
const struct submodule *submodule_from_ce(const struct cache_entry *ce);
void check_for_new_submodule_commits(struct object_id *oid);
int fetch_submodules(struct repository *r,
		     const struct strvec *options,
		     const char *prefix,
		     int command_line_option,
		     int default_option,
		     int quiet, int max_parallel_jobs);
unsigned is_submodule_modified(const char *path, int ignore_untracked);
int submodule_uses_gitfile(const char *path);

#define SUBMODULE_REMOVAL_DIE_ON_ERROR (1<<0)
#define SUBMODULE_REMOVAL_IGNORE_UNTRACKED (1<<1)
#define SUBMODULE_REMOVAL_IGNORE_IGNORED_UNTRACKED (1<<2)
int bad_to_remove_submodule(const char *path, unsigned flags);

/*
 * Call add_submodule_odb_by_path() to add the submodule at the given
 * path to a list. When register_all_submodule_odb_as_alternates() is
 * called, the object stores of all submodules in that list will be
 * added as alternates in the_repository.
 */
void add_submodule_odb_by_path(const char *path);
int register_all_submodule_odb_as_alternates(void);

/*
 * Checks if there are submodule changes in a..b. If a is the null OID,
 * checks b and all its ancestors instead.
 */
int submodule_touches_in_range(struct repository *r,
			       struct object_id *a,
			       struct object_id *b);
int find_unpushed_submodules(struct repository *r,
			     struct oid_array *commits,
			     const char *remotes_name,
			     struct string_list *needs_pushing);
struct refspec;
int push_unpushed_submodules(struct repository *r,
			     struct oid_array *commits,
			     const struct remote *remote,
			     const struct refspec *rs,
			     const struct string_list *push_options,
			     int dry_run);
/*
 * Given a submodule path (as in the index), return the repository
 * path of that submodule in 'buf'. Return -1 on error or when the
 * submodule is not initialized.
 */
int submodule_to_gitdir(struct strbuf *buf, const char *submodule);

/*
 * Given a submodule name, create a path to where the submodule's gitdir lives
 * inside of the provided repository's 'modules' directory.
 */
void submodule_name_to_gitdir(struct strbuf *buf, struct repository *r,
			      const char *submodule_name);

/*
 * Make sure that no submodule's git dir is nested in a sibling submodule's.
 */
int validate_submodule_git_dir(char *git_dir, const char *submodule_name);

#define SUBMODULE_MOVE_HEAD_DRY_RUN (1<<0)
#define SUBMODULE_MOVE_HEAD_FORCE   (1<<1)
int submodule_move_head(const char *path,
			const char *old,
			const char *new_head,
			unsigned flags);

void submodule_unset_core_worktree(const struct submodule *sub);

/*
 * Prepare the "env" parameter of a "struct child_process" for executing
 * a submodule by clearing any repo-specific environment variables, but
 * retaining any config in the environment.
 */
void prepare_submodule_repo_env(struct strvec *env);

#define ABSORB_GITDIR_RECURSE_SUBMODULES (1<<0)
void absorb_git_dir_into_superproject(const char *path,
				      unsigned flags);

/*
 * Return the absolute path of the working tree of the superproject, which this
 * project is a submodule of. If this repository is not a submodule of
 * another repository, return 0.
 */
int get_superproject_working_tree(struct strbuf *buf);

#endif
