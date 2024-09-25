/*
 * path-walk.c: implementation for path-based walks of the object graph.
 */
#include "git-compat-util.h"
#include "path-walk.h"
#include "blob.h"
#include "commit.h"
#include "dir.h"
#include "hashmap.h"
#include "hex.h"
#include "object.h"
#include "oid-array.h"
#include "revision.h"
#include "string-list.h"
#include "strmap.h"
#include "tag.h"
#include "trace2.h"
#include "tree.h"
#include "tree-walk.h"

struct type_and_oid_list
{
	enum object_type type;
	struct oid_array oids;
	int maybe_interesting;
};

#define TYPE_AND_OID_LIST_INIT { \
	.type = OBJ_NONE, 	 \
	.oids = OID_ARRAY_INIT	 \
}

struct path_walk_context {
	/**
	 * Repeats of data in 'struct path_walk_info' for
	 * access with fewer characters.
	 */
	struct repository *repo;
	struct rev_info *revs;
	struct path_walk_info *info;

	/**
	 * Map a path to a 'struct type_and_oid_list'
	 * containing the objects discovered at that
	 * path.
	 */
	struct strmap paths_to_lists;

	/**
	 * Store the current list of paths in a stack, to
	 * facilitate depth-first-search without recursion.
	 */
	struct string_list path_stack;
};

static int add_children(struct path_walk_context *ctx,
			const char *base_path,
			struct object_id *oid)
{
	struct tree_desc desc;
	struct name_entry entry;
	struct strbuf path = STRBUF_INIT;
	size_t base_len;
	struct tree *tree = lookup_tree(ctx->repo, oid);

	if (!tree) {
		error(_("failed to walk children of tree %s: not found"),
		      oid_to_hex(oid));
		return -1;
	} else if (parse_tree_gently(tree, 1)) {
		die("bad tree object %s", oid_to_hex(oid));
	}

	strbuf_addstr(&path, base_path);
	base_len = path.len;

	parse_tree(tree);
	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		struct type_and_oid_list *list;
		struct object *o;
		/* Not actually true, but we will ignore submodules later. */
		enum object_type type = S_ISDIR(entry.mode) ? OBJ_TREE : OBJ_BLOB;

		/* Skip submodules. */
		if (S_ISGITLINK(entry.mode))
			continue;

		/* If the caller doesn't want blobs, then don't bother. */
		if (!ctx->info->blobs && type == OBJ_BLOB)
			continue;

		if (type == OBJ_TREE) {
			struct tree *child = lookup_tree(ctx->repo, &entry.oid);
			o = child ? &child->object : NULL;
		} else if (type == OBJ_BLOB) {
			struct blob *child = lookup_blob(ctx->repo, &entry.oid);
			o = child ? &child->object : NULL;
		} else {
			/* Wrong type? */
			continue;
		}

		if (!o) /* report error?*/
			continue;

		/* Skip this object if already seen. */
		if (o->flags & SEEN)
			continue;
		o->flags |= SEEN;

		strbuf_setlen(&path, base_len);
		strbuf_add(&path, entry.path, entry.pathlen);

		/*
		 * Trees will end with "/" for concatenation and distinction
		 * from blobs at the same path.
		 */
		if (type == OBJ_TREE)
			strbuf_addch(&path, '/');

		if (!(list = strmap_get(&ctx->paths_to_lists, path.buf))) {
			CALLOC_ARRAY(list, 1);
			list->type = type;
			strmap_put(&ctx->paths_to_lists, path.buf, list);
			string_list_append(&ctx->path_stack, path.buf);
		}
		if (!(o->flags & UNINTERESTING))
			list->maybe_interesting = 1;
		oid_array_append(&list->oids, &entry.oid);
	}

	free_tree_buffer(tree);
	strbuf_release(&path);
	return 0;
}

/*
 * For each path in paths_to_explore, walk the trees another level
 * and add any found blobs to the batch (but only if they exist and
 * haven't been added yet).
 */
