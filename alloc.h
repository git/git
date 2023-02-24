#ifndef ALLOC_H
#define ALLOC_H

struct alloc_state;
struct tree;
struct commit;
struct tag;
struct repository;

void *alloc_blob_node(struct repository *r);
void *alloc_tree_node(struct repository *r);
void init_commit_node(struct commit *c);
void *alloc_commit_node(struct repository *r);
void *alloc_tag_node(struct repository *r);
void *alloc_object_node(struct repository *r);

struct alloc_state *allocate_alloc_state(void);
void clear_alloc_state(struct alloc_state *s);

#define alloc_nr(x) (((x)+16)*3/2)

/**
 * Dynamically growing an array using realloc() is error prone and boring.
 *
 * Define your array with:
 *
 * - a pointer (`item`) that points at the array, initialized to `NULL`
 *   (although please name the variable based on its contents, not on its
 *   type);
 *
 * - an integer variable (`alloc`) that keeps track of how big the current
 *   allocation is, initialized to `0`;
 *
 * - another integer variable (`nr`) to keep track of how many elements the
 *   array currently has, initialized to `0`.
 *
 * Then before adding `n`th element to the item, call `ALLOC_GROW(item, n,
 * alloc)`.  This ensures that the array can hold at least `n` elements by
 * calling `realloc(3)` and adjusting `alloc` variable.
 *
 * ------------
 * sometype *item;
 * size_t nr;
 * size_t alloc
 *
 * for (i = 0; i < nr; i++)
 * 	if (we like item[i] already)
 * 		return;
 *
 * // we did not like any existing one, so add one
 * ALLOC_GROW(item, nr + 1, alloc);
 * item[nr++] = value you like;
 * ------------
 *
 * You are responsible for updating the `nr` variable.
 *
 * If you need to specify the number of elements to allocate explicitly
 * then use the macro `REALLOC_ARRAY(item, alloc)` instead of `ALLOC_GROW`.
 *
 * Consider using ALLOC_GROW_BY instead of ALLOC_GROW as it has some
 * added niceties.
 *
 * DO NOT USE any expression with side-effect for 'x', 'nr', or 'alloc'.
 */
#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			REALLOC_ARRAY(x, alloc); \
		} \
	} while (0)

/*
 * Similar to ALLOC_GROW but handles updating of the nr value and
 * zeroing the bytes of the newly-grown array elements.
 *
 * DO NOT USE any expression with side-effect for any of the
 * arguments.
 */
#define ALLOC_GROW_BY(x, nr, increase, alloc) \
	do { \
		if (increase) { \
			size_t new_nr = nr + (increase); \
			if (new_nr < nr) \
				BUG("negative growth in ALLOC_GROW_BY"); \
			ALLOC_GROW(x, new_nr, alloc); \
			memset((x) + nr, 0, sizeof(*(x)) * (increase)); \
			nr = new_nr; \
		} \
	} while (0)

#endif
