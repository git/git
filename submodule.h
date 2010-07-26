#ifndef SUBMODULE_H
#define SUBMODULE_H

struct diff_options;

void set_diffopt_flags_from_submodule_config(struct diff_options *diffopt,
		const char *path);
int parse_submodule_config_option(const char *var, const char *value);
void handle_ignore_submodules_arg(struct diff_options *diffopt, const char *);
void show_submodule_summary(FILE *f, const char *path,
		unsigned char one[20], unsigned char two[20],
		unsigned dirty_submodule,
		const char *del, const char *add, const char *reset);
unsigned is_submodule_modified(const char *path, int ignore_untracked);

#endif
