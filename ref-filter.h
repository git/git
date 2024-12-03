#ifndef REF_FILTER_H
#define REF_FILTER_H

#include "gettext.h"
#include "oid-array.h"
#include "commit.h"
#include "string-list.h"
#include "strvec.h"
#include "commit-reach.h"

/* Quoting styles */
#define QUOTE_NONE 0
#define QUOTE_SHELL 1
#define QUOTE_PERL 2
#define QUOTE_PYTHON 4
#define QUOTE_TCL 8

#define FILTER_REFS_TAGS           0x0002
#define FILTER_REFS_BRANCHES       0x0004
#define FILTER_REFS_REMOTES        0x0008
#define FILTER_REFS_OTHERS         0x0010
#define FILTER_REFS_REGULAR        (FILTER_REFS_TAGS | FILTER_REFS_BRANCHES | \
				    FILTER_REFS_REMOTES | FILTER_REFS_OTHERS)
#define FILTER_REFS_DETACHED_HEAD  0x0020
#define FILTER_REFS_PSEUDOREFS     0x0040
#define FILTER_REFS_ROOT_REFS      0x0080
#define FILTER_REFS_KIND_MASK      (FILTER_REFS_REGULAR | FILTER_REFS_DETACHED_HEAD | \
				    FILTER_REFS_PSEUDOREFS | FILTER_REFS_ROOT_REFS)

struct atom_value;
struct ref_sorting;
struct ahead_behind_count;
struct option;

enum ref_sorting_order {
	REF_SORTING_REVERSE = 1<<0,
	REF_SORTING_ICASE = 1<<1,
	REF_SORTING_VERSION = 1<<2,
	REF_SORTING_DETACHED_HEAD_FIRST = 1<<3,
};

struct ref_array_item {
	struct object_id objectname;
	const char *rest;
	int flag;
	unsigned int kind;
	const char *symref;
	struct commit *commit;
	struct atom_value *value;
	struct ahead_behind_count **counts;
	char **is_base;

	char refname[FLEX_ARRAY];
};

struct ref_array {
	int nr, alloc;
	struct ref_array_item **items;
	struct rev_info *revs;

	struct ahead_behind_count *counts;
	size_t counts_nr;
};

struct ref_filter {
	const char **name_patterns;
	struct strvec exclude;
	struct oid_array points_at;
	struct commit_list *with_commit;
	struct commit_list *no_commit;
	struct commit_list *reachable_from;
	struct commit_list *unreachable_from;

	unsigned int with_commit_tag_algo : 1,
		match_as_path : 1,
		ignore_case : 1,
		detached : 1;
	unsigned int kind,
		lines;
	int abbrev,
		verbose;

	struct {
		struct contains_cache contains_cache;
		struct contains_cache no_contains_cache;
	} internal;
};

struct ref_format {
	/*
	 * Set these to define the format; make sure you call
	 * verify_ref_format() afterwards to finalize.
	 */
	const char *format;
	const char *rest;
	int quote_style;
	int use_color;

	/* Internal state to ref-filter */
	int need_color_reset_at_eol;

	/* List of bases for ahead-behind counts. */
	struct string_list bases;

	/* List of bases for is-base indicators. */
	struct string_list is_base_tips;

	struct {
		int max_count;
		int omit_empty;
	} array_opts;
};

#define REF_FILTER_INIT { \
	.points_at = OID_ARRAY_INIT, \
	.exclude = STRVEC_INIT, \
}
#define REF_FORMAT_INIT {             \
	.use_color = -1,              \
	.bases = STRING_LIST_INIT_DUP, \
	.is_base_tips = STRING_LIST_INIT_DUP, \
}

/*  Macros for checking --merged and --no-merged options */
#define _OPT_MERGED_NO_MERGED(option, filter, h) \
	{ OPTION_CALLBACK, 0, option, (filter), N_("commit"), (h), \
	  PARSE_OPT_LASTARG_DEFAULT | PARSE_OPT_NONEG, \
	  parse_opt_merge_filter, (intptr_t) "HEAD" \
	}
