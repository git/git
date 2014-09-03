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

static void process_blob(struct blob *blob,
			 struct object_array *p,
			 struct name_path *path,
			 const char *name,
			 struct connectivity_progress *cp)
{
	struct object *obj = &blob->object;

	if (!blob)
		die("bad blob object");
	if (obj->flags & SEEN)
		return;
	obj->flags |= SEEN;
	update_progress(cp);
	/* Nothing to do, really .. The blob lookup was the important part */
}

static void process_gitlink(const unsigned char *sha1,
			    struct object_array *p,
			    struct name_path *path,
			    const char *name)
{
	/* I don't think we want to recurse into this, really. */
}

static void process_tree(struct tree *tree,
			 struct object_array *p,
			 struct name_path *path,
			 const char *name,
			 struct connectivity_progress *cp)
{
	struct object *obj = &tree->object;
	struct tree_desc desc;
	struct name_entry entry;
	struct name_path me;

	if (!tree)
		die("bad tree object");
	if (obj->flags & SEEN)
		return;
	obj->flags |= SEEN;
	update_progress(cp);
	if (parse_tree(tree) < 0)
		die("bad tree object %s", sha1_to_hex(obj->sha1));
	add_object(obj, p, path, name);
	me.up = path;
	me.elem = name;
	me.elem_len = strlen(name);

	init_tree_desc(&desc, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		if (S_ISDIR(entry.mode))
			process_tree(lookup_tree(entry.sha1), p, &me, entry.path, cp);
		else if (S_ISGITLINK(entry.mode))
			process_gitlink(entry.sha1, p, &me, entry.path);
		else
			process_blob(lookup_blob(entry.sha1), p, &me, entry.path, cp);
	}
	free_tree_buffer(tree);
}

static void process_tag(struct tag *tag, struct object_array *p,
			const char *name, struct connectivity_progress *cp)
{
	struct object *obj = &tag->object;

	if (obj->flags & SEEN)
		return;
	obj->flags |= SEEN;
	update_progress(cp);

	if (parse_tag(tag) < 0)
		die("bad tag object %s", sha1_to_hex(obj->sha1));
	if (tag->tagged)
		add_object(tag->tagged, p, NULL, name);
}

static void walk_commit_list(struct rev_info *revs,
			     struct connectivity_progress *cp)
{
	int i;
	struct commit *commit;
	struct object_array objects = OBJECT_ARRAY_INIT;

	/* Walk all commits, process their trees */
	while ((commit = get_revision(revs)) != NULL) {
		process_tree(commit->tree, &objects, NULL, "", cp);
		update_progress(cp);
	}

	/* Then walk all the pending objects, recursively processing them too */
	for (i = 0; i < revs->pending.nr; i++) {
		struct object_array_entry *pending = revs->pending.objects + i;
		struct object *obj = pending->item;
		const char *name = pending->name;
		if (obj->type == OBJ_TAG) {
			process_tag((struct tag *) obj, &objects, name, cp);
			continue;
		}
		if (obj->type == OBJ_TREE) {
			process_tree((struct tree *)obj, &objects, NULL, name, cp);
			continue;
		}
		if (obj->type == OBJ_BLOB) {
			process_blob((struct blob *)obj, &objects, NULL, name, cp);
			continue;
		}
		die("unknown pending object %s (%s)", sha1_to_hex(obj->sha1), name);
	}
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
	walk_commit_list(revs, &cp);
	display_progress(cp.progress, cp.count);
}
