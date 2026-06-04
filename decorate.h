#ifndef DECORATE_H
#define DECORATE_H

/*
 * A data structure that associates Git objects to void pointers. See
 * t/unit-tests/t-example-decorate.c for a demonstration of how to use these
 * functions.
 */

/*
 * An entry in the data structure.
 */
struct decoration_entry {
	const struct object *base;
	void *decoration;
};

/*
 * The data structure.
 *
 * This data structure must be zero-initialized.
 */
struct decoration {
	/*
	 * Not used by the decoration mechanism. Clients may use this for
	 * whatever they want.
	 */
	const char *name;

	/*
	 * The capacity of "entries".
	 */
	unsigned int size;

	/*
	 * The number of real Git objects (that is, entries with non-NULL
	 * "base").
	 */
	unsigned int nr;

	/*
	 * The entries. This is an array of size "size", containing nr entries
	 * with non-NULL "base" and (size - nr) entries with NULL "base".
	 */
	struct decoration_entry *entries;
};

/*
 * Add an association from the given object to the given pointer (which may be
 * NULL), returning the previously associated pointer. If there is no previous
 * association, this function returns NULL.
 */
void *add_decoration(struct decoration *n, const struct object *obj, void *decoration);

/*
 * Return the pointer associated to the given object. If there is no
 * association, this function returns NULL.
 */
void *lookup_decoration(struct decoration *n, const struct object *obj);

/*
 * Clear all decoration entries, releasing any memory used by the structure.
 * If free_cb is not NULL, it is called for every decoration value currently
 * stored.
 *
 * After clearing, the decoration struct can be used again. The "name" field is
 * retained.
 */
void clear_decoration(struct decoration *n, void (*free_cb)(void *));

#endif
