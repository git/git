#ifndef STRING_LIST_H
#define STRING_LIST_H

struct string_list_item {
	char *string;
	void *util;
};

typedef int (*compare_strings_fn)(const char *, const char *);

struct string_list {
	struct string_list_item *items;
	unsigned int nr, alloc;
	unsigned int strdup_strings:1;
	compare_strings_fn cmp; /* NULL uses strcmp() */
};

#define STRING_LIST_INIT_NODUP { NULL, 0, 0, 0, NULL }
#define STRING_LIST_INIT_DUP   { NULL, 0, 0, 1, NULL }

void string_list_init(struct string_list *list, int strdup_strings);

void print_string_list(const struct string_list *p, const char *text);
void string_list_clear(struct string_list *list, int free_util);

/* Use this function to call a custom clear function on each util pointer */
/* The string associated with the util pointer is passed as the second argument */
typedef void (*string_list_clear_func_t)(void *p, const char *str);
void string_list_clear_func(struct string_list *list, string_list_clear_func_t clearfunc);

/* Use this function or the macro below to iterate over each item */
typedef int (*string_list_each_func_t)(struct string_list_item *, void *);
int for_each_string_list(struct string_list *list,
			 string_list_each_func_t, void *cb_data);
#define for_each_string_list_item(item,list) \
	for (item = (list)->items; item < (list)->items + (list)->nr; ++item)

/*
 * Apply want to each item in list, retaining only the ones for which
 * the function returns true.  If free_util is true, call free() on
 * the util members of any items that have to be deleted.  Preserve
 * the order of the items that are retained.
 */
void filter_string_list(struct string_list *list, int free_util,
			string_list_each_func_t want, void *cb_data);

/*
 * Remove any empty strings from the list.  If free_util is true, call
 * free() on the util members of any items that have to be deleted.
 * Preserve the order of the items that are retained.
 */
void string_list_remove_empty_items(struct string_list *list, int free_util);

/* Use these functions only on sorted lists: */
int string_list_has_string(const struct string_list *list, const char *string);
int string_list_find_insert_index(const struct string_list *list, const char *string,
				  int negative_existing_index);
/*
 * Inserts the given string into the sorted list.
 * If the string already exists, the list is not altered.
 * Returns the string_list_item, the string is part of.
 */
struct string_list_item *string_list_insert(struct string_list *list, const char *string);

/*
 * Checks if the given string is part of a sorted list. If it is part of the list,
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

/*
 * Add string to the end of list.  If list->strdup_string is set, then
 * string is copied; otherwise the new string_list_entry refers to the
 * input string.
 */
struct string_list_item *string_list_append(struct string_list *list, const char *string);

/*
 * Like string_list_append(), except string is never copied.  When
 * list->strdup_strings is set, this function can be used to hand
 * ownership of a malloc()ed string to list without making an extra
 * copy.
 */
struct string_list_item *string_list_append_nodup(struct string_list *list, char *string);

void string_list_sort(struct string_list *list);
int unsorted_string_list_has_string(struct string_list *list, const char *string);
struct string_list_item *unsorted_string_list_lookup(struct string_list *list,
						     const char *string);

void unsorted_string_list_delete_item(struct string_list *list, int i, int free_util);

/*
 * Split string into substrings on character delim and append the
 * substrings to list.  The input string is not modified.
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
