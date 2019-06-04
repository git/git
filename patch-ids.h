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
		    struct object_id *oid, int);
int init_patch_ids(struct repository *, struct patch_ids *);
int free_patch_ids(struct patch_ids *);
struct patch_id *add_commit_patch_id(struct commit *, struct patch_ids *);
struct patch_id *has_commit_patch_id(struct commit *, struct patch_ids *);

#endif /* PATCH_IDS_H */
