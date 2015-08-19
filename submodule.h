#ifndef SUBMODULE_H
#define SUBMODULE_H

struct diff_options;
struct argv_array;

enum {
	RECURSE_SUBMODULES_ERROR = -3,
	RECURSE_SUBMODULES_NONE = -2,
	RECURSE_SUBMODULES_ON_DEMAND = -1,
	RECURSE_SUBMODULES_OFF = 0,
	RECURSE_SUBMODULES_DEFAULT = 1,
	RECURSE_SUBMODULES_ON = 2
};

int is_staging_gitmodules_ok(void);
int update_path_in_gitmodules(const char *oldpath, const char *newpath);
int remove_path_from_gitmodules(const char *path);
void stage_updated_gitmodules(void);
void set_diffopt_flags_from_submodule_config(struct diff_options *diffopt,
		const char *path);
int submodule_config(const char *var, const char *value, void *cb);
void gitmodules_config(void);
void handle_ignore_submodules_arg(struct diff_options *diffopt, const char *);
void show_submodule_summary(FILE *f, const char *path,
		const char *line_prefix,
		unsigned char one[20], unsigned char two[20],
		unsigned dirty_submodule, const char *meta,
		const char *del, const char *add, const char *reset);
void set_config_fetch_recurse_submodules(int value);
void check_for_new_submodule_commits(unsigned char new_sha1[20]);
int fetch_populated_submodules(const struct argv_array *options,
			       const char *prefix, int command_line_option,
			       int quiet);
unsigned is_submodule_modified(const char *path, int ignore_untracked);
int submodule_uses_gitfile(const char *path);
int ok_to_remove_submodule(const char *path);
int merge_submodule(unsigned char result[20], const char *path, const unsigned char base[20],
		    const unsigned char a[20], const unsigned char b[20], int search);
int find_unpushed_submodules(unsigned char new_sha1[20], const char *remotes_name,
		struct string_list *needs_pushing);
int push_unpushed_submodules(unsigned char new_sha1[20], const char *remotes_name);
void connect_work_tree_and_git_dir(const char *work_tree, const char *git_dir);

#endif
