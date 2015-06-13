#ifndef REF_FILTER_H
#define REF_FILTER_H

#include "sha1-array.h"
#include "refs.h"
#include "commit.h"
#include "parse-options.h"

/* Quoting styles */
#define QUOTE_NONE 0
#define QUOTE_SHELL 1
#define QUOTE_PERL 2
#define QUOTE_PYTHON 4
#define QUOTE_TCL 8

#define FILTER_REFS_INCLUDE_BROKEN 0x1
#define FILTER_REFS_ALL 0x2

struct atom_value {
	const char *s;
	unsigned long ul; /* used for sorting when not FIELD_STR */
};

struct ref_sorting {
	struct ref_sorting *next;
	int atom; /* index into used_atom array (internal) */
	unsigned reverse : 1;
};

struct ref_array_item {
	unsigned char objectname[20];
	int flag;
	const char *symref;
	struct atom_value *value;
	char refname[FLEX_ARRAY];
};

struct ref_array {
	int nr, alloc;
	struct ref_array_item **items;
};

struct ref_filter {
	const char **name_patterns;
};

struct ref_filter_cbdata {
	struct ref_array *array;
	struct ref_filter *filter;
};

/*
 * API for filtering a set of refs. Based on the type of refs the user
 * has requested, we iterate through those refs and apply filters
 * as per the given ref_filter structure and finally store the
 * filtered refs in the ref_array structure.
 */
int filter_refs(struct ref_array *array, struct ref_filter *filter, unsigned int type);
/*  Clear all memory allocated to ref_array */
void ref_array_clear(struct ref_array *array);
/*  Parse format string and sort specifiers */
int parse_ref_filter_atom(const char *atom, const char *ep);
/*  Used to verify if the given format is correct and to parse out the used atoms */
int verify_ref_format(const char *format);
/*  Sort the given ref_array as per the ref_sorting provided */
void ref_array_sort(struct ref_sorting *sort, struct ref_array *array);
/*  Print the ref using the given format and quote_style */
void show_ref_array_item(struct ref_array_item *info, const char *format, int quote_style);
/*  Callback function for parsing the sort option */
int parse_opt_ref_sorting(const struct option *opt, const char *arg, int unset);
/*  Default sort option based on refname */
struct ref_sorting *ref_default_sorting(void);

#endif /*  REF_FILTER_H  */
