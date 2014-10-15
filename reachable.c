#include "cache.h"
#include "refs.h"
#include "tag.h"
#include "commit.h"
#include "blob.h"
#include "diff.h"
#include "revision.h"
#include "reachable.h"
#include "cache-tree.h"
#include "progress.h"
#include "list-objects.h"

struct connectivity_progress {
	struct progress *progress;
	unsigned long count;
};

static void update_progress(struct connectivity_progress *cp)
{
	cp->count++;
	if ((cp->count & 1023) == 0)
		display_progress(cp->progress, cp->count);
}

static int add_one_reflog_ent(unsigned char *osha1, unsigned char *nsha1,
		const char *email, unsigned long timestamp, int tz,
		const char *message, void *cb_data)
{
	struct object *object;
	struct rev_info *revs = (struct rev_info *)cb_data;

	object = parse_object(osha1);
	if (object)
		add_pending_object(revs, object, "");
	object = parse_object(nsha1);
	if (object)
		add_pending_object(revs, object, "");
	return 0;
}

static int add_one_ref(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *object = parse_object_or_die(sha1, path);
	struct rev_info *revs = (struct rev_info *)cb_data;

	add_pending_object(revs, object, "");

	return 0;
}

static int add_one_reflog(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	for_each_reflog_ent(path, add_one_reflog_ent, cb_data);
	return 0;
}

static void add_one_tree(const unsigned char *sha1, struct rev_info *revs)
{
	struct tree *tree = lookup_tree(sha1);
	if (tree)
		add_pending_object(revs, &tree->object, "");
}

static void add_cache_tree(struct cache_tree *it, struct rev_info *revs)
{
	int i;

	if (it->entry_count >= 0)
		add_one_tree(it->sha1, revs);
	for (i = 0; i < it->subtree_nr; i++)
		add_cache_tree(it->down[i]->cache_tree, revs);
}

static void add_cache_refs(struct rev_info *revs)
{
	int i;

	read_cache();
	for (i = 0; i < active_nr; i++) {
		/*
		 * The index can contain blobs and GITLINKs, GITLINKs are hashes
		 * that don't actually point to objects in the repository, it's
		 * almost guaranteed that they are NOT blobs, so we don't call
		 * lookup_blob() on them, to avoid populating the hash table
		 * with invalid information
		 */
		if (S_ISGITLINK(active_cache[i]->ce_mode))
			continue;

		lookup_blob(active_cache[i]->sha1);
		/*
		 * We could add the blobs to the pending list, but quite
		 * frankly, we don't care. Once we've looked them up, and
		 * added them as objects, we've really done everything
		 * there is to do for a blob
		 */
	}
	if (active_cache_tree)
		add_cache_tree(active_cache_tree, revs);
}

/*
 * The traversal will have already marked us as SEEN, so we
 * only need to handle any progress reporting here.
 */
static void mark_object(struct object *obj, const struct name_path *path,
			const char *name, void *data)
{
	update_progress(data);
}

static void mark_commit(struct commit *c, void *data)
{
	mark_object(&c->object, NULL, NULL, data);
}

void mark_reachable_objects(struct rev_info *revs, int mark_reflog,
			    struct progress *progress)
{
	struct connectivity_progress cp;

	/*
	 * Set up revision parsing, and mark us as being interested
	 * in all object types, not just commits.
	 */
	revs->tag_objects = 1;
	revs->blob_objects = 1;
	revs->tree_objects = 1;

	/* Add all refs from the index file */
	add_cache_refs(revs);

	/* Add all external refs */
	for_each_ref(add_one_ref, revs);

	/* detached HEAD is not included in the list above */
	head_ref(add_one_ref, revs);

	/* Add all reflog info */
	if (mark_reflog)
		for_each_reflog(add_one_reflog, revs);

	cp.progress = progress;
	cp.count = 0;

	/*
	 * Set up the revision walk - this will move all commits
	 * from the pending list to the commit walking list.
	 */
	if (prepare_revision_walk(revs))
		die("revision walk setup failed");
	traverse_commit_list(revs, mark_commit, mark_object, &cp);
	display_progress(cp.progress, cp.count);
}
