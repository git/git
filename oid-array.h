#ifndef OID_ARRAY_H
#define OID_ARRAY_H

#include "hash.h"

/**
 * The API provides storage and manipulation of sets of object identifiers.
 * The emphasis is on storage and processing efficiency, making them suitable
 * for large lists. Note that the ordering of items is not preserved over some
 * operations.
 *
 * Examples
 * --------
 * -----------------------------------------
 * int print_callback(const struct object_id *oid,
 * 		    void *data)
 * {
 * 	printf("%s\n", oid_to_hex(oid));
 * 	return 0; // always continue
 * }
 *
 * void some_func(void)
 * {
 *     struct oid_array hashes = OID_ARRAY_INIT;
 *     struct object_id oid;
 *
 *     // Read objects into our set
 *     while (read_object_from_stdin(oid.hash))
 *         oid_array_append(&hashes, &oid);
 *
 *     // Check if some objects are in our set
 *     while (read_object_from_stdin(oid.hash)) {
 *         if (oid_array_lookup(&hashes, &oid) >= 0)
 *             printf("it's in there!\n");
 *
 *          // Print the unique set of objects. We could also have
 *          // avoided adding duplicate objects in the first place,
 *          // but we would end up re-sorting the array repeatedly.
 *          // Instead, this will sort once and then skip duplicates
 *          // in linear time.
 *
 *         oid_array_for_each_unique(&hashes, print_callback, NULL);
 *     }
 */

/**
 * A single array of object IDs. This should be initialized by assignment from
 * `OID_ARRAY_INIT`. The `oid` member contains the actual data. The `nr` member
 * contains the number of items in the set. The `alloc` and `sorted` members
 * are used internally, and should not be needed by API callers.
 */
struct oid_array {
	struct object_id *oid;
	size_t nr;
	size_t alloc;
	int sorted;
};

#define OID_ARRAY_INIT { 0 }

/**
 * Add an item to the set. The object ID will be placed at the end of the array
 * (but note that some operations below may lose this ordering).
 */
void oid_array_append(struct oid_array *array, const struct object_id *oid);

/**
 * Perform a binary search of the array for a specific object ID. If found,
 * returns the offset (in number of elements) of the object ID. If not found,
 * returns a negative integer. If the array is not sorted, this function has
 * the side effect of sorting it.
 */
int oid_array_lookup(struct oid_array *array, const struct object_id *oid);

/**
 * Free all memory associated with the array and return it to the initial,
 * empty state.
 */
void oid_array_clear(struct oid_array *array);

typedef int (*for_each_oid_fn)(const struct object_id *oid,
			       void *data);
/**
 * Iterate over each element of the list, executing the callback function for
 * each one. Does not sort the list, so any custom hash order is retained.
 * If the callback returns a non-zero value, the iteration ends immediately
 * and the callback's return is propagated; otherwise, 0 is returned.
 */
int oid_array_for_each(struct oid_array *array,
		       for_each_oid_fn fn,
		       void *data);

/**
 * Iterate over each unique element of the list in sorted order, but otherwise
 * behave like `oid_array_for_each`. If the array is not sorted, this function
 * has the side effect of sorting it.
 */
int oid_array_for_each_unique(struct oid_array *array,
			      for_each_oid_fn fn,
			      void *data);

/**
 * Apply the callback function `want` to each entry in the array, retaining
 * only the entries for which the function returns true. Preserve the order
 * of the entries that are retained.
 */
void oid_array_filter(struct oid_array *array,
		      for_each_oid_fn want,
		      void *cbdata);

/**
 * Sort the array in order of ascending object id.
 */
void oid_array_sort(struct oid_array *array);

/**
 * Find the next unique oid in the array after position "cur".
 * The array must be sorted for this to work. You can iterate
 * over unique elements like this:
 *
 *   size_t i;
 *   oid_array_sort(array);
 *   for (i = 0; i < array->nr; i = oid_array_next_unique(array, i))
 *	printf("%s", oid_to_hex(array->oids[i]);
 *
 * Non-unique iteration can just increment with "i++" to visit each element.
 */
static inline size_t oid_array_next_unique(struct oid_array *array, size_t cur)
{
	do {
		cur++;
	} while (cur < array->nr &&
		 oideq(array->oid + cur, array->oid + cur - 1));
	return cur;
}

#endif /* OID_ARRAY_H */
