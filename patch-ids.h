#ifndef PATCH_IDS_H
#define PATCH_IDS_H

struct patch_id {
	struct hashmap_entry ent;
	unsigned char patch_id[GIT_SHA1_RAWSZ];
	struct commit *commit;
};

struct patch_ids {
	struct hashmap patches;
	struct diff_options diffopts;
};

int commit_patch_id(struct commit *commit, struct diff_options *options,
		    unsigned char *sha1, int);
int init_patch_ids(struct patch_ids *);
int free_patch_ids(struct patch_ids *);
struct patch_id *add_commit_patch_id(struct commit *, struct patch_ids *);
struct patch_id *has_commit_patch_id(struct commit *, struct patch_ids *);

#endif /* PATCH_IDS_H */
