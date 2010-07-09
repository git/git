#ifndef RERERE_H
#define RERERE_H

#include "string-list.h"

#define RERERE_AUTOUPDATE   01
#define RERERE_NOAUTOUPDATE 02

extern int setup_rerere(struct string_list *, int);
extern int rerere(int);
extern const char *rerere_path(const char *hex, const char *file);
extern int has_rerere_resolution(const char *hex);
extern int rerere_forget(const char **);

#define OPT_RERERE_AUTOUPDATE(v) OPT_UYN(0, "rerere-autoupdate", (v), \
	"update the index with reused conflict resolution if possible")

#endif
