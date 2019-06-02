#include "cache.h"
#include "dir.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "diff.h"
#include "tree-walk.h"
#include "revision.h"
#include "list-objects.h"
#include "list-objects-filter.h"
#include "list-objects-filter-options.h"
#include "oidmap.h"
#include "oidset.h"
#include "object-store.h"

/* Remember to update object flag allocation in object.h */
/*
 * FILTER_SHOWN_BUT_REVISIT -- we set this bit on tree objects
 * that have been shown, but should be revisited if they appear
 * in the traversal (until we mark it SEEN).  This is a way to
 * let us silently de-dup calls to show() in the caller.  This
 * is subtly different from the "revision.h:SHOWN" and the
 * "sha1-name.c:ONELINE_SEEN" bits.  And also different from
 * the non-de-dup usage in pack-bitmap.c
 */
#define FILTER_SHOWN_BUT_REVISIT (1<<21)

/*
 * A filter for list-objects to omit ALL blobs from the traversal.
 * And to OPTIONALLY collect a list of the omitted OIDs.
 */
struct filter_blobs_none_data {
	struct oidset *omits;
};

static enum list_objects_filter_result filter_blobs_none(
	struct repository *r,
	enum list_objects_filter_situation filter_situation,
	struct object *obj,
	const char *pathname,
	const char *filename,
	void *filter_data_)
{
	struct filter_blobs_none_data *filter_data = filter_data_;

	switch (filter_situation) {
	default:
		BUG("unknown filter_situation: %d", filter_situation);

	case LOFS_BEGIN_TREE:
		assert(obj->type == OBJ_TREE);
		/* always include all tree objects */
		return LOFR_MARK_SEEN | LOFR_DO_SHOW;

	case LOFS_END_TREE:
		assert(obj->type == OBJ_TREE);
		return LOFR_ZERO;

	case LOFS_BLOB:
		assert(obj->type == OBJ_BLOB);
		assert((obj->flags & SEEN) == 0);

		if (filter_data->omits)
			oidset_insert(filter_data->omits, &obj->oid);
		return LOFR_MARK_SEEN; /* but not LOFR_DO_SHOW (hard omit) */
	}
}

static void *filter_blobs_none__init(
	struct oidset *omitted,
	struct list_objects_filter_options *filter_options,
	filter_object_fn *filter_fn,
	filter_free_fn *filter_free_fn)
{
	struct filter_blobs_none_data *d = xcalloc(1, sizeof(*d));
	d->omits = omitted;

	*filter_fn = filter_blobs_none;
	*filter_free_fn = free;
	return d;
}

/*
 * A filter for list-objects to omit ALL trees and blobs from the traversal.
 * Can OPTIONALLY collect a list of the omitted OIDs.
 */
struct filter_trees_depth_data {
	struct oidset *omits;

	/*
	 * Maps trees to the minimum depth at which they were seen. It is not
	 * necessary to re-traverse a tree at deeper or equal depths than it has
	 * already been traversed.
	 *
	 * We can't use LOFR_MARK_SEEN for tree objects since this will prevent
	 * it from being traversed at shallower depths.
	 */
	struct oidmap seen_at_depth;

	unsigned long exclude_depth;
	unsigned long current_depth;
};

struct seen_map_entry {
	struct oidmap_entry base;
	size_t depth;
};

/* Returns 1 if the oid was in the omits set before it was invoked. */
static int filter_trees_update_omits(
	struct object *obj,
	struct filter_trees_depth_data *filter_data,
	int include_it)
{
	if (!filter_data->omits)
		return 0;

	if (include_it)
		return oidset_remove(filter_data->omits, &obj->oid);
	else
		return oidset_insert(filter_data->omits, &obj->oid);
}

