#ifndef SUBMODULE_H
#define SUBMODULE_H

struct diff_options;
struct argv_array;

enum {
	RECURSE_SUBMODULES_ON_DEMAND = -1,
	RECURSE_SUBMODULES_OFF = 0,
	RECURSE_SUBMODULES_DEFAULT = 1,
	RECURSE_SUBMODULES_ON = 2
};

void set_diffopt_flags_from_submodule_config(struct diff_options *diffopt,
		const char *path);
int submodule_config(const char *var, const char *value, void *cb);
void gitmodules_config(void);
int parse_submodule_config_option(const char *var, const char *value);
void handle_ignore_submodules_arg(struct diff_options *diffopt, const char *);
int parse_fetch_recurse_submodules_arg(const char *opt, const char *arg);
void show_submodule_summary(FILE *f, const char *path,
		unsigned char one[20], unsigned char two[20],
		unsigned dirty_submodule,
		const char *del, const char *add, const char *reset);
void set_config_fetch_recurse_submodules(int value);
void check_for_new_submodule_commits(unsigned char new_sha1[20]);
int fetch_populated_submodules(const struct argv_array *options,
			       const char *prefix, int command_line_option,
			       int quiet);
unsigned is_submodule_modified(const char *path, int ignore_untracked);
int merge_submodule(unsigned char result[20], const char *path, const unsigned char base[20],
		    const unsigned char a[20], const unsigned char b[20], int search);
int find_unpushed_submodules(unsigned char new_sha1[20], const char *remotes_name,
		struct string_list *needs_pushing);
int push_unpushed_submodules(unsigned char new_sha1[20], const char *remotes_name);

#endif
