#include "git-compat-util.h"
#include "commit.h"
#include "gettext.h"
#include "hex.h"
#include "khash.h"
#include "object.h"
#include "odb.h"
#include "oid-array.h"
#include "oidset.h"
#include "tree.h"
#include "tree-walk.h"
#include "tree-verify.h"
#include "packfile.h"
#include "trace2.h"

enum tree_state {
	TREE_UNTRUSTED = 0,
	TREE_TRUSTED   = 1,
	TREE_EXPANDED  = 2,
};

KHASH_INIT(oid_tree, struct object_id, unsigned char, 1,
	   oidhash_by_value, oideq_by_value)

static enum tree_state tree_map_get(kh_oid_tree_t *m,
				    const struct object_id *oid)
{
	khint_t pos = kh_get_oid_tree(m, *oid);
	if (pos == kh_end(m))
		return TREE_UNTRUSTED;
	return kh_val(m, pos);
}

static void tree_map_add(kh_oid_tree_t *m, const struct object_id *oid,
			 enum tree_state state)
{
	int added;
	khint_t pos = kh_put_oid_tree(m, *oid, &added);
	if (added)
		kh_val(m, pos) = state;
	else if (state > kh_val(m, pos))
		kh_val(m, pos) = state;
}

struct work_item {
	struct name_entry entry;
	struct oid_array parent_trees;
};

struct verify_state {
	kh_oid_tree_t *trees;
	struct oidset trusted_blobs;
	int trees_loaded;
	int blobs_checked;
	int exclude_promisor_objects;
};

/*
 * Merge-walk the work list against one base tree entry, recording
 * same-path parent subtrees as recursive comparison bases.
 * Returns the updated work-list cursor.
 */
static size_t collect_subtree_bases(struct work_item *work, size_t nr_work,
				    size_t wi, const struct name_entry *entry)
{
	while (wi < nr_work) {
		int cmp = base_name_compare(
			work[wi].entry.path, work[wi].entry.pathlen,
			work[wi].entry.mode,
			entry->path, entry->pathlen,
			entry->mode);
		if (cmp > 0)
			break;
		if (cmp < 0) {
			wi++;
			continue;
		}
		if (S_ISDIR(work[wi].entry.mode) &&
		    S_ISDIR(entry->mode))
			oid_array_append(&work[wi].parent_trees,
					 &entry->oid);
		return wi + 1;
	}
	return wi;
}

static void verify_blob(struct repository *repo,
			const struct object_id *oid,
			struct verify_state *vs)
{
	int type;

	if (oidset_contains(&vs->trusted_blobs, oid))
		return;

	vs->blobs_checked++;
	type = odb_read_object_info(repo->objects, oid, NULL);
	if (type == OBJ_BLOB) {
		oidset_insert(&vs->trusted_blobs, oid);
		return;
	}
	if (type >= 0)
		die(_("object %s is a %s, not a %s"),
		    oid_to_hex(oid), type_name(type), "blob");
	if (vs->exclude_promisor_objects &&
	    is_promisor_object(repo, oid))
		return;
	die(_("missing blob object '%s'"), oid_to_hex(oid));
}

static void *read_tree_object(struct object_database *odb,
			      const struct object_id *oid,
			      size_t *sizep)
{
	enum object_type type;
	void *buf = odb_read_object(odb, oid, &type, sizep);

	if (buf && type != OBJ_TREE) {
		free(buf);
		die(_("object %s is a %s, not a %s"),
		    oid_to_hex(oid), type_name(type), "tree");
	}
	return buf;
}

static void verify_tree(struct repository *repo,
			const struct object_id *new_tree_oid,
			const struct oid_array *base_trees,
			struct verify_state *vs, int depth)
{
	struct tree_desc desc;
	struct name_entry entry;
	struct work_item *work = NULL;
	size_t nr_work = 0, alloc_work = 0;
	int need_subtree_bases = 0;
	size_t i, tree_size;
	void *tree_buf;

	if (depth > repo->settings.max_allowed_tree_depth)
		die(_("exceeded maximum allowed tree depth"));

	if (tree_map_get(vs->trees, new_tree_oid) >= TREE_TRUSTED)
		return;

	tree_buf = read_tree_object(repo->objects, new_tree_oid, &tree_size);
	if (!tree_buf) {
		if (vs->exclude_promisor_objects &&
		    is_promisor_object(repo, new_tree_oid))
			return;
		die(_("bad tree object %s"),
		    oid_to_hex(new_tree_oid));
	}

	vs->trees_loaded++;
	init_tree_desc(&desc, new_tree_oid, tree_buf, tree_size);

	while (tree_entry(&desc, &entry)) {
		if (S_ISGITLINK(entry.mode))
			continue;
		if (S_ISDIR(entry.mode)) {
			if (tree_map_get(vs->trees, &entry.oid) >= TREE_TRUSTED)
				continue;
			need_subtree_bases = 1;
		} else {
			if (oidset_contains(&vs->trusted_blobs, &entry.oid))
				continue;
		}
		ALLOC_GROW(work, nr_work + 1, alloc_work);
		memset(&work[nr_work], 0, sizeof(work[nr_work]));
		work[nr_work].entry = entry;
		nr_work++;
	}

