#ifndef RERERE_H
#define RERERE_H

#include "string-list.h"

#define RERERE_AUTOUPDATE   01
#define RERERE_NOAUTOUPDATE 02

/*
 * Marks paths that have been hand-resolved and added to the
 * index. Set in the util field of such paths after calling
 * rerere_remaining.
 */
extern void *RERERE_RESOLVED;

extern int setup_rerere(struct string_list *, int);
extern int rerere(int);
extern const char *rerere_path(const char *hex, const char *file);
extern int rerere_forget(const char **);
extern int rerere_remaining(struct string_list *);
extern void rerere_clear(struct string_list *);
extern void rerere_gc(struct string_list *);

#define OPT_RERERE_AUTOUPDATE(v) OPT_UYN(0, "rerere-autoupdate", (v), \
	"update the index with reused conflict resolution if possible")

#endif