static enum list_objects_filter_result filter_trees_depth(
	struct repository *r,
	enum list_objects_filter_situation filter_situation,
	struct object *obj,
	const char *pathname,
	const char *filename,
	void *filter_data_)
{
	struct filter_trees_depth_data *filter_data = filter_data_;
	struct seen_map_entry *seen_info;
	int include_it = filter_data->current_depth <
		filter_data->exclude_depth;
	int filter_res;
	int already_seen;

	/*
	 * Note that we do not use _MARK_SEEN in order to allow re-traversal in
	 * case we encounter a tree or blob again at a shallower depth.
	 */

	switch (filter_situation) {
	default:
		BUG("unknown filter_situation: %d", filter_situation);

	case LOFS_END_TREE:
		assert(obj->type == OBJ_TREE);
		filter_data->current_depth--;
		return LOFR_ZERO;

	case LOFS_BLOB:
		filter_trees_update_omits(obj, filter_data, include_it);
		return include_it ? LOFR_MARK_SEEN | LOFR_DO_SHOW : LOFR_ZERO;

	case LOFS_BEGIN_TREE:
		seen_info = oidmap_get(
			&filter_data->seen_at_depth, &obj->oid);
		if (!seen_info) {
			seen_info = xcalloc(1, sizeof(*seen_info));
			oidcpy(&seen_info->base.oid, &obj->oid);
			seen_info->depth = filter_data->current_depth;
			oidmap_put(&filter_data->seen_at_depth, seen_info);
			already_seen = 0;
		} else {
			already_seen =
				filter_data->current_depth >= seen_info->depth;
		}

		if (already_seen) {
			filter_res = LOFR_SKIP_TREE;
		} else {
			int been_omitted = filter_trees_update_omits(
				obj, filter_data, include_it);
			seen_info->depth = filter_data->current_depth;

			if (include_it)
				filter_res = LOFR_DO_SHOW;
			else if (filter_data->omits && !been_omitted)
				/*
				 * Must update omit information of children
				 * recursively; they have not been omitted yet.
				 */
				filter_res = LOFR_ZERO;
			else
				filter_res = LOFR_SKIP_TREE;
		}

		filter_data->current_depth++;
		return filter_res;
	}
}

static void filter_trees_free(void *filter_data) {
	struct filter_trees_depth_data *d = filter_data;
	if (!d)
		return;
	oidmap_free(&d->seen_at_depth, 1);
	free(d);
}

static void *filter_trees_depth__init(
	struct oidset *omitted,
	struct list_objects_filter_options *filter_options,
	filter_object_fn *filter_fn,
	filter_free_fn *filter_free_fn)
{
	struct filter_trees_depth_data *d = xcalloc(1, sizeof(*d));
	d->omits = omitted;
	oidmap_init(&d->seen_at_depth, 0);
	d->exclude_depth = filter_options->tree_exclude_depth;
	d->current_depth = 0;

	*filter_fn = filter_trees_depth;
	*filter_free_fn = filter_trees_free;
	return d;
}

/*
 * A filter for list-objects to omit large blobs.
 * And to OPTIONALLY collect a list of the omitted OIDs.
 */
struct filter_blobs_limit_data {
	struct oidset *omits;
	unsigned long max_bytes;
};

static enum list_objects_filter_result filter_blobs_limit(
	struct repository *r,
	enum list_objects_filter_situation filter_situation,
	struct object *obj,
	const char *pathname,
	const char *filename,
	void *filter_data_)
{
	struct filter_blobs_limit_data *filter_data = filter_data_;
	unsigned long object_length;
	enum object_type t;

	switch (filter_situation) {
	default:
		BUG("unknown filter_situation: %d", filter_situation);

	case LOFS_BEGIN_TREE:
		assert(obj->type == OBJ_TREE);
		/* always include all tree objects */
		return LOFR_MARK_SEEN | LOFR_DO_SHOW;

	case LOFS_END_TREE:
		assert(obj->type == OBJ_TREE);
		return LOFR_ZERO;

	case LOFS_BLOB:
		assert(obj->type == OBJ_BLOB);
		assert((obj->flags & SEEN) == 0);

		t = oid_object_info(r, &obj->oid, &object_length);
		if (t != OBJ_BLOB) { /* probably OBJ_NONE */
			/*
			 * We DO NOT have the blob locally, so we cannot
			 * apply the size filter criteria.  Be conservative
			 * and force show it (and let the caller deal with
			 * the ambiguity).
			 */
			goto include_it;
		}

		if (object_length < filter_data->max_bytes)
			goto include_it;

		if (filter_data->omits)
			oidset_insert(filter_data->omits, &obj->oid);
		return LOFR_MARK_SEEN; /* but not LOFR_DO_SHOW (hard omit) */
	}

include_it:
	if (filter_data->omits)
		oidset_remove(filter_data->omits, &obj->oid);
	return LOFR_MARK_SEEN | LOFR_DO_SHOW;
}

