#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
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
#include "object-store-ll.h"
#include "pack-bitmap.h"
#include "pack-mtimes.h"
#include "config.h"
#include "run-command.h"

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
static void mark_object(struct object *obj UNUSED,
			const char *name UNUSED,
			void *data)
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
	report_recent_object_fn *cb;
	int ignore_in_core_kept_packs;

	struct oidset extra_recent_oids;
	int extra_recent_oids_loaded;
};

static int run_one_gc_recent_objects_hook(struct oidset *set,
					    const char *args)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT;
	FILE *out;
	int ret = 0;

	cmd.use_shell = 1;
	cmd.out = -1;

	strvec_push(&cmd.args, args);

	if (start_command(&cmd))
		return -1;

	out = xfdopen(cmd.out, "r");
	while (strbuf_getline(&buf, out) != EOF) {
		struct object_id oid;
		const char *rest;

		if (parse_oid_hex(buf.buf, &oid, &rest) || *rest) {
			ret = error(_("invalid extra cruft tip: '%s'"), buf.buf);
			break;
		}

		oidset_insert(set, &oid);
	}

	fclose(out);
	ret |= finish_command(&cmd);

	strbuf_release(&buf);
	return ret;
}

static void load_gc_recent_objects(struct recent_data *data)
{
	const struct string_list *programs;
	int ret = 0;
	size_t i;

	data->extra_recent_oids_loaded = 1;

	if (git_config_get_string_multi("gc.recentobjectshook", &programs))
		return;

	for (i = 0; i < programs->nr; i++) {
		ret = run_one_gc_recent_objects_hook(&data->extra_recent_oids,
						       programs->items[i].string);
		if (ret)
			die(_("unable to enumerate additional recent objects"));
	}
}

static int obj_is_recent(const struct object_id *oid, timestamp_t mtime,
			 struct recent_data *data)
{
	if (mtime > data->timestamp)
		return 1;

	if (!data->extra_recent_oids_loaded)
		load_gc_recent_objects(data);
	return oidset_contains(&data->extra_recent_oids, oid);
}

static void add_recent_object(const struct object_id *oid,
			      struct packed_git *pack,
			      off_t offset,
			      timestamp_t mtime,
			      struct recent_data *data)
{
	struct object *obj;
	enum object_type type;

	if (!obj_is_recent(oid, mtime, data))
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
	if (data->cb)
		data->cb(obj, pack, offset, mtime);
}

static int want_recent_object(struct recent_data *data,
			      const struct object_id *oid)
{
	if (data->ignore_in_core_kept_packs &&
	    has_object_kept_pack(oid, IN_CORE_KEEP_PACKS))
		return 0;
	return 1;
}

static int add_recent_loose(const struct object_id *oid,
			    const char *path, void *data)
{
	struct stat st;
	struct object *obj;

	if (!want_recent_object(data, oid))
		return 0;

	obj = lookup_object(the_repository, oid);

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

	add_recent_object(oid, NULL, 0, st.st_mtime, data);
	return 0;
}

static int add_recent_packed(const struct object_id *oid,
			     struct packed_git *p,
			     uint32_t pos,
			     void *data)
{
	struct object *obj;
	timestamp_t mtime = p->mtime;

	if (!want_recent_object(data, oid))
		return 0;

	obj = lookup_object(the_repository, oid);

	if (obj && obj->flags & SEEN)
		return 0;
	if (p->is_cruft) {
		if (load_pack_mtimes(p) < 0)
			die(_("could not load cruft pack .mtimes"));
		mtime = nth_packed_mtime(p, pos);
	}
	add_recent_object(oid, p, nth_packed_object_offset(p, pos), mtime, data);
	return 0;
}

int add_unseen_recent_objects_to_traversal(struct rev_info *revs,
					   timestamp_t timestamp,
					   report_recent_object_fn *cb,
					   int ignore_in_core_kept_packs)
{
	struct recent_data data;
	enum for_each_object_flags flags;
	int r;

	data.revs = revs;
	data.timestamp = timestamp;
	data.cb = cb;
	data.ignore_in_core_kept_packs = ignore_in_core_kept_packs;

	oidset_init(&data.extra_recent_oids, 0);
	data.extra_recent_oids_loaded = 0;

	r = for_each_loose_object(add_recent_loose, &data,
				  FOR_EACH_OBJECT_LOCAL_ONLY);
	if (r)
		goto done;

	flags = FOR_EACH_OBJECT_LOCAL_ONLY | FOR_EACH_OBJECT_PACK_ORDER;
	if (ignore_in_core_kept_packs)
		flags |= FOR_EACH_OBJECT_SKIP_IN_CORE_KEPT_PACKS;

	r = for_each_packed_object(add_recent_packed, &data, flags);

done:
	oidset_clear(&data.extra_recent_oids);

	return r;
}

static int mark_object_seen(const struct object_id *oid,
			     enum object_type type,
			     int exclude UNUSED,
			     uint32_t name_hash UNUSED,
			     struct packed_git *found_pack UNUSED,
			     off_t found_offset UNUSED)
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

	bitmap_git = prepare_bitmap_walk(revs, 0);
	if (bitmap_git) {
		traverse_bitmap_commit_list(bitmap_git, revs, mark_object_seen);
		free_bitmap_index(bitmap_git);
	} else {
		if (prepare_revision_walk(revs))
			die("revision walk setup failed");
		traverse_commit_list(revs, mark_commit, mark_object, &cp);
	}

	if (mark_recent) {
		revs->ignore_missing_links = 1;
		if (add_unseen_recent_objects_to_traversal(revs, mark_recent,
							   NULL, 0))
			die("unable to mark recent objects");
		if (prepare_revision_walk(revs))
			die("revision walk setup failed");
		traverse_commit_list(revs, mark_commit, mark_object, &cp);
	}

	display_progress(cp.progress, cp.count);
}
