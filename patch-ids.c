#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "sha1-lookup.h"
#include "patch-ids.h"

static int patch_id_defined(struct commit *commit)
{
	/* must be 0 or 1 parents */
	return !commit->parents || !commit->parents->next;
}

int commit_patch_id(struct commit *commit, struct diff_options *options,
		    struct object_id *oid, int diff_header_only)
{
	if (!patch_id_defined(commit))
		return -1;

	if (commit->parents)
		diff_tree_oid(&commit->parents->item->object.oid,
			      &commit->object.oid, "", options);
	else
		diff_root_tree_oid(&commit->object.oid, "", options);
	diffcore_std(options);
	return diff_flush_patch_id(options, oid, diff_header_only);
}

/*
 * When we cannot load the full patch-id for both commits for whatever
 * reason, the function returns -1 (i.e. return error(...)). Despite
 * the "neq" in the name of this function, the caller only cares about
 * the return value being zero (a and b are equivalent) or non-zero (a
 * and b are different), and returning non-zero would keep both in the
 * result, even if they actually were equivalent, in order to err on
 * the side of safety.  The actual value being negative does not have
 * any significance; only that it is non-zero matters.
 */
static int patch_id_neq(const void *cmpfn_data,
			const void *entry,
			const void *entry_or_key,
			const void *unused_keydata)
{
	/* NEEDSWORK: const correctness? */
	struct diff_options *opt = (void *)cmpfn_data;
	struct patch_id *a = (void *)entry;
	struct patch_id *b = (void *)entry_or_key;

	if (is_null_oid(&a->patch_id) &&
	    commit_patch_id(a->commit, opt, &a->patch_id, 0))
		return error("Could not get patch ID for %s",
			oid_to_hex(&a->commit->object.oid));
	if (is_null_oid(&b->patch_id) &&
	    commit_patch_id(b->commit, opt, &b->patch_id, 0))
		return error("Could not get patch ID for %s",
			oid_to_hex(&b->commit->object.oid));
	return !oideq(&a->patch_id, &b->patch_id);
}

int init_patch_ids(struct patch_ids *ids)
{
	memset(ids, 0, sizeof(*ids));
	diff_setup(&ids->diffopts);
	ids->diffopts.detect_rename = 0;
	ids->diffopts.flags.recursive = 1;
	diff_setup_done(&ids->diffopts);
	hashmap_init(&ids->patches, patch_id_neq, &ids->diffopts, 256);
	return 0;
}

int free_patch_ids(struct patch_ids *ids)
{
	hashmap_free(&ids->patches, 1);
	return 0;
}

static int init_patch_id_entry(struct patch_id *patch,
			       struct commit *commit,
			       struct patch_ids *ids)
{
	struct object_id header_only_patch_id;

	patch->commit = commit;
	if (commit_patch_id(commit, &ids->diffopts, &header_only_patch_id, 1))
		return -1;

	hashmap_entry_init(patch, sha1hash(header_only_patch_id.hash));
	return 0;
}

struct patch_id *has_commit_patch_id(struct commit *commit,
				     struct patch_ids *ids)
{
	struct patch_id patch;

	if (!patch_id_defined(commit))
		return NULL;

	memset(&patch, 0, sizeof(patch));
	if (init_patch_id_entry(&patch, commit, ids))
		return NULL;

	return hashmap_get(&ids->patches, &patch, NULL);
}

struct patch_id *add_commit_patch_id(struct commit *commit,
				     struct patch_ids *ids)
{
	struct patch_id *key;

	if (!patch_id_defined(commit))
		return NULL;

	key = xcalloc(1, sizeof(*key));
	if (init_patch_id_entry(key, commit, ids)) {
		free(key);
		return NULL;
	}

	hashmap_add(&ids->patches, key);
	return key;
}
