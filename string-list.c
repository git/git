#include "cache.h"
#include "string-list.h"

void string_list_init(struct string_list *list, int strdup_strings)
{
	memset(list, 0, sizeof(*list));
	list->strdup_strings = strdup_strings;
}

/* if there is no exact match, point to the index where the entry could be
 * inserted */
static int get_entry_index(const struct string_list *list, const char *string,
		int *exact_match)
{
	int left = -1, right = list->nr;
	compare_strings_fn cmp = list->cmp ? list->cmp : strcmp;

	while (left + 1 < right) {
		int middle = (left + right) / 2;
		int compare = cmp(string, list->items[middle].string);
		if (compare < 0)
			right = middle;
		else if (compare > 0)
			left = middle;
		else {
			*exact_match = 1;
			return middle;
		}
	}

	*exact_match = 0;
	return right;
}

/* returns -1-index if already exists */
static int add_entry(int insert_at, struct string_list *list, const char *string)
{
	int exact_match = 0;
	int index = insert_at != -1 ? insert_at : get_entry_index(list, string, &exact_match);

	if (exact_match)
		return -1 - index;

	if (list->nr + 1 >= list->alloc) {
		list->alloc += 32;
		REALLOC_ARRAY(list->items, list->alloc);
	}
	if (index < list->nr)
		memmove(list->items + index + 1, list->items + index,
				(list->nr - index)
				* sizeof(struct string_list_item));
	list->items[index].string = list->strdup_strings ?
		xstrdup(string) : (char *)string;
	list->items[index].util = NULL;
	list->nr++;

	return index;
}

struct string_list_item *string_list_insert(struct string_list *list, const char *string)
{
	int index = add_entry(-1, list, string);

	if (index < 0)
		index = -1 - index;

	return list->items + index;
}

int string_list_has_string(const struct string_list *list, const char *string)
{
	int exact_match;
	get_entry_index(list, string, &exact_match);
	return exact_match;
}

int string_list_find_insert_index(const struct string_list *list, const char *string,
				  int negative_existing_index)
{
	int exact_match;
	int index = get_entry_index(list, string, &exact_match);
	if (exact_match)
		index = -1 - (negative_existing_index ? index : 0);
	return index;
}

struct string_list_item *string_list_lookup(struct string_list *list, const char *string)
{
	int exact_match, i = get_entry_index(list, string, &exact_match);
	if (!exact_match)
		return NULL;
	return list->items + i;
}

void string_list_remove_duplicates(struct string_list *list, int free_util)
{
	if (list->nr > 1) {
		int src, dst;
		compare_strings_fn cmp = list->cmp ? list->cmp : strcmp;
		for (src = dst = 1; src < list->nr; src++) {
			if (!cmp(list->items[dst - 1].string, list->items[src].string)) {
				if (list->strdup_strings)
					free(list->items[src].string);
				if (free_util)
					free(list->items[src].util);
			} else
				list->items[dst++] = list->items[src];
		}
		list->nr = dst;
	}
}

int for_each_string_list(struct string_list *list,
			 string_list_each_func_t fn, void *cb_data)
{
	int i, ret = 0;
	for (i = 0; i < list->nr; i++)
		if ((ret = fn(&list->items[i], cb_data)))
			break;
	return ret;
}

void filter_string_list(struct string_list *list, int free_util,
			string_list_each_func_t want, void *cb_data)
{
	int src, dst = 0;
	for (src = 0; src < list->nr; src++) {
		if (want(&list->items[src], cb_data)) {
			list->items[dst++] = list->items[src];
		} else {
			if (list->strdup_strings)
				free(list->items[src].string);
			if (free_util)
				free(list->items[src].util);
		}
	}
	list->nr = dst;
}

static int item_is_not_empty(struct string_list_item *item, void *unused)
{
	return *item->string != '\0';
}

void string_list_remove_empty_items(struct string_list *list, int free_util) {
	filter_string_list(list, free_util, item_is_not_empty, NULL);
}

