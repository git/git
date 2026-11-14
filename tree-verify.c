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

/* Values are enum tree_state, stored as a byte to keep the map small. */
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

/* The entries of a tree that still need to be verified. */
struct work_list {
	struct work_item *items;
	size_t nr, alloc;
	bool has_subtrees;
};

static void work_list_clear(struct work_list *list)
{
	for (size_t i = 0; i < list->nr; i++)
		oid_array_clear(&list->items[i].parent_trees);
	free(list->items);
}

struct verify_state {
	kh_oid_tree_t *trees;
	struct oidset trusted_blobs;
	size_t trees_loaded;
	size_t blobs_checked;
	int exclude_promisor_objects;

	/*
	 * Scratch space for reading one tree at a time.  It lives here
	 * rather than on the stack to keep the frame of the recursive
	 * verify_tree() small.
	 */
	struct tree_desc desc;
	struct name_entry entry;
};

/*
 * Merge-walk the work list against one base tree entry, recording
 * same-path parent subtrees as recursive comparison bases.
 * Returns the updated work-list cursor.
 */
static size_t collect_subtree_bases(struct work_list *list, size_t wi,
				    const struct name_entry *entry)
{
	while (wi < list->nr) {
		struct work_item *item = &list->items[wi];
		int cmp = base_name_compare(item->entry.path,
					    item->entry.pathlen,
					    item->entry.mode,
					    entry->path, entry->pathlen,
					    entry->mode);
		if (cmp > 0)
			break;
		if (cmp < 0) {
			wi++;
			continue;
		}
		if (S_ISDIR(item->entry.mode) && S_ISDIR(entry->mode))
			oid_array_append(&item->parent_trees, &entry->oid);
		return wi + 1;
	}
	return wi;
}

/*
 * Report an object that could not be read, telling a missing object
 * apart from one that exists but cannot be read, e.g. because it is
 * corrupt.
 */
static int unreadable_object_error(struct repository *repo,
				   const struct object_id *oid,
				   const char *type)
{
	if (!odb_has_object(repo->objects, oid, 0))
		return error(_("missing %s object %s"), type, oid_to_hex(oid));
	return error(_("unable to read %s object %s"), type, oid_to_hex(oid));
}

static int verify_blob(struct repository *repo,
		       const struct object_id *oid,
		       struct verify_state *vs)
{
	struct object_info oi = OBJECT_INFO_INIT;
	enum object_type type;

	if (oidset_contains(&vs->trusted_blobs, oid))
		return 0;

	vs->blobs_checked++;
	oi.typep = &type;
	if (odb_read_object_info_extended(repo->objects, oid, &oi,
					  OBJECT_INFO_LOOKUP_REPLACE |
					  OBJECT_INFO_SKIP_FETCH_OBJECT)) {
		if (vs->exclude_promisor_objects &&
		    is_promisor_object(repo, oid))
			return 0;
		return unreadable_object_error(repo, oid, "blob");
	}
	if (type != OBJ_BLOB)
		return error(_("object %s is a %s, not a %s"),
			     oid_to_hex(oid), type_name(type), "blob");
	oidset_insert(&vs->trusted_blobs, oid);
	return 0;
}

/*
 * Read a tree object into *bufp.  Returns -1 if the object exists
 * but is not a tree.  Otherwise returns 0, leaving *bufp NULL if the
 * object could not be read.
 */
static int read_tree_object(struct object_database *odb,
			    const struct object_id *oid,
			    void **bufp, size_t *sizep)
{
	struct object_info oi = OBJECT_INFO_INIT;
	enum object_type type;

	oi.typep = &type;
	oi.sizep = sizep;
	oi.contentp = bufp;
	if (odb_read_object_info_extended(odb, oid, &oi,
					  OBJECT_INFO_LOOKUP_REPLACE |
					  OBJECT_INFO_SKIP_FETCH_OBJECT))
		*bufp = NULL;
	if (*bufp && type != OBJ_TREE) {
		FREE_AND_NULL(*bufp);
		return error(_("object %s is a %s, not a %s"),
			     oid_to_hex(oid), type_name(type), "tree");
	}
	return 0;
}

