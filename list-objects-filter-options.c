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
 * subordinate commands when necessary (although it's better to pass it through
 * expand_list_objects_filter_spec() first).  We also "intern" the arg for the
 * convenience of the current command.
 */
static int gently_parse_list_objects_filter(
	struct list_objects_filter_options *filter_options,
	const char *arg,
	struct strbuf *errbuf)
{
	const char *v0;

	if (filter_options->choice) {
		if (errbuf) {
			strbuf_addstr(
				errbuf,
				_("multiple filter-specs cannot be combined"));
		}
		return 1;
	}

	filter_options->filter_spec = strdup(arg);

	if (!strcmp(arg, "blob:none")) {
		filter_options->choice = LOFC_BLOB_NONE;
		return 0;

	} else if (skip_prefix(arg, "blob:limit=", &v0)) {
		if (git_parse_ulong(v0, &filter_options->blob_limit_value)) {
			filter_options->choice = LOFC_BLOB_LIMIT;
			return 0;
		}

	} else if (skip_prefix(arg, "tree:", &v0)) {
		if (!git_parse_ulong(v0, &filter_options->tree_exclude_depth)) {
			if (errbuf) {
				strbuf_addstr(
					errbuf,
					_("expected 'tree:<depth>'"));
			}
			return 1;
		}
		filter_options->choice = LOFC_TREE_DEPTH;
		return 0;

	} else if (skip_prefix(arg, "sparse:oid=", &v0)) {
		struct object_context oc;
		struct object_id sparse_oid;

		/*
		 * Try to parse <oid-expression> into an OID for the current
		 * command, but DO NOT complain if we don't have the blob or
		 * ref locally.
		 */
		if (!get_oid_with_context(the_repository, v0, GET_OID_BLOB,
					  &sparse_oid, &oc))
			filter_options->sparse_oid_value = oiddup(&sparse_oid);
		filter_options->choice = LOFC_SPARSE_OID;
		return 0;

	} else if (skip_prefix(arg, "sparse:path=", &v0)) {
		if (errbuf) {
			strbuf_addstr(
				errbuf,
				_("sparse:path filters support has been dropped"));
		}
		return 1;
	}
	/*
	 * Please update _git_fetch() in git-completion.bash when you
	 * add new filters
	 */

	if (errbuf)
		strbuf_addf(errbuf, _("invalid filter-spec '%s'"), arg);

	memset(filter_options, 0, sizeof(*filter_options));
	return 1;
}

int parse_list_objects_filter(struct list_objects_filter_options *filter_options,
			      const char *arg)
{
	struct strbuf buf = STRBUF_INIT;
	if (gently_parse_list_objects_filter(filter_options, arg, &buf))
		die("%s", buf.buf);
	return 0;
}

int opt_parse_list_objects_filter(const struct option *opt,
				  const char *arg, int unset)
{
	struct list_objects_filter_options *filter_options = opt->value;

	if (unset || !arg) {
		list_objects_filter_set_no_filter(filter_options);
		return 0;
	}

	return parse_list_objects_filter(filter_options, arg);
}

void expand_list_objects_filter_spec(
	const struct list_objects_filter_options *filter,
	struct strbuf *expanded_spec)
{
	strbuf_init(expanded_spec, strlen(filter->filter_spec));
	if (filter->choice == LOFC_BLOB_LIMIT)
		strbuf_addf(expanded_spec, "blob:limit=%lu",
			    filter->blob_limit_value);
	else if (filter->choice == LOFC_TREE_DEPTH)
		strbuf_addf(expanded_spec, "tree:%lu",
			    filter->tree_exclude_depth);
	else
		strbuf_addstr(expanded_spec, filter->filter_spec);
}

void list_objects_filter_release(
	struct list_objects_filter_options *filter_options)
{
	free(filter_options->filter_spec);
	free(filter_options->sparse_oid_value);
	memset(filter_options, 0, sizeof(*filter_options));
}

void partial_clone_register(
	const char *remote,
	const struct list_objects_filter_options *filter_options)
{
	/*
	 * Record the name of the partial clone remote in the
	 * config and in the global variable -- the latter is
	 * used throughout to indicate that partial clone is
	 * enabled and to expect missing objects.
	 */
	if (repository_format_partial_clone &&
	    *repository_format_partial_clone &&
	    strcmp(remote, repository_format_partial_clone))
		die(_("cannot change partial clone promisor remote"));

	git_config_set("core.repositoryformatversion", "1");
	git_config_set("extensions.partialclone", remote);

	repository_format_partial_clone = xstrdup(remote);

	/*
	 * Record the initial filter-spec in the config as
	 * the default for subsequent fetches from this remote.
	 */
	core_partial_clone_filter_default =
		xstrdup(filter_options->filter_spec);
	git_config_set("core.partialclonefilter",
		       core_partial_clone_filter_default);
}

void partial_clone_get_default_filter_spec(
	struct list_objects_filter_options *filter_options)
{
	/*
	 * Parse default value, but silently ignore it if it is invalid.
	 */
	if (!core_partial_clone_filter_default)
		return;
	gently_parse_list_objects_filter(filter_options,
					 core_partial_clone_filter_default,
					 NULL);
}
