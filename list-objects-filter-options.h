#ifndef LIST_OBJECTS_FILTER_OPTIONS_H
#define LIST_OBJECTS_FILTER_OPTIONS_H

#include "parse-options.h"
#include "strbuf.h"

/*
 * The list of defined filters for list-objects.
 */
enum list_objects_filter_choice {
	LOFC_DISABLED = 0,
	LOFC_BLOB_NONE,
	LOFC_BLOB_LIMIT,
	LOFC_TREE_DEPTH,
	LOFC_SPARSE_OID,
	LOFC__COUNT /* must be last */
};

struct list_objects_filter_options {
	/*
	 * 'filter_spec' is the raw argument value given on the command line
	 * or protocol request.  (The part after the "--keyword=".)  For
	 * commands that launch filtering sub-processes, or for communication
	 * over the network, don't use this value; use the result of
	 * expand_list_objects_filter_spec() instead.
	 */
	char *filter_spec;

	/*
	 * 'choice' is determined by parsing the filter-spec.  This indicates
	 * the filtering algorithm to use.
	 */
	enum list_objects_filter_choice choice;

	/*
	 * Choice is LOFC_DISABLED because "--no-filter" was requested.
	 */
	unsigned int no_filter : 1;

	/*
	 * Parsed values (fields) from within the filter-spec.  These are
	 * choice-specific; not all values will be defined for any given
	 * choice.
	 */
	struct object_id *sparse_oid_value;
	unsigned long blob_limit_value;
	unsigned long tree_exclude_depth;
};

/* Normalized command line arguments */
#define CL_ARG__FILTER "filter"

int parse_list_objects_filter(
	struct list_objects_filter_options *filter_options,
	const char *arg);

int opt_parse_list_objects_filter(const struct option *opt,
				  const char *arg, int unset);

#define OPT_PARSE_LIST_OBJECTS_FILTER(fo) \
	{ OPTION_CALLBACK, 0, CL_ARG__FILTER, fo, N_("args"), \
	  N_("object filtering"), 0, \
	  opt_parse_list_objects_filter }

/*
 * Translates abbreviated numbers in the filter's filter_spec into their
 * fully-expanded forms (e.g., "limit:blob=1k" becomes "limit:blob=1024").
 *
 * This form should be used instead of the raw filter_spec field when
 * communicating with a remote process or subprocess.
 */
void expand_list_objects_filter_spec(
	const struct list_objects_filter_options *filter,
	struct strbuf *expanded_spec);

void list_objects_filter_release(
	struct list_objects_filter_options *filter_options);

static inline void list_objects_filter_set_no_filter(
	struct list_objects_filter_options *filter_options)
{
	list_objects_filter_release(filter_options);
	filter_options->no_filter = 1;
}

void partial_clone_register(
	const char *remote,
	const struct list_objects_filter_options *filter_options);
void partial_clone_get_default_filter_spec(
	struct list_objects_filter_options *filter_options,
	const char *remote);

#endif /* LIST_OBJECTS_FILTER_OPTIONS_H */
