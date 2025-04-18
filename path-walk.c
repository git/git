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
#include "list-objects.h"
#include "object.h"
#include "oid-array.h"
#include "prio-queue.h"
#include "repository.h"
#include "revision.h"
#include "string-list.h"
#include "strmap.h"
#include "tag.h"
#include "trace2.h"
#include "tree.h"
#include "tree-walk.h"

static const char *root_path = "";

struct type_and_oid_list {
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
	 * Store the current list of paths in a priority queue,
	 * using object type as a sorting mechanism, mostly to
	 * make sure blobs are popped off the stack first. No
	 * other sort is made, so within each object type it acts
	 * like a stack and performs a DFS within the trees.
	 *
	 * Use path_stack_pushed to indicate whether a path
	 * was previously added to path_stack.
	 */
	struct prio_queue path_stack;
	struct strset path_stack_pushed;
};

static int compare_by_type(const void *one, const void *two, void *cb_data)
{
	struct type_and_oid_list *list1, *list2;
	const char *str1 = one;
	const char *str2 = two;
	struct path_walk_context *ctx = cb_data;

	list1 = strmap_get(&ctx->paths_to_lists, str1);
	list2 = strmap_get(&ctx->paths_to_lists, str2);

	/*
	 * If object types are equal, then use path comparison.
	 */
	if (!list1 || !list2 || list1->type == list2->type)
		return strcmp(str1, str2);

	/* Prefer tags to be popped off first. */
	if (list1->type == OBJ_TAG)
		return -1;
	if (list2->type == OBJ_TAG)
		return 1;

	/* Prefer blobs to be popped off second. */
	if (list1->type == OBJ_BLOB)
		return -1;
	if (list2->type == OBJ_BLOB)
		return 1;

	return 0;
}

static void push_to_stack(struct path_walk_context *ctx,
			  const char *path)
{
	if (strset_contains(&ctx->path_stack_pushed, path))
		return;

	strset_add(&ctx->path_stack_pushed, path);
	prio_queue_put(&ctx->path_stack, xstrdup(path));
}

static int add_tree_entries(struct path_walk_context *ctx,
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
		error("bad tree object %s", oid_to_hex(oid));
		return -1;
	}

	strbuf_addstr(&path, base_path);
	base_len = path.len;

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
			BUG("invalid type for tree entry: %d", type);
		}

		if (!o) {
			error(_("failed to find object %s"),
			      oid_to_hex(&o->oid));
			return -1;
		}

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

		if (ctx->info->pl) {
			int dtype;
			enum pattern_match_result match;
			match = path_matches_pattern_list(path.buf, path.len,
							  path.buf + base_len, &dtype,
							  ctx->info->pl,
							  ctx->repo->index);

			if (ctx->info->pl->use_cone_patterns &&
			    match == NOT_MATCHED)
				continue;
			else if (!ctx->info->pl->use_cone_patterns &&
				 type == OBJ_BLOB &&
				 match != MATCHED)
				continue;
		}

		if (!(list = strmap_get(&ctx->paths_to_lists, path.buf))) {
			CALLOC_ARRAY(list, 1);
			list->type = type;
			strmap_put(&ctx->paths_to_lists, path.buf, list);
		}
		push_to_stack(ctx, path.buf);

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

	if (!list)
		BUG("provided path '%s' that had no associated list", path);

	if (!list->oids.nr)
		return 0;

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
			} else if (list->type == OBJ_BLOB) {
				struct blob *b = lookup_blob(ctx->repo,
							     &list->oids.oid[i]);
				if (b && !(b->object.flags & UNINTERESTING))
					list->maybe_interesting = 1;
			} else {
				/* Tags are always interesting if visited. */
				list->maybe_interesting = 1;
			}
		}

		/* We have confirmed that all objects are UNINTERESTING. */
		if (!list->maybe_interesting)
			return 0;
	}

	/* Evaluate function pointer on this data, if requested. */
	if ((list->type == OBJ_TREE && ctx->info->trees) ||
	    (list->type == OBJ_BLOB && ctx->info->blobs) ||
	    (list->type == OBJ_TAG && ctx->info->tags))
		ret = ctx->info->path_fn(path, &list->oids, list->type,
					ctx->info->path_fn_data);

	/* Expand data for children. */
	if (list->type == OBJ_TREE) {
		for (size_t i = 0; i < list->oids.nr; i++) {
			ret |= add_tree_entries(ctx,
					    path,
					    &list->oids.oid[i]);
		}
	}

	oid_array_clear(&list->oids);
	strmap_remove(&ctx->paths_to_lists, path, 1);
	return ret;
}