	if (!nr_work) {
		free(tree_buf);
		goto done;
	}

	for (i = 0; base_trees && i < base_trees->nr; i++) {
		const struct object_id *base_oid = &base_trees->oid[i];
		int expanded = tree_map_get(vs->trees, base_oid) >= TREE_EXPANDED;
		struct tree_desc base_desc;
		struct name_entry scan_entry;
		size_t wi = 0, base_size;
		void *base_buf;

		if (expanded && !need_subtree_bases)
			continue;

		base_buf = read_tree_object(repo->objects, base_oid,
					    &base_size);
		if (!base_buf) {
			if (vs->exclude_promisor_objects &&
			    is_promisor_object(repo, base_oid))
				continue;
			die(_("bad tree object %s"),
			    oid_to_hex(base_oid));
		}

		vs->trees_loaded++;
		init_tree_desc(&base_desc, base_oid, base_buf, base_size);

		while (tree_entry(&base_desc, &scan_entry)) {
			if (S_ISGITLINK(scan_entry.mode))
				continue;

			if (need_subtree_bases)
				wi = collect_subtree_bases(work, nr_work,
							   wi, &scan_entry);

			if (!expanded) {
				if (S_ISDIR(scan_entry.mode))
					tree_map_add(vs->trees,
						     &scan_entry.oid,
						     TREE_TRUSTED);
				else
					oidset_insert(&vs->trusted_blobs,
						      &scan_entry.oid);
			}
		}

		if (!expanded)
			tree_map_add(vs->trees, base_oid, TREE_EXPANDED);

		free(base_buf);
	}

	for (i = 0; i < nr_work; i++) {
		if (S_ISDIR(work[i].entry.mode))
			verify_tree(repo, &work[i].entry.oid,
				    &work[i].parent_trees, vs,
				    depth + 1);
		else
			verify_blob(repo, &work[i].entry.oid, vs);
	}

	free(tree_buf);

done:
	tree_map_add(vs->trees, new_tree_oid, TREE_EXPANDED);
	for (i = 0; i < nr_work; i++)
		oid_array_clear(&work[i].parent_trees);
	free(work);
}

static void verify_commit_tree(struct repository *repo,
			       struct commit *commit,
			       struct verify_state *vs)
{
	struct oid_array base_trees = OID_ARRAY_INIT;
	const struct object_id *tree_oid;
	struct commit_list *p;

	/*
	 * Parent trees are trusted: boundary parents are already
	 * connected, and earlier incoming parents were verified
	 * first due to the topological processing order.
	 */
	for (p = commit->parents; p; p = p->next) {
		parse_commit_or_die(p->item);
		tree_oid = get_commit_tree_oid(p->item);
		if (!tree_oid)
			die(_("unable to load root tree for commit %s"),
			    oid_to_hex(&p->item->object.oid));
		tree_map_add(vs->trees, tree_oid, TREE_TRUSTED);
		oid_array_append(&base_trees, tree_oid);
	}

	tree_oid = get_commit_tree_oid(commit);
	if (!tree_oid)
		die(_("unable to load root tree for commit %s"),
		    oid_to_hex(&commit->object.oid));
	verify_tree(repo, tree_oid, &base_trees, vs, 0);
	oid_array_clear(&base_trees);
}

void verify_commits_incremental(struct repository *repo,
				struct commit_list **commits,
				int exclude_promisor_objects)
{
	struct verify_state vs = { 0 };
	struct commit_list *iter;
	unsigned nr_before;

	if (repo->fetch_if_missing)
		BUG("verify_commits_incremental must not be called "
		    "with fetch_if_missing set");

	vs.trees = kh_init_oid_tree();
	vs.exclude_promisor_objects = exclude_promisor_objects;

	/*
	 * Ancestors must be verified before descendants so that parent
	 * trees can be trusted without re-verification.  Sort explicitly
	 * rather than relying on the caller's ordering.
	 *
	 * sort_in_topological_order() silently drops cycle members,
	 * so explicitly check if the size has changed.
	 */
	nr_before = commit_list_count(*commits);
	sort_in_topological_order(commits, REV_SORT_IN_GRAPH_ORDER);
	if (commit_list_count(*commits) < nr_before)
		die(_("cycle detected in incoming commit graph"));

	*commits = commit_list_reverse(*commits);

	for (iter = *commits; iter; iter = iter->next)
		verify_commit_tree(repo, iter->item, &vs);

	kh_destroy_oid_tree(vs.trees);
	oidset_clear(&vs.trusted_blobs);
	trace2_data_intmax("connectivity", repo,
			   "trees_loaded", vs.trees_loaded);
	trace2_data_intmax("connectivity", repo,
			   "blobs_checked", vs.blobs_checked);
}
