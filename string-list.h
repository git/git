#ifndef STRING_LIST_H
#define STRING_LIST_H

struct string_list_item {
	char *string;
	void *util;
};
struct string_list
{
	struct string_list_item *items;
	unsigned int nr, alloc;
	unsigned int strdup_strings:1;
};

void print_string_list(const struct string_list *p, const char *text);
void string_list_clear(struct string_list *list, int free_util);

/* Use this function to call a custom clear function on each util pointer */
/* The string associated with the util pointer is passed as the second argument */
typedef void (*string_list_clear_func_t)(void *p, const char *str);
void string_list_clear_func(struct string_list *list, string_list_clear_func_t clearfunc);

/* Use this function to iterate over each item */
typedef int (*string_list_each_func_t)(struct string_list_item *, void *);
int for_each_string_list(struct string_list *list,
			 string_list_each_func_t, void *cb_data);

/* Use these functions only on sorted lists: */
int string_list_has_string(const struct string_list *list, const char *string);
int string_list_find_insert_index(const struct string_list *list, const char *string,
				  int negative_existing_index);
struct string_list_item *string_list_insert(struct string_list *list, const char *string);
struct string_list_item *string_list_insert_at_index(struct string_list *list,
						     int insert_at, const char *string);
struct string_list_item *string_list_lookup(struct string_list *list, const char *string);

/* Use these functions only on unsorted lists: */
struct string_list_item *string_list_append(struct string_list *list, const char *string);
void sort_string_list(struct string_list *list);
int unsorted_string_list_has_string(struct string_list *list, const char *string);
struct string_list_item *unsorted_string_list_lookup(struct string_list *list,
						     const char *string);
#endif /* STRING_LIST_H */
