#ifndef SUBMODULE_H
#define SUBMODULE_H

void show_submodule_summary(FILE *f, const char *path,
		unsigned char one[20], unsigned char two[20],
		unsigned dirty_submodule,
		const char *del, const char *add, const char *reset);
int is_submodule_modified(const char *path);

#endif