static void *filter_blobs_limit__init(
	struct oidset *omitted,
	struct list_objects_filter_options *filter_options,
	filter_object_fn *filter_fn,
	filter_free_fn *filter_free_fn)
{
	struct filter_blobs_limit_data *d = xcalloc(1, sizeof(*d));
	d->omits = omitted;
	d->max_bytes = filter_options->blob_limit_value;

	*filter_fn = filter_blobs_limit;
	*filter_free_fn = free;
	return d;
}

/*
 * A filter driven by a sparse-checkout specification to only
 * include blobs that a sparse checkout would populate.
 *
 * The sparse-checkout spec can be loaded from a blob with the
 * given OID or from a local pathname.  We allow an OID because
 * the repo may be bare or we may be doing the filtering on the
 * server.
 */
struct frame {
	/*
	 * defval is the usual default include/exclude value that
	 * should be inherited as we recurse into directories based
	 * upon pattern matching of the directory itself or of a
	 * containing directory.
	 */
	int defval;

	/*
	 * 1 if the directory (recursively) contains any provisionally
	 * omitted objects.
	 *
	 * 0 if everything (recursively) contained in this directory
	 * has been explicitly included (SHOWN) in the result and
	 * the directory may be short-cut later in the traversal.
	 */
	unsigned child_prov_omit : 1;
};

struct filter_sparse_data {
	struct oidset *omits;
	struct exclude_list el;

	size_t nr, alloc;
	struct frame *array_frame;
};

static enum list_objects_filter_result filter_sparse(
	struct repository *r,
	enum list_objects_filter_situation filter_situation,
	struct object *obj,
	const char *pathname,
	const char *filename,
	void *filter_data_)
{
	struct filter_sparse_data *filter_data = filter_data_;
	int val, dtype;
	struct frame *frame;

	switch (filter_situation) {
	default:
		BUG("unknown filter_situation: %d", filter_situation);

	case LOFS_BEGIN_TREE:
		assert(obj->type == OBJ_TREE);
		dtype = DT_DIR;
		val = is_excluded_from_list(pathname, strlen(pathname),
					    filename, &dtype, &filter_data->el,
					    r->index);
		if (val < 0)
			val = filter_data->array_frame[filter_data->nr].defval;

		ALLOC_GROW(filter_data->array_frame, filter_data->nr + 1,
			   filter_data->alloc);
		filter_data->nr++;
		filter_data->array_frame[filter_data->nr].defval = val;
		filter_data->array_frame[filter_data->nr].child_prov_omit = 0;

		/*
		 * A directory with this tree OID may appear in multiple
		 * places in the tree. (Think of a directory move or copy,
		 * with no other changes, so the OID is the same, but the
		 * full pathnames of objects within this directory are new
		 * and may match is_excluded() patterns differently.)
		 * So we cannot mark this directory as SEEN (yet), since
		 * that will prevent process_tree() from revisiting this
		 * tree object with other pathname prefixes.
		 *
		 * Only _DO_SHOW the tree object the first time we visit
		 * this tree object.
		 *
		 * We always show all tree objects.  A future optimization
		 * may want to attempt to narrow this.
		 */
		if (obj->flags & FILTER_SHOWN_BUT_REVISIT)
			return LOFR_ZERO;
		obj->flags |= FILTER_SHOWN_BUT_REVISIT;
		return LOFR_DO_SHOW;

	case LOFS_END_TREE:
		assert(obj->type == OBJ_TREE);
		assert(filter_data->nr > 0);

		frame = &filter_data->array_frame[filter_data->nr];
		filter_data->nr--;

		/*
		 * Tell our parent directory if any of our children were
		 * provisionally omitted.
		 */
		filter_data->array_frame[filter_data->nr].child_prov_omit |=
			frame->child_prov_omit;

		/*
		 * If there are NO provisionally omitted child objects (ALL child
		 * objects in this folder were INCLUDED), then we can mark the
		 * folder as SEEN (so we will not have to revisit it again).
		 */
		if (!frame->child_prov_omit)
			return LOFR_MARK_SEEN;
		return LOFR_ZERO;

	case LOFS_BLOB:
		assert(obj->type == OBJ_BLOB);
		assert((obj->flags & SEEN) == 0);

		frame = &filter_data->array_frame[filter_data->nr];

		dtype = DT_REG;
		val = is_excluded_from_list(pathname, strlen(pathname),
					    filename, &dtype, &filter_data->el,
					    r->index);
		if (val < 0)
			val = frame->defval;
		if (val > 0) {
			if (filter_data->omits)
				oidset_remove(filter_data->omits, &obj->oid);
			return LOFR_MARK_SEEN | LOFR_DO_SHOW;
		}

		/*
		 * Provisionally omit it.  We've already established that
		 * this pathname is not in the sparse-checkout specification
		 * with the CURRENT pathname, so we *WANT* to omit this blob.
		 *
		 * However, a pathname elsewhere in the tree may also
		 * reference this same blob, so we cannot reject it yet.
		 * Leave the LOFR_ bits unset so that if the blob appears
		 * again in the traversal, we will be asked again.
		 */
		if (filter_data->omits)
			oidset_insert(filter_data->omits, &obj->oid);

		/*
		 * Remember that at least 1 blob in this tree was
		 * provisionally omitted.  This prevents us from short
		 * cutting the tree in future iterations.
		 */
		frame->child_prov_omit = 1;
		return LOFR_ZERO;
	}
}


