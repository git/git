#ifndef RERERE_H
#define RERERE_H

#include "string-list.h"

struct pathspec;
struct repository;

#define RERERE_AUTOUPDATE   01
#define RERERE_NOAUTOUPDATE 02
#define RERERE_READONLY     04

/*
 * Marks paths that have been hand-resolved and added to the
 * index. Set in the util field of such paths after calling
 * rerere_remaining.
 */
extern void *RERERE_RESOLVED;

struct rerere_dir;
struct rerere_id {
	struct rerere_dir *collection;
	int variant;
};

int setup_rerere(struct repository *,struct string_list *, int);
#ifndef NO_THE_REPOSITORY_COMPATIBILITY_MACROS
#define rerere(flags) repo_rerere(the_repository, flags)
#endif
int repo_rerere(struct repository *, int);
/*
 * Given the conflict ID and the name of a "file" used for replaying
 * the recorded resolution (e.g. "preimage", "postimage"), return the
 * path to that filesystem entity.  With "file" specified with NULL,
 * return the path to the directory that houses these files.
 */
const char *rerere_path(const struct rerere_id *, const char *file);
int rerere_forget(struct repository *, struct pathspec *);
int rerere_remaining(struct repository *, struct string_list *);
void rerere_clear(struct repository *, struct string_list *);
void rerere_gc(struct repository *, struct string_list *);

#define OPT_RERERE_AUTOUPDATE(v) OPT_UYN(0, "rerere-autoupdate", (v), \
	N_("update the index with reused conflict resolution if possible"))

#endif
