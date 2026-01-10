#ifndef LIST_OBJECTS_FILTER_OPTIONS_H
#define LIST_OBJECTS_FILTER_OPTIONS_H

#include "gettext.h"
#include "object.h"
#include "strbuf.h"

struct option;
struct string_list;

/*
 * The list of defined filters for list-objects.
 */
enum list_objects_filter_choice {
	LOFC_DISABLED = 0,
	LOFC_BLOB_NONE,
	LOFC_BLOB_LIMIT,
	LOFC_TREE_DEPTH,
	LOFC_SPARSE_OID,
	LOFC_OBJECT_TYPE,
	LOFC_COMBINE,
	LOFC_AUTO,
	LOFC__COUNT /* must be last */
};

/*
 * Returns a configuration key suitable for describing the given object filter,
 * e.g.: "blob:none", "combine", etc.
 */
const char *list_object_filter_config_name(enum list_objects_filter_choice c);

struct list_objects_filter_options {
	/*
	 * 'filter_spec' is the raw argument value given on the command line
	 * or protocol request.  (The part after the "--keyword=".)  For
	 * commands that launch filtering sub-processes, or for communication
	 * over the network, don't use this value; use the result of
	 * expand_list_objects_filter_spec() instead.
	 * To get the raw filter spec given by the user, use the result of
	 * list_objects_filter_spec().
	 */
	struct strbuf filter_spec;

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
	 * Is LOFC_AUTO a valid option?
	 */
	unsigned int allow_auto_filter : 1;

	/*
	 * BEGIN choice-specific parsed values from within the filter-spec. Only
	 * some values will be defined for any given choice.
	 */

	char *sparse_oid_name;
	unsigned long blob_limit_value;
	unsigned long tree_exclude_depth;
	enum object_type object_type;

	/* LOFC_COMBINE values */

	/* This array contains all the subfilters which this filter combines. */
	size_t sub_nr, sub_alloc;
	struct list_objects_filter_options *sub;

	/*
	 * END choice-specific parsed values.
	 */
};

#define LIST_OBJECTS_FILTER_INIT { .filter_spec = STRBUF_INIT }
void list_objects_filter_init(struct list_objects_filter_options *filter_options);

/*
 * Parse value of the argument to the "filter" keyword.
 * On the command line this looks like:
 *       --filter=<arg>
 * and in the pack protocol as:
 *       "filter" SP <arg>
 *
 * The filter keyword will be used by many commands.
 * See Documentation/rev-list-options.adoc for allowed values for <arg>.
 *
 * Capture the given arg as the "filter_spec".  This can be forwarded to
 * subordinate commands when necessary (although it's better to pass it through
 * expand_list_objects_filter_spec() first).  We also "intern" the arg for the
 * convenience of the current command.
 */
int gently_parse_list_objects_filter(
	struct list_objects_filter_options *filter_options,
	const char *arg,
	struct strbuf *errbuf);

void list_objects_filter_die_if_populated(
	struct list_objects_filter_options *filter_options);

/*
 * Parses the filter spec string given by arg and either (1) simply places the
 * result in filter_options if it is not yet populated or (2) combines it with
 * the filter already in filter_options if it is already populated. In the case
 * of (2), the filter specs are combined as if specified with 'combine:'.
 *
 * Dies and prints a user-facing message if an error occurs.
 */
void parse_list_objects_filter(
	struct list_objects_filter_options *filter_options,
	const char *arg);

/**
 * The opt->value to opt_parse_list_objects_filter() is either a
 * "struct list_objects_filter_option *" when using
 * OPT_PARSE_LIST_OBJECTS_FILTER().
 */
int opt_parse_list_objects_filter(const struct option *opt,
				  const char *arg, int unset);

#define OPT_PARSE_LIST_OBJECTS_FILTER(fo) \
	OPT_CALLBACK(0, "filter", (fo), N_("args"), \
		     N_("object filtering"), opt_parse_list_objects_filter)

/*
 * Translates abbreviated numbers in the filter's filter_spec into their
 * fully-expanded forms (e.g., "limit:blob=1k" becomes "limit:blob=1024").
 * Returns a string owned by the list_objects_filter_options object.
 *
 * This form should be used instead of the raw list_objects_filter_spec()
 * value when communicating with a remote process or subprocess.
 */
const char *expand_list_objects_filter_spec(
	struct list_objects_filter_options *filter);

/*
 * Returns the filter spec string more or less in the form as the user
 * entered it. This form of the filter_spec can be used in user-facing
 * messages.  Returns a string owned by the list_objects_filter_options
 * object.
 */
const char *list_objects_filter_spec(
	struct list_objects_filter_options *filter);

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
	struct list_objects_filter_options *filter_options);
void partial_clone_get_default_filter_spec(
	struct list_objects_filter_options *filter_options,
	const char *remote);

void list_objects_filter_copy(
	struct list_objects_filter_options *dest,
	const struct list_objects_filter_options *src);

/*
 * Combine the filter specs in 'specs' into a combined filter string
 * like "combine:<spec1>+<spec2>", where <spec1>, <spec2>, etc are
 * properly urlencoded. If 'specs' contains no element, NULL is
 * returned. If 'specs' contains a single element, a copy of that
 * element is returned.
 */
char *list_objects_filter_combine(const struct string_list *specs);

/*
 * Check if 'filter_options' are an 'auto' filter, and if that's the
 * case populate it with the filter specified by 'new_filter'.
 */
void list_objects_filter_resolve_auto(
	struct list_objects_filter_options *filter_options,
	char *new_filter,
	struct strbuf *errbuf);

#endif /* LIST_OBJECTS_FILTER_OPTIONS_H */
