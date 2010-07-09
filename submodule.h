#ifndef SUBMODULE_H
#define SUBMODULE_H

struct diff_options;

void handle_ignore_submodules_arg(struct diff_options *diffopt, const char *);
void show_submodule_summary(FILE *f, const char *path,
		unsigned char one[20], unsigned char two[20],
		unsigned dirty_submodule,
		const char *del, const char *add, const char *reset);
unsigned is_submodule_modified(const char *path, int ignore_untracked);

#endif
