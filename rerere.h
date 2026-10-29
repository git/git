#ifndef RERERE_H
#define RERERE_H

#include "gettext.h"
#include "lockfile.h"
#include "string-list.h"
#include "strmap.h"

struct pathspec;
struct repository;

#define RERERE_AUTOUPDATE   01
#define RERERE_NOAUTOUPDATE 02
#define RERERE_READONLY     04
/* If MERGE_RR.lock is taken, return -1 as if rerere were disabled */
#define RERERE_NOWAIT       010

/*
 * Marks paths that have been hand-resolved and added to the
 * index. Set in the util field of such paths after calling
 * rerere_remaining.
 */
extern void *RERERE_RESOLVED;

struct rerere_cache {
	struct repository *repo;

	/* Automatically update cleanly resolved paths to the index. */
	int autoupdate;

	struct strmap dirs;

	struct lock_file write_lock;
};

struct rerere_dir;
struct rerere_id {
	struct rerere_dir *collection;
	int variant;
};

int rerere_cache_setup(struct rerere_cache *cache,
		       struct repository *r,
		       struct string_list *merge_rr,
		       int flags);
void rerere_cache_release(struct rerere_cache *cache);

/*
 * The main entry point that is called internally from codepaths that
 * perform mergy operations, possibly leaving conflicted index entries
 * and working tree files. It records (and optionally updates) resolved
 * conflicts and reuses recorded resolutions for unresolved conflicts.
 */
int rerere(struct repository *r, int flags);

/*
 * Given the conflict ID and the name of a "file" used for replaying
 * the recorded resolution (e.g. "preimage", "postimage"), return the
 * path to that filesystem entity.  With "file" specified with NULL,
 * return the path to the directory that houses these files.
 */
const char *rerere_path(struct repository *,
			struct strbuf *buf, const struct rerere_id *,
			const char *file);
int rerere_forget(struct repository *, struct pathspec *);
int rerere_remaining(struct repository *, struct string_list *);
void rerere_clear(struct repository *, struct string_list *);
void rerere_gc(struct repository *, struct string_list *, int);

/*
 * Check whether garbage collection for rerere entries is needed, which is
 * the case when there's at least `limit` stale entries that would be pruned.
 */
bool rerere_gc_needed(struct repository *r, size_t limit);

#define OPT_RERERE_AUTOUPDATE(v) OPT_UYN(0, "rerere-autoupdate", (v), \
	N_("update the index with reused conflict resolution if possible"))

#endif