static int walk_path(struct path_walk_context *ctx,
		     const char *path)
{
	struct type_and_oid_list *list;
	int ret = 0;

	list = strmap_get(&ctx->paths_to_lists, path);

	if (ctx->info->prune_all_uninteresting) {
		/*
		 * This is true if all objects were UNINTERESTING
		 * when added to the list.
		 */
		if (!list->maybe_interesting)
			return 0;

		/*
		 * But it's still possible that the objects were set
		 * as UNINTERESTING after being added. Do a quick check.
		 */
		list->maybe_interesting = 0;
		for (size_t i = 0;
		     !list->maybe_interesting && i < list->oids.nr;
		     i++) {
			if (list->type == OBJ_TREE) {
				struct tree *t = lookup_tree(ctx->repo,
							     &list->oids.oid[i]);
				if (t && !(t->object.flags & UNINTERESTING))
					list->maybe_interesting = 1;
			} else {
				struct blob *b = lookup_blob(ctx->repo,
							     &list->oids.oid[i]);
				if (b && !(b->object.flags & UNINTERESTING))
					list->maybe_interesting = 1;
			}
		}

		/* We have confirmed that all objects are UNINTERESTING. */
		if (!list->maybe_interesting)
			return 0;
	}

	/* Evaluate function pointer on this data, if requested. */
	if ((list->type == OBJ_TREE && ctx->info->trees) ||
	    (list->type == OBJ_BLOB && ctx->info->blobs))
		ret = ctx->info->path_fn(path, &list->oids, list->type,
					ctx->info->path_fn_data);

	/* Expand data for children. */
	if (list->type == OBJ_TREE) {
		for (size_t i = 0; i < list->oids.nr; i++) {
			ret |= add_children(ctx,
					    path,
					    &list->oids.oid[i]);
		}
	}

	oid_array_clear(&list->oids);
	strmap_remove(&ctx->paths_to_lists, path, 1);
	return ret;
}

static void clear_strmap(struct strmap *map)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;

	hashmap_for_each_entry(&map->map, &iter, e, ent) {
		struct type_and_oid_list *list = e->value;
		oid_array_clear(&list->oids);
	}
	strmap_clear(map, 1);
	strmap_init(map);
}

/**
 * Given the configuration of 'info', walk the commits based on 'info->revs' and
 * call 'info->path_fn' on each discovered path.
 *
 * Returns nonzero on an error.
 */