/* List the entries of a tree that are not yet known to be trusted. */
static void list_untrusted_entries(struct verify_state *vs,
				   const struct object_id *tree_oid,
				   void *tree_buf, size_t tree_size,
				   struct work_list *list)
{
	struct name_entry *entry = &vs->entry;

	init_tree_desc(&vs->desc, tree_oid, tree_buf, tree_size);
	while (tree_entry(&vs->desc, entry)) {
		if (S_ISGITLINK(entry->mode))
			continue;
		if (S_ISDIR(entry->mode)) {
			if (tree_map_get(vs->trees, &entry->oid) >=
			    TREE_TRUSTED)
				continue;
			list->has_subtrees = true;
		} else {
			if (oidset_contains(&vs->trusted_blobs, &entry->oid))
				continue;
		}
		ALLOC_GROW(list->items, list->nr + 1, list->alloc);
		memset(&list->items[list->nr], 0, sizeof(list->items[0]));
		list->items[list->nr++].entry = *entry;
	}
}

/*
 * Scan one trusted base tree: expand it if that has not happened yet,
 * and collect its same-path subtrees as bases for untrusted subtrees.
 */
static int scan_base_tree(struct repository *repo, struct verify_state *vs,
			  const struct object_id *base_oid,
			  struct work_list *list)
{
	int expanded = tree_map_get(vs->trees, base_oid) >= TREE_EXPANDED;
	struct name_entry *entry = &vs->entry;
	size_t wi = 0, base_size;
	void *base_buf;

	if (expanded && !list->has_subtrees)
		return 0;

	if (read_tree_object(repo->objects, base_oid, &base_buf, &base_size))
		return -1;
	if (!base_buf) {
		if (vs->exclude_promisor_objects &&
		    is_promisor_object(repo, base_oid))
			return 0;
		return unreadable_object_error(repo, base_oid, "tree");
	}

	vs->trees_loaded++;
	init_tree_desc(&vs->desc, base_oid, base_buf, base_size);

	while (tree_entry(&vs->desc, entry)) {
		if (S_ISGITLINK(entry->mode))
			continue;

		/* Same-path subtree as a base for an untrusted one. */
		if (list->has_subtrees)
			wi = collect_subtree_bases(list, wi, entry);

		/* Expand: the base is trusted, so are its entries. */
		if (!expanded) {
			if (S_ISDIR(entry->mode))
				tree_map_add(vs->trees, &entry->oid,
					     TREE_TRUSTED);
			else
				oidset_insert(&vs->trusted_blobs,
					      &entry->oid);
		}
	}

	if (!expanded)
		tree_map_add(vs->trees, base_oid, TREE_EXPANDED);

	free(base_buf);
	return 0;
}

/*
 * Verify a tree against the trusted base trees, loading as little as
 * possible:
 *
 * First list the entries that are not yet known to be trusted.  When
 * the parent trees have been expanded before, only the changed entries
 * remain.
 *
 * Then scan the trusted base trees (the same-path trees of the parent
 * commits).  A base that has not been expanded yet is read so that its
 * entries can be marked trusted; this often shows that listed entries
 * are trusted after all.  An already expanded base is still read when
 * there are untrusted subtrees, because its subtree at the same path is
 * what lets the untrusted subtree skip its own unchanged entries in
 * turn.
 *
 * Finally check that the remaining blobs exist, and recurse into the
 * remaining subtrees with the same-path subtrees as their bases.
 */
static int verify_tree(struct repository *repo,
		       const struct object_id *tree_oid,
		       const struct oid_array *trusted_bases,
		       struct verify_state *vs, int depth)
{
	struct work_list list = { 0 };
	size_t i, tree_size;
	void *tree_buf;
	int ret = 0;

	/*
	 * Enforce core.maxTreeDepth like process_tree() in list-objects.c,
	 * so that the full and incremental checks reject the same trees.
	 */
	if (depth > repo->settings.max_allowed_tree_depth)
		return error(_("exceeded maximum allowed tree depth"));

	if (tree_map_get(vs->trees, tree_oid) >= TREE_TRUSTED)
		return 0;

