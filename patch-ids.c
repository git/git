#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "sha1-lookup.h"
#include "patch-ids.h"

static int commit_patch_id(struct commit *commit, struct diff_options *options,
		    unsigned char *sha1)
{
	if (commit->parents)
		diff_tree_sha1(commit->parents->item->object.sha1,
		               commit->object.sha1, "", options);
	else
		diff_root_tree_sha1(commit->object.sha1, "", options);
	diffcore_std(options);
	return diff_flush_patch_id(options, sha1);
}

static const unsigned char *patch_id_access(size_t index, void *table)
{
	struct patch_id **id_table = table;
	return id_table[index]->patch_id;
}

static int patch_pos(struct patch_id **table, int nr, const unsigned char *id)
{
	return sha1_pos(id, table, nr, patch_id_access);
}

#define BUCKET_SIZE 190 /* 190 * 21 = 3990, with slop close enough to 4K */
struct patch_id_bucket {
	struct patch_id_bucket *next;
	int nr;
	struct patch_id bucket[BUCKET_SIZE];
};

int init_patch_ids(struct patch_ids *ids)
{
	memset(ids, 0, sizeof(*ids));
	diff_setup(&ids->diffopts);
	DIFF_OPT_SET(&ids->diffopts, RECURSIVE);
	if (diff_setup_done(&ids->diffopts) < 0)
		return error("diff_setup_done failed");
	return 0;
}

int free_patch_ids(struct patch_ids *ids)
{
	struct patch_id_bucket *next, *patches;

	free(ids->table);
	for (patches = ids->patches; patches; patches = next) {
		next = patches->next;
		free(patches);
	}
	return 0;
}

static struct patch_id *add_commit(struct commit *commit,
				   struct patch_ids *ids,
				   int no_add)
{
	struct patch_id_bucket *bucket;
	struct patch_id *ent;
	unsigned char sha1[20];
	int pos;

	if (commit_patch_id(commit, &ids->diffopts, sha1))
		return NULL;
	pos = patch_pos(ids->table, ids->nr, sha1);
	if (0 <= pos)
		return ids->table[pos];
	if (no_add)
		return NULL;

	pos = -1 - pos;

	bucket = ids->patches;
	if (!bucket || (BUCKET_SIZE <= bucket->nr)) {
		bucket = xcalloc(1, sizeof(*bucket));
		bucket->next = ids->patches;
		ids->patches = bucket;
	}
	ent = &bucket->bucket[bucket->nr++];
	hashcpy(ent->patch_id, sha1);

	if (ids->alloc <= ids->nr) {
		ids->alloc = alloc_nr(ids->nr);
		ids->table = xrealloc(ids->table, sizeof(ent) * ids->alloc);
	}
	if (pos < ids->nr)
		memmove(ids->table + pos + 1, ids->table + pos,
			sizeof(ent) * (ids->nr - pos));
	ids->nr++;
	ids->table[pos] = ent;
	return ids->table[pos];
}

struct patch_id *has_commit_patch_id(struct commit *commit,
				     struct patch_ids *ids)
{
	return add_commit(commit, ids, 1);
}

struct patch_id *add_commit_patch_id(struct commit *commit,
				     struct patch_ids *ids)
{
	return add_commit(commit, ids, 0);
}
