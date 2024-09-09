/*
 * path-walk.h : Methods and structures for walking the object graph in batches
 * by the paths that can reach those objects.
 */
#include "object.h" /* Required for 'enum object_type'. */

struct rev_info;
struct oid_array;

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
	 * which commits are UNINTERESTING or not.
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
	 */
	int commits;
	int trees;
	int blobs;
	int tags;
};

#define PATH_WALK_INFO_INIT {   \
	.blobs = 1,		\
	.trees = 1,		\
	.commits = 1,		\
	.tags = 1,		\
}

/**
 * Given the configuration of 'info', walk the commits based on 'info->revs' and
 * call 'info->path_fn' on each discovered path.
 *
 * Returns nonzero on an error.
 */
int walk_objects_by_path(struct path_walk_info *info);
