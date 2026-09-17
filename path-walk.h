/*
 * path-walk.h : Methods and structures for walking the object graph in batches
 * by the paths that can reach those objects.
 */
#include "object.h" /* Required for 'enum object_type'. */

struct rev_info;
struct oid_array;
struct pattern_list;

/**
 * The type of a function pointer for the method that is called on a list of
 * objects reachable at a given path.
 */
typedef int (*path_fn)(const char *path,
		       struct oid_array *oids,
		       enum object_type type,
		       void *data);

struct path_walk_info {
	/**
	 * revs provides the definitions for the commit walk, including
	 * which commits are UNINTERESTING or not. This structure is
	 * expected to be owned by the caller.
	 */
	struct rev_info *revs;

	/**
	 * The caller wishes to execute custom logic on objects reachable at a
	 * given path. Every reachable object will be visited exactly once, and
	 * the first path to see an object wins. This may not be a stable choice.
	 */
	path_fn path_fn;
	void *path_fn_data;

	/**
	 * Initialize which object types the path_fn should be called on. This
	 * could also limit the walk to skip blobs if not set.
	 *
	 * Note: even when 'blobs' or 'trees' is disabled, objects that are
	 * directly requested as pending objects will still be emitted to
	 * path_fn. Only objects discovered during the tree walk are filtered by
	 * these flags.
	 */
	int commits;
	int trees;
	int blobs;
	int tags;

	/**
	 * If 'strict_types' is 0, then direct object requests will no longer
	 * override the object type restrictions.
	 */
	int strict_types;

	/**
	 * If non-zero, specifies a maximum blob size. Blobs with a
	 * size equal to or greater than this limit will not be
	 * emitted unless included in 'pending'.
	 */
	unsigned long blob_limit;

	/**
	 * When 'prune_all_uninteresting' is set and a path has all objects
	 * marked as UNINTERESTING, then the path-walk will not visit those
	 * objects. It will not call path_fn on those objects and will not
	 * walk the children of such trees.
	 */
	int prune_all_uninteresting;

	/**
	 * When 'edge_aggressive' is set, then the revision walk will use
	 * the '--object-edge-aggressive' option to mark even more objects
	 * as uninteresting.
	 */
	int edge_aggressive;

	/**
	 * Specify a sparse-checkout definition to match our paths to. Do not
	 * walk outside of this sparse definition. If the patterns are in
	 * cone mode, then the search may prune directories that are outside
	 * of the cone. If not in cone mode, then all tree paths will be
	 * explored but the path_fn will only be called when the path matches
	 * the sparse-checkout patterns.
	 *
	 * When 'pl_sparse_trees' is zero, the sparse patterns only restrict
	 * blobs and all trees are included in the walk output. This matches
	 * the behavior of the sparse:oid object filter. When nonzero, trees
	 * are also pruned by the sparse patterns (as used by backfill).
	 */
	struct pattern_list *pl;
	int pl_sparse_trees;
};

#define PATH_WALK_INFO_INIT {   \
	.blobs = 1,		\
	.trees = 1,		\
	.commits = 1,		\
	.tags = 1,		\
}

void path_walk_info_init(struct path_walk_info *info);
void path_walk_info_clear(struct path_walk_info *info);

/**
 * Given the configuration of 'info', walk the commits based on 'info->revs' and
 * call 'info->path_fn' on each discovered path.
 *
 * Returns nonzero on an error.
 */
int walk_objects_by_path(struct path_walk_info *info);

struct list_objects_filter_options;
/**
 * Given a set of options for filtering objects, return 1 if the options
 * are compatible with the path-walk API and 0 otherwise.
 */
int path_walk_filter_compatible(struct list_objects_filter_options *options);