static void clear_paths_to_lists(struct strmap *map)
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

static struct repository *edge_repo;
static struct type_and_oid_list *edge_tree_list;

static void show_edge(struct commit *commit)
{
	struct tree *t = repo_get_commit_tree(edge_repo, commit);

	if (!t)
		return;

	if (commit->object.flags & UNINTERESTING)
		t->object.flags |= UNINTERESTING;

	if (t->object.flags & SEEN)
		return;
	t->object.flags |= SEEN;

	oid_array_append(&edge_tree_list->oids, &t->object.oid);
}

static int setup_pending_objects(struct path_walk_info *info,
				 struct path_walk_context *ctx)
{
	struct type_and_oid_list *tags = NULL;
	struct type_and_oid_list *tagged_blobs = NULL;
	struct type_and_oid_list *root_tree_list = NULL;

	if (info->tags)
		CALLOC_ARRAY(tags, 1);
	if (info->blobs)
		CALLOC_ARRAY(tagged_blobs, 1);
	if (info->trees)
		root_tree_list = strmap_get(&ctx->paths_to_lists, root_path);

	/*
	 * Pending objects include:
	 * * Commits at branch tips.
	 * * Annotated tags at tag tips.
	 * * Any kind of object at lightweight tag tips.
	 * * Trees and blobs in the index (with an associated path).
	 */
	for (size_t i = 0; i < info->revs->pending.nr; i++) {
		struct object_array_entry *pending = info->revs->pending.objects + i;
		struct object *obj = pending->item;

		/* Commits will be picked up by revision walk. */
		if (obj->type == OBJ_COMMIT)
			continue;

		/* Navigate annotated tag object chains. */
		while (obj->type == OBJ_TAG) {
			struct tag *tag = lookup_tag(info->revs->repo, &obj->oid);
			if (!tag) {
				error(_("failed to find tag %s"),
				      oid_to_hex(&obj->oid));
				return -1;
			}
			if (tag->object.flags & SEEN)
				break;
			tag->object.flags |= SEEN;

			if (tags)
				oid_array_append(&tags->oids, &obj->oid);
			obj = tag->tagged;
		}

		if (obj->type == OBJ_TAG)
			continue;

		/* We are now at a non-tag object. */
		if (obj->flags & SEEN)
			continue;
		obj->flags |= SEEN;

		switch (obj->type) {
		case OBJ_TREE:
			if (!info->trees)
				continue;
			if (pending->path) {
				struct type_and_oid_list *list;
				char *path = *pending->path ? xstrfmt("%s/", pending->path)
							    : xstrdup("");
				if (!(list = strmap_get(&ctx->paths_to_lists, path))) {
					CALLOC_ARRAY(list, 1);
					list->type = OBJ_TREE;
					strmap_put(&ctx->paths_to_lists, path, list);
				}
				oid_array_append(&list->oids, &obj->oid);
				free(path);
			} else {
				/* assume a root tree, such as a lightweight tag. */
				oid_array_append(&root_tree_list->oids, &obj->oid);
			}
			break;

		case OBJ_BLOB:
			if (!info->blobs)
				continue;
			if (pending->path) {
				struct type_and_oid_list *list;
				char *path = pending->path;
				if (!(list = strmap_get(&ctx->paths_to_lists, path))) {
					CALLOC_ARRAY(list, 1);
					list->type = OBJ_BLOB;
					strmap_put(&ctx->paths_to_lists, path, list);
				}
				oid_array_append(&list->oids, &obj->oid);
			} else {
				/* assume a root tree, such as a lightweight tag. */
				oid_array_append(&tagged_blobs->oids, &obj->oid);
			}
			break;

		case OBJ_COMMIT:
			/* Make sure it is in the object walk */
			if (obj != pending->item)
				add_pending_object(info->revs, obj, "");
			break;

		default:
			BUG("should not see any other type here");
		}
	}

	/*
	 * Add tag objects and tagged blobs if they exist.
	 */
	if (tagged_blobs) {
		if (tagged_blobs->oids.nr) {
			const char *tagged_blob_path = "/tagged-blobs";
			tagged_blobs->type = OBJ_BLOB;
			tagged_blobs->maybe_interesting = 1;
			strmap_put(&ctx->paths_to_lists, tagged_blob_path, tagged_blobs);
			push_to_stack(ctx, tagged_blob_path);
		} else {
			oid_array_clear(&tagged_blobs->oids);
			free(tagged_blobs);
		}
	}
	if (tags) {
		if (tags->oids.nr) {
			const char *tag_path = "/tags";
			tags->type = OBJ_TAG;
			tags->maybe_interesting = 1;
			strmap_put(&ctx->paths_to_lists, tag_path, tags);
			push_to_stack(ctx, tag_path);
		} else {
			oid_array_clear(&tags->oids);
			free(tags);
		}
	}