#define OPT_MERGED(f, h) _OPT_MERGED_NO_MERGED("merged", f, h)
#define OPT_NO_MERGED(f, h) _OPT_MERGED_NO_MERGED("no-merged", f, h)

#define OPT_REF_SORT(var) \
	OPT_STRING_LIST(0, "sort", (var), \
			N_("key"), N_("field name to sort on"))
#define OPT_REF_FILTER_EXCLUDE(var) \
	OPT_STRVEC(0, "exclude", &(var)->exclude, \
		   N_("pattern"), N_("exclude refs which match pattern"))

/*
 * API for filtering a set of refs. Based on the type of refs the user
 * has requested, we iterate through those refs and apply filters
 * as per the given ref_filter structure and finally store the
 * filtered refs in the ref_array structure.
 */
int filter_refs(struct ref_array *array, struct ref_filter *filter, unsigned int type);
/*
 * Filter refs using the given ref_filter and type, sort the contents
 * according to the given ref_sorting, format the filtered refs with the
 * given ref_format, and print them to stdout.
 */
void filter_and_format_refs(struct ref_filter *filter, unsigned int type,
			    struct ref_sorting *sorting,
			    struct ref_format *format);
/*  Clear all memory allocated to ref_array */
void ref_array_clear(struct ref_array *array);
/*  Used to verify if the given format is correct and to parse out the used atoms */
int verify_ref_format(struct ref_format *format);
/*  Sort the given ref_array as per the ref_sorting provided */
void ref_array_sort(struct ref_sorting *sort, struct ref_array *array);
/*  Set REF_SORTING_* sort_flags for all elements of a sorting list */
void ref_sorting_set_sort_flags_all(struct ref_sorting *sorting, unsigned int mask, int on);
/*  Based on the given format and quote_style, fill the strbuf */
int format_ref_array_item(struct ref_array_item *info,
			  struct ref_format *format,
			  struct strbuf *final_buf,
			  struct strbuf *error_buf);
/* Release a "struct ref_sorting" */
void ref_sorting_release(struct ref_sorting *);
/*  Convert list of sort options into ref_sorting */
struct ref_sorting *ref_sorting_options(struct string_list *);
/*  Function to parse --merged and --no-merged options */
int parse_opt_merge_filter(const struct option *opt, const char *arg, int unset);
/*  Get the current HEAD's description */
char *get_head_description(void);
/*  Set up translated strings in the output. */
void setup_ref_filter_porcelain_msg(void);

/*
 * Print up to maxcount ref_array elements to stdout using the given
 * ref_format.
 */
void print_formatted_ref_array(struct ref_array *array, struct ref_format *format);

/*
 * Print a single ref, outside of any ref-filter. Note that the
 * name must be a fully qualified refname.
 */
void pretty_print_ref(const char *name, const struct object_id *oid,
		      struct ref_format *format);

/*
 * Push a single ref onto the array; this can be used to construct your own
 * ref_array without using filter_refs().
 */
struct ref_array_item *ref_array_push(struct ref_array *array,
				      const char *refname,
				      const struct object_id *oid);

/*
 * If the provided format includes ahead-behind atoms, then compute the
 * ahead-behind values for the array of filtered references. Must be
 * called after filter_refs() but before outputting the formatted refs.
 *
 * If this is not called, then any ahead-behind atoms will be blank.
 */
void filter_ahead_behind(struct repository *r,
			 struct ref_format *format,
			 struct ref_array *array);

/*
 * If the provided format includes is-base atoms, then compute the base checks
 * for those tips against all refs.
 *
 * If this is not called, then any is-base atoms will be blank.
 */
void filter_is_base(struct repository *r,
		    struct ref_format *format,
		    struct ref_array *array);

void ref_filter_init(struct ref_filter *filter);
void ref_filter_clear(struct ref_filter *filter);

void ref_format_init(struct ref_format *format);
void ref_format_clear(struct ref_format *format);

#endif /*  REF_FILTER_H  */
