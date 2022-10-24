#ifndef PATCH_IDS_H
#define PATCH_IDS_H

#include "diff.h"
#include "hashmap.h"

struct commit;
struct object_id;
struct repository;

struct patch_id {
	struct hashmap_entry ent;
	struct object_id patch_id;
	struct commit *commit;
};

struct patch_ids {
	struct hashmap patches;
	struct diff_options diffopts;
};

int commit_patch_id(struct commit *commit, struct diff_options *options,
		    struct object_id *oid, int, int);
int init_patch_ids(struct repository *, struct patch_ids *);
int free_patch_ids(struct patch_ids *);

/* Add a patch_id for a single commit to the set. */
struct patch_id *add_commit_patch_id(struct commit *, struct patch_ids *);

/* Returns true if the patch-id of "commit" is present in the set. */
int has_commit_patch_id(struct commit *commit, struct patch_ids *);

/*
 * Iterate over all commits in the set whose patch id matches that of
 * "commit", like:
 *
 *   struct patch_id *cur;
 *   for (cur = patch_id_iter_first(commit, ids);
 *        cur;
 *        cur = patch_id_iter_next(cur, ids) {
 *           ... look at cur->commit
 *   }
 */
struct patch_id *patch_id_iter_first(struct commit *commit, struct patch_ids *);
struct patch_id *patch_id_iter_next(struct patch_id *cur, struct patch_ids *);

#endif /* PATCH_IDS_H */
