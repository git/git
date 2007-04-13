#include "cache.h"
#include "diff.h"
#include "commit.h"
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

static uint32_t take2(const unsigned char *id)
{
	return ((id[0] << 8) | id[1]);
}

/*
 * Conventional binary search loop looks like this:
 *
 *      do {
 *              int mi = (lo + hi) / 2;
 *              int cmp = "entry pointed at by mi" minus "target";
 *              if (!cmp)
 *                      return (mi is the wanted one)
 *              if (cmp > 0)
 *                      hi = mi; "mi is larger than target"
 *              else
 *                      lo = mi+1; "mi is smaller than target"
 *      } while (lo < hi);
 *
 * The invariants are:
 *
 * - When entering the loop, lo points at a slot that is never
 *   above the target (it could be at the target), hi points at a
 *   slot that is guaranteed to be above the target (it can never
 *   be at the target).
 *
 * - We find a point 'mi' between lo and hi (mi could be the same
 *   as lo, but never can be the same as hi), and check if it hits
 *   the target.  There are three cases:
 *
 *    - if it is a hit, we are happy.
 *
 *    - if it is strictly higher than the target, we update hi with
 *      it.
 *
 *    - if it is strictly lower than the target, we update lo to be
 *      one slot after it, because we allow lo to be at the target.
 *
 * When choosing 'mi', we do not have to take the "middle" but
 * anywhere in between lo and hi, as long as lo <= mi < hi is
 * satisfied.  When we somehow know that the distance between the
 * target and lo is much shorter than the target and hi, we could
 * pick mi that is much closer to lo than the midway.
 */
static int patch_pos(struct patch_id **table, int nr, const unsigned char *id)
{
	int hi = nr;
	int lo = 0;
	int mi = 0;

	if (!nr)
		return -1;

	if (nr != 1) {
		unsigned lov, hiv, miv, ofs;

		for (ofs = 0; ofs < 18; ofs += 2) {
			lov = take2(table[0]->patch_id + ofs);
			hiv = take2(table[nr-1]->patch_id + ofs);
			miv = take2(id + ofs);
			if (miv < lov)
				return -1;
			if (hiv < miv)
				return -1 - nr;
			if (lov != hiv) {
				/*
				 * At this point miv could be equal
				 * to hiv (but id could still be higher);
				 * the invariant of (mi < hi) should be
				 * kept.
				 */
				mi = (nr-1) * (miv - lov) / (hiv - lov);
				if (lo <= mi && mi < hi)
					break;
				die("oops");
			}
		}
		if (18 <= ofs)
			die("cannot happen -- lo and hi are identical");
	}

	do {
		int cmp;
		cmp = hashcmp(table[mi]->patch_id, id);
		if (!cmp)
			return mi;
		if (cmp > 0)
			hi = mi;
		else
			lo = mi + 1;
		mi = (hi + lo) / 2;
	} while (lo < hi);
	return -lo-1;
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
	ids->diffopts.recursive = 1;
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
