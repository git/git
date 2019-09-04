#ifndef STRING_LIST_H
#define STRING_LIST_H

/**
 * The string_list API offers a data structure and functions to handle
 * sorted and unsorted arrays of strings.  A "sorted" list is one whose
 * entries are sorted by string value in `strcmp()` order.
 *
 * The caller:
 *
 * . Allocates and clears a `struct string_list` variable.
 *
 * . Initializes the members. You might want to set the flag `strdup_strings`
 *   if the strings should be strdup()ed. For example, this is necessary
 *   when you add something like git_path("..."), since that function returns
 *   a static buffer that will change with the next call to git_path().
 *
 * If you need something advanced, you can manually malloc() the `items`
 * member (you need this if you add things later) and you should set the
 * `nr` and `alloc` members in that case, too.
 *
 * . Adds new items to the list, using `string_list_append`,
 *   `string_list_append_nodup`, `string_list_insert`,
 *   `string_list_split`, and/or `string_list_split_in_place`.
 *
 * . Can check if a string is in the list using `string_list_has_string` or
 *   `unsorted_string_list_has_string` and get it from the list using
 *   `string_list_lookup` for sorted lists.
 *
 * . Can sort an unsorted list using `string_list_sort`.
 *
 * . Can remove duplicate items from a sorted list using
 *   `string_list_remove_duplicates`.
 *
 * . Can remove individual items of an unsorted list using
 *   `unsorted_string_list_delete_item`.
 *
 * . Can remove items not matching a criterion from a sorted or unsorted
 *   list using `filter_string_list`, or remove empty strings using
 *   `string_list_remove_empty_items`.
 *
 * . Finally it should free the list using `string_list_clear`.
 *
 * Example:
 *
 *     struct string_list list = STRING_LIST_INIT_NODUP;
 *     int i;
 *
 *     string_list_append(&list, "foo");
 *     string_list_append(&list, "bar");
 *     for (i = 0; i < list.nr; i++)
 *             printf("%s\n", list.items[i].string)
 *
 * NOTE: It is more efficient to build an unsorted list and sort it
 * afterwards, instead of building a sorted list (`O(n log n)` instead of
 * `O(n^2)`).
 *
 * However, if you use the list to check if a certain string was added
 * already, you should not do that (using unsorted_string_list_has_string()),
 * because the complexity would be quadratic again (but with a worse factor).
 */

/**
 * Represents an item of the list. The `string` member is a pointer to the
 * string, and you may use the `util` member for any purpose, if you want.
 */
struct string_list_item {
	char *string;
	void *util;
};

typedef int (*compare_strings_fn)(const char *, const char *);

/**
 * Represents the list itself.
 *
 * . The array of items are available via the `items` member.
 * . The `nr` member contains the number of items stored in the list.
 * . The `alloc` member is used to avoid reallocating at every insertion.
 *   You should not tamper with it.
 * . Setting the `strdup_strings` member to 1 will strdup() the strings
 *   before adding them, see above.
 * . The `compare_strings_fn` member is used to specify a custom compare
 *   function, otherwise `strcmp()` is used as the default function.
 */
struct string_list {
	struct string_list_item *items;
	unsigned int nr, alloc;
	unsigned int strdup_strings:1;
	compare_strings_fn cmp; /* NULL uses strcmp() */
};

#define STRING_LIST_INIT_NODUP { NULL, 0, 0, 0, NULL }
#define STRING_LIST_INIT_DUP   { NULL, 0, 0, 1, NULL }

/* General functions which work with both sorted and unsorted lists. */

/**
 * Initialize the members of the string_list, set `strdup_strings`
 * member according to the value of the second parameter.
 */
void string_list_init(struct string_list *list, int strdup_strings);

/** Callback function type for for_each_string_list */
typedef int (*string_list_each_func_t)(struct string_list_item *, void *);

/**
 * Apply `want` to each item in `list`, retaining only the ones for which
 * the function returns true.  If `free_util` is true, call free() on
 * the util members of any items that have to be deleted.  Preserve
 * the order of the items that are retained.
 */
void filter_string_list(struct string_list *list, int free_util,
			string_list_each_func_t want, void *cb_data);

/**
 * Free a string_list. The `string` pointer of the items will be freed
 * in case the `strdup_strings` member of the string_list is set. The
 * second parameter controls if the `util` pointer of the items should
 * be freed or not.
 */
void string_list_clear(struct string_list *list, int free_util);

/**
 * Callback type for `string_list_clear_func`.  The string associated
 * with the util pointer is passed as the second argument
 */
typedef void (*string_list_clear_func_t)(void *p, const char *str);

/** Call a custom clear function on each util pointer */
void string_list_clear_func(struct string_list *list, string_list_clear_func_t clearfunc);