int walk_objects_by_path(struct path_walk_info *info)
{
	const char *root_path = "";
	int ret = 0, has_uninteresting = 0;
	size_t commits_nr = 0, paths_nr = 0;
	struct commit *c;
	struct type_and_oid_list *root_tree_list;
	struct type_and_oid_list *commit_list;
	struct path_walk_context ctx = {
		.repo = info->revs->repo,
		.revs = info->revs,
		.info = info,
		.path_stack = STRING_LIST_INIT_DUP,
		.paths_to_lists = STRMAP_INIT
	};
	struct oidset root_tree_set = OIDSET_INIT;

	trace2_region_enter("path-walk", "commit-walk", info->revs->repo);

	CALLOC_ARRAY(commit_list, 1);
	commit_list->type = OBJ_COMMIT;

	if (info->tags)
		info->revs->tag_objects = 1;

	/* Insert a single list for the root tree into the paths. */
	CALLOC_ARRAY(root_tree_list, 1);
	root_tree_list->type = OBJ_TREE;
	root_tree_list->maybe_interesting = 1;
	strmap_put(&ctx.paths_to_lists, root_path, root_tree_list);

	/*
	 * Set these values before preparing the walk to catch
	 * lightweight tags pointing to non-commits.
	 */
	info->revs->blob_objects = info->blobs;
	info->revs->tree_objects = info->trees;

	if (prepare_revision_walk(info->revs))
		die(_("failed to setup revision walk"));

	info->revs->blob_objects = info->revs->tree_objects = 0;

	if (info->tags) {
		struct oid_array tagged_blob_list = OID_ARRAY_INIT;
		struct oid_array tags = OID_ARRAY_INIT;

		trace2_region_enter("path-walk", "tag-walk", info->revs->repo);

		/*
		 * Walk any pending objects at this point, but they should only
		 * be tags.
		 */
		for (size_t i = 0; i < info->revs->pending.nr; i++) {
			struct object_array_entry *pending = info->revs->pending.objects + i;
			struct object *obj = pending->item;

			if (obj->type == OBJ_COMMIT)
				continue;

			while (obj->type == OBJ_TAG) {
				struct tag *tag = lookup_tag(info->revs->repo,
							     &obj->oid);
				if (oid_array_lookup(&tags, &obj->oid) < 0)
					oid_array_append(&tags, &obj->oid);
				obj = tag->tagged;
			}

			switch (obj->type) {
			case OBJ_TREE:
				if (info->trees &&
				    oid_array_lookup(&root_tree_list->oids, &obj->oid) < 0)
					oid_array_append(&root_tree_list->oids, &obj->oid);
				break;

			case OBJ_BLOB:
				if (info->blobs &&
				    oid_array_lookup(&tagged_blob_list, &obj->oid) < 0)
					oid_array_append(&tagged_blob_list, &obj->oid);
				break;

			case OBJ_COMMIT:
				/* Make sure it is in the object walk */
				add_pending_object(info->revs, obj, "");
				break;

			default:
				BUG("should not see any other type here");
			}
		}

		info->path_fn("", &tags, OBJ_TAG, info->path_fn_data);

		if (tagged_blob_list.nr && info->blobs)
			info->path_fn("/tagged-blobs", &tagged_blob_list, OBJ_BLOB,
				      info->path_fn_data);

		trace2_data_intmax("path-walk", ctx.repo, "tags", tags.nr);
		trace2_region_leave("path-walk", "tag-walk", info->revs->repo);
		oid_array_clear(&tags);
		oid_array_clear(&tagged_blob_list);
	}

	while ((c = get_revision(info->revs))) {
		struct object_id *oid;
		struct tree *t;
		commits_nr++;

		if (info->commits)
			oid_array_append(&commit_list->oids,
					 &c->object.oid);

		/* If we only care about commits, then skip trees. */
		if (!info->trees && !info->blobs)
			continue;

		oid = get_commit_tree_oid(c);
		t = lookup_tree(info->revs->repo, oid);

		if (t) {
			oidset_insert(&root_tree_set, oid);
			oid_array_append(&root_tree_list->oids, oid);
		} else {
			warning("could not find tree %s", oid_to_hex(oid));
		}

		if (t && (c->object.flags & UNINTERESTING)) {
			t->object.flags |= UNINTERESTING;
			has_uninteresting = 1;
		}
	}

	trace2_data_intmax("path-walk", ctx.repo, "commits", commits_nr);
	trace2_region_leave("path-walk", "commit-walk", info->revs->repo);

	/* Track all commits. */
	if (info->commits)
		ret = info->path_fn("", &commit_list->oids, OBJ_COMMIT,
				    info->path_fn_data);
	oid_array_clear(&commit_list->oids);
	free(commit_list);

	/*
	 * Before performing a DFS of our paths and emitting them as interesting,
	 * do a full walk of the trees to distribute the UNINTERESTING bit. Use
	 * the sparse algorithm if prune_all_uninteresting was set.
	 */
	if (has_uninteresting) {
		trace2_region_enter("path-walk", "uninteresting-walk", info->revs->repo);
		if (info->prune_all_uninteresting)
			mark_trees_uninteresting_sparse(ctx.repo, &root_tree_set);
		else
			mark_trees_uninteresting_dense(ctx.repo, &root_tree_set);
		trace2_region_leave("path-walk", "uninteresting-walk", info->revs->repo);
	}
	oidset_clear(&root_tree_set);

	string_list_append(&ctx.path_stack, root_path);

	trace2_region_enter("path-walk", "path-walk", info->revs->repo);
	while (!ret && ctx.path_stack.nr) {
		char *path = ctx.path_stack.items[ctx.path_stack.nr - 1].string;
		ctx.path_stack.nr--;
		paths_nr++;

		ret = walk_path(&ctx, path);

		free(path);
	}
	trace2_data_intmax("path-walk", ctx.repo, "paths", paths_nr);
	trace2_region_leave("path-walk", "path-walk", info->revs->repo);

	clear_strmap(&ctx.paths_to_lists);
	string_list_clear(&ctx.path_stack, 0);
	return ret;
}
