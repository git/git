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

static int add_one_ref(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	struct object *object = parse_object_or_die(sha1, path);
	struct rev_info *revs = (struct rev_info *)cb_data;

	add_pending_object(revs, object, "");

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
		struct blob *blob;

		/*
		 * The index can contain blobs and GITLINKs, GITLINKs are hashes
		 * that don't actually point to objects in the repository, it's
		 * almost guaranteed that they are NOT blobs, so we don't call
		 * lookup_blob() on them, to avoid populating the hash table
		 * with invalid information
		 */
		if (S_ISGITLINK(active_cache[i]->ce_mode))
			continue;

		blob = lookup_blob(active_cache[i]->sha1);
		if (blob)
			blob->object.flags |= SEEN;

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

struct recent_data {
	struct rev_info *revs;
	unsigned long timestamp;
};

static void add_recent_object(const unsigned char *sha1,
			      unsigned long mtime,
			      struct recent_data *data)
{
	struct object *obj;
	enum object_type type;

	if (mtime <= data->timestamp)
		return;

	/*
	 * We do not want to call parse_object here, because
	 * inflating blobs and trees could be very expensive.
	 * However, we do need to know the correct type for
	 * later processing, and the revision machinery expects
	 * commits and tags to have been parsed.
	 */
	type = sha1_object_info(sha1, NULL);
	if (type < 0)
		die("unable to get object info for %s", sha1_to_hex(sha1));

	switch (type) {
	case OBJ_TAG:
	case OBJ_COMMIT:
		obj = parse_object_or_die(sha1, NULL);
		break;
	case OBJ_TREE:
		obj = (struct object *)lookup_tree(sha1);
		break;
	case OBJ_BLOB:
		obj = (struct object *)lookup_blob(sha1);
		break;
	default:
		die("unknown object type for %s: %s",
		    sha1_to_hex(sha1), typename(type));
	}

	if (!obj)
		die("unable to lookup %s", sha1_to_hex(sha1));

	add_pending_object(data->revs, obj, "");
}

static int add_recent_loose(const unsigned char *sha1,
			    const char *path, void *data)
{
	struct stat st;
	struct object *obj = lookup_object(sha1);

	if (obj && obj->flags & SEEN)
		return 0;

	if (stat(path, &st) < 0) {
		/*
		 * It's OK if an object went away during our iteration; this
		 * could be due to a simultaneous repack. But anything else
		 * we should abort, since we might then fail to mark objects
		 * which should not be pruned.
		 */
		if (errno == ENOENT)
			return 0;
		return error("unable to stat %s: %s",
			     sha1_to_hex(sha1), strerror(errno));
	}

	add_recent_object(sha1, st.st_mtime, data);
	return 0;
}

static int add_recent_packed(const unsigned char *sha1,
			     struct packed_git *p, uint32_t pos,
			     void *data)
{
	struct object *obj = lookup_object(sha1);

	if (obj && obj->flags & SEEN)
		return 0;
	add_recent_object(sha1, p->mtime, data);
	return 0;
}

int add_unseen_recent_objects_to_traversal(struct rev_info *revs,
					   unsigned long timestamp)
{
	struct recent_data data;
	int r;

	data.revs = revs;
	data.timestamp = timestamp;

	r = for_each_loose_object(add_recent_loose, &data);
	if (r)
		return r;
	return for_each_packed_object(add_recent_packed, &data);
}

void mark_reachable_objects(struct rev_info *revs, int mark_reflog,
			    unsigned long mark_recent,
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
		add_reflogs_to_pending(revs, 0);

	cp.progress = progress;
	cp.count = 0;

	/*
	 * Set up the revision walk - this will move all commits
	 * from the pending list to the commit walking list.
	 */
	if (prepare_revision_walk(revs))
		die("revision walk setup failed");
	traverse_commit_list(revs, mark_commit, mark_object, &cp);

	if (mark_recent) {
		revs->ignore_missing_links = 1;
		if (add_unseen_recent_objects_to_traversal(revs, mark_recent))
			die("unable to mark recent objects");
		if (prepare_revision_walk(revs))
			die("revision walk setup failed");
		traverse_commit_list(revs, mark_commit, mark_object, &cp);
	}

	display_progress(cp.progress, cp.count);
}