/**
 * Apply `func` to each item. If `func` returns nonzero, the
 * iteration aborts and the return value is propagated.
 */
int for_each_string_list(struct string_list *list,
			 string_list_each_func_t func, void *cb_data);

/** Iterate over each item, as a macro. */
#define for_each_string_list_item(item,list)            \
	for (item = (list)->items;                      \
	     item && item < (list)->items + (list)->nr; \
	     ++item)

/**
 * Remove any empty strings from the list.  If free_util is true, call
 * free() on the util members of any items that have to be deleted.
 * Preserve the order of the items that are retained.
 */
void string_list_remove_empty_items(struct string_list *list, int free_util);

/* Use these functions only on sorted lists: */

/** Determine if the string_list has a given string or not. */
int string_list_has_string(const struct string_list *list, const char *string);
int string_list_find_insert_index(const struct string_list *list, const char *string,
				  int negative_existing_index);

/**
 * Insert a new element to the string_list. The returned pointer can
 * be handy if you want to write something to the `util` pointer of
 * the string_list_item containing the just added string. If the given
 * string already exists the insertion will be skipped and the pointer
 * to the existing item returned.
 *
 * Since this function uses xrealloc() (which die()s if it fails) if the
 * list needs to grow, it is safe not to check the pointer. I.e. you may
 * write `string_list_insert(...)->util = ...;`.
 */
struct string_list_item *string_list_insert(struct string_list *list, const char *string);

/**
 * Remove the given string from the sorted list.  If the string
 * doesn't exist, the list is not altered.
 */
void string_list_remove(struct string_list *list, const char *string,
			int free_util);

/**
 * Check if the given string is part of a sorted list. If it is part of the list,
 * return the coresponding string_list_item, NULL otherwise.
 */
struct string_list_item *string_list_lookup(struct string_list *list, const char *string);

/*
 * Remove all but the first of consecutive entries with the same
 * string value.  If free_util is true, call free() on the util
 * members of any items that have to be deleted.
 */
void string_list_remove_duplicates(struct string_list *sorted_list, int free_util);


/* Use these functions only on unsorted lists: */

/**
 * Add string to the end of list.  If list->strdup_string is set, then
 * string is copied; otherwise the new string_list_entry refers to the
 * input string.
 */
struct string_list_item *string_list_append(struct string_list *list, const char *string);

/**
 * Like string_list_append(), except string is never copied.  When
 * list->strdup_strings is set, this function can be used to hand
 * ownership of a malloc()ed string to list without making an extra
 * copy.
 */
struct string_list_item *string_list_append_nodup(struct string_list *list, char *string);

/**
 * Sort the list's entries by string value in `strcmp()` order.
 */
void string_list_sort(struct string_list *list);

/**
 * Like `string_list_has_string()` but for unsorted lists. Linear in
 * size of the list.
 */
int unsorted_string_list_has_string(struct string_list *list, const char *string);

/**
 * Like `string_list_lookup()` but for unsorted lists. Linear in size
 * of the list.
 */
struct string_list_item *unsorted_string_list_lookup(struct string_list *list,
						     const char *string);
/**
 * Remove an item from a string_list. The `string` pointer of the
 * items will be freed in case the `strdup_strings` member of the
 * string_list is set. The third parameter controls if the `util`
 * pointer of the items should be freed or not.
 */
void unsorted_string_list_delete_item(struct string_list *list, int i, int free_util);

/**
 * Split string into substrings on character `delim` and append the
 * substrings to `list`.  The input string is not modified.
 * list->strdup_strings must be set, as new memory needs to be
 * allocated to hold the substrings.  If maxsplit is non-negative,
 * then split at most maxsplit times.  Return the number of substrings
 * appended to list.
 *
 * Examples:
 *   string_list_split(l, "foo:bar:baz", ':', -1) -> ["foo", "bar", "baz"]
 *   string_list_split(l, "foo:bar:baz", ':', 0) -> ["foo:bar:baz"]
 *   string_list_split(l, "foo:bar:baz", ':', 1) -> ["foo", "bar:baz"]
 *   string_list_split(l, "foo:bar:", ':', -1) -> ["foo", "bar", ""]
 *   string_list_split(l, "", ':', -1) -> [""]
 *   string_list_split(l, ":", ':', -1) -> ["", ""]
 */
int string_list_split(struct string_list *list, const char *string,
		      int delim, int maxsplit);

/*
 * Like string_list_split(), except that string is split in-place: the
 * delimiter characters in string are overwritten with NULs, and the
 * new string_list_items point into string (which therefore must not
 * be modified or freed while the string_list is in use).
 * list->strdup_strings must *not* be set.
 */
int string_list_split_in_place(struct string_list *list, char *string,
			       int delim, int maxsplit);
#endif /* STRING_LIST_H */