void string_list_clear(struct string_list *list, int free_util)
{
	if (list->items) {
		int i;
		if (list->strdup_strings) {
			for (i = 0; i < list->nr; i++)
				free(list->items[i].string);
		}
		if (free_util) {
			for (i = 0; i < list->nr; i++)
				free(list->items[i].util);
		}
		free(list->items);
	}
	list->items = NULL;
	list->nr = list->alloc = 0;
}

void string_list_clear_func(struct string_list *list, string_list_clear_func_t clearfunc)
{
	if (list->items) {
		int i;
		if (clearfunc) {
			for (i = 0; i < list->nr; i++)
				clearfunc(list->items[i].util, list->items[i].string);
		}
		if (list->strdup_strings) {
			for (i = 0; i < list->nr; i++)
				free(list->items[i].string);
		}
		free(list->items);
	}
	list->items = NULL;
	list->nr = list->alloc = 0;
}


void print_string_list(const struct string_list *p, const char *text)
{
	int i;
	if ( text )
		printf("%s\n", text);
	for (i = 0; i < p->nr; i++)
		printf("%s:%p\n", p->items[i].string, p->items[i].util);
}

struct string_list_item *string_list_append_nodup(struct string_list *list,
						  char *string)
{
	struct string_list_item *retval;
	ALLOC_GROW(list->items, list->nr + 1, list->alloc);
	retval = &list->items[list->nr++];
	retval->string = string;
	retval->util = NULL;
	return retval;
}

struct string_list_item *string_list_append(struct string_list *list,
					    const char *string)
{
	return string_list_append_nodup(
			list,
			list->strdup_strings ? xstrdup(string) : (char *)string);
}

/* Yuck */
static compare_strings_fn compare_for_qsort;

/* Only call this from inside string_list_sort! */
static int cmp_items(const void *a, const void *b)
{
	const struct string_list_item *one = a;
	const struct string_list_item *two = b;
	return compare_for_qsort(one->string, two->string);
}

void string_list_sort(struct string_list *list)
{
	compare_for_qsort = list->cmp ? list->cmp : strcmp;
	qsort(list->items, list->nr, sizeof(*list->items), cmp_items);
}

struct string_list_item *unsorted_string_list_lookup(struct string_list *list,
						     const char *string)
{
	int i;
	compare_strings_fn cmp = list->cmp ? list->cmp : strcmp;

	for (i = 0; i < list->nr; i++)
		if (!cmp(string, list->items[i].string))
			return list->items + i;
	return NULL;
}

int unsorted_string_list_has_string(struct string_list *list,
				    const char *string)
{
	return unsorted_string_list_lookup(list, string) != NULL;
}

void unsorted_string_list_delete_item(struct string_list *list, int i, int free_util)
{
	if (list->strdup_strings)
		free(list->items[i].string);
	if (free_util)
		free(list->items[i].util);
	list->items[i] = list->items[list->nr-1];
	list->nr--;
}

int string_list_split(struct string_list *list, const char *string,
		      int delim, int maxsplit)
{
	int count = 0;
	const char *p = string, *end;

	if (!list->strdup_strings)
		die("internal error in string_list_split(): "
		    "list->strdup_strings must be set");
	for (;;) {
		count++;
		if (maxsplit >= 0 && count > maxsplit) {
			string_list_append(list, p);
			return count;
		}
		end = strchr(p, delim);
		if (end) {
			string_list_append_nodup(list, xmemdupz(p, end - p));
			p = end + 1;
		} else {
			string_list_append(list, p);
			return count;
		}
	}
}

int string_list_split_in_place(struct string_list *list, char *string,
			       int delim, int maxsplit)
{
	int count = 0;
	char *p = string, *end;

	if (list->strdup_strings)
		die("internal error in string_list_split_in_place(): "
		    "list->strdup_strings must not be set");
	for (;;) {
		count++;
		if (maxsplit >= 0 && count > maxsplit) {
			string_list_append(list, p);
			return count;
		}
		end = strchr(p, delim);
		if (end) {
			*end = '\0';
			string_list_append(list, p);
			p = end + 1;
		} else {
			string_list_append(list, p);
			return count;
		}
	}
}