	return 0;
}

/**
 * Given the configuration of 'info', walk the commits based on 'info->revs' and
 * call 'info->path_fn' on each discovered path.
 *
 * Returns nonzero on an error.
 */
int walk_objects_by_path(struct path_walk_info *info)
{
	int ret;
	size_t commits_nr = 0, paths_nr = 0;
	struct commit *c;
	struct type_and_oid_list *root_tree_list;
	struct type_and_oid_list *commit_list;
	struct path_walk_context ctx = {
		.repo = info->revs->repo,
		.revs = info->revs,
		.info = info,
		.path_stack = {
			.compare = compare_by_type,
			.cb_data = &ctx
		},
		.path_stack_pushed = STRSET_INIT,
		.paths_to_lists = STRMAP_INIT
	};

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
	push_to_stack(&ctx, root_path);

	/*
	 * Set these values before preparing the walk to catch
	 * lightweight tags pointing to non-commits and indexed objects.
	 */
	info->revs->blob_objects = info->blobs;
	info->revs->tree_objects = info->trees;

	if (prepare_revision_walk(info->revs))
		die(_("failed to setup revision walk"));

	/*
	 * Walk trees to mark them as UNINTERESTING.
	 * This is particularly important when 'edge_aggressive' is set.
	 */
	info->revs->edge_hint_aggressive = info->edge_aggressive;
	edge_repo = info->revs->repo;
	edge_tree_list = root_tree_list;
	mark_edges_uninteresting(info->revs, show_edge,
				 info->prune_all_uninteresting);
	edge_repo = NULL;
	edge_tree_list = NULL;

	info->revs->blob_objects = info->revs->tree_objects = 0;

	trace2_region_enter("path-walk", "pending-walk", info->revs->repo);
	ret = setup_pending_objects(info, &ctx);
	trace2_region_leave("path-walk", "pending-walk", info->revs->repo);

	if (ret)
		return ret;

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

		if (!t) {
			error("could not find tree %s", oid_to_hex(oid));
			return -1;
		}

		if (t->object.flags & SEEN)
			continue;
		t->object.flags |= SEEN;
		oid_array_append(&root_tree_list->oids, oid);
	}

	trace2_data_intmax("path-walk", ctx.repo, "commits", commits_nr);
	trace2_region_leave("path-walk", "commit-walk", info->revs->repo);

	/* Track all commits. */
	if (info->commits && commit_list->oids.nr)
		ret = info->path_fn("", &commit_list->oids, OBJ_COMMIT,
				    info->path_fn_data);
	oid_array_clear(&commit_list->oids);
	free(commit_list);

	trace2_region_enter("path-walk", "path-walk", info->revs->repo);
	while (!ret && ctx.path_stack.nr) {
		char *path = prio_queue_get(&ctx.path_stack);
		paths_nr++;

		ret = walk_path(&ctx, path);

		free(path);
	}

	/* Are there paths remaining? Likely they are from indexed objects. */
	if (!strmap_empty(&ctx.paths_to_lists)) {
		struct hashmap_iter iter;
		struct strmap_entry *entry;

		strmap_for_each_entry(&ctx.paths_to_lists, &iter, entry)
			push_to_stack(&ctx, entry->key);

		while (!ret && ctx.path_stack.nr) {
			char *path = prio_queue_get(&ctx.path_stack);
			paths_nr++;

			ret = walk_path(&ctx, path);

			free(path);
		}
	}

	trace2_data_intmax("path-walk", ctx.repo, "paths", paths_nr);
	trace2_region_leave("path-walk", "path-walk", info->revs->repo);

	clear_paths_to_lists(&ctx.paths_to_lists);
	strset_clear(&ctx.path_stack_pushed);
	clear_prio_queue(&ctx.path_stack);
	return ret;
}

void path_walk_info_init(struct path_walk_info *info)
{
	struct path_walk_info empty = PATH_WALK_INFO_INIT;
	memcpy(info, &empty, sizeof(empty));
}

void path_walk_info_clear(struct path_walk_info *info)
{
	if (info->pl) {
		clear_pattern_list(info->pl);
		free(info->pl);
	}
}
