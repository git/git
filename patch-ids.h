#ifndef PATCH_IDS_H
#define PATCH_IDS_H

struct patch_id {
	unsigned char patch_id[20];
	char seen;
};

struct patch_ids {
	struct diff_options diffopts;
	int nr, alloc;
	struct patch_id **table;
	struct patch_id_bucket *patches;
};

int init_patch_ids(struct patch_ids *);
int free_patch_ids(struct patch_ids *);
struct patch_id *add_commit_patch_id(struct commit *, struct patch_ids *);
struct patch_id *has_commit_patch_id(struct commit *, struct patch_ids *);

#endif /* PATCH_IDS_H */