static void filter_sparse_free(void *filter_data)
{
	struct filter_sparse_data *d = filter_data;
	/* TODO free contents of 'd' */
	free(d);
}

static void *filter_sparse_oid__init(
	struct oidset *omitted,
	struct list_objects_filter_options *filter_options,
	filter_object_fn *filter_fn,
	filter_free_fn *filter_free_fn)
{
	struct filter_sparse_data *d = xcalloc(1, sizeof(*d));
	d->omits = omitted;
	if (add_excludes_from_blob_to_list(filter_options->sparse_oid_value,
					   NULL, 0, &d->el) < 0)
		die("could not load filter specification");

	ALLOC_GROW(d->array_frame, d->nr + 1, d->alloc);
	d->array_frame[d->nr].defval = 0; /* default to include */
	d->array_frame[d->nr].child_prov_omit = 0;

	*filter_fn = filter_sparse;
	*filter_free_fn = filter_sparse_free;
	return d;
}

static void *filter_sparse_path__init(
	struct oidset *omitted,
	struct list_objects_filter_options *filter_options,
	filter_object_fn *filter_fn,
	filter_free_fn *filter_free_fn)
{
	struct filter_sparse_data *d = xcalloc(1, sizeof(*d));
	d->omits = omitted;
	if (add_excludes_from_file_to_list(filter_options->sparse_path_value,
					   NULL, 0, &d->el, NULL) < 0)
		die("could not load filter specification");

	ALLOC_GROW(d->array_frame, d->nr + 1, d->alloc);
	d->array_frame[d->nr].defval = 0; /* default to include */
	d->array_frame[d->nr].child_prov_omit = 0;

	*filter_fn = filter_sparse;
	*filter_free_fn = filter_sparse_free;
	return d;
}

typedef void *(*filter_init_fn)(
	struct oidset *omitted,
	struct list_objects_filter_options *filter_options,
	filter_object_fn *filter_fn,
	filter_free_fn *filter_free_fn);

/*
 * Must match "enum list_objects_filter_choice".
 */
static filter_init_fn s_filters[] = {
	NULL,
	filter_blobs_none__init,
	filter_blobs_limit__init,
	filter_trees_depth__init,
	filter_sparse_oid__init,
	filter_sparse_path__init,
};

void *list_objects_filter__init(
	struct oidset *omitted,
	struct list_objects_filter_options *filter_options,
	filter_object_fn *filter_fn,
	filter_free_fn *filter_free_fn)
{
	filter_init_fn init_fn;

	assert((sizeof(s_filters) / sizeof(s_filters[0])) == LOFC__COUNT);

	if (filter_options->choice >= LOFC__COUNT)
		BUG("invalid list-objects filter choice: %d",
		    filter_options->choice);

	init_fn = s_filters[filter_options->choice];
	if (init_fn)
		return init_fn(omitted, filter_options,
			       filter_fn, filter_free_fn);
	*filter_fn = NULL;
	*filter_free_fn = NULL;
	return NULL;
}
