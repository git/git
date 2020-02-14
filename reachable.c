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
#include "packfile.h"
#include "worktree.h"
#include "object-store.h"
#include "pack-bitmap.h"

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

static int add_one_ref(const char *path, const struct object_id *oid,
		       int flag, void *cb_data)
{
	struct rev_info *revs = (struct rev_info *)cb_data;
	struct object *object;

	if ((flag & REF_ISSYMREF) && (flag & REF_ISBROKEN)) {
		warning("symbolic ref is dangling: %s", path);
		return 0;
	}

	object = parse_object_or_die(oid, path);
	add_pending_object(revs, object, "");

	return 0;
}

/*
 * The traversal will have already marked us as SEEN, so we
 * only need to handle any progress reporting here.
 */
static void mark_object(struct object *obj, const char *name, void *data)
{
	update_progress(data);
}

static void mark_commit(struct commit *c, void *data)
{
	mark_object(&c->object, NULL, data);
}

struct recent_data {
	struct rev_info *revs;
	timestamp_t timestamp;
};

static void add_recent_object(const struct object_id *oid,
			      timestamp_t mtime,
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
	type = oid_object_info(the_repository, oid, NULL);
	if (type < 0)
		die("unable to get object info for %s", oid_to_hex(oid));

	switch (type) {
	case OBJ_TAG:
	case OBJ_COMMIT:
		obj = parse_object_or_die(oid, NULL);
		break;
	case OBJ_TREE:
		obj = (struct object *)lookup_tree(the_repository, oid);
		break;
	case OBJ_BLOB:
		obj = (struct object *)lookup_blob(the_repository, oid);
		break;
	default:
		die("unknown object type for %s: %s",
		    oid_to_hex(oid), type_name(type));
	}

	if (!obj)
		die("unable to lookup %s", oid_to_hex(oid));

	add_pending_object(data->revs, obj, "");
}

static int add_recent_loose(const struct object_id *oid,
			    const char *path, void *data)
{
	struct stat st;
	struct object *obj = lookup_object(the_repository, oid);

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
		return error_errno("unable to stat %s", oid_to_hex(oid));
	}

	add_recent_object(oid, st.st_mtime, data);
	return 0;
}

static int add_recent_packed(const struct object_id *oid,
			     struct packed_git *p, uint32_t pos,
			     void *data)
{
	struct object *obj = lookup_object(the_repository, oid);

	if (obj && obj->flags & SEEN)
		return 0;
	add_recent_object(oid, p->mtime, data);
	return 0;
}

int add_unseen_recent_objects_to_traversal(struct rev_info *revs,
					   timestamp_t timestamp)
{
	struct recent_data data;
	int r;

	data.revs = revs;
	data.timestamp = timestamp;

	r = for_each_loose_object(add_recent_loose, &data,
				  FOR_EACH_OBJECT_LOCAL_ONLY);
	if (r)
		return r;
	return for_each_packed_object(add_recent_packed, &data,
				      FOR_EACH_OBJECT_LOCAL_ONLY);
}

static void *lookup_object_by_type(struct repository *r,
				   const struct object_id *oid,
				   enum object_type type)
{
	switch (type) {
	case OBJ_COMMIT:
		return lookup_commit(r, oid);
	case OBJ_TREE:
		return lookup_tree(r, oid);
	case OBJ_TAG:
		return lookup_tag(r, oid);
	case OBJ_BLOB:
		return lookup_blob(r, oid);
	default:
		die("BUG: unknown object type %d", type);
	}
}

static int mark_object_seen(const struct object_id *oid,
			     enum object_type type,
			     int exclude,
			     uint32_t name_hash,
			     struct packed_git *found_pack,
			     off_t found_offset)
{
	struct object *obj = lookup_object_by_type(the_repository, oid, type);
	if (!obj)
		die("unable to create object '%s'", oid_to_hex(oid));

	obj->flags |= SEEN;
	return 0;
}

void mark_reachable_objects(struct rev_info *revs, int mark_reflog,
			    timestamp_t mark_recent, struct progress *progress)
{
	struct connectivity_progress cp;
	struct bitmap_index *bitmap_git;

	/*
	 * Set up revision parsing, and mark us as being interested
	 * in all object types, not just commits.
	 */
	revs->tag_objects = 1;
	revs->blob_objects = 1;
	revs->tree_objects = 1;

	/* Add all refs from the index file */
	add_index_objects_to_pending(revs, 0);

	/* Add all external refs */
	for_each_ref(add_one_ref, revs);

	/* detached HEAD is not included in the list above */
	head_ref(add_one_ref, revs);
	other_head_refs(add_one_ref, revs);

	/* Add all reflog info */
	if (mark_reflog)
		add_reflogs_to_pending(revs, 0);

	cp.progress = progress;
	cp.count = 0;

	bitmap_git = prepare_bitmap_walk(revs, NULL);
	if (bitmap_git) {
		traverse_bitmap_commit_list(bitmap_git, revs, mark_object_seen);
		free_bitmap_index(bitmap_git);
		return;
	}

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
