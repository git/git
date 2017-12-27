#include "cache.h"
#include "commit.h"
#include "config.h"
#include "revision.h"
#include "argv-array.h"
#include "list-objects.h"
#include "list-objects-filter.h"
#include "list-objects-filter-options.h"

/*
 * Parse value of the argument to the "filter" keyword.
 * On the command line this looks like:
 *       --filter=<arg>
 * and in the pack protocol as:
 *       "filter" SP <arg>
 *
 * The filter keyword will be used by many commands.
 * See Documentation/rev-list-options.txt for allowed values for <arg>.
 *
 * Capture the given arg as the "filter_spec".  This can be forwarded to
 * subordinate commands when necessary.  We also "intern" the arg for
 * the convenience of the current command.
 */
int parse_list_objects_filter(struct list_objects_filter_options *filter_options,
			      const char *arg)
{
	const char *v0;

	if (filter_options->choice)
		die(_("multiple object filter types cannot be combined"));

	filter_options->filter_spec = strdup(arg);

	if (!strcmp(arg, "blob:none")) {
		filter_options->choice = LOFC_BLOB_NONE;
		return 0;
	}

	if (skip_prefix(arg, "blob:limit=", &v0)) {
		if (!git_parse_ulong(v0, &filter_options->blob_limit_value))
			die(_("invalid filter-spec expression '%s'"), arg);
		filter_options->choice = LOFC_BLOB_LIMIT;
		return 0;
	}

	if (skip_prefix(arg, "sparse:oid=", &v0)) {
		struct object_context oc;
		struct object_id sparse_oid;

		/*
		 * Try to parse <oid-expression> into an OID for the current
		 * command, but DO NOT complain if we don't have the blob or
		 * ref locally.
		 */
		if (!get_oid_with_context(v0, GET_OID_BLOB,
					  &sparse_oid, &oc))
			filter_options->sparse_oid_value = oiddup(&sparse_oid);
		filter_options->choice = LOFC_SPARSE_OID;
		return 0;
	}

	if (skip_prefix(arg, "sparse:path=", &v0)) {
		filter_options->choice = LOFC_SPARSE_PATH;
		filter_options->sparse_path_value = strdup(v0);
		return 0;
	}

	die(_("invalid filter-spec expression '%s'"), arg);
	return 0;
}

int opt_parse_list_objects_filter(const struct option *opt,
				  const char *arg, int unset)
{
	struct list_objects_filter_options *filter_options = opt->value;

	if (unset || !arg) {
		list_objects_filter_release(filter_options);
		return 0;
	}

	return parse_list_objects_filter(filter_options, arg);
}

void list_objects_filter_release(
	struct list_objects_filter_options *filter_options)
{
	free(filter_options->filter_spec);
	free(filter_options->sparse_oid_value);
	free(filter_options->sparse_path_value);
	memset(filter_options, 0, sizeof(*filter_options));
}
