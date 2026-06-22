#ifndef USERDIFF_H
#define USERDIFF_H

#include "notes-cache.h"

struct index_state;
struct repository;

struct userdiff_funcname {
	const char *pattern;
	char *pattern_owned;
	int cflags;
};

struct external_diff {
	char *cmd;
	unsigned trust_exit_code:1;
};

struct userdiff_driver {
	const char *name;
	struct external_diff external;
	const char *algorithm;
	char *algorithm_owned;
	int binary;
	struct userdiff_funcname funcname;
	const char *word_regex;
	char *word_regex_owned;
	const char *word_regex_multi_byte;
	const char *textconv;
	char *textconv_owned;
	struct notes_cache *textconv_cache;
	int textconv_want_cache;
};
enum userdiff_driver_type {
	USERDIFF_DRIVER_TYPE_BUILTIN = 1<<0,
	USERDIFF_DRIVER_TYPE_CUSTOM = 1<<1,
};
typedef int (*each_userdiff_driver_fn)(struct userdiff_driver *,
				       enum userdiff_driver_type, void *);

int userdiff_config(const char *k, const char *v);
struct userdiff_driver *userdiff_find_by_name(const char *name);
struct userdiff_driver *userdiff_find_by_path(struct index_state *istate,
					      const char *path);

/*
 * Initialize any textconv-related fields in the driver and return it, or NULL
 * if it does not have textconv enabled at all.
 */
struct userdiff_driver *userdiff_get_textconv(struct repository *r,
					      struct userdiff_driver *driver);

/*
 * Iterate over all userdiff drivers. The userdiff_driver_type
 * argument to each_userdiff_driver_fn indicates their type. Return
 * non-zero to exit early from the loop.
 */
int for_each_userdiff_driver(each_userdiff_driver_fn, void *);

#endif /* USERDIFF */