	if (read_tree_object(repo->objects, tree_oid, &tree_buf, &tree_size))
		return -1;
	if (!tree_buf) {
		/*
		 * A missing tree that is promised can be fetched on demand,
		 * so it is not an error, as in the full check.
		 */
		if (vs->exclude_promisor_objects &&
		    is_promisor_object(repo, tree_oid))
			return 0;
		return unreadable_object_error(repo, tree_oid, "tree");
	}

	vs->trees_loaded++;
	list_untrusted_entries(vs, tree_oid, tree_buf, tree_size, &list);

	/* Scan the base trees, unless nothing is left to verify. */
	for (i = 0; list.nr && i < trusted_bases->nr; i++) {
		ret = scan_base_tree(repo, vs, &trusted_bases->oid[i], &list);
		if (ret)
			goto done;
	}

	/*
	 * Check the remaining entries in tree order.  Verifying a subtree
	 * can show that later entries are trusted, which then makes them
	 * cheap to skip.
	 */
	for (i = 0; i < list.nr && !ret; i++) {
		struct work_item *item = &list.items[i];

		if (S_ISDIR(item->entry.mode))
			ret = verify_tree(repo, &item->entry.oid,
					  &item->parent_trees, vs, depth + 1);
		else
			ret = verify_blob(repo, &item->entry.oid, vs);
	}
	if (!ret)
		tree_map_add(vs->trees, tree_oid, TREE_EXPANDED);

done:
	free(tree_buf);
	work_list_clear(&list);
	return ret;
}

static int verify_commit_tree(struct repository *repo,
			      struct commit *commit,
			      struct verify_state *vs)
{
	struct oid_array parent_trees = OID_ARRAY_INIT;
	const struct object_id *tree_oid;
	struct commit_list *p;
	int ret;

	/*
	 * Parent trees are trusted: boundary parents are already
	 * connected, and earlier incoming parents were verified
	 * first due to the topological processing order.
	 *
	 * The revision walk skips missing parents that are promised
	 * (see process_parents()), so do the same here.  Such a parent
	 * contributes no base, which only means less of the new tree
	 * can be skipped.
	 */
	for (p = commit->parents; p; p = p->next) {
		if (repo_parse_commit_gently(repo, p->item, 1) < 0) {
			if (vs->exclude_promisor_objects &&
			    is_promisor_object(repo, &p->item->object.oid))
				continue;
			ret = error(_("unable to parse commit %s"),
				    oid_to_hex(&p->item->object.oid));
			goto out;
		}
		tree_oid = get_commit_tree_oid(p->item);
		if (!tree_oid) {
			ret = error(_("unable to load root tree for commit %s"),
				    oid_to_hex(&p->item->object.oid));
			goto out;
		}
		tree_map_add(vs->trees, tree_oid, TREE_TRUSTED);
		oid_array_append(&parent_trees, tree_oid);
	}

	tree_oid = get_commit_tree_oid(commit);
	if (!tree_oid) {
		ret = error(_("unable to load root tree for commit %s"),
			    oid_to_hex(&commit->object.oid));
		goto out;
	}
	ret = verify_tree(repo, tree_oid, &parent_trees, vs, 0);
out:
	oid_array_clear(&parent_trees);
	return ret;
}

int verify_commits_incremental(struct repository *repo,
			       struct commit_list **commits,
			       int exclude_promisor_objects)
{
	struct verify_state vs = { 0 };
	struct commit_list *iter;
	unsigned nr_before;
	int ret = 0;

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
	if (commit_list_count(*commits) < nr_before) {
		ret = error(_("cycle detected in incoming commit graph"));
		goto out;
	}

	*commits = commit_list_reverse(*commits);

	for (iter = *commits; iter && !ret; iter = iter->next)
		ret = verify_commit_tree(repo, iter->item, &vs);

out:
	kh_destroy_oid_tree(vs.trees);
	oidset_clear(&vs.trusted_blobs);
	trace2_data_intmax("connectivity", repo,
			   "trees_loaded", (intmax_t)vs.trees_loaded);
	trace2_data_intmax("connectivity", repo,
			   "blobs_checked", (intmax_t)vs.blobs_checked);
	return ret;
}
