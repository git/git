#ifndef LIST_OBJECTS_FILTER_H
#define LIST_OBJECTS_FILTER_H

struct list_objects_filter_options;
struct object;
struct oidset;
struct repository;

/*
 * During list-object traversal we allow certain objects to be
 * filtered (omitted) from the result.  The active filter uses
 * these result values to guide list-objects.
 *
 * _ZERO      : Do nothing with the object at this time.  It may
 *              be revisited if it appears in another place in
 *              the tree or in another commit during the overall
 *              traversal.
 *
 * _MARK_SEEN : Mark this object as "SEEN" in the object flags.
 *              This will prevent it from being revisited during
 *              the remainder of the traversal.  This DOES NOT
 *              imply that it will be included in the results.
 *
 * _DO_SHOW   : Show this object in the results (call show() on it).
 *              In general, objects should only be shown once, but
 *              this result DOES NOT imply that we mark it SEEN.
 *
 * _SKIP_TREE : Used in LOFS_BEGIN_TREE situation - indicates that
 *              the tree's children should not be iterated over. This
 *              is used as an optimization when all children will
 *              definitely be ignored.
 *
 * Most of the time, you want the combination (_MARK_SEEN | _DO_SHOW)
 * but they can be used independently, such as when sparse-checkout
 * pattern matching is being applied.
 *
 * A _MARK_SEEN without _DO_SHOW can be called a hard-omit -- the
 * object is not shown and will never be reconsidered (unless a
 * previous iteration has already shown it).
 *
 * A _DO_SHOW without _MARK_SEEN can be used, for example, to
 * include a directory, but then revisit it to selectively include
 * or omit objects within it.
 *
 * A _ZERO can be called a provisional-omit -- the object is NOT shown,
 * but *may* be revisited (if the object appears again in the traversal).
 * Therefore, it will be omitted from the results *unless* a later
 * iteration causes it to be shown.
 */
enum list_objects_filter_result {
	LOFR_ZERO      = 0,
	LOFR_MARK_SEEN = 1<<0,
	LOFR_DO_SHOW   = 1<<1,
	LOFR_SKIP_TREE = 1<<2,
};

enum list_objects_filter_situation {
	LOFS_BEGIN_TREE,
	LOFS_END_TREE,
	LOFS_BLOB
};

typedef enum list_objects_filter_result (*filter_object_fn)(
	struct repository *r,
	enum list_objects_filter_situation filter_situation,
	struct object *obj,
	const char *pathname,
	const char *filename,
	void *filter_data);

typedef void (*filter_free_fn)(void *filter_data);

/*
 * Constructor for the set of defined list-objects filters.
 * Returns a generic "void *filter_data".
 *
 * The returned "filter_fn" will be used by traverse_commit_list()
 * to filter the results.
 *
 * The returned "filter_free_fn" is a destructor for the
 * filter_data.
 */
void *list_objects_filter__init(
	struct oidset *omitted,
	struct list_objects_filter_options *filter_options,
	filter_object_fn *filter_fn,
	filter_free_fn *filter_free_fn);

#endif /* LIST_OBJECTS_FILTER_H */
